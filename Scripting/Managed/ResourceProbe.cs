using System;
using System.Globalization;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed
{
    /// <summary>
    /// 反向接口探针（csharp-reverse-interface-binding 3.1 / 8.1）：C# 实现 C++ 接口的**行为**素材。
    ///
    /// 它只做一件事：按引擎侧设的环境变量 `CASCADED_CSHARP_RESOURCE_PHASE` 走一个阶段机，
    /// 把观测到的读数（句柄、错误码、计数）写进 `CASCADED_CSHARP_RESOURCE_READINGS` 给的原生侧
    /// 读数文件。C++ 侧（GPUBackendTester 的 TestCSharpResourceFactory）读同一个文件并断言。
    ///
    /// 为什么用读数文件而不是返回值：跨边界只有 Tick 的 int 回值，而本用例要传的是句柄（uint64）、
    /// 错误码与计数。文件与 `CASCADED_EDITOR_READINGS_FILE` 是同一套做法（测试专用通道，默认关）。
    /// </summary>
    internal static unsafe class ResourceProbe
    {
        private const string PhaseVariable = "CASCADED_CSHARP_RESOURCE_PHASE";
        private const string ReadingsVariable = "CASCADED_CSHARP_RESOURCE_READINGS";
        private const string ProbeVariable = "CASCADED_CSHARP_RESOURCE_PROBE";

        private static bool s_tablesBound;
        private static string s_lastPhase = "\u0000";
        private static ResourceFactoryBase s_factory;
        private static ResourceFactoryBase s_throwingFactory;
        private static ResourceFactoryBase s_nullFactory;

        /// <summary>C# 工厂：类型名与资源字节都写死，C++ 侧拿同一组期望对照。</summary>
        private sealed class ProbeFactory : ResourceFactoryBase
        {
            internal static int CreateCount;

            public override string GetResourceType()
            {
                return ProbeConstants.FactoryTypeName;
            }

            public override ResourceBase Create()
            {
                ++CreateCount;
                var resource = new ProbeResource();
                LastCreated = resource;
                return resource;
            }

            internal static ProbeResource LastCreated;
        }

        /// <summary>C# 资源：Serialize 返回写死的 4 字节（{0xDE,0xAD,0xBE,0xEF}）。</summary>
        private sealed class ProbeResource : ResourceBase
        {
            internal static bool ThrowOnSerialize;
            internal static int SerializeCount;

            public override byte[] Serialize()
            {
                ++SerializeCount;
                if (ThrowOnSerialize)
                {
                    throw new InvalidOperationException(ProbeConstants.ThrowingSerializeText);
                }
                return new byte[] { 0xDE, 0xAD, 0xBE, 0xEF };
            }
        }

        /// <summary>Create 抛异常的工厂（验"Create 失败抛异常、不注册空"）。</summary>
        private sealed class ThrowingFactory : ResourceFactoryBase
        {
            internal static int CreateCount;

            public override string GetResourceType()
            {
                return ProbeConstants.ThrowingFactoryTypeName;
            }

            public override ResourceBase Create()
            {
                ++CreateCount;
                throw new InvalidOperationException(ProbeConstants.ThrowingCreateText);
            }
        }

        /// <summary>Create 返回 null 的工厂（验"返回 null 也转成异常、不注册空"）。</summary>
        private sealed class NullFactory : ResourceFactoryBase
        {
            internal static int CreateCount;

            public override string GetResourceType()
            {
                return ProbeConstants.NullFactoryTypeName;
            }

            public override ResourceBase Create()
            {
                ++CreateCount;
                return null;
            }
        }

        // ---- 读数写入 ----
        private static void Write(string key, object value)
        {
            string path = Environment.GetEnvironmentVariable(ReadingsVariable);
            if (string.IsNullOrEmpty(path))
            {
                return;
            }
            string text = key + "=" + Convert.ToString(value, CultureInfo.InvariantCulture) + "\n";
            // 不带 BOM：原生侧按行首 `key=` 解析，第一行带 BOM 会让第一条读数读不到。
            File.AppendAllText(path, text, new UTF8Encoding(false));
        }

        private static void Fail(string text)
        {
            Write("probe_error", text);
            throw new InvalidOperationException("[资源反向探针] " + text);
        }

        /// <summary>把两个反向接口的函数表装进托管桥（幂等；取表失败就点名失败原因）。</summary>
        private static void BindReverseTables(EngineApiMirror* api)
        {
            if (s_tablesBound)
            {
                return;
            }
            BindOne(api, EngineApiBindings.ResourceManagement__ResourceFactoryInterfaceId, "IResourceFactory",
                    table => IResourceFactoryBindings.Bind(table));
            BindOne(api, EngineApiBindings.ResourceManagement__ResourceInterfaceId, "IResource",
                    table => IResourceBindings.Bind(table));
            s_tablesBound = true;
            Write("tables_bound", 1);
        }

        // 指针类型不能当泛型实参（`Action<byte*>` 是 CS0306）：用自定义委托 —— 委托签名可以含指针，
        // 只是不能把它塞进 `Action<>` / `Func<>` 这类泛型类型。类本身是 unsafe，这里不必再写 unsafe。
        private delegate void BindTableDelegate(byte* table);

        private static void BindOne(EngineApiMirror* api, uint id, string name, BindTableDelegate bind)
        {
            uint size = api->GetInterfaceApi(id, null, EngineApiBindings.TableVersion, 0, null, null);
            if (size == 0)
            {
                Fail($"取 {name} 反向函数表失败：编号 {id} 没有登记（角色 / kBindingPolicies 缺行？）");
            }
            byte* table = stackalloc byte[(int)size];
            uint written = api->GetInterfaceApi(id, null, EngineApiBindings.TableVersion, size, table, null);
            if (written != size)
            {
                Fail($"取 {name} 反向函数表失败：写入 {written} 字节、期望 {size} 字节");
            }
            bind(table);
        }

        /// <summary>每个 Tick 调一次（Host.Tick 里，`EnsureBound` 之后）。默认关 = 零行为。</summary>
        internal static void Tick(EngineApiMirror* api)
        {
            if (api == null)
            {
                return;
            }
            if (Environment.GetEnvironmentVariable(ProbeVariable) != "1")
            {
                return;
            }
            string phase = Environment.GetEnvironmentVariable(PhaseVariable) ?? string.Empty;
            if (phase == s_lastPhase)
            {
                return;
            }
            s_lastPhase = phase;
            try
            {
                BindReverseTables(api);
                switch (phase)
                {
                    case "create-factory":
                        s_factory = new ProbeFactory();
                        ulong handle = IResourceFactoryBindings.Create(s_factory);
                        Write("factory_type", ProbeConstants.FactoryTypeName);
                        Write("factory_handle", handle);
                        Write("factory_keepalive", IResourceFactoryBindings.KeepAliveCount(handle));
                        Write("factory_base_type", s_factory.GetType().BaseType.Name);
                        break;
                    case "create-failing-factories":
                        s_throwingFactory = new ThrowingFactory();
                        ulong throwingHandle = IResourceFactoryBindings.Create(s_throwingFactory);
                        s_nullFactory = new NullFactory();
                        ulong nullHandle = IResourceFactoryBindings.Create(s_nullFactory);
                        Write("throwing_factory_handle", throwingHandle);
                        Write("throwing_factory_type", ProbeConstants.ThrowingFactoryTypeName);
                        Write("null_factory_handle", nullHandle);
                        Write("null_factory_type", ProbeConstants.NullFactoryTypeName);
                        break;
                    case "after-create":
                        Write("resource_handle", ProbeFactory.LastCreated == null ? 0UL : IResourceBindings.GetBorrowHandle(ProbeFactory.LastCreated));
                        Write("create_count", ProbeFactory.CreateCount);
                        Write("serialize_calls", ProbeResource.SerializeCount);
                        break;
                    case "make-serialize-throw":
                        ProbeResource.ThrowOnSerialize = true;
                        Write("serialize_calls", ProbeResource.SerializeCount);
                        break;
                    case "after-throw":
                        Write("serialize_calls", ProbeResource.SerializeCount);
                        break;
                    case "dispose-resource":
                        if (ProbeFactory.LastCreated == null)
                        {
                            Fail("dispose-resource：C# 侧没有造过资源（after-create 阶段丢了？）");
                        }
                        else
                        {
                            ulong resourceHandle = IResourceBindings.GetBorrowHandle(ProbeFactory.LastCreated);
                            int first = IResourceBindings.Release(resourceHandle);
                            int second = IResourceBindings.Release(resourceHandle);
                            Write("dispose_code", first);
                            Write("dispose_again_code", second);
                            Write("resource_keepalive_after_dispose", IResourceBindings.KeepAliveCount(resourceHandle));
                        }
                        break;
                    case "dispose-factory":
                        {
                            ulong factoryHandle = s_factory == null ? 0UL : IResourceFactoryBindings.GetBorrowHandle(s_factory);
                            int first = IResourceFactoryBindings.Release(factoryHandle);
                            int second = IResourceFactoryBindings.Release(factoryHandle);
                            Write("factory_dispose_code", first);
                            Write("factory_dispose_again_code", second);
                            Write("factory_keepalive_after_dispose", IResourceFactoryBindings.KeepAliveCount(factoryHandle));
                        }
                        break;
                    // 同一个阶段机只会跑一次同名阶段（`s_lastPhase` 去重），所以"报计数"分成不同名字的
                    // 多个阶段：否则第二次 tick 被静默跳过，tester 读到的是旧计数（假绿/假红）。
                    case "report-counters":
                    case "report-counters-thread":
                    case "report-counters-create":
                        Write("factory_create_count", ProbeFactory.CreateCount);
                        Write("throwing_create_count", ThrowingFactory.CreateCount);
                        Write("null_create_count", NullFactory.CreateCount);
                        Write("serialize_calls", ProbeResource.SerializeCount);
                        break;
                    case "report-resource-keepalive":
                        Write("resource_keepalive",
                              ProbeFactory.LastCreated == null ? ulong.MaxValue
                                  : IResourceBindings.KeepAliveCount(IResourceBindings.GetBorrowHandle(ProbeFactory.LastCreated)));
                        Write("resource_handle",
                              ProbeFactory.LastCreated == null ? 0UL : IResourceBindings.GetBorrowHandle(ProbeFactory.LastCreated));
                        break;
                    default:
                        break;
                }
                Write("phase", phase);
            }
            catch (Exception ex)
            {
                Write("probe_error", ex.GetType().Name + ": " + ex.Message);
                throw;
            }
        }
    }

    /// <summary>探针与原生侧共用的写死常量（两边各写一份、值必须相同 —— 参照物不许从被测物推导）。</summary>
    internal static class ProbeConstants
    {
        internal const string FactoryTypeName = "CSharpProbe";
        internal const string ThrowingFactoryTypeName = "CSharpThrowProbe";
        internal const string NullFactoryTypeName = "CSharpNullProbe";
        internal const string ThrowingCreateText = "反向工厂 Create 故意抛出的异常";
        internal const string ThrowingSerializeText = "反向资源 Serialize 故意抛出的异常";
    }
}

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed
{
    /// <summary>
    /// 导入器反向探针（resource-importer-refactor 4.1 / 4.2）：C# 实现 `ResourceImporter` +
    /// `ResourceImporterProductions`（拉取式）的**行为**素材。
    ///
    /// 与 `ResourceProbe` 同一套做法：按 `CASCADED_CSHARP_IMPORTER_PHASE` 走阶段机，把读数写进
    /// `CASCADED_CSHARP_IMPORTER_READINGS` 给的原生读数文件；C++ 侧（GPUBackendTester 的
    /// `TestCSharpImporter`）读同一个文件并断言。默认关（没设 `CASCADED_CSHARP_IMPORTER_PROBE=1`
    /// 时 Tick 立即返回、零行为）。
    ///
    /// 一条导入器**产两条资源**（同一实例的非 1:1 形态）：两条的字节写死且不相同，C++ 侧按同一组
    /// 期望逐条核。
    /// </summary>
    internal static unsafe class ResourceImporterProbe
    {
        private const string PhaseVariable = "CASCADED_CSHARP_IMPORTER_PHASE";
        private const string ReadingsVariable = "CASCADED_CSHARP_IMPORTER_READINGS";
        private const string ProbeVariable = "CASCADED_CSHARP_IMPORTER_PROBE";

        /// <summary>两条产出的资源路径（与 C++ 侧写死的期望逐字相同）。</summary>
        internal const string PathA = "CSharpProbe/Imported_A.res";
        internal const string PathB = "CSharpProbe/Imported_B.res";

        /// <summary>两条产出的字节（与 C++ 侧写死的期望逐字相同）。</summary>
        internal static readonly byte[] BytesA = { 0xC5, 0x01 };
        internal static readonly byte[] BytesB = { 0xC5, 0x02 };

        private static bool s_tablesBound;
        private static string s_lastPhase = "\u0000";
        private static ProbeImporter s_importer;
        private static readonly List<ProbeResource> s_round1 = new List<ProbeResource>();
        private static readonly List<ProbeResource> s_round2 = new List<ProbeResource>();
        private static ProbeProductions s_round1Productions;
        private static ProbeProductions s_round2Productions;

        /// <summary>C# 资源：Serialize 返回写死的两条字节之一（按构造时的下标选）。</summary>
        private sealed class ProbeResource : ResourceBase
        {
            internal byte[] Bytes;
            internal int SerializeCount;

            public override byte[] Serialize()
            {
                ++SerializeCount;
                return Bytes;
            }
        }

        /// <summary>产出迭代器：队列式（先问 HasNext，再取一条；取完即推进）。</summary>
        private sealed class ProbeProductions : ResourceImporterProductionsBase
        {
            internal readonly List<string> Paths = new List<string>();
            internal readonly List<ProbeResource> Resources = new List<ProbeResource>();
            private int m_Index;

            public override bool HasNext()
            {
                return m_Index < Paths.Count;
            }

            public override string NextPath()
            {
                return Paths[m_Index];
            }

            public override ResourceBase NextResource()
            {
                ProbeResource resource = Resources[m_Index];
                ++m_Index;
                return resource;
            }
        }

        /// <summary>C# 导入器：一个实例、每次 Produce 产两条（非 1:1）。目录自己声明。</summary>
        private sealed class ProbeImporter : ResourceImporterBase
        {
            internal int ProduceCount;

            public override string GetTags()
            {
                return "CSharp;Probe";
            }

            // C# 反向实现读不到管线传入的扫描根（见 ResourceImporter.h 的注释），
            // 所以目录必须自己声明；这里给工程根的 CAResources（C++ 侧只核"非空"）。
            public override string GetSourceDirectory()
            {
                return Environment.GetEnvironmentVariable("CASCADED_CSHARP_IMPORTER_SOURCE") ?? string.Empty;
            }

            public override string GetDestDirectory()
            {
                return string.Empty;
            }

            public override ResourceImporterProductionsBase Produce()
            {
                ++ProduceCount;
                var productions = new ProbeProductions();
                var first = new ProbeResource { Bytes = BytesA };
                var second = new ProbeResource { Bytes = BytesB };
                productions.Paths.Add(PathA);
                productions.Resources.Add(first);
                productions.Paths.Add(PathB);
                productions.Resources.Add(second);
                if (ProduceCount == 1)
                {
                    s_round1.Clear();
                    s_round1.Add(first);
                    s_round1.Add(second);
                    s_round1Productions = productions;
                }
                else
                {
                    s_round2.Clear();
                    s_round2.Add(first);
                    s_round2.Add(second);
                    s_round2Productions = productions;
                }
                Write("produce_count", ProduceCount);
                return productions;
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
            throw new InvalidOperationException("[导入器反向探针] " + text);
        }

        private delegate void BindTableDelegate(byte* table);

        /// <summary>把三个反向接口的函数表装进托管桥（幂等；取表失败就点名失败原因）。</summary>
        private static void BindReverseTables(EngineApiMirror* api)
        {
            if (s_tablesBound)
            {
                return;
            }
            BindOne(api, EngineApiBindings.ResourceManagement__ResourceImporterInterfaceId, "ResourceImporter",
                    table => ResourceImporterBindings.Bind(table));
            BindOne(api, EngineApiBindings.ResourceManagement__ResourceImporterProductionsInterfaceId,
                    "ResourceImporterProductions",
                    table => ResourceImporterProductionsBindings.Bind(table));
            // 资源代理（NextResource 的 CreateProxy 与保活读数都走它）。
            BindOne(api, EngineApiBindings.ResourceManagement__ResourceInterfaceId, "IResource",
                    table => IResourceBindings.Bind(table));
            s_tablesBound = true;
            Write("tables_bound", 1);
        }

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

        private static void WriteRound(string prefix, List<ProbeResource> resources)
        {
            if (resources.Count != 2)
            {
                Fail(prefix + "：本轮产出的资源条数不是 2（" + resources.Count + "）");
            }
            Write(prefix + "_handle_a", IResourceBindings.GetBorrowHandle(resources[0]));
            Write(prefix + "_handle_b", IResourceBindings.GetBorrowHandle(resources[1]));
            Write(prefix + "_keepalive_a", IResourceBindings.KeepAliveCount(IResourceBindings.GetBorrowHandle(resources[0])));
            Write(prefix + "_keepalive_b", IResourceBindings.KeepAliveCount(IResourceBindings.GetBorrowHandle(resources[1])));
            Write(prefix + "_serialize_a", resources[0].SerializeCount);
            Write(prefix + "_serialize_b", resources[1].SerializeCount);
        }

        private static void WriteProductions(string prefix, ProbeProductions productions)
        {
            if (productions == null)
            {
                Write(prefix + "_productions_handle", 0UL);
                Write(prefix + "_productions_keepalive", ulong.MaxValue);
                return;
            }
            ulong handle = ResourceImporterProductionsBindings.GetBorrowHandle(productions);
            Write(prefix + "_productions_handle", handle);
            Write(prefix + "_productions_keepalive", ResourceImporterProductionsBindings.KeepAliveCount(handle));
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
                    case "create-importer":
                        s_importer = new ProbeImporter();
                        ulong handle = ResourceImporterBindings.Create(s_importer);
                        Write("importer_handle", handle);
                        Write("importer_tags", s_importer.GetTags());
                        Write("importer_base_type", s_importer.GetType().BaseType.Name);
                        Write("importer_keepalive", ResourceImporterBindings.KeepAliveCount(handle));
                        break;
                    case "after-first-scan":
                        Write("produce_count", s_importer == null ? 0 : s_importer.ProduceCount);
                        WriteRound("round1", s_round1);
                        WriteProductions("round1", s_round1Productions);
                        break;
                    case "after-second-scan":
                        Write("produce_count", s_importer == null ? 0 : s_importer.ProduceCount);
                        WriteRound("round2", s_round2);
                        WriteProductions("round2", s_round2Productions);
                        // 重导入替换：第二轮必须是**新对象**（旧代理被管线释放后旧对象不再被注册表持有）。
                        Write("round2_is_new_a", s_round1.Count == 2 && s_round2.Count == 2 &&
                                                !ReferenceEquals(s_round1[0], s_round2[0]) ? 1 : 0);
                        Write("round2_is_new_b", s_round1.Count == 2 && s_round2.Count == 2 &&
                                                !ReferenceEquals(s_round1[1], s_round2[1]) ? 1 : 0);
                        // 第一轮的借用句柄在替换时由代理析构退役：那边**不写保活读数** ——
                        // `ReverseKeepAliveCount` 只按句柄里的槽位下标查表（代际只用来判失效），
                        // 旧槽位可能已被第二轮的新代理复用，读旧句柄会读到新代理的计数（实测
                        // A 读 1 / B 读 0）。"旧代理真的析构了"用 `ReleaseImpl(旧句柄) == 1001`
                        // （代际不符）判，由 C++ 侧断言。
                        break;
                    case "report-serialize":
                        WriteRound("round2", s_round2);
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
}

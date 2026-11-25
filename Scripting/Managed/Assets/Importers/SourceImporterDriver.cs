using System;
using Scripting.Generated.Bindings;
using Scripting.Generated.Bindings.ResourceManagement;

namespace Scripting.Managed.Assets.Importers
{
    /// <summary>
    /// 源导入器的测试驱动接线（source-importers-png-obj 任务 2.3 / design D1）：C# 侧**没有**
    /// `AddImporter` / `ScanSourceDirectory` 绑定，所以照 `ResourceImporterProbe` /
    /// `TestCSharpImporter` 的先例走测试驱动：
    ///
    ///   ① 门控 `CASCADED_SOURCE_IMPORTERS=1` 时绑三张反向函数表（ResourceImporter /
    ///      ResourceImporterProductions / IResource，照 `ResourceImporterProbe.BindReverseTables`）；
    ///   ② 用 `ResourceImporterBindings.Create` 造两个导入器的**强句柄**，把句柄 / 标签 / 反向基类名 /
    ///      保活计数 / 源目录写进读数文件（env `CASCADED_SOURCE_IMPORTERS_READINGS`）；
    ///   ③ C++ 测试侧读句柄 → `ResolveImpl` → `pImportingSystem->AddImporter` →
    ///      `pImportingSystem->ScanSourceDirectory`（注意扫描入口属 `ResourceImportingSystem`）；
    ///   ④ 扫描之后测试把阶段设成 `report-round-<n>`，这里把该轮产出的借用句柄与保活计数写进读数 ——
    ///      重导入断言"旧句柄 ReleaseImpl == 1001 / 新对象是新句柄"靠的就是这些读数。
    ///
    /// 默认关（`Host.Tick` 只在门控为 1 时调进来）：不绑表、不建导入器、不写读数、零行为。
    /// </summary>
    internal static unsafe class SourceImporterDriver
    {
        private static bool s_tablesBound;
        private static string s_lastPhase = "\u0000";
        private static PngTextureImporter s_png;
        private static ObjMeshImporter s_obj;
        private static ulong s_pngHandle;
        private static ulong s_objHandle;

        /// <summary>每个 Tick 调一次（`Host.Tick` 里，`EnsureBound` 之后；门控由 `Host` 判）。</summary>
        internal static void Tick(EngineApiMirror* api)
        {
            if (api == null)
            {
                return;
            }
            string phase = Environment.GetEnvironmentVariable(SourceImporterEnvironment.PhaseVariable) ?? string.Empty;
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
                    case "create-importers":
                        CreateImporters();
                        break;
                    case "report-round-1":
                        ReportRound(1);
                        break;
                    case "report-round-2":
                        ReportRound(2);
                        break;
                    case "report-round-3":
                        ReportRound(3);
                        break;
                    case "report-round-4":
                        ReportRound(4);
                        break;
                    default:
                        break;
                }
                SourceImporterEnvironment.Write("phase", phase);
            }
            catch (Exception error)
            {
                SourceImporterEnvironment.Write("driver_error", error.GetType().Name + ": " + error.Message);
                throw;
            }
        }

        private static void CreateImporters()
        {
            s_png = new PngTextureImporter();
            s_obj = new ObjMeshImporter();
            s_pngHandle = ResourceImporterBindings.Create(s_png);
            s_objHandle = ResourceImporterBindings.Create(s_obj);
            WriteImporter("png", s_png, s_pngHandle);
            WriteImporter("obj", s_obj, s_objHandle);
        }

        private static void WriteImporter(string prefix, SourceImporterBase importer, ulong handle)
        {
            SourceImporterEnvironment.Write(prefix + "_importer_handle", handle);
            SourceImporterEnvironment.Write(prefix + "_importer_type", importer.GetType().Name);
            SourceImporterEnvironment.Write(prefix + "_importer_tags", importer.GetTags());
            SourceImporterEnvironment.Write(prefix + "_importer_base_type", ReverseBaseName(importer));
            SourceImporterEnvironment.Write(prefix + "_importer_keepalive", ResourceImporterBindings.KeepAliveCount(handle));
            SourceImporterEnvironment.Write(prefix + "_source_directory", importer.GetSourceDirectory());
            SourceImporterEnvironment.Write(prefix + "_dest_directory", importer.GetDestDirectory());
        }

        /// <summary>沿基类链找生成的反向基类名（本项目的具体导入器多一层 `SourceImporterBase`）。</summary>
        private static string ReverseBaseName(object target)
        {
            Type type = target.GetType();
            while (type != null)
            {
                if (type.Name == "ResourceImporterBase")
                {
                    return type.Name;
                }
                type = type.BaseType;
            }
            return "(基类链里没有 ResourceImporterBase)";
        }

        private static void ReportRound(int round)
        {
            ReportImporter("png", s_png, round);
            ReportImporter("obj", s_obj, round);
        }

        private static void ReportImporter(string prefix, SourceImporterBase importer, int round)
        {
            if (importer == null)
            {
                SourceImporterEnvironment.Write(prefix + "_produce_count", 0);
                SourceImporterEnvironment.Write(prefix + "_rounds", 0);
                return;
            }
            SourceImporterEnvironment.Write(prefix + "_produce_count", importer.ProduceCount);
            SourceImporterEnvironment.Write(prefix + "_rounds", importer.Rounds.Count);
            SourceImporterEnvironment.Write(prefix + "_error_count", importer.ErrorCount);
            if (importer.Rounds.Count < round)
            {
                SourceImporterEnvironment.Write(prefix + "_round" + round + "_count", -1);
                return;
            }
            SourceProductions productions = importer.Rounds[round - 1];
            SourceImporterEnvironment.Write(prefix + "_round" + round + "_count", productions.Count);
            for (int index = 0; index < productions.Count; ++index)
            {
                ulong handle = IResourceBindings.GetBorrowHandle(productions.Resources[index]);
                SourceImporterEnvironment.Write(prefix + "_round" + round + "_path_" + index,
                    productions.Paths[index]);
                SourceImporterEnvironment.Write(prefix + "_round" + round + "_handle_" + index, handle);
                SourceImporterEnvironment.Write(prefix + "_round" + round + "_keepalive_" + index,
                    IResourceBindings.KeepAliveCount(handle));
            }
        }

        // ---- 反向函数表 ----
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
            // 资源代理：NextResource 的 CreateProxy 与借用句柄/保活读数都走它。
            BindOne(api, EngineApiBindings.ResourceManagement__ResourceInterfaceId, "IResource",
                table => IResourceBindings.Bind(table));
            s_tablesBound = true;
            SourceImporterEnvironment.Write("tables_bound", 1);
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

        private static void Fail(string text)
        {
            SourceImporterEnvironment.Write("driver_error", text);
            throw new InvalidOperationException("[源导入器驱动] " + text);
        }
    }
}

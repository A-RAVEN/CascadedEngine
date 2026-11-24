# 测试规范

本项目把测试分三层。**任何新功能都必须落在其中至少一层，并且留下一条"在缺陷存在时会变红"的断言** —— 只贴日志、只手工造一次红态、只写"已验证"，都不算测试。

## 三层，按"要不要进程 / 模块 / GPU"划分

| 层 | 载体 | 适合测什么 | 不适合测什么 |
|---|---|---|---|
| **① 纯逻辑** | `Test/CoreTests`（27 个 `.cpp`，`main` 在 `SerializerTest.cpp`） | 序列化字节、哈希值、反射与类型表、容器语义、绑定回调/嵌套镜像/公开值类型、原生五类表面的工厂与句柄往返、反向代理的结构（句柄 kind / releaseUser / 跳过方法 1013）、导入器退役/解耦（源码范围零命中 + 拉取式接口签名）这类**不碰模块、不开窗口、不需要 GPU**的判定 | 需要加载模块、需要真实后端、需要多进程或"启动即退出"的行为 |
| **② 进程内行为** | `Test/GPUBackendTester`（当前 **23** 个测试，注册在 `Main.cpp` 的 `runTestWithDiagnostics("TestXxx", TestXxx)` 列表里；其中 `TestEditorAssetBrowser` 必须**独立进程**跑，见下） | 要真后端、真要窗口、要在引擎内部断言某模块的行为（例：`IScriptHost` 可用、`Tick` 回值正确、10 类窗口回调与生命周期、C# 提交三角形的真实像素、C# 按着色器参数驱动的常量色像素、C# 纹理采样的真实像素、C# 场景的序列化往返 / 生命周期 / 场景→图像素、C# 工厂/资源经反向代理被 C++ 调到、C# 导入器拉取式产资源 + 着色器导入器字节对拍、批式读写六条 I/O 失败路径、编辑器资产浏览的 list-assets 条目与 assets-changed） | 进程级失败（启动就该非零退出）、需要改环境变量的场景 |
| **③ 进程级 / 多进程** | `check_*.py` 前缀共 14 个（`Tools/` 8 个 + `Tools/CodeGen/` 6 个），外加 `verify_readme.py`、`read_dump.py`、`spin_check.py` 三个助手 | "缺文件 / 版本不符 / ABI 不符 ⇒ 退出码与错误串"这类**一启动就失败**的路径；生成物与审计表的对账；两侧日志逐字段对拍；正常/负向像素报告的对账；`CSharpHost/README.md` 的 ABI 读数对拍 | 需要引擎内部状态的断言 |

## 统一约定

1. **命名**：①层 `<主题>Test.cpp` 里一个函数一个用例；②层 `Test<主题>`（会成为 `--list` 里的一行）；③层 `check_<主题>.py`。
2. **必须能失败**：每条测试都要在文档或注释里写清"**怎么把它弄红**"。写不出红态构造方法 = 这条测试不算数。
3. **红态构造优先用运行时开关，不改源码**：需要模拟"版本不符 / 抛异常"这类异常路径时，用环境变量开关（例：`CASCADED_CSHARP_SELFTEST=abi-mismatch`）而不是临时改 `.cpp`/`.cs` 再改回来 —— 后者既脏工作区，又不可重复。
4. **进程级测试必须自己还原**：③层脚本只改 `%TEMP%` 里的副本与输出目录；跑完必须让工作区与产物回到跑之前的状态（脚本里写在 `finally` 段）。
5. **判定与读数**：①层打印 `PASSED` 行、退出码 0/1；②层每个测试在 `test_output/result.json` 里有一行 `{"name","status","duration_ms","exit_code"}`，**进程退出码 0 = 所有被选中的测试都 pass、1 = 至少一条 fail**（`vulkan-gpu-queries` 任务 1.2 起，此前 `main` 无条件返回 `EXIT_SUCCESS`），判定仍以 `status` 为准；③层退出码 `0` 全过 / `1` 有失败 / `2` 判据自身失效（找不到被测程序或产物）。
6. **失败信息要能直接定位**：`期望什么 / 实际什么 / 怎么重跑这一条`。禁止只写"失败"。
7. **产物不入库**：`test_output/`、`crash_*.dmp`、`%TEMP%` 下的中间文件都是跑测试的产物，跑完清掉。

## 现有测试清单

### ① `Test/CoreTests`（28 个文件）

`BindingsCallbackTest` / `BindingsCrossInterfaceTest` / `BindingsGetApiTest` / `BindingsHandleTest` / `BindingsLayoutTest` / `BindingsNativeShapesTest` / `BindingsNativeSurfaceTest` / `BindingsNestedLayoutTest` / `BindingsRenderFoundationTest` / `BindingsReverseTest` / `BindingsWeakHandleTest` / `CodeGenKindsTest` / `CodeGenMetaTest` / `CodeGenRuntimeTest` / `EmitReflectionTest` / `FileLoaderErrorTest` / `FriendLikeTest` / `GeneratedContainerTest` / `GeneratedHashContainerTest` / `GeneratedHashTest` / `GeneratedSerializerTest` / `PipelineStateObjectTest` / `ResourceBatchTest` / `ResourceImporterTest` / `SerializerTest`（含 `main` 与分配烟测）/ `ShaderLibraryBytesTest` / `WindowBindingsCompileTest`。

跑法：`out\build\x64-debug\bin\CoreTests.exe`（退出码 0，`PASSED` 行数当前为 **26** = 16 条主分支打印 + 9 条函数内自印 + 新增的 `Resource importer retirement/decoupling test PASSED` 一行；实测只有 25 行时先查这一行在不在）。`ResourceImporterTest` 由 `resource-importer-refactor` 加入：按目录递归扫 **C++ 源码范围**（`GeneralResources/header+private`、两个后端的 `ShaderLibrary` 目录）断言旧 1:1 导入器抽象 / 模板（折行感知）/ 后缀扫描成员 / 旧入口名零命中，断言两个着色器导入器的源码里没有五个写注册表入口，并用编译期成员指针 + `SingleResourceProductions` 的最小行为对拉取式接口的签名与"先问 HasNext 再取一条、取完即空"钉死。**怎么把它弄红**：把任意一个退役符号写回范围内源码 ⇒ `E_IMPORTER_RETIRED` / `E_IMPORTER_TEMPLATE`；在 `ShaderImporter_Vulkan.cpp` 里加一句 `RegisterResource` ⇒ `E_IMPORTER_REGISTERS`；把 `SingleResourceProductions::NextResource` 改成不清空指针 ⇒ `E_IMPORTER_ITER_SECOND`。`BindingsReverseTest` 由 `csharp-reverse-interface-binding` 加入：直接 include 反向夹具产物，用 C 函数指针当"假 C# 实现"，断言代理继承/可实例化、句柄表 kind 1→4（强 → 借用）、每条被绑定虚方法真的调到、`CA_BIND_SKIP` 的方法抛 1013、`releaseUser` 只调一次、保活计数 1→0、旧句柄 1001 且重复释放幂等。**怎么把它弄红**：把生成的代理方法体改成空体 ⇒ `impl.*Calls` 不涨；删掉代理析构里的 `release(m_user)` ⇒ `releaseUser 没有被调`；删掉 `TakeOwnership` 里的 `entry.kind = 4` ⇒ kind 断言红。
`FileLoaderErrorTest` 由 `failure-visibility-hygiene` 加入：`%TEMP%` 下真文件验证 `cacore::file_error` 的三条红态判据（缺失路径读文本 / 读二进制、父目录不存在的写）与两条对照（0 字节文件、正常写读）；`LoadBinaryFile` 的读不足分支与 `WriteBinaryFile` 的写流失败分支没有独立注入读数（只实现检查 + 走读），见 `CACore/README.md` 的错误行为表。`BindingsNativeSurfaceTest` 直接 include 真实接口的生成产物，断言五个原生表面的工厂槽 / 句柄往返 / 窗口尺寸与后台缓冲描述，并含一条值类**第二条对象通道**的读数：`Present` 用 `WindowHandle` 构造的后台缓冲引用必须回错误码 0 且呈现清单收到 1 项（`[原生表面] Present(windows 句柄构造) 错误码=0 呈现项=1`）。旧的 `ScriptRenderBridgeTest` 随桥一起删除（5.1），它原来的拒绝路径 / 字节复制 / 图结构判据由四条 C# 渲染用例与托管读数行承接。

### ② `Test/GPUBackendTester`（23 个测试）

`TestSimpleTriangle` / `TestTriangleWithConstantColor` / `TestTriangleWithStructuredBufferColor` / `TestTriangleWithImageBuffer` / `TestDoublePass` / `TestComputeBuffer` / `TestReadback` / `TestIMGUI` / `TestScriptHost` / `TestCSharpWindow` / `TestCSharpTriangle` / `TestCSharpScene` / `TestCSharpWindowCallbacks` / `TestGPUTiming` / `TestCSharpConstantColor` / `TestCSharpStructuredBuffer` / `TestCSharpTextureSampling` / `TestEditorWindowPanel` / `TestIOErrorChannel` / `TestCSharpResourceFactory` / `TestCSharpImporter` / `TestCSharpAssets` / `TestEditorAssetBrowser`。

`TestCSharpImporter` 由 `resource-importer-refactor` 加入（**第 21 条，`--list` 里最后一行，全量里必须排在 `TestCSharpWindowCallbacks` 之前**，因为它依赖 C# 运行时；同时它会在 Vulkan 上重导入一次着色器库，之后只剩那条关运行时的用例）。两段判据：① **导入器级字节对拍** —— 启动时那一次 `ScanSourceDirectory` 已把 `<后端>ShaderLibrary.shLib` 写到资源根，测试算它的 SHA-256 并与**改动前**（2026-11-22 20:02 的 tester 运行，存档 `openspec/changes/resource-importer-refactor/evidence/2.2-改前字节.txt`）逐字节比：Vulkan `7a3d7d0c…7c81`、D3D12 `5d9551ca…93f3`；② **C# 导入器端到端** —— `Scripting/Managed/ResourceImporterProbe.cs` 按 `CASCADED_CSHARP_IMPORTER_PHASE` 阶段机真跑"造 C# 导入器 / 第一次扫描 / 第二次扫描（源目录不存在）/ 报序列化计数"，把句柄、保活计数、Produce 次数写进 `CASCADED_CSHARP_IMPORTER_READINGS`；C++ 侧经 `GetInterfaceApiForTest(11/12)` 取 **CSharpHost 的**两张反向函数表，`ResolveImpl` 后 `AddImporter`，再跑两次 `ScanSourceDirectory` 并断言：同一导入器实例一次 `Produce()` 产两条（两条路径 `HasResource` 真、`GetResource` 是同一实例、不是同一个对象）、写字节钉死 `{C5,01}` / `{C5,02}` 且落盘字节相同、`CreateProxy` 之后保活 1、第二次扫描（传入不存在的源目录）**不早退**且重导入**换掉旧对象**（两个旧借用句柄都按代际失效 = 重复释放 1001、C# 侧 `ReferenceEquals` 判新；**不用旧句柄的保活读数**——`ReverseKeepAliveCount` 只按槽位下标查表，旧槽位可能被第二轮新代理复用，实测旧 A 会读到 1）、C++ 对照导入器（声明自己的源根）的产出在两轮里都注册且落盘、C++ 对照资源释放后 `GetOrLoadResource` 真读盘回读成功、C# 资源 `Deserialize` 抛 1013 且不动批、`ReleaseResource` 后保活 1→0。**怎么把它弄红**：把管线改回 `get_or_create` 式注册 ⇒ `E_IMPORTER_REPLACE`；把 `SerializeAll` 挪回早退之后 ⇒ 第二次扫描后对照资源不再落盘 ⇒ `E_IMPORTER_CONTRAST_FILE_2`；让 C# 的 `NextResource` 复用同一对象 ⇒ `E_IMPORTER_ONE_RESOURCE` / `E_IMPORTER_NEW_OBJECTS`；把着色器导入器的清集合或 `output` 交出的对象改错 ⇒ `E_IMPORTER_SHLIB_BYTES`；把 `kBindingPolicies` 的 11/12 行删掉 ⇒ 阶段一就 `E_IMPORTER_TABLE`。

`TestCSharpAssets` 由 `csharp-resource-asset-foundation` 加入（**第 22 条，`--list` 最后一行，全量里必须排在 `TestCSharpWindowCallbacks` 之前**，因为它依赖 C# 运行时）：C# 侧 `Scripting/Managed/ResourceAssetProbe.cs` 按 `CASCADED_CSHARP_ASSET_PHASE` 阶段机真跑「门面注册两工厂 → 新建填数据 → SerializeAll → 释放 + 自读加载 → 同路径替换 → 失败面 → 渲染消费」，把句柄 / 十六进制字节 / 错误码 / 文本写进 `CASCADED_CSHARP_ASSET_READINGS`；C++ 侧 `TestCSharpAssets` 读同一份读数与磁盘文件断言：落盘字节按 CATX/CAMH 写死格式逐字节对照（贴图 44 字节、网格 116 字节）、加载往返逐字段一致、替换后旧借用句柄经 CSharpHost 的 IResource 反向表 `ReleaseImpl` = 1001、C# 侧失败面的异常类型 + Code + 文本（坏句柄 1001 / 未注册 1014 / 同路径 1014 / 损坏版本、截短文件、缺失文件都是 `AssetFormatException` 且旧条目仍在 / Serialize 两段式：首次哨兵字节原样 + 二次 1014 带 1012 原文）、原生直检门面六条（错误通道读数，不向外抛），以及两后端离屏像素（纹理三角形 bg=(0,0,255,255) center=(0,255,0,255)；网格索引绘制 bg 同上 center=(255,0,0,255)）。**怎么把它弄红**：删掉门面 `CreateResource` 的同路径守卫 ⇒ `same_path_code` / `E_ASSET_NATIVE_SAME_PATH`；把 `AssetDatabase` 的 IO 包装去掉 ⇒ `missing_type` 变成 `FileNotFoundException` ⇒ `E_ASSET_READING`；把坏句柄的 1001 改成 1014 ⇒ `bad_handle_code`；改 `Texture2D.Serialize` 任一字段顺序 ⇒ `E_ASSET_TEXTURE_BYTES`；删 `kBindingPolicies` 门面行 ⇒ 阶段一取表失败（1.3 红态）。读数存档：`openspec/changes/csharp-resource-asset-foundation/evidence/4.2-用例-*.json` 与 `4.2-用例日志.txt`。

`TestCSharpResourceFactory` 由 `csharp-reverse-interface-binding` 加入（**第 20 条，全量里必须排在 `TestCSharpWindowCallbacks` 之前**，因为它依赖 C# 运行时）：C# 侧 `Scripting/Managed/ResourceProbe.cs` 按 `CASCADED_CSHARP_RESOURCE_PHASE` 阶段机真跑"造 C# 工厂 / 造 C# 资源 / 制造失败 / Dispose / 报计数"，把句柄与读数写进 `CASCADED_CSHARP_RESOURCE_READINGS` 给的文件；C++ 侧读同一份文件并断言 —— C# 工厂经句柄注册（`RegisterFactory(typeName, handle, 借用指针, 释放回调)`：注册表保留句柄、持借用指针，强引用仍在创建模块的句柄表里，所以注册后同一句柄仍解析到同一个代理；tester 经 `IScriptHost::GetInterfaceApiForTest(9/10)` 取 **CSharpHost 的**反向函数表，resolve/release/keepalive 都走表里函数指针，不调 tester 自己那份产物函数）、按类型名运行时 `CreateResource` 造 C# 资源、写路径往返钉死 `{0xDE,0xAD,0xBE,0xEF}`（长度 4、提交 1 次）、`Deserialize` 抛 1013、`Serialize` 失败落槽且下一次调用抛 1012（含原始文本、失败那次不写批）、非注册线程 1005 且 C# 覆写调用数不涨、`Create` 抛异常/返回 null 都不注册空（`HasResource` false、第二次重调 Create 计数 2）、未注册类型明确报错、C++ 原生工厂对照写死 `{0x11,0x22,0x33}`、寿命两档（C# Dispose 报 0 再 1001、按路径仍是同一实例、保活计数 1→0）。**怎么把它弄红**：把代理 `Serialize` 的失败分支改成继续 `Write` ⇒ `E_REVERSE_FAIL_WROTE`；C# Dispose 改成什么都不做 ⇒ `E_REVERSE_DISPOSE_AGAIN`；`CreateResource` 不查 nullptr ⇒ `E_REVERSE_CREATE_NULL`；把 `kBindingPolicies` 里 9/10 行删掉 ⇒ 阶段一就 `取 IResourceFactory 反向函数表失败`。

`TestEditorAssetBrowser` 由 `editor-asset-browser` 加入（**第 23 条，`--list` 最后一行；必须独立进程跑** —— 编辑器服务是进程级一次性状态机，`TestEditorWindowPanel` 收尾会主动停服务，两个编辑器用例同进程不能共存，所以它**不进全量同进程顺序跑**，只按 `--test TestEditorAssetBrowser` 跑）：用例自建临时资源根并预置三条已知文件（`Assets/Registered.tex` 142 字节且**已注册**、`ShaderLibraries/Browser.shLib` 377 字节未注册、`Docs/Notes.txt` 21 字节未知扩展名 → kind=file），并把修改时间用 `SetFileTime` 钉到写死的 UTC 值；起编辑器服务后用 WebSocket 发 `list-assets`，断言七项（path / nodeId / sizeBytes / kind / modifiedUtc / registered / children）逐条等于写死期望、registered 真值与假值都出现、children 恒空；写一个新文件再枚举 ⇒ 条目 +1 且收到恰好一条 `assets-changed(added=1, removed=0)`（不带 objectId）；把根切到空目录 ⇒ `ok=true` 且 `assets` 空数组；根非法 / 目录不存在 / 根空串 / `CASCADED_EDITOR_SELFTEST=no-facade`（跳过门面绑定）四种"根不可用"各回 `E_ASSET_ROOT` 且 10 秒内都等到该 requestId 的 result（证明枚举异常没有穿出到 `Host.Tick` 外层 catch 造成客户端静默挂起）；最后按读数文件里该 requestId 那一帧的 `cmdCalls` 断言门面调用 = 1（`GetResourceRootPath`）+ N（每文件一次 `HasResource`）、`lastCmd=list-assets`、`mode=main-thread`。**怎么把它弄红**：漏枚举一个文件 / 种类映射错 ⇒ 七项期望不符；不广播 `assets-changed` ⇒ 事件断言收不到；空目录当错误回 ⇒ 空态断言红；门面调用不 `NoteNativeCall` ⇒ `cmdCalls` 不等于 1+N；枚举异常穿出去 ⇒ 对应的 requestId 等不到 result（`E_ASSETBROWSER_RESULT`）。读数存档：`openspec/changes/editor-asset-browser/evidence/4.1-*.txt`。


（这份清单必须与 `GPUBackendTester.exe --list` 逐条一致，顺序也一致 —— 上面就是 `--list` 的实际顺序。全量运行的执行顺序略有不同：`TestCSharpConstantColor`、`TestCSharpStructuredBuffer` 与 `TestCSharpTextureSampling` 都排在 `TestGPUTiming` 之后、`TestCSharpWindowCallbacks` 之前，因为最后那条会显式关闭托管运行时、之后再没有测试能用 C#。`TestGPUTiming` 由 `vulkan-gpu-queries` 加入，覆盖一次图执行的逐 pass GPU 计时；`TestCSharpWindowCallbacks` 覆盖 10 类窗口/输入回调、替换/置空/重绑/关闭与借用窗口失效。`TestCSharpTriangle` 由 `csharp-render-backend-triangle` 加入（4.1 换面）：C# 经**原生直绑表面 + 糖层**用自己的顶点字节 / 着色器 / 窗口与离屏目标建图、提交并等待；C++ 侧从托管读数行 `[托管 读数]` 拿原生图对象地址，再从图附件的资源引用里取同一份离屏纹理做像素判据。`TestCSharpConstantColor` 由 `csharp-render-shader-constants` 加入：C# 用单 `POSITION` 顶点流 + `ConstantColor` 参数结构（3 个 `ColorSubStruct`）建图、两次 `SetPassParam` 绑定 `constantColorBlock`，原生侧读离屏目标做精确像素判据，并在同一次运行里由 C# 探针做两个**托管前置校验**负例（未知类型名 / 未登记着色器路径），C++ 侧按读数行 `negUnknown` / `negPath` 核对被拒；`skip-param` 的「后端执行次数 0」同样读自读数行。`TestCSharpStructuredBuffer` 由 `csharp-render-structured-buffer` 加入：C# 把图内登记的 1 元素结构化缓冲按名绑进 `StructuredColor` 参数结构、两次 `SetPassParam` 绑定 `structuredColorBlock`，原生侧读离屏目标做精确像素判据（背景 `(0,255,0,255)`、中心 `(255,0,255,255)`），并核对两个 pass 的缓冲字段解析名（读数行 `boundBuffer0/1` = `sbColors`）、缓冲元数据读数为只读 + 描述符数组长度 1、以及托管前置校验拒绝缓冲表外字段名（读数行 `negField`）。`TestCSharpTextureSampling` 由 `csharp-render-texture-sampling` 加入（**排在 `TestCSharpWindowCallbacks` 之前**，与另外两条 C# 用例同一理由）：C# 一次 `CreateTexture("texYellow2x2", 2, 2, 16 字节)` 完成创建 + 上传 + 转采样态，用位置 + 纹理坐标顶点流与 `TextureSampleBlock` 参数结构（`testTexture` / `testSampler`）两次 `SetPassParam` 绑定 `textureBlock`，原生侧读离屏目标做精确像素判据（背景 `(0,0,255,255)`、中心 `(255,255,0,255)`），并核对两个 pass 的纹理字段解析句柄（读数行 `texture` 与 `boundTexture0/1` 相等 ⇒ 与 `CreateTexture` 返回的是**同一对象**）、上传执行与提交执行分开计数、以及托管 disposed 摘要行。`TestIOErrorChannel` 由 `io-error-channel` 加入：批式读写六条失败路径各自打印一行读数、各自记判定（1.2 不存在的路径在 `Batch()` 抛 `ca_io::io_error`；1.3 越界读返回 `false` 且批内位置不推进；1.4① 排队读后删除源文件、1.4② 排队读后把源文件截短到 8 字节都必须在 `SubmitAndWait` 抛 `io_error`；1.5 写目标父目录不存在同样抛；5.1 经 `GetOrLoadResource` 加载不存在的资产路径抛 `io_error` 并点名该路径）。**怎么把它弄红**：把 `IOBatchImpl::Read` 的越界分支改回无条件接受 ⇒ 只有 1.3 那行判据不成立、整条用例判红；把 `SubmitAndWait` 的 `gcount()` 检查删掉 ⇒ 只有 1.4② 那行不成立。）

`TestCSharpScene` 由 `csharp-scene-foundation` 加入（**第 19 条，`--list` 里紧跟 `TestCSharpTriangle`，全量里必须排在 `TestCSharpWindowCallbacks` 之前**，同样依赖 C# 运行时）：C# 侧 `Scripting/Managed/Scene/SceneProbe.cs` 真跑四段——无活动场景零行为 → 序列化往返与失败通道 → 注册新场景并渲染 → 注销后从磁盘 `Load` 回来再注册 → 坏资源名——把结果打成 `[托管 读数] probe=scene-idle / scene-format / scene-state` 与 `[场景 断言]` 行；C++ 侧判读这些读数，并自己读回离屏捕获的像素（背景 `(0,0,255,255)`、中心 `(255,0,0,255)`）与**原生图**结构（2 个 pass、1 项呈现、每 pass 一条 `POSITION(0)+COLOR(12)` 步长 24 顶点流、顶点数 6 = 两个三角形）。序列化断言：两实体往返 **54** 个字段 0 处不等（`rtChecked=54 rtMismatch=0`）、改一个 `Position` 分量后两份文件字节不同（`diffBytes=1`）、截断 / 版本 999 / 缺 id / 字段类型错 / 缺 `formatVersion` 各抛 `SceneFormatException` 且消息点名具体问题、`Save` 到不存在的目录抛 `IOException`（带路径）、坏资源名场景**能加载**（`badResLoad=ok`，Load 不查资源）而渲染期明确报错点名该着色器。生命周期断言：`OnStart` 首次注册调一次、重复注册同一场景不再调（`repeatDelta=0`）、`OnUpdate` 每帧递增且 `lastDtMicros` 逐帧等于引擎传入的 `deltaSeconds`（0.125 → 125000、0.25 → 250000）。**怎么把它弄红**：`CASCADED_CSHARP_SCENE_SELFTEST=skip-onupdate` ⇒ `E_SCENE_DT`；`=bad-color` ⇒ `E_SCENE_PIXEL`；把 Load 改成静默补默认值 ⇒ `trunc` / `version` / `missingId` 读数变成 `none`；把坏着色器路径改成静默 `continue` ⇒ Tick 回值正常且 `LastError` 里没有那个路径。读数存档：`openspec/changes/csharp-scene-foundation/evidence/`（`3.2-读数-vulkan.log` + 两张离屏捕获 PNG）。

`TestEditorWindowPanel` 由 editor-window-panel 加入（**第 18 条，全量里必须排在 `TestCSharpWindowCallbacks` 之前**，因为它依赖 C# 运行时）：测试自设 `CASCADED_EDITOR=1` 与专用端口 18391 起服务，覆盖 welcome/①set-title/②set-size/⑥单窗口三分区读数与计数增量/⑦撤销/⑧版本拒绝、③未知编号/④外部关窗失效/⑤线程边界/形状错与非法参数零原生调用，以及两层像素证据——⑨离屏捕获目标中心红、⑩窗口**真实 swapchain backbuffer 呈现前读回**视口中心精确 `(255,0,0,255)`、⑪改尺寸后捕获尺寸=新视口且仍红；收尾用 `CASCADED_EDITOR_STOP=1` 主动停服务（隔离后续用例）。**布局断言随 editor-asset-browser 的三分区更新**（2026-11-24 用户裁定底部通栏 dock）：就绪判据要两个控制器都绿（`controller/nav` + `dockController/dockNav`），启动断言面板/视口高 = 客户区高 − 240 且 dock 矩形 `(0, 客户区高−240, 客户区宽, 240)` 铺满底部，改尺寸后视口高 = 640 − 240 = 400、`set-size` 到 900x560 后捕获目标 = 580x320；**像素判据仍是中心精确 `(255,0,0,255)`（未放松），另加视口高显式断言补高度分辨力**。`CASCADED_EDITOR_SELFTEST=no-runtime` 变体验证运行时缺失时明确禁用不崩；`no-dock-controller` 变体验证第二控制器失败时同样明确禁用。

跑法：`GPUBackendTester.exe --backend vulkan|d3d12 --headless 10 --report <json>`；`--list` 列出可用测试；`--test <名字>` 只跑一个。每个测试各自写 `test_output/<名字>.log`，崩溃会写 `crash_YYYYMMDD_HHMMSS.dmp` 并在 stderr 打印异常名、异常码、崩溃模块。

### ③ 进程级脚本（`check_*.py` 前缀 14 个 + 助手 3 个）

`check_legacy_symbols.py`（旧符号守卫）、`check_hash_sources.py`（哈希来源盘点）、`check_bindable_audit.py`（可绑定面审计对账）、`check_binding_baseline.py`（绑定产物逐字节基线对账；当前基线 = `openspec/changes/archive/2026-11-11-csharp-bindings-namespace-alignment/evidence/基线产物哈希.txt`）、`check_native_snapshot.py`（真实头投影快照的独立清单对账）、`check_gpu_timing.py`（普通 `ExecuteGraph` 不得创建计时资源）、`check_window_system_bindings.py`（`GetMonitor` 与 10 类回调的 C++ / 托管两侧日志逐字段对拍）、`check_csharp_triangle.py`（C# 三角形的正常/负向像素报告对账）、`check_csharp_constant_color.py`（C# 常量色的正常/负向报告对账）、`check_csharp_structured_buffer.py`（C# 结构化缓冲的正常/负向报告对账）、`check_csharp_texture_sampling.py`（C# 纹理采样的正常/负向报告对账）、`check_csharp_host_failures.py`（宿主五条失败路径）、`check_csharp_host_readme.py`（`CSharpHost/README.md` 的 ABI 版本 / 接口编号表 / 探针开关 vs 代码真实值）、`verify_readme.py`（README 里的命令逐条重跑）、`read_dump.py`（读崩溃转储）、`spin_check.py`（轮界进度检查）。它们自己就是"进程级断言"的样板：跑别人的程序，只看**退出码 + 输出里的可核子串**。

`check_csharp_constant_color.py` 的判据（`csharp-render-shader-constants` 任务 4.1）：报告目录里必须有 `csharp_constant_color_<backend>_<mode>.json` 六份（两个后端 × normal / bad-struct-colors / skip-param）。正常模式要求 `status = pass` 且同次 PNG `TestCSharpConstantColor_<backend>_normal.png` 非空；`bad-struct-colors` 要求 `status = fail` 且 `error` 含 `E_CONSTCOLOR_PIXEL`；`skip-param` 要求 `status = fail`、`error` 含 `E_CONSTCOLOR_PARAM` **并且**含「后端执行次数 0」（判据是提交前拒绝，不读像素 —— 未定义内容的像素不能当判据）。缺报告、正常不是 pass、负向被错误接受、skip-param 没有执行次数读数都非零；报告目录不存在按"判据自身失效"退出 2。红态构造：不跑任何常量色测试直接运行它 ⇒ 六份报告全缺、退出 1（见 `openspec/changes/archive/2026-11-04-csharp-render-shader-constants/evidence/host-pixel-red.log`）；把某份 `skip-param` 报告改成 `pass` ⇒ "负向结果被错误接受"。

`check_csharp_structured_buffer.py` 的判据（`csharp-render-structured-buffer` 任务 4.1）：报告目录里必须有 `csharp_structured_buffer_<backend>_<mode>.json` 八份（两个后端 × normal / bad-buffer-color / unbound-field / bad-buffer-name）。正常模式要求 `status = pass` 且同次 PNG `TestCSharpStructuredBuffer_<backend>_normal.png` 非空；`bad-buffer-color` 要求 `status = fail`、`error` 含 `E_SB_PIXEL` **并且**含 `(0,0,255,255)`（判据是中心像素精确等于它，不是"非 magenta"这种空帧也能过的弱判据）；`unbound-field` 与 `bad-buffer-name` 要求 `status = fail`、`error` 含 `E_SB_BIND`、含「后端执行次数 0」与对应名字（`colorStructuredBuffer` / `sbColorsMissing`），两者都**不读像素**（提交前拒绝，未定义内容的像素不能当判据）。缺报告、正常不是 pass、负向被错误接受、错误码或读数不对都非零；报告目录不存在按"判据自身失效"退出 2。红态构造：不跑任何结构化缓冲测试直接运行它 ⇒ 八份报告全缺、退出 1（见 `openspec/changes/archive/2026-11-04-csharp-render-structured-buffer/evidence/host-pixel-red.log`）；把某份 `unbound-field` 报告改成 `pass` ⇒ "负向结果被错误接受"（自检读数见 `openspec/changes/archive/2026-11-04-csharp-render-structured-buffer/evidence/pixel-validation.log` 的 F 节）。

`check_csharp_texture_sampling.py` 的判据（`csharp-render-texture-sampling` 任务 1.2 / 4.1）：报告目录里必须有 `csharp_texture_sampling_<backend>_<mode>.json` 八份（两个后端 × normal / bad-texture-color / unbound-image / unbound-sampler）。两种像素模式（`normal` 与 `bad-texture-color`）要求同次 PNG `TestCSharpTextureSampling_<backend>_<mode>.png` 非空：`normal` 要求 `status = pass`；`bad-texture-color` 要求 `status = fail`、`error` 含 `E_TEX_PIXEL` **并且**含 `(0,255,255,255)`（判据是中心像素精确跟着纹理字节变，证明"字节 → 纹理 → 采样 → 像素"整链参与）。`unbound-image` 与 `unbound-sampler` 要求 `status = fail`、`error` 含 `E_TEX_BIND`、点名 `testTexture` / `testSampler`、含「本次提交执行增量 0」与「上传执行 1」（上传那一次是单列计数、不计入提交判据），两者都**不读像素**。缺报告、正常不是 pass、负向被错误接受、错误码或读数不对都非零；报告目录不存在按"判据自身失效"退出 2。红态构造：不跑任何纹理测试直接运行它 ⇒ 八份报告全缺、退出 1；报告目录不存在 ⇒ 退出 2（两态读数见 `openspec/changes/archive/2026-11-04-csharp-render-texture-sampling/evidence/tex-test-skeleton.log`）。

`check_csharp_host_readme.py` 的判据（`csharp-render-shader-constants` 任务 4.2；`csharp-render-texture-sampling` 任务 3.1 扩到 README 全文；`render-interface-merge` 任务 4.1 把真值来源换成生成产物）：`CSharpHost/README.md` 的 ABI 版本、函数表字节数、接口编号表与探针开关清单，逐项对拍生成产物 —— `codegen/bindings/BindingRegistry.Bindings.h` 的 `kEngineApiVersion`、`static_assert(sizeof(EngineApi) == N)`、`kBindingInterfaceId*` 编号常量，以及生成 `EngineApi.Bindings.cs` 的编号常量；另外逐行扫描 README **全文**的「函数表 N 字节」「ABI vN」「`sizeof(EngineApi) == N`」「托管侧期望 N」「不是 N」读数，任一处与真实值不符都点名行号与原文。生成产物缺失或读不到关键常量 ⇒ 按"判据自身失效"退出 2（不跳过）。红态构造：把 README 的 `**v9**` 改成 `v8` ⇒ 退出码 1 并点名「ABI 版本不符：README 写 v8、生成 kEngineApiVersion=9」；把编号表某行的接口名改掉 ⇒ 退出码 1 并点名编号与两侧名字。

`check_csharp_triangle.py` 的判据（`csharp-render-backend-triangle` 任务 4.1）：报告目录里必须有 `csharp_triangle_<backend>_<mode>.json` 十份（两个后端 × normal / skip-draw / bad-vertex / skip-present / skip-wait）。正常模式要求 `status = pass` 且同次 PNG 非空；四个负向模式要求 `status = fail` 且 `error` 里的错误码对得上（skip-draw → `E_TRIANGLE_DRAW`、bad-vertex → `E_TRIANGLE_PIXEL`、skip-present → `E_TRIANGLE_PRESENT`、skip-wait → `E_TRIANGLE_WAIT`）。缺报告、正常不是 pass、负向被错误接受都非零；报告目录本身不存在按"判据自身失效"退出 2。红态构造：不跑任何像素测试直接运行它 ⇒ 十份报告全缺、退出 1（见 `openspec/changes/archive/2026-11-03-csharp-render-backend-triangle/evidence/host-pixel-red.log`）；把某份 skip-draw 报告改成 `pass` ⇒ "负向结果被错误接受"。

`check_gpu_timing.py` 的判据（`vulkan-gpu-queries` 任务 5.1，design D6）：读出两个后端源文件、剥注释、按花括号配平取出 `void <类>::ExecuteGraph(` 的函数体，函数体里不得出现 `ExecuteGraphTimed` / `GetTimingCapabilities` / `D3D12GPUTiming` / `VulkanGPUTiming` / `CreateQueryHeap` / `createQueryPool` / `SetTimingRecorder`。退出码 0 = 两个后端都干净、1 = 有违反、2 = 判据自身失效（找不到文件/函数体，或函数体里没有 `CompileAndExecute`）。红态构造：在 D3D12 的普通 `ExecuteGraph` 里插一句 `(void)GetTimingCapabilities(EGPUQueueType::eDirect);` ⇒ 退出码 1 并点名该符号；删掉后回到 0。

**绑定发射器的判据在另一个目录，但已经接进构建**：`Tools/CodeGen/check_bindings.py` 做这些事 —— 期望签名逐行对拍、C++/C# **两层对齐**（native 字段层 + 托管表面层）、**结构桩判据**（剥注释后看 thunk 有没有真调实现；真实接口桩数必须 0、夹具只打印）、**覆盖率下限**（thunk 数必须等于期望文件里的独立清单）、**托管表面判据**（`public` 类型里不许出现 `delegate*` / `void*` / `ulong`，表面成员数必须等于「接口头里的纯虚方法数 − `CA_BIND_SKIP` 数」）、**第二张矩阵的六行 conformance**、**共享公开值类型判据**（`RenderDesc` / `RenderNested` / 枚举等文件的内容、`bool` 的 `I1` 声明、`[Flags]`、`// 限定名：` 唯一性，以及"共享文件不许混进函数表/异常类"；`GPUBufferDescriptor` 的两个 `uint64_t` 字段按 `ulong` 允许，其余共享记录仍禁 `ulong`）、**渲染基础公开表面签名**（`IBindFoundation` 的记录 / 枚举 / `byte[]` / `IntPtr` 整条签名）、**夹具识别**（`BindableShapes.Render*.h` 必须仍被 `BindableShapes*.h` 规则收进夹具集合）、**登记表判据**（`render-interface-merge` 任务 1.3/2.1：编号 → 接口完整限定名逐项对账、主表必须 1..N 连续 / 夹具表 8..M 连续、`EngineApi` 两侧各 9 个字段逐字声明、`kEngineApiVersion = 12`、`static_assert(sizeof(EngineApi) == 56u`、C# 编号常量与绑定清单条目数（ABI v12：身份链改完整限定名，布局仍 9 字段 / 56 字节））。它由 `build.py` 在绑定生成之后调用并传 `--product-dir`，非零退出直接让构建失败，stdout 逐行转发（所以"夹具桩数"那行每次构建都看得见）。行为断言落在 `Test/CoreTests/`（第 ① 层）：`BindingsGetApiTest.cpp`（取表握手）、`BindingsHandleTest.cpp`（强/弱/借用三类句柄、代际回绕、并发、1002 的触发与清零反例）、`BindingsWeakHandleTest.cpp`（弱引用不抬高计数、对象析构后 1001）、`WindowBindingsCompileTest.cpp`（`IWindow` 产物编译 + 借用句柄 + 句柄发行者 + 1001）、`BindingsCallbackTest.cpp`（函数指针 + 用户令牌的原生回调：参数顺序、替换、旧注册号注销、空指针、重入 1004、异常 1003 与原文）、`BindingsNestedLayoutTest.cpp`（递归纯标量记录的布局/按值搬运/defaulted 比较运算符 + 非法嵌套成员非零退出并点名路径）、`BindingsCrossInterfaceTest.cpp`（跨头文件目标入口、发行者与目标两类角色、配对释放、无见证 / 过期见证借用、`shared_ptr` 入参重建）、`BindingsRenderFoundationTest.cpp`（公开记录 / 枚举 / 标志位 / 包装值 / 字节混合参数的原生读数、空指针配非零长度在 thunk 入口拒绝，以及 8 个 `BindableShapes.RenderReject*.h` 的生成期拒绝探针）、`BindingsNativeShapesTest.cpp`（原生形态：非纯虚/非虚采集、自返回、静态工厂返回纯标量记录、Create、重载序数槽、资源引用值类、序列化块往返与五例畸形块、参数注入，以及六条生成期拒绝探针）、`BindingsNativeSurfaceTest.cpp`（原生五类表面：工厂槽、句柄往返、`WindowHandle` 三方法、标量容器两段式，以及值类第二条对象通道 —— `Present(WindowHandle 构造的引用)` 错误码 0 / 呈现项 1）。

**托管表面这一层由谁把守**（`csharp-window-creation` 任务 5.2/5.6/5.7，三条互相独立的判据）：

| 判据 | 位置 | 它抓的是 |
|---|---|---|
| `check_surface()` | `check_bindings.py`，每次构建都跑 | 表面类型在不在、名字是不是逐字照抄、`public` 类型里有没有 native 细节（`delegate*`/`void*`/`ulong`）、表面成员数对不对（期望数从**接口头**算，不从产物读） |
| `check_surface_matrix()` | 同上 | 第二张矩阵六行各自的**整条签名**（`public bool GetFlag()` 之类）—— 只比名字抓不住 `bool`→`int` |
| `SurfaceSignatureCheck.cs` | `Scripting/Managed/`，编进 `Scripting.Managed` | 61 行含 `Func<>` / `Action<>` 的签名断言（口径：`git grep -c 'Func<\|Action<' -- Scripting/Managed/SurfaceSignatureCheck.cs`，2026-11-05 实测仍为 61；**该文件本身没有计数断言**，61 只是仓库级观测口径）：签名映射错就是**编译错误**；`VerifyValueTypes` 另行按真实 CLR 布局核对公开记录与枚举（含 `TextureSamplerDescriptor` 的大小 / 偏移与三个枚举底层类型） |

`check_error_codes()` 另管错误码：产物里的 `<线形>_ERROR_*` 常量与所有 `_setError` 的码逐个对 `Tools/CodeGen/CodeGenMain.cpp` 的 `kBindErrorCodes`（1001–1010，全部具名），裸字面量直接红；`Tools/CodeGen/README.md` 的错误码表与 `check_bindings.py` 的 `ERROR_CODE_TABLE` 必须与它同源。`Tools/check_window_system_bindings.py --log <test_output/...log> [--callbacks]` 是另一条独立的第 ③ 层判据：`GetMonitor` 的两个矩形 + DPI、无效显示器编号的 1006、10 类回调的窗口身份与全部标量参数都从**同一次测试进程写出的日志**里逐项比对，缺一项或数值不符即非零。

**跨边界端到端落在第 ② 层**：`Test/GPUBackendTester` 的 `TestCSharpWindow` 与 `TestCSharpWindowCallbacks` 是把「C++ → 托管表面 → 引擎侧对账」走通的两条用例。`TestCSharpWindow` 用真实窗口系统：C++ 侧建窗、登记两个实例（编号 1 = `IWindow`、编号 2 = `IWindowSystem`，实例名见 `CSharpHost/README.md`）、泵一帧显示器列表，托管侧读尺寸/关闭标志/计数/`GetMonitor`（两个矩形 + DPI）并验证无效显示器编号抛 1006；`Tools/check_window_system_bindings.py` 对拍两侧日志。`TestCSharpWindowCallbacks` 用固定夹具走 10 类回调与生命周期（替换、置空、重绑、关闭、借用窗口失效、重入 1004、跨线程 1005、异常 1003），并在最后显式调用 `IScriptHost::Shutdown` 验证关闭前先执行托管注销；脚本加 `--callbacks` 逐类对拍窗口身份与全部标量参数。跑法：`GPUBackendTester.exe --backend vulkan --test <名字> --headless 30`，读数在 `test_output/<名字>.log`，判定在 `test_output/result.json` 的 `status`；进程退出码同样反映红绿（0 = 全部 pass，1 = 至少一条 fail），但**判定以 `status` 为准**。

## C#Host 的测试怎么做（本规范的第一份应用样板）

**已实现（2026-10-18，`csharp-hostfxr-bootstrap` 的 5.1–5.3）**：下面两层都在跑，红态都构造过。实测读数：

```
② 层：out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --headless 2 --report %TEMP%\v.json
       → 9/9 pass（含 TestScriptHost）
     红态：把 Host.cs 里 Tick 的 return s_tickCount; 改成 return 0; → pass=8 / fail=1，唯一的 fail 就是 TestScriptHost

③ 层：python Tools\check_csharp_host_failures.py
       → 5 个场景：5 通过、0 失败（退出码 0）
     红态：把场景 1 的期望退出码临时改成 4 → [失败] 缺托管程序集…、4 通过、1 失败（退出码 1）
```

`CSharpHost` 有两类行为要测，正好各落一层：

### 层 ②：`TestScriptHost`（加它的时候是第 9 条；当前清单一共 23 条，其中 `TestEditorAssetBrowser` 独立进程跑，全量同进程顺序跑 22 条）

测"运行时起来了且双向可调"，断言全部来自进程内状态：

```
GetInstance<cs_script::IScriptHost>() 非空
  → IsAvailable() == true
  → LastError() 是空串
  → Tick(0.016) == 1         // 托管侧回的是"第几次 Tick"
  → Tick(0.016) == 2         // 递增，证明状态在托管侧真的保留了
  → LastError() 仍是空串
```

- **为什么在 ②层**：它需要模块加载与真实运行时；①层不开模块。
- **怎么弄红**：在 `Host.cs` 里把 `Tick` 的 `return s_tickCount;` 改成 `return 0;` ⇒ 只有这一条红、其余 8 条仍绿（证明它真在断言，而不是跟着别人一起绿）。
- **注意区分**：`Scripting.Managed.dll` 缺失属于"宿主级前置失败"（进程退出码 3、整批红），不是本条的断言失败 —— 这种情形交给下面 ③层的脚本断言退出码。

### 层 ③：`Tools/check_csharp_host_failures.py`

测"五条失败路径都响"，每条都是**启动即失败**，只能从进程外面断言：

| 场景 | 怎么制造（用运行时开关，不改源码） | 断言 |
|---|---|---|
| 缺托管程序集 | 把 `bin\Scripting.Managed.dll` 临时改名 | 退出码 **3** 且输出含 `Scripting.Managed.dll` 与 `找过的路径` |
| 配置路径不存在 | `CASCADED_CSHARP_RUNTIMECONFIG=<%TEMP% 里不存在的路径>` | 退出码 **3** 且输出含该路径与 `hostfxr.dll` |
| 要的运行时版本本机没有 | `CASCADED_CSHARP_RUNTIMECONFIG` 指向一份 `"version":"7.0.0"` 的副本 | 退出码 **3** 且输出含 `7.0.0` 与 `本机已装的运行时版本` |
| 函数表 ABI 不符 | `CASCADED_CSHARP_SELFTEST=abi-mismatch`（引擎侧把当前 v10 加成 11） | 退出码 **3** 且输出含 `ABI 版本不符` 与 `托管侧期望 10`（点名当前 ABI，不再期待旧版本 2 / 6 / 7 / 8 / 9） |
| 托管方法抛异常 | `CASCADED_CSHARP_SELFTEST=throw`（托管侧 `Tick` 抛一次） | 退出码 **1**（受控失败：`GPUBackendTester` 对失败的当前操作按既有约定返回 1）、输出含 `Tick 抛异常` 与错误码 **4**、且**不产生**新 `crash_*.dmp` |

- **运行时自检开关是这条测试能自动化的前提**：CSharpHost 与托管侧的自检开关（`abi-mismatch`、`throw`、`layout-mismatch`、`monitor-mismatch`、`monitor-skip-invalid`、`CASCADED_CSHARP_CALLBACK_PROBE` / `CASCADED_CSHARP_CALLBACK_PHASE`）让红态不必临时改源码；开关只在 Debug 下生效，并在 `CSharpHost/README.md` 里登记。
- **脚本结构**：`for each 场景 → 造环境 → 跑 tester（--headless 1）→ 断言退出码与子串 → finally 还原`；退出码 `0/1/2` 同第 5 条约定。
- **跑一次要多久**：每个场景一次 headless 启动（实测 1~2 秒），五条加起来 10 秒量级，可以放进"改完必须跑"的那一组。

### 两层的关系

②层管"**正常能跑通**"，③层管"**坏的时候会响**"。两层都绿才算 C#Host 有测试；只做 ②层会漏掉全部失败路径，只做 ③层会漏掉"Tick 回值其实错了"这类静默错误。

## 改完代码必须跑的最小集合

```powershell
python build.py --config Debug
out\build\x64-debug\bin\CoreTests.exe
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --headless 10 --report $env:TEMP\v.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --headless 10 --report $env:TEMP\d.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestGPUTiming --headless 10
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestGPUTiming --headless 10
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpWindow --headless 2
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpWindowCallbacks --headless 2
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestEditorWindowPanel --headless 10
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestEditorWindowPanel --headless 10
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestEditorAssetBrowser --headless 10
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestEditorAssetBrowser --headless 10
$env:CASCADED_EDITOR_SELFTEST='no-runtime'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestEditorWindowPanel --headless 10; Remove-Item Env:\CASCADED_EDITOR_SELFTEST
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpScene --headless 10 --report $env:TEMP\scene_v.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpScene --headless 10 --report $env:TEMP\scene_d.json
$env:CASCADED_CSHARP_SCENE_SELFTEST='skip-onupdate'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpScene --headless 10 --report $env:TEMP\scene_red_dt.json; Remove-Item Env:\CASCADED_CSHARP_SCENE_SELFTEST
$env:CASCADED_CSHARP_SCENE_SELFTEST='bad-color'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpScene --headless 10 --report $env:TEMP\scene_red_pixel.json; Remove-Item Env:\CASCADED_CSHARP_SCENE_SELFTEST
python Tools\check_window_system_bindings.py --log test_output\TestCSharpWindow.log
python Tools\check_window_system_bindings.py --log test_output\TestCSharpWindowCallbacks.log --callbacks
python Tools\check_gpu_timing.py
python Tools\CodeGen\check_bindable_audit.py
python Tools\CodeGen\check_bindings.py --product-dir out\build\x64-debug\codegen\bindings
python Tools\check_csharp_constant_color.py
python Tools\check_csharp_structured_buffer.py
python Tools\check_csharp_host_readme.py
python Tools\CodeGen\verify_readme.py
```

C# 三角形的像素判据（`csharp-render-backend-triangle`）另外要跑十次：两个后端 × 正常与四个 Debug 自检模式，报告路径按 `test_output\csharp_triangle_<backend>_<mode>.json` 命名，最后 `python Tools\check_csharp_triangle.py` 对账（缺一份即非零）：

```powershell
# normal（不设模式变量）
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_d3d12_normal.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_vulkan_normal.json
# 四个负向模式（每个后端各一次，共 8 份）
$env:CASCADED_CSHARP_RENDER_SELFTEST='skip-draw';    out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_d3d12_skip-draw.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='bad-vertex';   out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_d3d12_bad-vertex.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='skip-present'; out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_d3d12_skip-present.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='skip-wait';    out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_d3d12_skip-wait.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='skip-draw';    out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_vulkan_skip-draw.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='bad-vertex';   out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_vulkan_bad-vertex.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='skip-present'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_vulkan_skip-present.json
$env:CASCADED_CSHARP_RENDER_SELFTEST='skip-wait';    out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTriangle --headless 10 --report test_output\csharp_triangle_vulkan_skip-wait.json
Remove-Item Env:\CASCADED_CSHARP_RENDER_SELFTEST
python Tools\check_csharp_triangle.py
```

C# 常量色（着色器参数）的像素判据（`csharp-render-shader-constants`）另外要跑六次：两个后端 × 正常与两个 Debug 自检模式，报告路径按 `test_output\csharp_constant_color_<backend>_<mode>.json` 命名，最后 `python Tools\check_csharp_constant_color.py` 对账（缺一份即非零）：

```powershell
# normal（不设模式变量）
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpConstantColor --headless 10 --report test_output\csharp_constant_color_d3d12_normal.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpConstantColor --headless 10 --report test_output\csharp_constant_color_vulkan_normal.json
# 两个负向模式（每个后端各一次，共 4 份）
$env:CASCADED_CSHARP_CONSTCOLOR_SELFTEST='bad-struct-colors'; out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpConstantColor --headless 10 --report test_output\csharp_constant_color_d3d12_bad-struct-colors.json
$env:CASCADED_CSHARP_CONSTCOLOR_SELFTEST='skip-param';        out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpConstantColor --headless 10 --report test_output\csharp_constant_color_d3d12_skip-param.json
$env:CASCADED_CSHARP_CONSTCOLOR_SELFTEST='bad-struct-colors'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpConstantColor --headless 10 --report test_output\csharp_constant_color_vulkan_bad-struct-colors.json
$env:CASCADED_CSHARP_CONSTCOLOR_SELFTEST='skip-param';        out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpConstantColor --headless 10 --report test_output\csharp_constant_color_vulkan_skip-param.json
Remove-Item Env:\CASCADED_CSHARP_CONSTCOLOR_SELFTEST
python Tools\check_csharp_constant_color.py
```

C# 结构化缓冲的像素与提交前拒绝判据（`csharp-render-structured-buffer`）另外要跑八次：两个后端 × 正常与三个 Debug 自检模式，报告路径按 `test_output\csharp_structured_buffer_<backend>_<mode>.json` 命名，最后 `python Tools\check_csharp_structured_buffer.py` 对账（缺一份即非零）：

```powershell
# normal（不设模式变量）
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_d3d12_normal.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_vulkan_normal.json
# 三个负向模式（每个后端各一次，共 6 份）
$env:CASCADED_CSHARP_SB_SELFTEST='bad-buffer-color'; out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_d3d12_bad-buffer-color.json
$env:CASCADED_CSHARP_SB_SELFTEST='unbound-field';    out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_d3d12_unbound-field.json
$env:CASCADED_CSHARP_SB_SELFTEST='bad-buffer-name';  out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_d3d12_bad-buffer-name.json
$env:CASCADED_CSHARP_SB_SELFTEST='bad-buffer-color'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_vulkan_bad-buffer-color.json
$env:CASCADED_CSHARP_SB_SELFTEST='unbound-field';    out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_vulkan_unbound-field.json
$env:CASCADED_CSHARP_SB_SELFTEST='bad-buffer-name';  out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpStructuredBuffer --headless 10 --report test_output\csharp_structured_buffer_vulkan_bad-buffer-name.json
Remove-Item Env:\CASCADED_CSHARP_SB_SELFTEST
python Tools\check_csharp_structured_buffer.py
```

C# 纹理采样的像素与提交前拒绝判据（`csharp-render-texture-sampling`）另外要跑八次：两个后端 × 正常与三个 Debug 自检模式，报告路径按 `test_output\csharp_texture_sampling_<backend>_<mode>.json` 命名，最后 `python Tools\check_csharp_texture_sampling.py` 对账（缺一份即非零）：

```powershell
# normal（不设模式变量）
out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_d3d12_normal.json
out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_vulkan_normal.json
# 三个负向模式（每个后端各一次，共 6 份）
$env:CASCADED_CSHARP_TEX_SELFTEST='bad-texture-color'; out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_d3d12_bad-texture-color.json
$env:CASCADED_CSHARP_TEX_SELFTEST='unbound-image';     out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_d3d12_unbound-image.json
$env:CASCADED_CSHARP_TEX_SELFTEST='unbound-sampler';   out\build\x64-debug\bin\GPUBackendTester.exe --backend d3d12 --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_d3d12_unbound-sampler.json
$env:CASCADED_CSHARP_TEX_SELFTEST='bad-texture-color'; out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_vulkan_bad-texture-color.json
$env:CASCADED_CSHARP_TEX_SELFTEST='unbound-image';     out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_vulkan_unbound-image.json
$env:CASCADED_CSHARP_TEX_SELFTEST='unbound-sampler';   out\build\x64-debug\bin\GPUBackendTester.exe --backend vulkan --test TestCSharpTextureSampling --headless 10 --report test_output\csharp_texture_sampling_vulkan_unbound-sampler.json
Remove-Item Env:\CASCADED_CSHARP_TEX_SELFTEST
python Tools\check_csharp_texture_sampling.py
```

判定：CoreTests 退出码 0 且 `PASSED` 26 行；两份全量 report 里 22 条 `status` 全为 `pass`（全量同进程顺序跑 22 条；**第 23 条 `TestEditorAssetBrowser` 必须独立进程另跑**，两后端各一次 `status = pass`，不进全量 report）；两条 `TestGPUTiming` 与两条 `TestCSharpWindow*` 命令退出码 0；两条 `TestCSharpImporter`（Vulkan / D3D12）`status` 为 `pass`，且日志里的导入器级字节哈希与 `evidence/2.2-改前字节.txt` 逐字相同；`check_window_system_bindings.py` 两份日志都 0；`check_gpu_timing.py`、`check_bindable_audit.py`、`check_bindings.py`、`verify_readme.py`、`check_csharp_host_readme.py` 退出码 0；十份 `csharp_triangle_*` report 里正常两份 `pass`、负向八份 `fail` 且错误码对得上（skip-draw 是 `E_TRIANGLE_DRAW`），`check_csharp_triangle.py` 退出码 0；六份 `csharp_constant_color_*` report 里正常两份 `pass`、`bad-struct-colors` 两份 `fail` 且错误码 `E_CONSTCOLOR_PIXEL`、`skip-param` 两份 `fail` 且错误码 `E_CONSTCOLOR_PARAM` 并带「后端执行次数 0」，`check_csharp_constant_color.py` 退出码 0；八份 `csharp_structured_buffer_*` report 里正常两份 `pass`、`bad-buffer-color` 两份 `fail` 且错误码 `E_SB_PIXEL` 并带中心 `(0,0,255,255)`、`unbound-field` 两份与 `bad-buffer-name` 两份 `fail` 且错误码 `E_SB_BIND` 并带「后端执行次数 0」，`check_csharp_structured_buffer.py` 退出码 0；八份 `csharp_texture_sampling_*` report 里正常两份 `pass`、`bad-texture-color` 两份 `fail` 且错误码 `E_TEX_PIXEL` 并带中心 `(0,255,255,255)`、`unbound-image` 两份与 `unbound-sampler` 两份 `fail` 且错误码 `E_TEX_BIND` 并带「本次提交执行增量 0」与「上传执行 1」，`check_csharp_texture_sampling.py` 退出码 0；两条 `TestCSharpScene` 绿跑 `status = pass`、两档场景红态 `status = fail` 且错误码对得上（`skip-onupdate` → `E_SCENE_DT`、`bad-color` → `E_SCENE_PIXEL`）。新增/改动 C#Host 时再加 `TestScriptHost` 与 `Tools\check_csharp_host_failures.py`（ABI 期望版本随当前生成常量走，当前 v9）。

## C# 脚本层与 `--show-window-seconds`

`Scripting/Managed/UserScript.cs` 是**用户可写的脚本**：宿主 `Host.cs` 在**编号 1、2 两张函数表都绑定成功之后**调用它一次（整个进程只调一次），使用者改这个文件重新构建即生效，不必碰 `Host.cs`。脚本里建的窗口是弱持有：不 `CloseWindow()` 就一直活到进程退出（`Dispose` 只释放句柄）。

`GPUBackendTester` 的 `--show-window-seconds <N>`：所有选中的测试跑完之后，进程再保持 N 秒，每轮 `Tick()` + `UpdateSystem()`，用来让窗口留在屏幕上给人看见/截图。**没有默认值**：不传就不进这段循环；`N` 必须是正整数（`0` / 负数 / 非数字 / 溢出都非零退出并在 stderr 说明用法）。判据落在 `test_output/keep_window.log`：`保持窗口判据：通过/失败（循环 … 次 / Tick 调用 … 次 / 实际 … 秒 / 计数 … → …）`，`Tick` 次数必须大于 0 且等于循环次数。为什么单独一个文件：测试自己的日志此时已关闭、`stdout`/`stderr` 都已改道，`CA_LOG` 写进去看不到。

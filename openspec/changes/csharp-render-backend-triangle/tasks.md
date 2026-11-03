# csharp-render-backend-triangle 实施任务

用户故事：

- **US1**：C# 渲染调用方需要用自己的顶点数据、着色器和目标构建并提交简单三角形，而不接触原生调度器和具体建图类。
- **US2**：引擎维护者需要让新增渲染资源在提交、等待、重绑和关闭期间保持明确所有权与失效行为。
- **US3**：渲染验证人员需要两个后端的真实像素证据，并能证明跳过绘制或错误顶点会让判据失败。

证据统一写入 `openspec/changes/csharp-render-backend-triangle/evidence/`。每条红态必须来自缺陷仍存在时的真实命令；不得用预期失败文本代替运行输出，不得在仓库其他位置留下临时探针或日志。

## 1. 建立适配面、测试入口与红态

- [ ] 1.1 [US1][US2][US3] 新增 `Interface/RenderInterface/header/ScriptRenderBridge.h`，按 design 声明三个窄纯虚接口；把每条方法加入当前唯一 `bindable-audit.md` 与 `Tools/CodeGen/check_bindable_audit.py` 的 `INTERFACES`，同步表内接口数与方法合计，强化审计脚本拒绝重复行并用头文件实算数字核对汇总；在 `build.py` 登记主头和三个期望产物前缀。新增 `Test/CoreTests/ScriptRenderBridgeTest.cpp` 并在 `SerializerTest.cpp` 注册，写出原生图结构、字节复制、参数拒绝和生命周期断言。｜**验收**：在适配实现前运行 `python build.py --config Debug`，必须因适配实现或生成物缺失而实际失败，且审计表覆盖全部新方法；临时重复一行或写错汇总数字时审计必须非零，恢复后通过，相关输出一并存入 `evidence/adapter-red.log`。不得用保留空接口、跳过新方法或删除断言取得红态。
- [ ] 1.2 [US1][US2][US3] 在 `Scripting/Managed/SurfaceSignatureCheck.cs` 增加三个接口的完整公开签名断言，在 `Test/GPUBackendTester/Main.cpp` 注册 `TestCSharpTriangle` 并建立独立的 C++ 像素期望与原生读取入口，新增 `Tools/check_csharp_triangle.py` 的正常/负向报告判据。｜**验收**：在宿主和脚本实现前构建或运行必须实际失败，失败应包含渲染类型未绑定、适配器未登记、托管提交不存在或像素证据缺失之一，输出存为 `evidence/host-pixel-red.log`；不得先用 C++ 硬编码三角形或仅检查方法名取得通过。

## 2. 实现原生建图、验证与提交生命周期

- [ ] 2.1 [US1][US2] 在 `Interface/RenderInterface/private/ScriptRenderBridge.*` 实现目标与图对象：窗口目标、固定格式离屏目标、命名内部缓冲、同步上传复制、两个 `float3` 属性、三角形 pass、呈现和 pass 计数。所有无效布局、重复名称、未知引用、越界绘制和错误呈现目标必须在进入具体后端前拒绝。｜**验收**：1.1 的原生结构断言全部通过；上传后修改输入数组不改变原生字节，逐项核对属性、着色器、顶点数和清除色。分别构造大小不符、重复名称、属性越界、零顶点、超缓冲绘制和错误呈现，每项都返回非零状态、`GetLastError()` 点名参数且实现调用次数为 0；随后一次成功调用必须清空旧错误，红绿证据存为 `evidence/graph-builder-mutations.log`。
- [ ] 2.2 [US1][US2] 实现 `IScriptRenderBackend` 的目标创建、图创建、提交与 `WaitIdle`：主线程内新建调度器，待完成集合持有图强引用，等待后清理；提供仅供原生测试读取离屏 `ImageHandle` 的弱引用钩子。完成三个接口的真实生成物和目标句柄接线，不修改 `CodeGenMain.cpp`。｜**验收**：生成器与 `check_bindings.py` 的真实接口无桩、两侧对齐和公开表面判据通过；提交后释放托管图仍能执行并等待，释放后调用被挡下，错误类型和失效句柄不进入实现；原生测试能核对等待调用次数、待完成集合清理、窗口描述快照和真实呈现清单。临时移除待完成强引用、目标类型校验或等待计数时对应测试必须变红，证据存为 `evidence/submission-lifetime.log`。

## 3. 完成宿主与 C# 真实示例

- [ ] 3.1 [US1][US2] 更新 `CSharpHost/private/CSharpHost.cpp`、`CSharpHost/CMakeLists.txt`、`Scripting/Managed/EngineApi.cs` 与 `Host.cs`：手写 ABI 只追加三个接口编号并升级版本，链接适配实现，登记真实后端，允许图与目标表无默认实例，路由布局清单，启动和重绑覆盖三个新接口，关闭先清理渲染表面。｜**验收**：两侧 ABI 版本与 `sizeof` 一致，后端表返回真实适配器，图与目标表可空调用目标解析，缺依赖有明确错误；现有窗口取表和生成表版本不变。临时阻止空实例目标表或漏登记后端时对应测试必须失败，输出存为 `evidence/host-render-api.log`。
- [ ] 3.2 [US1][US2][US3] 更新 `Scripting/Managed/UserScript.cs`、`SurfaceSignatureCheck.cs` 和布局/公开值检查，加入独立于一次性 `OnEngineReady` 的 `OnRenderProbe`；`TestCSharpTriangle` 在自己的 Tick 前设置 `CASCADED_CSHARP_RENDER_PROBE=1` 并清理，加入 Debug 的 `skip-draw` / `bad-vertex` / `skip-present` / `skip-wait` 自检。C# 必须提供三个实色顶点、24 字节步长、位置偏移 0、颜色偏移 12、着色器路径、窗口与离屏目标、绘制和等待，并保留图与目标到原生核对完成；未启用探针不得创建 GPU 工作。｜**验收**：托管编译与启动通过，所有签名委托和真实 CLR 布局检查执行；全量运行中先前的 `OnEngineReady` 不妨碍本测试探针。正常探针实际调用 `AddBuffer`、两次同逻辑 `AddTrianglePass`、`Present`、`SubmitGraph` 和 `WaitIdle`，呈现清单与等待计数都有断言；四种自检和跳过布局检查分别变红，证据存为 `evidence/managed-triangle.log`。

## 4. 完成双后端像素验收与文档

- [ ] 4.1 [US1][US3] 在 `TestCSharpTriangle` 中通过既有 `Readback` 读取同一次 C# 提交的离屏目标，用窗口描述快照核对尺寸和格式，精确检查绿色背景与纯红中心，并核对真实呈现清单、等待次数和待完成集合清理，保存后端对应的日志和 PNG；完成 `Tools/check_csharp_triangle.py`，分别识别两个后端的正常 `pass` 与 `skip-draw` / `bad-vertex` / `skip-present` / `skip-wait` 的预期 `fail`。｜**验收**：D3D12 与 Vulkan 正常像素全部通过；四种负向模式在两个后端都给出对应失败，脚本在缺正常证据或负向被错误接受时非零。完整命令、退出码和读数存为 `evidence/pixel-validation.log`，不得只保存非空 PNG或使用依赖尺寸的插值容差。
- [ ] 4.2 [US2][US3] 扩充 `Tools/CodeGen/check_bindings.py` 对三个真实接口的方法集合、公开签名和真实实现调用判据；把 `GPUBufferDescriptor` 登记为共享公开记录，允许其两个定宽 `uint64_t` 字段使用 C# `ulong`，同时继续拒绝公开方法中的原生句柄和未登记 `ulong`。更新 `CSharpHost/README.md` 的手写 ABI、接口编号、依赖与探针开关说明；核对 `Tools/CodeGen/README.md` 是否只需更新真实接口清单而不新增形状。｜**验收**：分别临时删除一个公开方法、把一个原生转发改为零值桩、漏登记审计行、删错 `GPUBufferDescriptor` 字段或在公开方法中放入原生句柄，每项检查或构建都实际失败并点名接口/方法/类型，恢复后通过，存为 `evidence/render-binding-checks.log`；`python Tools/CodeGen/verify_readme.py` 退出 0，文档命令与真实 ABI 一致。
- [ ] 4.3 [US3] 更新 `docs/testing.md` 的 CoreTests 文件清单、GPUBackendTester 清单、第三层脚本与实际读数；把 `TODO/006-csharp-render-backend-binding.md` 标为已升级到本 change，并把 `docs/ROADMAP.md` 的第二项标为已建立本 change。｜**验收**：文档中的所有路径存在，测试清单与 `GPUBackendTester.exe --list` 及 `SerializerTest.cpp` 注册一致，PASSED 和测试数量来自本次真实输出而非手算；Roadmap 不把本 change 写成已归档，也不把后续通用渲染框架写成已批准。

## 5. 全量回归与最终构建

- [ ] 5.1 [US1][US2][US3] 在成功的增量构建后运行 `out/build/x64-debug/bin/CoreTests.exe`、`python Tools/CodeGen/check_bindings.py --product-dir out/build/x64-debug/codegen/bindings`、`python Tools/CodeGen/check_bindable_audit.py`、`python Tools/check_csharp_triangle.py`、`python Tools/check_csharp_host_failures.py`，以及两个后端完整 `GPUBackendTester.exe --headless 10`；随后运行 `python Tools/check_window_system_bindings.py --log test_output/TestCSharpWindow.log` 和 `python Tools/check_window_system_bindings.py --log test_output/TestCSharpWindowCallbacks.log --callbacks`。｜**验收**：所有命令退出 0，新像素测试与既有窗口、回调、弱句柄、借用句柄、宿主失败路径和两个后端全套测试均通过；报告与日志来自本次运行，全部输出和退出码存入 `evidence/final-regression.log`，任何失败都必须修复后重跑。
- [ ] 5.2 [US1][US2][US3] 最终运行 `python build.py --config Debug`，若失败则分析并修复直到输出 `BUILD SUCCESSFUL`；随后运行 `openspec validate csharp-render-backend-triangle`、`openspec validate --all`、`git diff --check -- . ':(exclude)openspec/changes/csharp-render-backend-triangle/evidence/**'` 和 `openspec instructions apply --change csharp-render-backend-triangle --json`。｜**验收**：构建成功，OpenSpec 校验通过，非证据文件没有空白错误，`remaining` 为 0 且本文件无未勾项；证据目录保留所有已声称的红态与恢复记录，工作区没有临时探针、崩溃转储或越出本 change 范围的实现改动。最终命令与输出存入 `evidence/final-build.log`。

## 6. 既有判据修订对照

本 change 只新增能力和测试，不删除或放松既有判据。`docs/testing.md` 的文件数、测试数和命令读数必须以实际运行更新；`CSharpHost` 的手写 ABI 只在末尾追加字段并升级版本，生成接口表版本和窗口断言保持不变。任何为适应新渲染面而修改旧窗口、回调、句柄或像素断言的行为都必须视为缺陷并恢复，不得以“新功能需要”作为放宽理由。

### 6.1 无效参数的错误通道

- **改前**：建图与提交方法返回 `void`，验收只写“每项都非零”，没有可见的错误返回或文本。
- **改后**：可失败方法返回 `int32_t`，0 为成功、非 0 为拒绝；`GetLastError()` 必须点名参数，成功调用清空旧错误。判据同时检查状态、文本和实现未进入，严格程度提高。

### 6.2 像素判据

- **改前**：中心三个通道期望 85 并允许 84–86，目标尺寸未固定；320×240 的正确插值也可能越界。
- **改后**：三个顶点使用相同红色，中心精确要求 `(255, 0, 0, 255)`，不再依赖尺寸或插值容差；`bad-vertex` 改一个顶点为蓝色，仍以中心非纯红证明脚本数据参与插值。

### 6.3 探针调用时机

- **改前**：渲染探针依附进程只调用一次的 `OnEngineReady`，会被先前的 `TestScriptHost` 消耗。
- **改后**：使用独立的 `OnRenderProbe` 和独立调用标记，由 `TestCSharpTriangle` 在自己的 Tick 前设置开关并清理；新增全量运行场景，要求日志与像素来自同一次测试。

### 6.4 呈现目标数量

- **改前**：“每个图至多一个呈现目标”被错误归给 Roadmap，代码和路线图中没有该限制。
- **改后**：删除这个没有出处的上限；`Present` 仍只接受后台缓冲，示例只用一个窗口，现有 `GPUGraph` 的多目标容器语义不被改变。

### 6.5 呈现与等待的可观察判据

- **改前**：只要求删除 `Present` 或 `WaitIdle` 后测试变红，但离屏像素与后端内部同步可能使删除不可见。
- **改后**：原生测试直接检查真实呈现清单、等待调用次数和待完成集合清理，并新增 `skip-present` 与 `skip-wait` 两个独立负向模式。

### 6.6 审计与公开值类型判据

- **改前**：重复审计行与旧汇总数字不会使脚本失败；`GPUBufferDescriptor` 未登记，且共享记录中的合法 `ulong` 被全文禁用。
- **改后**：重复行和汇总不符必须非零；`GPUBufferDescriptor` 的两个定宽字段允许 `ulong`，公开方法中的原生句柄和未登记 `ulong` 仍拒绝，原有限制没有整体取消。

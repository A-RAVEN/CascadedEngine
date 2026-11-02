# csharp-render-binding-foundations

## Why

`docs/ROADMAP.md` 阶段 4 把“生成器与句柄基础能力”列为 C# 简单三角形之前的前置 change。当前 `CodeGen.exe` 无法公开平坦记录和枚举，不能投影渲染描述符里的枚举、标志位与便利方法，也不能组合转换智能指针实参、字节数据和其他参数；资源句柄还按发行接口而非目标资源类型校验。直接给 `CRenderBackend` 或 `GPUGraph` 加 `CA_BIND` 只会得到空壳、缺失的托管方法或无法编译的产物。

## What Changes

- 扩展绑定记录与公开值类型：支持平坦及嵌套的公开数据记录、记录字段中的枚举与 `uenum::EnumFlags<T>`，允许不改变数据布局的非虚便利方法；补齐 `bool` 和定宽整数映射，并用 `Marshal.SizeOf` / `Marshal.OffsetOf` 验证真实 CLR 布局。虚函数、非公开字段、非默认构造记录及含字符串、容器、指针或智能指针的记录仍在生成期拒绝。
- 从真实 C++ 声明生成公开 C# 枚举与记录。多个接口使用同一值类型时，共享一份确定的公开产物；同名但不同限定名的类型必须显式拒绝，不能静默覆盖。
- 为独立参数补充包装值投影：`cacore::NameHash` / `cacore::PathHash` 按 UTF-8 文本转换，`cacore::HashObj<T>` 按其所含值转换；`T` 必须同时满足记录条件和具有显式哈希来源。上述投影不声称支持返回方向或嵌入其他记录。
- 为 `castl::vector<uint8_t> const&` 补充字节入参投影，公开 C# 使用 `byte[]`，调用期间固定数组并在原生侧重建向量；空数组、空指针与非零长度的组合必须有确定行为。
- 把逐方法的 C ABI、原生实参转换、返回值转换和公开 C# 调用组成同一份转换描述，使记录、枚举、标志位、文本、字节和句柄能与智能指针返回组合，不再由分支先后顺序决定哪些参数被漏转。
- 句柄表项同时记录发行者与目标对象的完整限定类型。现有原生入口继续按发行者校验，新增目标接口入口供公开 C# 资源对象解析和释放；函数表只追加槽，ABI 版本不变。`shared_ptr` 入参按目标类型反查并重建强引用，弱返回句柄临时取得强引用，借用句柄不得伪装成可持有的智能指针；`weak_ptr` 入参继续拒绝。`build.py` 从实际 `CA_BIND` 声明聚合短名与完整限定名，未登记的抽象接口返回不得生成悬空的 C# 类型。
- 扩充生成夹具、`Tools/CodeGen/check_bindings.py`、CoreTests 与托管行为断言，实际核对公开枚举值、记录字段顺序、CLR 布局、目标释放和释放后调用，并同步 `Tools/CodeGen/README.md`。现有窗口绑定的公开签名和回调、弱句柄、借用句柄及原生发行者行为必须保持。

## Non-goals

- 不给 `CRenderBackend`、`GPUGraph`、`GPUBuffer`、`GPUTexture` 或其他生产渲染类型加 `CA_BIND`，不创建脚本专用渲染适配接口，不接渲染后端到 `CSharpHost`，也不实现 C# 简单三角形。
- 不绑定具体建图类、静态工厂、非纯虚方法、模板方法或链式 `Class&` 返回。
- 不实现 `std::span`、托管回读、跨调用异步字节缓冲区、GPU 计时、容器出参、映射容器、联合体、定长数组或通用序列化块。
- 不支持包装值返回、包装值嵌入记录、`weak_ptr` 入参，也不支持含文本或容器的记录镜像；`weak_ptr` 返回形成的弱句柄仍按本 change 的规则支持。
- 不处理通用重载改名、`CA_BIND_SKIP` 与镜像预收集的顺序问题，也不借本 change 选择性拼接现有 `CRenderBackend`。
- 不改变函数表 ABI 版本，不引入新的外部依赖，不修改渲染结果或原生调度策略。

## Capabilities

### New Capabilities

无。本 change 只扩展现有绑定能力，不建立渲染运行时能力。

### Modified Capabilities

- `csharp-bindable-surface`：补充枚举与标志位、公开数据记录、包装值、字节入参以及跨接口目标类型句柄的投影和拒绝规则。
- `csharp-bindings`：规定共享公开值类型产物、可组合的逐方法转换、公开 C# 枚举与记录、字节数组固定，以及跨头文件句柄解析和释放行为。

## Impact

- 生成器与文档：`Tools/CodeGen/CodeGenMain.cpp`、`Tools/CodeGen/README.md`、`Tools/CodeGen/check_bindings.py`。
- 生成夹具与期望：`Tools/CodeGen/BindableShapes*.h`、`Tools/CodeGen/BindableShapes.expected.txt`，以及按跨头文件场景新增的夹具头。
- 构建接线：`build.py` 的实际接口收集、完整限定名清单与生成期对账；`CSharpHost/private/CSharpHost.cpp` 只更新追加槽后的函数表大小断言；`Scripting/Managed/Scripting.Managed.csproj` 继续按 `*.Bindings.cs` 编译所有公开产物。
- 测试：`Test/CoreTests/Bindings*.cpp`、必要的 CoreTests 注册点，以及 `Scripting/Managed/SurfaceSignatureCheck.cs`、`Scripting/Managed/BindingLayout.cs`、`Scripting/Managed/Host.cs` 和 `Tools/check_window_system_bindings.py` 的布局、公开值、目标释放与释放后调用断言。
- 回归范围：现有 `IWindow` / `IWindowSystem` 生成物、强句柄、弱句柄、借用句柄、回调和布局自检。
- 明确不影响：生产渲染接口、D3D12 / Vulkan 后端、`CSharpHost` 渲染实例登记和 `TODO/006-csharp-render-backend-binding.md` 中的简单三角形实现。

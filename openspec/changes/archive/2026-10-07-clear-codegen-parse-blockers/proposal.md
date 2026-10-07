## Why

`build.py` 接入清单里本该有、却进不去的有四个键类型：`graphics_backend::ViewRectData`、
`graphics_backend::GPUPipelineStateKey`、`graphics_backend::VertexInputBindingData` 和它内部的
`InputElementDesc`。它们今天只能靠手写 `cacore::codegen::custom_hash_trait` 特化来当哈希键 —— 卡住它们的
不是设计（这四个类型没有指针成员、没有私有成员），是工具的保真门以 **exit 6** 拒收：
`Interface/RenderInterface/header/GPUGraph.h` **自身**有 5 条错误级诊断；
`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 所在的 include 链把 clang 的错误上限
（默认 20 条，见 clang 官方手册 `-ferror-limit`）打满，工具报 `too many errors emitted, stopping now`。

为什么现在修：

1. 那条链现在**踩在错误上限的边上**。门只在"主头自身有错误"或"诊断升级成致命"时才拒，include 链里的错误
   单独不算 —— 所以今天过的行，明天往链上加一个头就可能一起被拒。用 `-ferror-limit=1` 把链上任何一条错误
   升级成致命之后实测：连已经在清单里的 `RectSate` 与 `ShaderLibrary` 都是脏的。这是同一根因的第二次出现，
   该修机制而不是再记一条边界。
2. 这 5 条加链上那几条，逐条都定位到了具体文件与具体写法，而且**都是一行到几行的写法问题**，不是环境缺东西
   （缺东西那类：`directx/d3dx12.h` 那条 2026-10-05 实测已经是 exit 0）。
3. 归档的 `retire-legacy-serde-hash` 把这四个类型排除在接入清单之外的**真实记录**是它 `tasks.md` 里的补记段
   （理由写的是"解析这两个头时工具 exit 6"）与 `build.py` 清单里那条注释 —— 那是环境性的理由，不是设计性的：
   环境修好，手写特化也就该换成生成物。

## What Changes

- **清掉 `GPUGraph.h` 自身那 5 条诊断**（改之前实测逐字：3 × `call to 'error_with_location' is ambiguous`、
  2 × `no member named 'second' in 'std::shared_ptr<graphics_backend::ShaderStruct>'`）。
- **清掉 include 链上另外三处**：`Interface/RenderInterface/header/Common.h:372` 的枚举窄化
  （`enum class EResourceUsage : uint32_t` 里的 `eAll = ~0`，标准不允许、MSVC 放行）；
  `CACore/header/DebugUtils.h` 的两个 `error_with_location` 重载对字符串字面量判不出高下
  （`Interface/RenderInterface/header/ShaderBindingBuilder.h:176/177` 是同一根因的第二次出现）；
  `Tools/CodeGen/clang_shim/cpuid.h` 重定义 clang 内建 `__cpuid`（工具自己的解析期垫片）。
- **修垫片的形状**：它的 `__cpuid_count` 是 3 个参数的函数，而 DirectXMath 在 clang 身份下真正调用的是
  6 个参数的形式（实测：把 `_XM_AVX2_INTRINSICS_` 打开就是 `no matching function for call to '__cpuid_count'`）。
- **四个键类型进接入清单**：先给类型加标注（`CA_REFLECT(serializable)`，嵌套类型写在类体内）——
  工具只对带标注的类型出码；再加 `build.py` 的行、各自所在头 include 生成物、删掉对应的手写 `custom_hash_trait`、
  在**真实消费它们的编译单元**里加字段表与"哈希入口看得见生成特化"的断言。清单 72 行 → 76 行。
- **加一条"哈希值不许变"的守卫，并且先证明它会红**：迁移前先对这四个类型的实例经**既有入口**
  （`cacore::hash<T>` / `HashObj<T>::GetHash()`，不是直接调特化）测出当前哈希值并落档；换成生成物后必须
  **相等**。同时对一个填充字节被改成不同值的对象做反向对照（两值必须不同），证明这条守卫不是恒真。
- **加一条"链上错误清零"的探针**：用 `-ferror-limit=1` 把链上任何一条错误升级成可观测的拒绝
  （实测：`RectSate` 不传这个开关是 exit 0、stderr 空；传了就是 exit 6 并打印 `Common.h:372` 那条）。
- **文档**：`Tools/CodeGen/README.md` 补一节"解析期垫片"（它是什么、为什么仓库里需要它、不许做什么、怎么复核）。

## Capabilities

### New Capabilities

（无）

### Modified Capabilities

- `codegen-framework`：**新增**一条要求「解析期垫片不得重定义编译器内建，且必须写在文档里」——
  本次是新增（`## ADDED`），不是给基线那条「解析头文件所用的编译环境按模块推导，且不猜宏」追加内容；
  归档时基线里会多出一条并列要求。
- `codegen-type-migration`：**新增**一条要求「被解析门拒收的键类型必须迁进接入清单」——四个类型进清单、
  三个发射器退出码 0、手写特化删除、解析读数由 6 变 0，且 MUST NOT 靠放松保真门达成。

## Non-goals

- 不迁 `graphics_backend::ImageHandle` / `BufferHandle` / `ShaderSetData`（含指针成员，工具的**形状**拒绝）
  与 `graphics_backend::ShaderResourceSet` / `VulkanShaderResourceSet`（前者是"名字 → 结构体指针"的映射，
  容器元素是指针；后者按缓存好的 `hash` 成员算）。它们的理由写在源码注释里
  （`D3D12RenderBackend/private/GPUGraph/GPUResourceBindingInstance.h` 与
  `VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h`），**不在**归档 change 的「已知边界」一节里
  —— 那一节只有 `ShaderSetData` 与两个句柄两条。本次一个字不动。
- 不给 vendored 的 LLVM 补 clang 资源目录（`E:\Projects\CACodeGen\ExternalLibraries\LLVM` 在本仓库之外），
  本次只把仓库内那份垫片改成不重定义内建的形状。
- 不放松工具的保真门：本次的验收是"诊断清零"，不是"把门调松"。门本身（`untrustworthy()` 的判法）不改，
  也不许往推导环境里塞 `-ferror-limit` 之类把上限抬高的开关。
- 不动 `D3D12RenderBackend` 的推导环境：2026-10-05 实测 `D3D12RenderBackend/private/D3D12Includes.h` 与
  `D3D12RenderBackend/private/ShaderLibrary/D3D12ShaderStruct.h` 都不再有解析失败。

## Impact

- 引擎头（2 个文件）：`Interface/RenderInterface/header/GPUGraph.h`、`Interface/RenderInterface/header/Common.h`
- CACore 头（1 个文件）：`CACore/header/DebugUtils.h`
- 后端头（1 个文件）：`D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h`（加标注、include 生成物、删手写特化）
- 工具（2 个文件）：`Tools/CodeGen/clang_shim/cpuid.h`、`Tools/CodeGen/README.md`
- 构建清单：`build.py` 的 `CODEGEN_ANNOTATED`（72 行 → 76 行）
- 断言与用例：Interface 侧 `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`（`ViewRectData`）、
  D3D12 侧该模块自己的编译单元（另外三个类型）—— 具体落点见 tasks
- 运行时风险面：这四个键的哈希值若变化，会改变以它们为键的内存字典的分桶（语义不变，但"值有没有变"必须逐值证明），
  由上面那条守卫钉住。**"这四个类型不参与落盘"是待核实的断言，不是结论**：任务里要求逐个确认它们不出现在任何
  已迁移类型的成员链上（出现在成员链上就会进落盘字节）。

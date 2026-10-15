# 哈希算法来源的盘点（探针读数）

本文件是 `require-explicit-hash-source` 任务 1.x 的读数。**动代码之前**先跑探针：把
`CACore/header/CodeGenHash.h` 里两条隐式兜底**临时**关掉、构建一次，看谁在靠它们活着。

## 落地读数（2.x / 3.x / 4.x / 5.x，2026-10-11）

### 2.1 / 2.3 分派链：删掉两档之后只剩六档

1. 显式特化（生成物或手写）→ 用它；
2. 指针句柄 / 裸指针 → 按地址；
3. 基本类型 / 枚举 → 自身字节；
4. 容器 → 排规范顺序后逐元素；
5. 包装类型 → 拆开算内层；
6. 否则 → **静态断言**，正文点名三条出路（`CA_REFLECT` / 手写 `custom_hash_trait` / `CA_HASH_BY_BYTES`）。

删掉的两档（改前逐字）：`else if constexpr (std::is_trivially_copyable_v<objType>) { hash_raw(&object, sizeof(objType)); }`
与 `else if constexpr (has_std_hash<objType>) { const auto standardHash = castl::hash<objType>{}(object); hash_raw(&standardHash, sizeof(standardHash)); }`。

### 2.2 `CA_HASH_BY_BYTES`（`CACore/header/CodeGenHashFwd.h`）

```cpp
#define CA_HASH_BY_BYTES(Type)                                                                        \
    static_assert(::std::has_unique_object_representations_v<Type>,                                    \
                  "CA_HASH_BY_BYTES: 这个类型有填充字节或有多种表示的成员（例如 float / double），"    \
                  "按整块字节算哈希会把这些字节也算进去；请改用 CA_REFLECT 生成字段级特化");           \
    namespace cacore::codegen                                                                          \
    {                                                                                                  \
        template<>                                                                                     \
        struct custom_hash_trait<Type>                                                                 \
        {                                                                                              \
            static void hash(Type const& obj, auto& hasher)                                            \
            {                                                                                          \
                hasher.hash_raw(&obj, sizeof(Type));                                                   \
            }                                                                                          \
        };                                                                                             \
    }
```

写在**全局作用域**（宏体里要开 `namespace cacore::codegen { … }`）。

### 2.5 / 2.6 机制自检（两条都能失败）

- **2.5**：把 `CA_HASH_BY_BYTES(::vk::PushConstantRange)` 注释掉 → 构建失败，断言正文是那句
  "没有可用的哈希来源…"，实例化链最内层是 `E=vk::PushConstantRange` ✓；改回 → 编过。
- **2.6**：给含 `float` 成员的临时类型写 `CA_HASH_BY_BYTES` →
  `Test/CoreTests/GeneratedHashTest.cpp(348): error C2338: static assertion failed: 'CA_HASH_BY_BYTES: 这个类型有填充字节…'` ✓；
  删掉该探针 → 编过。

### 2.4 两个入口同链

`CACore/header/HashKeys.h` 的 `cacore::hash<T>` 里那句
`if constexpr (has_std_hash<T> && std::is_same_v<result_type, size_t>) { return std::hash<T>{}(obj); }` 删掉，
与 `hasher::hash<T>` 合成一条链。**改造前后 CoreTests 的哈希读数逐字相同**（见 5.3），
所以 3.4 **不需要补任何委托特化** —— 这是读数支持的结论，不是"省一步"。

### 3.1 / 3.2 补声明的实测（与 1.1 清单的对照）

| 类型 | 实际处置 | 读数 |
|---|---|---|
| `graphics_backend::GPUTextureView` | **改成按字段手写特化**（原计划是补 `CA_HASH_BY_BYTES`） | 先按字节声明 → 宏里那条断言当场挡住（`has_unique_object_representations_v` 为**假**：`ETextureAspect` 与后面 `uint32_t` 之间有填充）→ 说明它以前"按整块字节算"把填充也算了进去，与它自己的 `operator<=>`（按成员比）不一致；改成逐字段后编过 |
| `graphics_backend::GPUTextureSwizzle` | `CA_HASH_BY_BYTES`（4 个 `EColorChannel`，无填充） | 断言通过 |
| `vk::Flags<BitType>` | **泛化**特化（一条覆盖全部 flag 类型：`PipelineLayoutCreateFlags` / `DescriptorSetLayoutCreateFlags` / `PipelineInputAssemblyStateCreateFlags` …） | 断言通过 |
| `vk::VertexInputBindingDescription`、`vk::VertexInputAttributeDescription`、`vk::PushConstantRange` | `CA_HASH_BY_BYTES`，写在模块的 `VulkanIncludes.h` 里 | 断言通过 |

⚠ 这三条是**编译器**找出来的、1.1 的三份清单里没有：它们出现在**容器的元素位置**
（`castl::vector<vk::…>`，写在手写特化的函数体里），而盘点脚本按"当键用的写法"扫，扫不到这种位置
（脚本的边界写在 4.x 那节与 `Tools/CodeGen/README.md` 里）。

### 3.3 那 32 个站点的修法与成对读数

`Interface/RenderInterface/header/CPipelineStateObject.h` 末尾从 **1 份**产物加到 **6 份**
（`CPipelineStateObject` 自己 + 五个成员类型），并把那段"其余交给消费方 include"的注释改成实测说明。

- **红态**（把那六份再摘掉）：构建失败，**26 个编译单元**，断言点名 `CPipelineStateObject`；跨模块站点例如
  `VulkanRenderBackendNew/private/VulkanObjectManaging/DescriptorSetLayoutManager.cpp`、
  `D3D12RenderBackend/private/GPUObjects/GPUComputePipelineInstance.cpp`。
- **绿态**（补回）：`BUILD SUCCESSFUL`。

### 4.1 – 4.4 盘点脚本

`Tools/CodeGen/check_hash_sources.py`（+ 例外清单 `Tools/CodeGen/hash_source_exceptions.txt`，
+ `Tools/CodeGen/README.md` 第十节）。读数：

```
python Tools/CodeGen/check_hash_sources.py
扫描 270 个文件、命中 0 个来源不明的键类型
例外 1 个（hash_source_exceptions.txt，逐条列出）：
  PipelineStateDesc：…（唯一的用户 PipelineStatesDic 是死代码；补它要连带
  ShaderStateDescriptor / CRenderPassInfo / D3DVertexBindingSlot，另立改动）
清单 74 个类型；键类型共 35 个
EXIT=0
```

自检（4.2）：把 `GPUTexture.h` 里 `custom_hash_trait<::graphics_backend::GPUTextureView>` 那一行注释掉 →
`命中 1 个来源不明的键类型` + 点名 `GPUTextureView` + `EXIT=1`；改回 → `命中 0` + `EXIT=0`。

**逐条对账**（1.1 的清单 → 4.1 的读数，34 个键类型 + 例外 1 个）：

| 类别 | 条数 | 说明 |
|---|---|---|
| 生成物 | 17 个类型 | 接入清单 76 行（74 个不同短名）里的键类型 |
| 手写声明 | 8 个类型 | `BufferHandle` / `ImageHandle` / `ComPtr` / `ShaderResourceSet` / `VulkanShaderResourceSet` / `TypedVKHashVal` / `NameHash` / `PathHash` / `HashObj` 等 |
| 内置档 | 9 个类型 | 标量、`Flags`/`FlagBits`、智能指针、`VmaAllocation`、模块 Includes 头里声明过的第三方类型 |
| 例外 | 1 个类型 | `PipelineStateDesc`（见上，另立改动） |

### 5.x 判据

| 项 | 读数 |
|---|---|
| 5.1 `--codegen-all` 构建 | `BUILD SUCCESSFUL`；76 行进度、76 行 `generated`、`CODEGEN FAILED` 0、非零退出 0；`codegen 44.1s / 编译 70.0s / 合计 114.1s` |
| 5.2 `CoreTests` | 退出码 0、`PASSED` 12、`FAILED` 0 |
| 5.2 两个后端 tester | vulkan：退出码 0、`result.json` 8 条 status 全 pass；d3d12：同 |
| **5.3 值不变** | 改造前后逐字相同：`legacy hash entry uses the generated trait = 1 (legacy=15402126173295036644 fieldwise=15402126173295036644)`；`legacy hash host NameHash = 11718336935620011197`、`PathHash = 12386457803575074623`、`HashObj<int> = 7869321708915449410`；`key hash readings = 9566659391000707361 / 7588715830773829420 / 17516807725481476295 / 17773842414398720855`；`CPipelineStateObject bytes == old runtime = 1 (bytes=301)`；`hash ignores padding = 1` |
| 5.4 漏了就编不过 | 见 3.3 的红/绿成对读数（26 个单元点名 `CPipelineStateObject`） |
| 5.5 两个入口同值 | `two entries NameHash = 11718336935620011197 / 11718336935620011197`；`PathHash = 12386457803575074623 / …`；`HashObj<int> = 7869321708915449410 / …`；`EmitSample = 15402126173295036644 / …`；`CPipelineStateObject = 16012413602016233964 / …`（五对全等） |

## 探针怎么跑

1. 两条隐式兜底的条件临时改成 `&& false`：
   - `else if constexpr (std::is_trivially_copyable_v<objType>)`（"平凡可拷贝就按整块字节"）
   - `else if constexpr (has_std_hash<objType>)`（隐式的 `std::hash` 来源）
2. `build.py` 的 `build()` 里给 `cmake --build` 透传 `-- -k 0`（临时）：ninja 默认第一条失败就停，
   不透传就只拿得到一个编译单元的读数。
3. `python build.py --config Debug`（**预期失败**），收日志、按实例化链分类。
4. 跑完把上面两处全部改回：回退后 `git status --porcelain` = 0、`python build.py --config Debug` → `BUILD SUCCESSFUL`。

## 读数一：谁在靠这两条兜底活着（37 处，5 个类型）

| 最内层类型 | 站点数 | 性质 |
|---|---|---|
| `DepthStencilStates` | **32** | **已在接入清单里、却在 32 个编译单元里退化** —— 真缺陷（补 include，不是补声明） |
| `graphics_backend::GPUTextureView` | 3 | 既没标注、也没手写特化 → 今天按整块字节算（属于"要显式声明"那一类） |
| `graphics_backend::PipelineLayoutCache` | 1 | **有**手写特化（`VulkanRenderBackendNew/private/PipelineStates/PipelineLayout.h`），但那个单元看不见它 |
| `graphics_backend::VertexInputStatesCache` | 1 | 同上（特化在 `VulkanRenderBackendNew/private/PipelineStates/VertexInputStates.h`） |
| `graphics_backend::DescriptorSetLayoutCache` | 1 | 同上 |

**⚠ 第一次抽取抽错了类型，读数留档**：只看实例化链的第一帧会读到 `CPipelineStateObject`，于是"37 个单元缺
`CPipelineStateObject` 的特化"这个结论是错的。真实链条的最内层是：

```
GPUGraph.h：HashObj<CPipelineStateObject> 实例化
 → cacore::hash<CPipelineStateObject, cahash::sha256_hash>::operator()
 → hasher<sha256_hash>::hash<CPipelineStateObject>
 → custom_hash_trait<CPipelineStateObject>::hash            ← 这一层的特化是**可见的**（产物已被 include）
 → out\build\x64-debug\codegen\Interface_RenderInterface\CPipelineStateObject.codegen.h(92):
     cgHashContainerValue(h, obj.depthStencilStates)
 → cgHashContainerValue<hasher_t, DepthStencilStates>
 → hasher<sha256_hash>::hash<DepthStencilStates>            ← 漏点在这里
 → CodeGenHash.h(182) 静态断言
```

教训：**认类型要看实例化链的最内层，不能看第一帧。**

## 读数二：32 个站点的根因是那条 include 约定（37 → 6）

`Interface/RenderInterface/header/CPipelineStateObject.h` 末尾只 include 了 `RectSate.codegen.h`，并逐字写着：

> `// RectSate` 的生成物（哈希特化给 `HashObj<RectSate>` 用；上面那五个类型的生成物由消费方自己 include，
> `// 见 Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp 与 PipelineStateObjectTest.cpp`）。

探针逐步实测（每一步都跑一次全量构建）：

| 改动 | 断言命中 |
|---|---|
| 原样 | 37 |
| 在 `GPUGraph.h` **末尾**补 `CPipelineStateObject.codegen.h` | 37 |
| 在 `GPUGraph.h` 的 include 段（第一次用到 `HashObj<CPipelineStateObject>` **之前**）补同一行 | 37 |
| 在 `CPipelineStateObject.h` 末尾补它**自己**的产物 | 37 |
| 再把它的**五个成员类型**的产物也补上（该头末尾共 6 行） | **6** |

结论：这 32 个站点不是"某个头忘了一行"，而是**"类型自己的头只 include 一部分产物、其余交给消费方"这条约定**
造成的 —— 只要某个单元用到 `HashObj<CPipelineStateObject>` 就会连带实例化成员类型的哈希，而它几乎不可能
恰好把六个成员类型的产物都 include 上。剩下 6 个站点与这条约定无关。

（"补在末尾太晚 / 补在使用点之前才有效"这条**本次没有验证成立**：两种放法在原样状态下都是 37，因为当时漏的
是成员类型。位置规则留到 apply 时用"成员类型修好之后"的状态再测。）

## 读数三：可见性机制的一次对照（能回答"在不在"，回答不了"缺哪个"）

在 `CPipelineStateObject.h` 末尾与 `GPUGraph.h` 末尾各放一条
`static_assert(cacore::codegen::has_custom_hash_func<::CPipelineStateObject, cacore::codegen::hasher<>>)`：
**两处都是 0 命中**（都可见），而同一次构建里 37 个单元照样报错 —— 因为报错的不是 `CPipelineStateObject`
本身，而是它链路下游的成员类型。所以"在某个头里放静态断言"这种自检**只能证明**这一层的特化在不在，
要回答"到底缺哪个类型"必须看实例化链。

## 读数四（**未做完**）：两个入口不同链

- 代码事实：`cacore::hash<T>`（`CACore/header/HashKeys.h`）里逐字写着
  `if constexpr (has_std_hash<T> && std::is_same_v<result_type, size_t>)` → `return std::hash<T>{}(obj);`；
  而 `hasher::hash<T>`（`CACore/header/CodeGenHash.h`）是"**先看生成特化**、`std::hash` 垫在最后一档"。
  两个入口条件不同 → 顺序不同。
- 三个"双来源"样本：`cacore::PathHash` / `cacore::NameHash` / `cacore::HashObj<...>` **同时**有手写
  `custom_hash_trait` 与 `namespace std` 里的 `hash` 特化（都在 `CACore/header/HashKeys.h` 里）。
- ⚠ **"藏掉一个来源、两个入口各自怎样"的运行期读数本次没做**（要临时改 `Test/CoreTests/` 并跑一次 CoreTests）：
  任务 1.3 只交了上面这两条静态事实，**运行期一半留到 apply**。

## 读数五（1.2，运行期）：摘掉一个编译单元的产物 → **编得过**，哈希值悄悄变了

临时把 `Test/CoreTests/GeneratedHashTest.cpp` 里那四份产物的 `#include` 注释掉
（`CPipelineStateObject.codegen.h` / `RasterizerStates.codegen.h` / `DepthStencilStates.codegen.h` /
`StencilStates.codegen.h`）：

| | 读数 |
|---|---|
| `python build.py --config Debug` | **`BUILD SUCCESSFUL`** —— 编译期一句提示都没有 |
| `CoreTests.exe` | 退出码 **1**，`PASSED` **9** / `FAILED` **2** |
| 失败的两条 | `Generated hash test FAILED`；`CPipelineStateObject hash ignores padding = 0 (fields equal = 1, overall bytes differ = 1, HashObj equal = 0, control differs = 1)` |

把四份 `#include` 补回之后：同一条判据回到 **1**（`padding differs = 1, fields equal = 1, raw bytes differ = 1`），
`PASSED` 回到 10。这就是归档 change 记的那次"填充字节不进哈希"由 1 掉成 0 —— 现在**可以按需复现**。

## 读数六（1.3，运行期）：两个入口的来源不同

临时把 `CACore/header/HashKeys.h` 里 `custom_hash_trait<::cacore::NameHash>` 那一段用 `#if 0` 藏起来，
并在 `Test/CoreTests/GeneratedHashTest.cpp` 放两条断言（**编过即证明**）：

```cpp
static_assert(!cacore::codegen::has_custom_hash_func<cacore::NameHash, cacore::codegen::hasher<>>, "…");
static_assert(cacore::codegen::has_std_hash<cacore::NameHash>, "…");
```

| | codegen 入口（`hasher<>::hash`） | `std::hash` 入口 |
|---|---|---|
| 手写特化在 | 有来源，读数 `11718336935620011197` | 有来源（`namespace std { struct hash<cacore::NameHash> }`） |
| 藏掉手写特化 | **没有来源**（上面那条断言编过即证明）→ 悄悄换整块字节 → **`16222291486138785658`** | **照样有来源**（另一条断言编过即证明） |

两条断言都编过（`BUILD SUCCESSFUL`）= "一个入口丢了来源、另一个入口不受影响"；同一次 `CoreTests` 退出码 1
（`Generated hash test FAILED`），因为那个数正是它打印的那一行。

## 探针期间踩到的坑（会影响 apply 怎么写）

1. **`-k 0` 必须透传**：ninja 默认第一条失败就停；不这么做，一次构建只拿得到一个编译单元的读数。
   `build.py` 没有这个开关，探针是临时加的（用完已改回）。
2. **失败构建会留下 `_build_run.bat`**（`run_msvc` 失败时故意保留 bat 供排查）—— 探针跑完要删，
   否则工作区不干净。
3. **认类型要看实例化链的最内层**（读数一的教训）。
4. **这些类型不在 `graphics_backend` 里**：`CPipelineStateObject` / `RectSate` / `InputAssemblyStates`
   在**全局命名空间**（写成 `graphics_backend::CPipelineStateObject` 是编译错误）—— 探针第一版就写错了断言。
   而 `GPUTextureView` / 三个缓存类型在 `graphics_backend` 里。
5. **补 include 的位置这件事，本次没得到结论**（见读数二的括注）；不要在没有实测的情况下写进设计。
6. **判据要能在"问题还在"的状态下变红**：这次的红态就是"关掉兜底 → 37 个单元编不过"，绿态是
   "回退后 `BUILD SUCCESSFUL`"；两者都留了日志。

## 附：37 个受影响的编译单元

```
VulkanRenderBackendNew（21）: VulkanBuffer / VulkanCommandListManager / VulkanFrameManager /
  VulkanGraphExecutor / VulkanGraphLocalResourceManager / VulkanLinearMemoryManager / VulkanMemoryManager /
  VulkanPassRWState / VulkanPipelineLibrary / VulkanResourceAliasing / VulkanResourceBindingInstance /
  VulkanShaderStruct / VulkanSubobjectBase / VulkanTexture / VulkanWindowHandle / RenderBackend_Vulkan /
  DescriptorSetLayoutManager / PipelineLayoutManager / PipelineLibraryCache / VertexInputStateManager / VMAImpl
D3D12RenderBackend（14）: CommandListManager / D3D12ShaderStruct / D3D12SubobjectBase / D3DBufferObject /
  D3DImageObject / FrameBoundResourceManager / GPUComputePipelineInstance / GPUDescriptorHeap /
  GPUGraphExecutor / GPUResourceBindingInstance / GPUResourceStates / MemoryManager / RenderBackend_D3D12 /
  WindowContext
IMGUIContext（1）: IMGUIContext.cpp
Test（1）: Test/GPUBackendTester 的 Main.cpp
```

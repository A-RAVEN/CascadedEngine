> 每条任务的 **Oracle** 必须能失败；标了「红态」的，必须把**改动前**的命令与真实输出贴进对话，只贴绿不算证据。
> 构建只走 `python build.py`（项目唯一合法入口）。本 change **一行都不改** `Reflection.h` / `Serialization.h` / `Hasher.h`。
> **前置条件**：`wire-codegen-real-type` 已落地并归档（`openspec/changes/archive/2026-10-01-wire-codegen-real-type`）——
> 四列清单、按模块并集推 include、宏不唯一即拒绝、预编译头补 `-include`、真实类型 `GPUBufferDescriptor` 都已就位。
>
> ⚠ **第二轮对抗验证改掉的三处前提**（逐条有实测，见 design 末尾的记录表）：
> ① 第 1 步不只是"EmitSample 三份不变"——真实类型那份入库反射头里的两个 `uint64_t` 会从 `Opaque` 变成
> `UInt`，**必须一起重新生成入库**，否则构建当场红；
> ② 新夹具**不能挂在模块根 `Tools/CodeGen`**：该根实测 0 个 include 目录，夹具一 include 引擎容器头
> （`CAUnorderedMap.h → Hasher.h → CAHash.h → <sha256.h>`）工具就 exit 6；
> ③ 「构建打印产物路径」只有在**入库文件已存在**时才可能为绿 —— 清单加条目之前必须先把产物入库。

## 1. 分类按规范化类型判定（单独一步；这一步会改掉真实类型的入库反射头）

- [ ] 1.1 分类前取类型的规范化形式，别名不再落进 `Opaque`
  **Oracle**：新增分类夹具编进 CoreTests，用 `static_assert` 直接断言表的种类值 —— `unsigned int` 与 `uint32_t` 都是 `TypeKind::UInt`、`unsigned long long` 与 `uint64_t` 都是 `TypeKind::UInt`。**红态**：同一断言在改动前编译失败（`uint32_t` 实际是 `TypeKind::Opaque`），贴编译错误原文
- [ ] 1.2 预期变更落到入库产物：`EmitSample` 三份**逐字节不变**；真实类型 `Interface/RenderInterface/generated/GPUBufferDescriptor.Reflection.h` **必须重新生成并入库**（两个 `uint64_t` 从 `Opaque` 变 `UInt`），另两份按对拍结果处理
  **Oracle**：改完 D1 先跑一次 `python build.py --config Debug --no-configure`，**贴出它报的那条 `differs byte-for-byte`**（证明该文件确实变了）；把新产物复制入库后再跑 → `BUILD SUCCESSFUL`，`git status --short Interface/RenderInterface/generated/` 只列出预期的那一份（若另两份也变，逐份说明原因）
- [ ] 1.3 根因写对：`uint32_t` 落进 `Opaque` 是因为 clang 报 **`CXType_Elaborated`（119）**
  **Oracle**：用一次临时诊断打印 `clang_getCursorType(...).kind`，夹具用上一个 change 归档目录里的 `evidence/fixtures/type_kinds.h`，把输出贴进证据；诊断用完即删并重新构建

## 2. 容器识别与元素描述

- [ ] 2.1 按结构与运行时同口径识别容器（有 `size()` 且有 `value_type` 的记录类型；C 数组按 extent），不按名字列白名单；**按位存储的容器（如 `castl::vector<bool>`）必须走拒绝路径** —— 它满足结构判据，却过不了旧运行时的 `for (auto& item : object)` 循环
  **Oracle**：`castl::vector<unsigned>` / `castl::string` / `castl::unordered_map<unsigned,unsigned>` / `castl::set<int>` 都带 `Container` 标志且 `vector` 的元素描述给出 `UInt`；`castl::vector<bool>` 出序列化被拒（红态：现状 exit 0 出码，贴出产物里那句 `s.serialize(obj.bitFlags);`）
- [ ] 2.2 字段表加元素描述（`key` / `element` 指向同构描述）与标志位；**包装器单独一位**（`Wrapper`），`HasOpaqueElement` **只记裸指针 / 托管指针 / 句柄**，不把包装器算进去；`TypeKind` 枚举不动
  **Oracle**：`castl::vector<castl::vector<int>>` 读出两层容器；`castl::vector<int*>` 与 `castl::vector<castl::shared_ptr<T>>` 的 `HasOpaqueElement` 置位；`castl::vector<cacore::NameHash>` 只置 `Wrapper`、`HasOpaqueElement` 为假；`int a[2][3]` 的元素描述是 `int[3]`
- [ ] 2.3 新增夹具与消费单元：夹具头 + 一个**真实目标**的编译单元 include 生成头并断言（主 spec 已落地"仅逐字节对拍不算被消费"）
  **Oracle**：夹具的所属目录**必须是引擎模块**（实测 `Interface` 可用）；夹具的 include 集合写死为 `CASTL/CAContainerBase.h` + 必要 STL 头（实测这样在 `Tools/CodeGen` 下也能跑；一旦 include `CAUnorderedMap.h` 就因 `sha256.h` 找不到而 exit 6 —— 红态贴出该 exit 6）；消费单元所在目标编译通过，删掉入库生成头后该目标编译失败
- [ ] 2.4 落点与 include 约定落地（按 D10，用户裁定"使用者用起来尽量简单"）：清单从四列收回**三列**、生成物一律进 `<所属目录>/generated/`、`build.py` 增加 `--commit-generated` 开关；消费代码写**不带相对路径**的 `#include "<类型>.Reflection.h"`
  **Oracle**：① 把消费单元里的 `#include "../generated/GPUBufferDescriptor.Reflection.h"` 改成 `#include "GPUBufferDescriptor.Reflection.h"` 后构建仍通过（证明模块的 include 路径已经把 `generated` 收进来）；② 不带 `--commit-generated` 跑构建时源码树**没有任何改动**（`git status --short` 为空）；③ 手动删掉一份入库产物后跑构建 → 失败并**提示加 `--commit-generated`**（贴出该提示），加该开关重跑 → 产物回来、构建通过
- [ ] 2.5 编译期断言：工具认出的容器集合与运行时口径一致
  **Oracle**：同一编译单元里断言"表里带 `Container` 标志" ⇔ `containerStates<T>::is_container_with_size`；把一个夹具换成 `struct Plain { int a; };` 后断言仍成立（对照组）
- [ ] 2.6 表结构变更后**逐条目**重新生成并入库，再跑构建
  **Oracle**：改动前先跑一次构建贴出 `differs byte-for-byte`（表结构变了，入库件必然不匹配）；用 `--commit-generated` 刷完两个条目的产物后 `python build.py --config Debug` → `BUILD SUCCESSFUL`，每个条目三行 `matches golden`；`git status --short` 列出的文件与预期一致（**不写死行数**）

## 3. 无序容器的规范顺序（含嵌套与哈希范围）

- [ ] 3.1 生成代码对无序容器成员：摊平成元素数组 → 按**参与排序的一侧**排序 → 按数组形状写出；**排序按元素描述递归**，不依赖"元素类型有没有生成序列化器"（容器类型永远不会有）
  **Oracle**：两个内容相同、插入顺序不同的 `unordered_map` 序列化后逐字节相同（红态：贴出两份产物与首个不同字节的偏移）
- [ ] 3.2 嵌套与容器元素同样稳定：① 元素类型自带生成序列化器（`vector<Inner>`，`Inner` 里有 `unordered_map`）；② **元素本身就是容器**（`vector<unordered_map<...>>`、`unordered_map<u, unordered_map<...>>`）；③ **C 数组的元素是无序容器**（`unordered_map m[2]`）
  **Oracle**：三类各两个"只有内层插入顺序不同"的对象，产物逐字节相同、哈希相同。**红态（实测）**：① 112 字节、首差偏移 20、两个哈希不同；② firstDiff=16 与 20；③ firstDiff=16
- [ ] 3.3 哈希范围写清楚：元素里的 `float` / `double` **显式调用元素类型的哈希特化**（不能靠 `h.hash(item)` —— 平凡可拷贝的元素会先走整块字节那条分支），并把这条不变量的适用范围写进 spec（有序容器含浮点也在内，修不了就明写例外）
  **Oracle**：`unordered_map<uint32_t, float>` 的 `+0.0f` / `-0.0f` 相等判断为真时哈希相同；**红态（实测）** 两个哈希 `1a2742e842fb0203` / `1a27c2e842fbdb83` 不同；再补一个"元素是平凡可拷贝且内含 float 的结构体"的用例（红态：`h.hash(element)` 与显式特化结论相反）
- [ ] 3.4 读回来走的是 `cacore::deserializer`（**不是** `batch_deserializer`）：分支顺序 `reserve` → `push_back` → `insert` → `resize`+下标，且**个数不为 0 时不清空目标容器**
  **Oracle**：两条都要能失败 —— ① 读进**空**容器：内容与写出内容相等；② 读进**非空**容器：生成代码先 `clear()` 再读，读后内容只由载荷决定（旧元素必须消失）；**红态**：不改时读进 3 元素目标得到 size=5、元素个数为 0 的载荷读进 1 元素目标仍是 size=1（贴出这两个读数）
- [ ] 3.5 对照组与回归：内容不同 → 字节与哈希都不同；有序容器（`vector` / `map` / `string`）改动前后产物逐字节相同
  **Oracle**：对照组两个不同内容的 `unordered_map` 字节与哈希都不同；有序容器用**事先存档**的改动前产物（存进 `openspec/changes/codegen-container-semantics/evidence/before/`，贴出生成命令）逐字节对拍

## 4. 拒绝路径

- [ ] 4.1 五条拒绝：① 参与排序的一侧没有全序；② 元素含裸指针 / 托管指针 / 句柄且请求序列化；③ 重复键容器（按规范化类型名判定）；④ 键或元素不可默认构造；⑤ 按位存储容器（见 2.1）
  **Oracle**：各有一个具名夹具与非零退出、stderr 点名成员与原因。**① 的比较判据必须接受 `operator<=>`** —— 引擎真实键类型 `NameHash` / `PathHash` / `CAHash::result_type` 只有 `<=>`、没有 `operator<`（实测这些头文件里 `operator<(` 零命中）；红态：按 `operator<` 判定会把它们全拒，贴出该误拒
- [ ] 4.2 反射豁免与清单口径：spec 与 tasks 对"哪些豁免"必须一致（含 ④）；并写明**清单里的类型必须三个发射器都能出**（`build.py` 每条固定跑三个发射器，任一非零即整体构建失败），只能出反射的类型不进清单
  **Oracle**：同一批夹具 `--emit-reflection` 的退出码与 spec 的豁免名单逐条一致（贴出对照）；design 的已知边界里写明 `build.py` 没有 per-entry 发射器选择
- [ ] 4.3 新增退出码与拒绝条件写进 `Tools/CodeGen/README.md` 的退出码表
  **Oracle**：`git grep -n -F "<退出码含义原文>" -- Tools/CodeGen/README.md` 逐条命中（带文件限定；`verify_readme.py` 只跑命令示例、不检查退出码表），并跑一次 `verify_readme.py` 确认 0 处不符

## 5. 包装器标记与对外文档

- [ ] 5.1 包装器只记录、不拒绝：`vector<NameHash>`、`unordered_map<NameHash, X>` 这类**必须放行**（旧运行时用 `managed_wrapper_traits` 解包，本来就能读写）
  **Oracle**：两个放行夹具出序列化 exit 0 且有产物（红态：按"包装器算不透明 → 拒绝"实现时它们被拒，贴出该拒绝）
- [ ] 5.2 `Tools/CodeGen/README.md` 写明：容器规则、磁盘形状契约（先个数、再按规范顺序逐个；`map` 是键值对数组；多维数组按维度嵌套）、**排序比较用什么**、五条拒绝条件、退出码表
  **Oracle**：`git grep -n -F "<契约与比较判据的原文>" -- Tools/CodeGen/README.md` 命中（带文件限定），`verify_readme.py` 0 处不符

## 6. 编译验证与修复

- [ ] 6.1 跑 `python build.py --config Debug` 直到 `BUILD SUCCESSFUL`（失败则分析并修到通过），随后跑 `CoreTests.exe` 与 `python Tools/CodeGen/verify_readme.py`
  **Oracle**：构建退出码 0；`CoreTests.exe` 全部用例 PASSED、退出码 0；`verify_readme.py` 0 处不符

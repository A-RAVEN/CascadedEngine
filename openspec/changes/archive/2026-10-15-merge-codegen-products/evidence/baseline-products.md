# 改造前的读数（基线）

本文件是 `merge-codegen-products` 的参照侧：改造**之前**的实物读数、被本改动推翻的基线条文清单、
以及改造后逐条比对的原始记录。所有命令都在仓库根目录 `E:\Projects\CascadedEngine` 下执行。

---

## 1.0 被本改动推翻的基线条文核对

### 命令与全部命中（16 处）

```
git grep -n "三份" -- openspec/specs/
```

命中 16 行，分三组，逐条处置如下。

**A 组：`openspec/specs/codegen-framework/spec.md`（6 处）**

| 行 | 原文片段 | 处置 |
|---|---|---|
| 362 | 要求标题「一个类型一次调用出三份产物」 | 本 change 的 `codegen-framework` **RENAMED** 改掉 |
| 364 | 正文「同时产出反射表、读写代码、哈希代码三份产物（输出目录由参数给出）…」 | 同一条要求的正文，被 **MODIFIED** 整条替换 |
| 366 | 场景标题「一次调用三份产物」 | 被 MODIFIED 里的新场景「一次调用一份产物」替换 |
| 369 | 场景正文「MUST 一次调用产出三份文件」 | 同上被替换 |
| 379 | 场景「空输出目录按缺值拒绝」里「会让三份产物落到盘符根且退出码为 0」 | **本 change 造成的失效**：合并成一份之后盘符根上只会落一份，`三份` 与新基线自相矛盾。已在 delta 里改成「会让产物落到盘符根且退出码为 0」（去掉计数，句子对两个版本都为真）；**要求本身一字未动** —— 仍是退出码 2 拒绝、仍 MUST NOT 写到盘符根 |
| 384 | 场景「接受集合的差异写在明处」里「`--emit-all` 让三份产物的拒绝条件全部生效」 | **不动**：这里说的是**三节各自的拒绝条件**（反射 / 序列化 / 哈希各有一套拒发码），与"写几个文件"无关，MODIFIED 里逐字保留 |

**B 组：`openspec/specs/build-throughput/spec.md`（4 处）**

| 行 | 原文片段 | 处置 |
|---|---|---|
| 26 | 要求标题「一个类型只解析一次，一次出三份产物」 | 本 change 的 `build-throughput` **RENAMED** 改掉 |
| 28 | 正文「…同时产出反射表、读写代码、哈希代码三份产物（产物字节与文件名都不变）」 | 被 **MODIFIED** 整条替换（新正文：一份 `<类型名>.codegen.h`、三节） |
| 37 | 场景 WHEN「用"一次出三份"的方式生成一遍」 | 被 MODIFIED 里的新场景「产物内容不变」替换 |
| 38 | 场景 THEN「三份产物的字节 MUST 与原来逐个生成时逐字节相同」 | 同上被替换（新场景改成 `-o` 单节输出对拍 228 次） |

**C 组：`openspec/specs/codegen-runtime/spec.md`（6 处）—— 全部不动**

72 / 77 / 78 / 87 / 91 / 92 行里的「三份头」指的是**旧运行时那三个头文件**
（`CACore/header/Reflection.h`、`Serialization.h`、`Hasher.h`，第 77 行点名了这三个路径），
属于 `retire-legacy-serde-hash` 那件事，与 CodeGen 生成物有几份毫无关系。

### "三份"之外的同类失效（同一次核对里查出来的）

用更宽的匹配口径再扫一遍（`\.(Reflection|Serialization|Hash)\.h`），基线里还有一处**编号与文件名被本改动弄假**：

```
git grep -n -E '\.(Reflection|Serialization|Hash)\.h' -- openspec/specs/
→ openspec/specs/build-throughput/spec.md:67
```

- 67 行的例子是「`Compiler.h` 直接 `#include "UniformElement.Serialization.h"`」——
  这个文件在本改动之后叫 `UniformElement.codegen.h`。改为 `#include "UniformElement.codegen.h"`。
- 同一个要求（「生成段并行执行多个类型」）的「空的产物目录也能一次构建成功」场景里写着
  「清单里有 **48** 份生成头被别的头直接 `#include`」。这个 48 是 `Compiler.h` **一个头**的读数
  （归档 `2026-10-10-improve-build-time` 的 design 原话：「`Interface/ShaderCompiler/header/Compiler.h`
  直接 `#include "UniformElement.Serialization.h"` 等 48 份生成头」，实测该文件此刻正是 24 个类型 × 2 节 = 48 行）。
  合并成一份之后同一个头是 **24** 份。原句写成"清单里有 48 份"，与实测口径对不上（同一个头之外还有别的头也直接 include 生成物），
  所以改成把读数落到具体头上：「`Interface/ShaderCompiler/header/Compiler.h` 一个头就是 24 份，改造前同一个头是 48 份」。
- 因此 `specs/build-throughput/spec.md` 里多了一条 **MODIFIED**（「生成段并行执行多个类型」整条重写，只动上面两处）。

**逐行对拍（证明只动了那两处）**：把基线的该要求整块与 delta 里的整块取出逐行比较，
结果只有两行不同（就是上面那两处），其余 27 行逐字相同：

```
基线块行数 = 29 / delta 块行数 = 28
<= …（`Compiler.h` 直接 `#include "UniformElement.Serialization.h"`）…
=> …（`Compiler.h` 直接 `#include "UniformElement.codegen.h"`）…
<= - **THEN** MUST 一次成功（`BUILD SUCCESSFUL`）…清单里有 48 份生成头被别的头直接 `#include`…
=> - **THEN** MUST 一次成功（`BUILD SUCCESSFUL`）…（`Interface/ShaderCompiler/header/Compiler.h` 一个头就是 24 份，改造前同一个头是 48 份）…
<= （基线块末尾多一个空行 —— 那是它与下一条要求之间的分隔行，不属本条）
```

### 两条 RENAMED 的标题核对

用一条脚本把 delta 里 FROM / TO / MODIFIED 标题取出，与基线逐字比较（不是眼睛看）：

```
delta = openspec/changes/merge-codegen-products/specs/codegen-framework/spec.md
  FROM      = ### Requirement: 一个类型一次调用出三份产物
  TO        = ### Requirement: 一个类型一次调用出一份产物（三节）
  MODIFIED  = ### Requirement: 一个类型一次调用出一份产物（三节）
  FROM 在基线里命中行数 = 1
  TO 与 MODIFIED 标题逐字相同 = True
delta = openspec/changes/merge-codegen-products/specs/build-throughput/spec.md
  FROM      = ### Requirement: 一个类型只解析一次，一次出三份产物
  TO        = ### Requirement: 一个类型只解析一次，一次出一份产物（三节）
  MODIFIED  = ### Requirement: 一个类型只解析一次，一次出一份产物（三节）
  FROM 在基线里命中行数 = 1
  TO 与 MODIFIED 标题逐字相同 = True
```

另用 `git grep -n -F "### Requirement: 一个类型一次调用出三份产物" -- openspec/specs/codegen-framework/spec.md`
与 `…-F "### Requirement: 一个类型只解析一次，一次出三份产物" -- openspec/specs/build-throughput/spec.md`
各命中 1 行，与上表一致。

### 校验

```
openspec validate merge-codegen-products
→ Change 'merge-codegen-products' is valid
```

---

## 1.1 基线的固化（改工具之前）

```
$src = "out\build\x64-debug\codegen"
$dst = Join-Path $env:TEMP "codegen_baseline_merge"
Copy-Item -Path (Join-Path $src "*") -Destination $dst -Recurse -Force
```

取数时的仓库状态与实物读数：

| 项 | 读数 |
|---|---|
| HEAD 提交号 | `fb41fa48c17981a09eb75adc5bc43f7d2de691e3` |
| `git status --porcelain` 条数 | 1（本次记录改动所在的那份待办 change 目录） |
| 源目录 `out/build/x64-debug/codegen` | **228 份 / 386,180 字节 / 7,497 行** |
| 基线副本 `%TEMP%\codegen_baseline_merge` | **228 份 / 386,180 字节 / 7,497 行**（与源目录一致） |
| 目录结构 | 保持原样，例如 `\CACore\Holder.Hash.h`、`\CACore\Holder.Reflection.h`、`\CACore\Holder.Serialization.h` |
| 别的预设 | `out/build/codegen-tool` 与 `out/build/x64-relWithDebugInfo` **都没有 `codegen` 目录**，所以 228 份只在 x64-debug 这一处 |

---

## 1.2 design D3 的前提：清单 76 行逐行都带 `serializable`

从 `build.py` 的 `CODEGEN_ANNOTATED = [` 到它的 `]` 之间逐行取出元组，再对**每一行**做检查：
该行的类型定义前面必须**恰好有一处** `CA_REFLECT(...)`，且它的参数里有 `serializable`。

```
清单行数 = 76
清单里的头文件数 = 18
逐行核对：不符的行数 = 0
```

**一处口径提醒（不是缺陷）**：先按"每个头里 `CA_REFLECT(serializable)` 这个字符串出现几次"数，
18 个头里有 2 个对不上 —— `Tools/CodeGen/TestEmit.h` 是 1 行 / 0 处，`Tools/CodeGen/TestPrivate.h` 是 1 行 / 2 处。
原因是两次都属口径问题，不是标注缺失：

- `TestEmit.h` 写的是 `CA_REFLECT(serializable, min = 0)`（多一个属性），字符串精确匹配数不到它；
- `TestPrivate.h` 里有两个带 `serializable` 的类型：`PrivateLike` 与 `FriendLike`，而清单里**只有 `FriendLike`**
  —— `PrivateLike` 是"不加 friend 就退出码 5"那条红态对照的探针（该头里逐字写着
  `判据是"加这一行就出码成功、不加仍然退出码 5"，两个方向都要有读数`），它**故意不进清单**。

所以真正成立的口径是"逐行按类型找它的标记"，读数 0 处不符。更强的一条事实在下面。

**更强的事实：全量生成里 76 行全部 exit 0**（若某行没带 `serializable`，它会在 `--emit-all` 下以退出码 9 失败）：

```
python build.py --config Debug --codegen-all        # 前台跑，日志落到 %TEMP%\merge_1_2_codegen_all.log
EXIT=0
  codegen: 并发 8 条（逻辑核 32，上限 8）
  codegen: 重跑 76 条、跳过 0 条（输入没变的那些）
  [耗时] configure 0.0s / codegen 39.4s / 编译 58.4s / 合计 97.8s
BUILD SUCCESSFUL

日志里 `CODEGEN FAILED` 出现 0 次；含"非零退出"的行 0 条；`[n/76]` 进度行 152 条（76 行 × 2）。
```

**顺带核对**：这次全量重生成之后，构建目录里的 228 份产物与 1.1 的基线副本**逐字节全同**
（228 件比对，相同 228、不同或缺失 0）—— 所以 1.1 的基线在跑过构建之后仍然有效。

---

## 1.3 改前读数

### ① 产物件数 / 字节 / 行数

```
Get-ChildItem out/build/*/codegen -Recurse -File
```

| 预设 | 件数 | 字节 | 行数 |
|---|---|---|---|
| `x64-debug` | **228** | **386,180** | **7,497** |
| `x64-relWithDebugInfo` | 无 `codegen` 目录 | — | — |
| `codegen-tool` | 无 `codegen` 目录 | — | — |

### ② 源码里的 include 行数（与工件里原先写的数不符，已现场更正）

```
git grep -n -E '\.(Reflection|Serialization|Hash)\.h' -- '*.h' '*.cpp'
  | Select-String -SimpleMatch '#include'
```

| 项 | 实测 | 工件原稿 | 差 |
|---|---|---|---|
| include 行数 | **267** | 270 | −3 |
| 其中 `Reflection` | **79** | 80 | −1 |
| 其中 `Serialization` | **88** | 89 | −1 |
| 其中 `Hash` | **100** | 101 | −1 |
| 涉及文件数 | **26** | 26 | 0 |
| 去重后的「文件 × 类型」行数（= 合并后的终态） | **137** | 138 | −1 |

**为什么原稿偏大 3 行**：原稿那组数是在 `clear-codegen-parse-blockers` 施工**中途**取的，
当时 `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp` 里还挂着 `ViewRectData` 的三行 include
（同一个类型的三节），该文件收尾时变成 27 行 / 9 个类型；原稿没有在收尾时重量一次。
证据：该文件此刻是 **27 行 / 9 类型**，比"多一个三节类型"正好少 3 行、少 1 个「文件 × 类型」——
267 + 3 = 270、137 + 1 = 138，两处差额同源。

**已按实测把 4 处判据/描述改成 267 与 137**（改前 → 改后）：

| 文件 | 改前 | 改后 |
|---|---|---|
| `tasks.md` 1.3 的"改造前实测" | `include **270** 行 = Reflection 80 + Serialization 89 + Hash 101` | `include **267** 行 = Reflection 79 + Serialization 88 + Hash 100` |
| `tasks.md` 4.4 的判据 | `终态 include 行数 = **138**` | `终态 include 行数 = **137**` |
| `design.md` Context | `26 个源码文件、270 行 include（Reflection 80 / Serialization 89 / Hash 101）…合并后是 138 行` | `…267 行 include（Reflection 79 / Serialization 88 / Hash 100）…合并后是 137 行` |
| `design.md` Goals | `从 270 行收到 138 行` | `从 267 行收到 137 行` |
| `proposal.md` Why | `26 个文件、270 行…Reflection 80 行、Serialization 89 行、Hash 101 行…合并之后是 138 行（现在 270 行里有 132 行…）` | `26 个文件、267 行…Reflection 79 行、Serialization 88 行、Hash 100 行…合并之后是 137 行（现在 267 行里有 130 行…）` |
| `proposal.md` Impact / 读数变化 | `26 个文件，270 行 → 138 行`、`源码 include 行 270 → 138` | `26 个文件，267 行 → 137 行`、`源码 include 行 267 → 137` |

### ③ 消费文件的节种类分布（同样与工件原稿不符，已现场更正）

按"这个文件里每个类型各有几节"分类（依据是上面那条命令的逐文件行数）：

| 类别 | 个数 | 文件 |
|---|---|---|
| 每个类型三节（各删两行） | **12** | `TestWrapperAssertions.cpp`、`GPUPipelineInstance.h`、两个 `ShaderLibrary.h`、`CPipelineStateObject.h`、`CVertexInputDescriptor.h`、`GPUGraph.h`、`ShaderProvider.h`、`ShaderResourceHandle.h`、`TextureSampler.h`、`CPipelineStateObjectAssertions.cpp`、`FriendLikeTest.cpp` |
| 每个类型两节（各删一行） | **4** | `Compiler.h`（48 → 24）、`GeneratedHashContainerTest.cpp`（4 → 2）、`PipelineStateObjectTest.cpp`（12 → 6）、`ResourceBatchTest.cpp`（2 → 1） |
| 混合（要分别处理） | **1** | `TestContainerAssertions.cpp`：7 行 / 4 个类型 —— `Inner`、`Outer`、`FixedArrays` 各两节，`Containers` 只有一节 |
| 只有一节（只改名字，行数不变） | **9** | `GPUPipelineInstance.cpp`、`PipelineStatesObject.cpp`、`GPUBufferDescriptorReflection.cpp`、`CodeGenKindsTest.cpp`、`EmitReflectionTest.cpp`、`GeneratedContainerTest.cpp`、`GeneratedHashTest.cpp`、`GeneratedSerializerTest.cpp`、`ShaderStructsCodeGenAssertions.cpp` |

合计 12 + 4 + 1 + 9 = **26** 个文件，与上表一致。

原稿写的是「12 个含三节、6 个含两节、8 个只含一节」，两处与实测不符：
`ShaderStructsCodeGenAssertions.cpp` 被算进了"两节"，但它 24 行 / 24 个类型，**每个类型只有一节**
（24 行全是 `.Reflection.h`），按动作属于"只改名字"那一类；`TestContainerAssertions.cpp` 是混合的，两边都不是。
已把 `tasks.md` 的 4.2 / 4.3 与 `design.md`、`proposal.md` 的分布改成上表。

### ④ 十条拒绝码的改前实测（2.4 的对照侧）

命令一律 `out\build\x64-debug\bin\CodeGen.exe <头> <类型> -I CACore/header …`，在仓库根执行；
每条都给了 `-o` 或 `--out-dir`，十条跑完**临时目录里 0 件产物**（读数：`件数 = 0`）。

| 码 | 探针 | 实测退出码 |
|---|---|---|
| 2 | `Tools/CodeGen/TestEmit.h` 的 `EmitSample`，给 `--emit-reflection --out-dir <目录>` | 2 |
| 5 | `Tools/CodeGen/TestPrivate.h` 的 `PrivateLike`，`--emit-reflection` | 5 |
| 6 | `Tools/CodeGen/TestStdLayout.h` 的 `StdLayout`，加 `-nostdinc++ -nostdinc` | 6 |
| 9 | 归档夹具 `…/2026-09-20-engine-codegen-framework/evidence/fixtures/uisample.h` 的 `NoSer`，`--emit-serialization` | 9 |
| 10 | `Tools/CodeGen/TestEmit.h` 的 `EmitSample`，`--emit-serialization --emit-hash` | 10 |
| 13 | `Tools/CodeGen/TestShapes.h` 的 `BitPacked`，`--emit-serialization` | 13 |
| 14 | `Tools/CodeGen/TestShapes.h` 的 `MultiKey`，`--emit-serialization` | 14 |
| 15 | `Tools/CodeGen/TestShapes.h` 的 `RawPointerMember`，`--emit-serialization` | 15 |
| 16 | `Tools/CodeGen/TestShapes.h` 的 `NoDefaultElement`，`--emit-serialization` | 16 |
| 17 | `Tools/CodeGen/TestShapes.h` 的 `NoOrderKeyMap`，`--emit-serialization` | 17 |

stderr 原文（逐条，改造前）：

```
[2] CodeGen: --out-dir only applies together with --emit-all; use -o <file> for a single emitter.

[5] CodeGen: refusing to emit for 'codegen_private_test::PrivateLike' — it has non-public members, and the generated code reads members by name from outside the class (an in-class marker does not change that access check). Make them public, or put `CA_CODEGEN_FRIEND(PrivateLike);` inside the class body (it friends the four generated entry points — the equivalent of the old `CA_PRIVATE_REFLECTION`).

[6] CodeGen: refusing to extract from 'Tools/CodeGen/TestStdLayout.h' — the compile environment fed to this tool is incomplete, so the parsed AST is truncated and the type table would be silently wrong. Fidelity cannot be judged by the exit code.
      unresolved include (fatal): CACore/header\CodeGenMeta.h:14:10: fatal error: 'cstddef' file not found
      前几条错误（不在被解析的头里，多半来自它 include 的文件）:
        CACore/header\CodeGenMeta.h:14:10: fatal error: 'cstddef' file not found
      degraded members of 'StdLayout' (reported offset 0 although not the first member): key, tail

[9] CodeGen: refusing --emit-serialization for 'audit_ns::NoSer' — the type's CA_REFLECT marker does not carry the `serializable` option (marker options seen: 'ui'). Add `serializable` to the marker, or emit reflection only.

[10] CodeGen: refusing — 2 emitter flags in one run; --emit-reflection / --emit-serialization / --emit-hash are mutually exclusive per invocation. Invoke the tool once per emitter, or ask for all three at once with --emit-all --out-dir <dir>.

[13] CodeGen: refusing --emit-serialization for 'codegen_shapes::BitPacked' — member 'v' is a bit-packed container ('std::vector<bool>'): its elements are stored packed, so the old runtime's element loop cannot bind a reference to them. Use a container of bytes (or widen the element type) instead.

[14] CodeGen: refusing --emit-serialization for 'codegen_shapes::MultiKey' — member 'm' is a duplicate-key container ('std::multimap<int, int>'): equal keys have no canonical order, and this generator does not define a secondary sort key. Use a unique-key container, or serialize a container of key/value pairs instead.

[15] CodeGen: refusing --emit-serialization for 'codegen_shapes::RawPointerMember' — member 'p' is a raw pointer ('int *'): the old runtime does not write pointer members, so the stream would silently lose it, and writing the address itself means nothing after a restart. Store the value, or store a name / index into a table that is serialized alongside.

[16] CodeGen: refusing --emit-serialization for 'codegen_shapes::NoDefaultElement' — member 'v': the element type 'codegen_shapes::NoDefaultCtor' has no usable default constructor, and the generated reader builds a value with `T t{}` before reading into it. Give the type a default constructor, or use a container of a type that has one.

[17] CodeGen: refusing --emit-serialization for 'codegen_shapes::NoOrderKeyMap' — member 'm': an unordered container is sorted before it is written, but its sorting side has no ordering operator (neither `operator<` nor `operator<=>`), so the canonical order is undefined. Give the type an ordering operator, or use an ordered container with an explicit comparator.
```

### ⑤ 扫描器读数

```
python Tools/CodeGen/check_legacy_symbols.py
EXIT=0
== 逐符号扫描（源码 + 构建目录里的生成物） ==
扫描 517 个文件、命中 0 行
== 旧实现指纹 ==
扫描 517 个文件、命中 0 行
== 三份旧头必须不存在 ==
  CACore/header/Reflection.h: 不存在
  CACore/header/Serialization.h: 不存在
  CACore/header/Hasher.h: 不存在
```

⚠ 这个脚本打印的是**扫描的文件数**（517），不是产物件数 —— 不许拿它与"228 → 76"并排当同一件事说。

### 顺带更正的其它引用（同属"本改动或归档造成的失效"）

| 文件 | 改前 | 改后 | 依据 |
|---|---|---|---|
| `tasks.md` 4.2 标题与清单 | 「**6 个含两节**的文件」，清单含 `ShaderStructsCodeGenAssertions.cpp` | 「**5 个含两节**的文件」，清单去掉它，并逐文件写上实测行数（48→24、7→4、4→2、12→6、2→1） | 上表的逐文件读数 |
| `tasks.md` 4.3 标题与清单 | 「**8 个只含一节**的文件」 | 「**9 个只含一节**的文件」，清单加入 `ShaderStructsCodeGenAssertions.cpp`（24 → 24） | 同上 |
| `design.md` Goals | 「include 从 270 行收到 138 行」 | 「从 267 行收到 137 行」 | 实测 |
| `proposal.md` What Changes | 「消费点从 270 行收到 138 行…12 个含三节的各删两行、6 个含两节的各删一行、8 个含一节的只改名字」 | 「从 267 行收到 137 行…12 个三节文件、4 个两节文件加 1 个混合文件、9 个单节文件」 | 实测 |
| `proposal.md` Impact「消费点」 | 「26 个文件，270 行 → 138 行…6 个含两节…8 个含一节」 | 「26 个文件，267 行 → 137 行…4 个含两节加 1 个混合…9 个含一节」 | 实测 |
| `tasks.md` 4.4 / 5.8 判据 | 终态 include **138** 行 | **137** 行 | 实测去重读数 |
| `tasks.md` 5.5、`design.md` D8、`proposal.md` Impact（既有判据一条） | `openspec/changes/clear-codegen-parse-blockers/evidence/probe_parse_gate.py`（该目录已归档，路径悬空）、`（第 36 行）` | `openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/evidence/probe_parse_gate.py`，判据改用逐字引文 `PRODUCTS_EXPECTED = 3`，并写明脚本里 `REPO = Path(__file__).resolve().parents[4]` 那层深度要一起改回来 | 归档把该 change 挪进了 `archive/2026-10-07-…`；行号引用违反本项目"引用锚点必须逐字可 grep、禁止行号"的规则 |

（`tasks.md`、`design.md`、`proposal.md` 里保留的 270 / 138 字样只剩下"成文时的旧数、已改正"这类对照说明，
不是判据本身。）

---

## 2.x 工具侧改造后的读数

### 2.1 / 2.2 一份产物与用户可见文本

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-all --out-dir %TEMP%\merge_2_1_single
```

| 项 | 读数 |
|---|---|
| 目录里的文件 | **1 份**：`EmitSample.codegen.h`（4,312 B） |
| stdout | 14 行 `CodeGen deps:` + **恰好 1 行** `CodeGen wrote: …/EmitSample.codegen.h` |
| 文件头 12 行 | 一行文件头注释 + `#pragma once`（1 次）+ `#include "CodeGenMeta.h"` / `CodeGenSerialization.h` / `CodeGenHash.h` 各一次 |
| 三节的注释行 | 第 1 行（文件头）、第 10 行（反射节）、第 31 行（序列化节）、第 66 行（哈希节） |
| 顺序 | `TypeDescriptor<` 偏移 676 < `CodeGenSerializer<` 1988 < `CodeGenEqual<` 3353 → 反射 → 序列化 → 哈希 |
| 去重（严格口径） | `#include "CodeGenSerialization.h"` 出现 **1 次**；裸字符串 `CodeGenSerialization.h` 出现 2 次 —— 另一次是那一节注释里的 "Requires: … + CodeGenSerialization.h on the include path"，所以判据要用 `#include "…"` 这个完整形态，不能用裸名字 |
| 总行数 | 99 行 |

改后的两条文本原文：

```
usage: CodeGen <file.h> <TypeName> [--emit-reflection] [--qualified-name=<qn>] [-o <out>] [clang-arg ...]
       CodeGen <file.h> <TypeName> --emit-all --out-dir <dir> [--qualified-name=<qn>] [clang-arg ...]
       --emit-all writes ONE file per type: <dir>/<TypeName>.codegen.h (reflection + serialization + hash).

CodeGen: --emit-all needs --out-dir <dir> — it writes one file per type (<Type>.codegen.h, holding the
reflection + serialization + hash sections), so a bare -o <file> would name only a single section.

CodeGen: refusing — --emit-all already asks for all three sections, so it cannot be combined with
--emit-reflection / --emit-serialization / --emit-hash.
```

### 2.3 228 次逐节对拍

脚本：`%TEMP%\merge_2_3_compare.py`（复用 `build.py` 的 `_compile_env_by_root` 与 `CODEGEN_EXTRA_*` 拼命令行，
只把 `--emit-all --out-dir` 换成单节 `--emit-* -o`；输出写到 `%TEMP%\merge_2_3_single_sections`，
**与基线目录无关**）。

```
清单 76 行 → 待比较 228 次
总比较 = 228   分类 = {'same': 228}
逐字节相同 = 228 / 228
写出的文件件数 = 228
```

### 2.4 十条拒绝码（改造后，与 1.3④ 逐条对照）

退出码与 stderr **逐字相同**：码 2 / 5 / 6 / 9 / 10 / 13 / 14 / 15 / 16 / 17 全部与改前一致
（15 行 stderr 逐行在 1.3④ 的记录里查到，对不上 0 行），十条跑完临时目录里 **0 件产物**。

### 2.5 `-o` 与 `--out-dir` 的搭配关系

- `--emit-reflection --out-dir <目录>` → 退出码 **2**，原文 `CodeGen: --out-dir only applies together with --emit-all; use -o <file> for a single emitter.`
- `-o` 单节输出与基线逐字节相同：`EmitSample.Reflection.h` 的 `Get-FileHash` 两边都是
  `DFA6F72E4286B41603ADE0B31E5AD43755CD77EFEA6D4721F38EF31293B7BAD3`。

---

## 3.x / 4.x 构建侧与消费点的读数

### 3.1 账本与"防空检查"

`out/build/x64-debug/codegen.stamp.json`：**76 行**，每行的 `out` 映射**只有一项**，键是 `codegen`
（`out 项数 != 1 的行数 = 0`）。原始片段开头：

```
{"[\"CACore/header/CACore/CAHash.h\", \"result_type\", \"CACore\", \"cahash::sha256_hash::result_type\"]": {"deps": {…}, "dirs": {…}, "env": "…", "header": "…", "out": {"codegen": "…"}, "tool": "…"}}
```

**防空检查实测**：把 `CODEGEN_PRODUCTS` 故意改成 `["codegenx"]` 再跑构建 —— 必须失败：

```
EXIT=1
- CodeGen 'EmitSample' --emit-all: 工具退出码 0，却没有写出这些产物：…\EmitSample.codegenx.h —— 多半是这个类型的标注被删了
『却没有写出这些产物』条数 = 76
CODEGEN FAILED (76) — build not reported as successful
BUILD SUCCESSFUL 出现 = False
```

改回 `["codegen"]` 之后 `--codegen-all` 构建：`EXIT=0`、`BUILD SUCCESSFUL`、
`codegen: 重跑 76 条、跳过 0 条`、76 行进度、76 行 `generated`、`CODEGEN FAILED` 0 次、非零退出 0 行、
`[耗时] configure 0.0s / codegen 40.5s / 编译 62.7s / 合计 103.2s`。

### 3.2 清理覆盖所有预设

在三个预设里各造一个旧命名的僵尸文件（`x64-debug` 的 `Test_CoreTests\EmitSample.Hash.h`、
`x64-relWithDebugInfo` 的 `ShaderCompilerSlang\UniformElement.Serialization.h`、
`codegen-tool` 的 `CodeGenToolStale\Probe.Reflection.h`），跑一次构建：

| 文件 | 构建前 `Test-Path` | 构建后 `Test-Path` |
|---|---|---|
| `out\build\x64-debug\codegen\Test_CoreTests\EmitSample.Hash.h` | True | **False** |
| `out\build\x64-relWithDebugInfo\codegen\ShaderCompilerSlang\UniformElement.Serialization.h` | True | **False** |
| `out\build\codegen-tool\codegen\CodeGenToolStale\Probe.Reflection.h` | True | **False** |

构建日志：`codegen: 清掉旧命名的产物 230 份（扫过 3 个预设的 codegen 目录）`；
构建后 `out/build` 下旧后缀 **0 份**、新后缀 **76 份**。

### 4.1 / 4.2 / 4.3 消费点的 include 行数（改前 → 改后）

| 文件 | 改前 | 改后 |
|---|---|---|
| `CACore/private/TestWrapperAssertions.cpp` | 3 | 1 |
| `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` | 9 | 3 |
| `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` | 36 | 12 |
| `Interface/RenderInterface/header/CPipelineStateObject.h` | 3 | 1 |
| `Interface/RenderInterface/header/CVertexInputDescriptor.h` | 9 | 3 |
| `Interface/RenderInterface/header/GPUGraph.h` | 3 | 1 |
| `Interface/RenderInterface/header/ShaderProvider.h` | 3 | 1 |
| `Interface/RenderInterface/header/ShaderResourceHandle.h` | 3 | 1 |
| `Interface/RenderInterface/header/TextureSampler.h` | 3 | 1 |
| `Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp` | 27 | 9 |
| `Test/CoreTests/FriendLikeTest.cpp` | 3 | 1 |
| `VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibrary.h` | 39 | 13 |
| `Interface/ShaderCompiler/header/Compiler.h` | 48 | 24 |
| `Interface/RenderInterface/private/TestContainerAssertions.cpp` | 7 | 4 |
| `Test/CoreTests/GeneratedHashContainerTest.cpp` | 4 | 2 |
| `Test/CoreTests/PipelineStateObjectTest.cpp` | 12 | 6 |
| `Test/CoreTests/ResourceBatchTest.cpp` | 2 | 1 |
| `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp` | 6 | 6 |
| `D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp` | 6 | 6 |
| `Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp` | 1 | 1 |
| `ShaderCompilerSlang/private/ShaderStructsCodeGenAssertions.cpp` | 24 | 24 |
| `Test/CoreTests/CodeGenKindsTest.cpp` | 1 | 1 |
| `Test/CoreTests/EmitReflectionTest.cpp` | 1 | 1 |
| `Test/CoreTests/GeneratedContainerTest.cpp` | 4 | 4 |
| `Test/CoreTests/GeneratedHashTest.cpp` | 9 | 9 |
| `Test/CoreTests/GeneratedSerializerTest.cpp` | 1 | 1 |
| **合计（26 个文件）** | **267** | **137** |

### 4.4 复核

- 源码树里旧后缀 include：`git grep -n -E '\.(Reflection|Serialization|Hash)\.h' -- '*.h' '*.cpp'` 过滤出
  `#include` 行 = **0 行**（`openspec/` 下的归档与文档里的历史引用不算，它们没有出现在 `.h/.cpp` 里）。
- 终态 include 行 = **137**，去重（文件 × 类型）后也是 **137**；分布在 **26** 个文件。
- 再跑一次构建：见 3.1 的 `--codegen-all` 读数（`BUILD SUCCESSFUL`）。
- 三处**不是 include** 的旧名字残留（注释与断言消息）同批改掉：
  `TestContainerAssertions.cpp` 两条 `static_assert` 的说明文字（`Outer.Serialization.h` / `Inner.Serialization.h`
  → `Outer.codegen.h` / `Inner.codegen.h`）、`EmitReflectionTest.cpp` 开头注释里的
  `generated/EmitSample.Reflection.h` 与"入库黄金样本"那段（那段描述早已过期：生成物不入库）。

---

## 5.5 归档探针的改前 → 改后

`openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/evidence/probe_parse_gate.py`

| 处 | 改前 | 改后 |
|---|---|---|
| 产物份数常量 | `PRODUCTS_EXPECTED = 3` | `PRODUCTS_EXPECTED = 1` |
| 文档注释里的绿态判据 | 「四个目标类型**退出码为 0 且各写出 3 份产物**」 | 「各写出**一份**产物（`<类型名>.codegen.h`；在那之前是三份）」 |
| `CONTROLS` 上方注释 | 「它们不产出 3 份产物（要么出 3 份、要么是空名单）」 | 「它们不产出这一份产物（要么出一份、要么是空名单）」 |
| `--expect` 的 help | 「green = 期望四个都 exit 0 且各出 3 份产物（修完之后）」 | 「各出一份产物（修完之后）」 |
| 仓库根推导 | `REPO = Path(__file__).resolve().parents[4]` | `parents[5]`（归档把文件往下挪了一层） |
| 文档注释里的两条用法路径 | `openspec/changes/clear-codegen-parse-blockers/…` | `openspec/changes/archive/2026-10-07-clear-codegen-parse-blockers/…` |

改后原样跑（`--expect green`），**绿态判据不再报"只写出 1 份产物（应为 3 份）"**：

```
EXIT=0
目标类型（改之前应当被拒）
  ViewRectData            exit=0  主头诊断=0  致命=False  产物=1
  GPUPipelineStateKey     exit=0  主头诊断=0  致命=False  产物=1
  VertexInputBindingData  exit=0  主头诊断=0  致命=False  产物=1
  InputElementDesc        exit=0  主头诊断=0  致命=False  产物=1
对照（应当解析干净）
  ResourceHandleKeyData   exit=0  主头诊断=0  致命=False  产物=1
  RectSate                exit=0  主头诊断=0  致命=False  产物=1
  D3D12Probe              exit=0  主头诊断=0  致命=False  产物=0
期望=green；不符合期望的：（无）；跑了 4/4 个目标、3/3 个对照；跳过 0 个
```

**红态复现（证明这条判据本来会红）**：把同一条读数（`exit=0, 产物=1`）喂给改前的期望值 ——

```
旧 EXPECTED=3 时这一条读数判为: 只写出 1 份产物（应为 3 份）
新 EXPECTED=1 时这一条读数判为: 通过
```

---

## 5.1 – 5.4 / 5.6 – 5.9 收尾读数

### 构建与测试

```
python build.py --config Debug --codegen-all
EXIT=0
codegen: 清掉旧命名的产物 0 份（扫过 3 个预设的 codegen 目录）
codegen: 重跑 76 条、跳过 0 条（输入没变的那些）
[76 行进度]  [76 行 generated]  CODEGEN FAILED 0 次  非零退出 0 行
[耗时] configure 0.0s / codegen 40.5s / 编译 62.7s / 合计 103.2s
BUILD SUCCESSFUL

out\build\x64-debug\bin\CoreTests.exe          → EXIT=0，PASSED 12 行，FAILED 0 行
GPUBackendTester --backend vulkan --headless 10 → EXIT=0，test_output/result.json 8 条 status 全 pass
GPUBackendTester --backend d3d12  --headless 10 → EXIT=0，test_output/result.json 8 条 status 全 pass
   （两次各留一份快照：%TEMP%\merge_5_3_vulkan_result.json / merge_5_3_d3d12_result.json）
python Tools/CodeGen/check_legacy_symbols.py   → EXIT=0，扫描 365 个文件、命中 0 行
   （517 → 365 就是"228 份产物变 76 份"这 152 个文件的差）
```

### 5.6 文档

```
python Tools/CodeGen/verify_readme.py → README: 20 documented command(s) run, 0 mismatch(es)   EXIT=0
```

⚠ 这一条**不是**一次通过：第一次跑报了 1 处不符 —— 我把示例 6 里那条退出码 10 的报错原文顺手改成了
"all three sections"，而工具那句并没有改（改的是另一条 `--emit-all` 的报错）。判据当场把它抓出来，
改回原文后 0 处不符。**结论**：报错文本属于"拒绝语义不动"的一部分（2.4 逐字对照过），文档只能**照抄**。

`Tools/CodeGen/README.md` 里改掉的站点（新锚点逐条 grep，各命中 1 处）：

- 接口表 `--emit-all` 行：「一次调用把上面三节**全部**发射出来，写进**一份** `<TypeName>.codegen.h`」
- 接口表 `--out-dir` 行：「写**一份** `<目录>/<TypeName>.codegen.h`」
- 「发射标志互斥」段：「要一次拿齐三节、写进一份 `<类型名>.codegen.h`」
- 「`--emit-all` 不是逐个发射的同义词」段：「它把三节的拒绝条件全部打开」
- 接线示例：`#include "Player.codegen.h"`、「它们 `#include "EmitSample.codegen.h"` 这一份生成物」
- 示例 10 改成「一次调用出一份产物（三节）」，并**当场原样跑过**：`CodeGen wrote:
  .review_tmp/readme/emit-all/EmitSample.codegen.h`（stdout 15 行 = 14 行 deps + 这 1 行），产物 4,312 字节
- §六 账本段：「上一轮的那一份产物」、「生成段在跑工具**之前**还会清掉旧命名的产物（…**所有预设**都清）」
- §七 接线第三步：「`#include "<类型>.codegen.h"`（一个类型一份…三节都在里面）」
- 构建耗时一节补了 2026-10-11 的一组读数（全量 `configure 0.0s / codegen 40.5s / 编译 62.7s / 合计 103.2s`）
- 仓库脚本 `.claude/workflows/scripts/verify-codegen-tasks-order.js`：动作对象 `EmitSample.Reflection.h`
  → `EmitSample.codegen.h`

`CLAUDE.md`：`Select-String -SimpleMatch -Pattern 'Reflection.h','Serialization.h','Hash.h','三份产物','codegen.h'`
命中 **0 行** → 核过、无需改。

### 5.8 实物读数（与 1.3 并排）

| 项 | 1.3（改前） | 5.8（改后） |
|---|---|---|
| 构建目录里的产物 | 228 份 / 386,180 字节 / 7,497 行 | **76 份**（`*.codegen.h`） |
| 所有预设里的旧后缀 | 228 份 | **0 份** |
| 源码 include 行 | 267 行（79 / 88 / 100），26 个文件 | **137 行**，26 个文件 |
| 去重（文件 × 类型） | 267 | **137** |

### 5.9 工件与规格

```
openspec validate merge-codegen-products → Change 'merge-codegen-products' is valid
工件点名的 16 个路径逐一 Test-Path：缺 0 个
```

### 5.10 收尾的那次构建

```
python build.py --config Debug
EXIT=0
codegen: 清掉旧命名的产物 0 份（扫过 3 个预设的 codegen 目录）
codegen: 重跑 0 条、跳过 76 条（输入没变的那些）
[耗时] configure 0.0s / codegen 1.2s / 编译 2.3s / 合计 3.4s
BUILD SUCCESSFUL
```





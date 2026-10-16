## Why

旧的反射 / 序列化 / 哈希设施**已经退场**：`CACore/header/Reflection.h`、`Serialization.h`、`Hasher.h` 三个路径在文件系统上不存在，`CA_REFLECTION`、`CATypeDescriptor`、`cacore::serializer`、`cacore::deserializer`、`visit_members`、`managed_pointer_traits` 在源码树里各 **0 处**（口径：除 `Tools/CodeGen/check_legacy_symbols.py` 的守卫符号表以外 —— 那张表列这些名字是它的职责；2026-10-15 实测）。

但退场时留下的**过渡装置与过渡期文字**还在，而且有两处与现实相反：

- `build.py` 里"删旧命名产物"的那个壳（`CODEGEN_LEGACY_SUFFIXES` + `cleanup_legacy_codegen_products()`；常量与函数连同注释 35 行，加调用点与打印行共 41 行）每次构建都在**空转** —— 源码树 0 处引用旧命名产物、构建目录 0 个旧命名文件。
- `CACore/header/HashKeys.h` 开头的注释还在说"旧驱动（`aggregateHasher` 那套）留在 `Hasher.h` 里等 9.3 删掉。本头**不依赖**那三份待删的头"；`CACore/header/CodeGenHashFwd.h` 开头还举"旧设施的 `Hasher.h`"当例子。那份头已经不存在了。
- 三份主规格里还写着「系统 SHALL **保留**现有 `CA_REFLECTION` 的运行时行为……生成元数据是**加法**，不替换」、「元素是内含无序容器的结构体时**必须拒绝**」（退出码 12 已撤）、「同一个对象分别用生成物与**旧运行时**（`cacore::serializer` 逐成员写）各写一遍，两份字节逐字节相同」（旧运行时已删除，跑不出这个对照）。

代码早退了、规格还停在过渡态 —— 下一个人照着规格读，会以为旧设施还在。本 change 把过渡装置与过渡期文字收回去，让规格与代码说的是同一件事。

## What Changes

- **删掉空转的过渡装置**：`build.py` 的 `CODEGEN_LEGACY_SUFFIXES` 常量（182 行）、`cleanup_legacy_codegen_products()` 函数（185–215 行）与它的调用点（600 行）、打印行（601–605 行）一起删除 —— 常量与函数连同注释 35 行，加调用点与打印行共 **41 行**。
- **改掉过期注释里的假陈述**：`CACore/header/HashKeys.h` 开头那句（"留在 `Hasher.h` 里等 9.3 删掉 / 那三份待删的头"）与 `CACore/header/CodeGenHashFwd.h` 开头那句（"例如旧设施的 `Hasher.h`"）改成现状。
- **把过渡期的规格收回去**（三份主规格，逐条见 `specs/`）：
  - `codegen-framework`：**删除**「保留 CA_REFLECTION 运行时语义」；**改**「生成源码以显式源清单纳入构建」（守卫范围改成今天的实际范围：只断言生成描述符的类型名非空，字段数 / 名字 / offset 由消费单元的断言文件对着写死的期望值比）；**改**「无序容器的序列化与哈希必须走规范顺序」（删掉"元素是内含无序容器的结构体时必须拒绝"这条场景，并把"读进非空目标时先清空"的依据从已删除的旧运行时改成新运行时的规则）。
  - `codegen-runtime`：**改**「生成物的读写必须自带游标」里那条对照场景 —— 对照物从"旧运行时各写一遍"改成测试里**冻结成字面量的期望字节**（`Test/CoreTests/CodeGenRuntimeTest.cpp` 已经是这个写法）。
  - `codegen-type-migration`：**删除**「已有类型的迁移形态是双份描述」整条（老宏不存在了）；**改**「私有成员与指针成员两类形状的处置必须写在明处」（`CA_PRIVATE_REFLECTION` → 今天的 `CA_CODEGEN_FRIEND`）；**改**「迁移必须逐类型可核，且不得留下静默不生效的钩子」（判据里的 `cacore::aggregateHasher<>` 已随旧头删除，换成 `cacore::codegen::hasher<>`）。
  - 顺手把归档时留下的 `TBD - created by archiving change … Update Purpose after archive.` 补成一句话 —— 实测 `openspec/specs/` 下**六份**规格都还留着这一行（`build-throughput`、`codegen-framework`、`codegen-runtime`、`codegen-type-migration`、`vma-vulkan-function-binding`、`vulkan-backend-teardown`），每份照它自己的要求写一句 Purpose。
- **把"为什么留着"写进对外文档**：`Tools/CodeGen/README.md` 新增一小节，说明单节发射开关（`--emit-reflection` / `--emit-serialization` / `--emit-hash` / `-o`）与 `Tools/CodeGen/check_legacy_symbols.py` **不是**旧账 —— 前者是「三节与改造前逐字节相同」那条规格判据的载体，后者是防旧符号回流的守卫；顺手修掉该文档里两个重复的「## 五、」章节号。
- **仓库外**：全局规则文件 `~/.dsh/AGENTS.md` 末尾那张"两套反射 / 序列化设施"的表整表过期（还写着旧三头存在、"绝大多数引擎类型仍用老宏"、"新生成物仍调用旧运行时"），改成现状。

## Capabilities

### New Capabilities

（无）—— 本 change 不引入新能力，只把既有能力规格里已经不成立的句子收回去。

### Modified Capabilities

- `codegen-framework`: 删一条（保留 CA_REFLECTION 运行时语义）、改两条（显式源清单纳入构建的守卫范围；无序容器规范顺序里两条过期场景）。
- `codegen-runtime`: 改一条（自带游标那条要求里"与旧运行时逐字节对照"的对照物）。
- `codegen-type-migration`: 删一条（迁移形态是双份描述）、改两条（私有成员与指针成员两类形状的处置；迁移必须逐类型可核里的判据符号）。

## Non-goals

- **不删单节发射开关**（`--emit-reflection` / `--emit-serialization` / `--emit-hash` / `-o`）：`codegen-framework` 里「三节由同一段发射代码产出」的判据挂在「`-o` 单节输出与改造前逐字节相同」上；删掉它，那条判据没法复核。本 change 只把这件事写进 README，不动开关。
- **不删 `Tools/CodeGen/check_legacy_symbols.py`**：名字带 legacy，但它是**守卫**（扫旧符号，命中即退出码 **1**；退出码 **2** 只用于用法错或找不到仓库根，命中 0 行时返回 0）。
- **不删 `SubmitAndWait`**（源码树 11 处）：那是 `GeneralResources` / `IOManager_FS` 的异步提交等待 API，与旧哈希设施**同名不同物**。
- **不删 `CA_PRIVATE_REFLECTION` 那 5 处注释**：宏本身已不存在，那 5 处是在解释"`CA_CODEGEN_FRIEND` 是它的等价物"，有解释价值；本 change 不动它们。
- **不重写 `codegen-type-migration` 其余要求**（嵌套类型标注、不在构建内的模块、D3D12 推导环境、被解析门拒收的键类型）：它们描述的能力今天仍然成立，本次只改其中**已经不成立**的句子。
- **不改工具命令行、退出码表、接入清单、任何生成物与任何哈希值**：本 change 不产生新的产物形状。
- **不动渲染后端与测试器**：本 change 只收 CodeGen 这条线的过渡遗留。

## Impact

- **删除**：`build.py` 里 41 行过渡装置（常量与函数连同注释 35 行 + 调用点与打印行 6 行）。
- **修改**：`CACore/header/HashKeys.h`、`CACore/header/CodeGenHashFwd.h`（各一处注释）、`Tools/CodeGen/README.md`（新增一节 + 修章节号）、`openspec/specs/codegen-framework/spec.md`、`openspec/specs/codegen-runtime/spec.md`、`openspec/specs/codegen-type-migration/spec.md`（增量归档后合并），以及**六份**主规格的 `Purpose` 一行：`build-throughput`、`codegen-framework`、`codegen-runtime`、`codegen-type-migration`、`vma-vulkan-function-binding`、`vulkan-backend-teardown`（后三份只改这一行、不碰要求）、仓库外 `~/.dsh/AGENTS.md`。
- **新增**：本 change 下建 `evidence/` 目录，存"改动前的三组读数"与"归档后备查的 grep 清单"（tasks 1.1 / 2.2 / 4.5 / 7.2 都以它为落点）。
- **风险**：删掉清理壳之后，别的检出目录里残留的旧命名产物不再被自动清 —— 没有任何代码 `#include` 它们（源码树 0 处），只占磁盘。这条理由写进 design，并由 tasks 里的"删前删后构建 + 计数"读数兜住。

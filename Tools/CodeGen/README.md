# CodeGen —— 把带标注的 C++ 类型变成代码的构建期工具

`Tools/CodeGen/CodeGenMain.cpp` 编译出的 `CodeGen.exe` 是一个**命令行工具**：给它一个头文件和一个类型名，
它用 libclang 解析该类型，读出**声明顺序的字段表**，再按请求发射三选一的代码片段（反射描述符 / 序列化
特化 / 哈希特化）。它**不是**引擎的一部分：引擎不链接 libclang，这个工具只在构建期跑。

- 产物路径：`out/build/<preset>/bin/CodeGen.exe`（Debug 预设是 `out/build/x64-debug/bin/`），旁边会自动
  拷入 `libclang.dll`。
- **它是一份独立工程**（`Tools/CodeGen/CMakeLists.txt` 自带 `cmake_minimum_required` + `project()`）：引擎那棵
  构建树里**没有**它的目标，所以改工具不会让引擎重新配/重编。构建它的唯一入口仍是项目脚本 ——
  `python build.py --config Debug`（生成段第一步会用独立构建目录 `out/build/codegen-tool` 配好、编好它，
  再逐条生成）。
  **不要**手敲 ninja/cmake。
- 本文档面向**外部使用者**：接口、退出码、可直接复制运行的示例与已知限制。

## 零、前置条件（构建引擎之前必须先满足）

- **构建引擎必须能跑 `CodeGen.exe`，也就是必须装好 libclang。** 生成物只活在构建目录里（源码树里没有
  生成代码），所以生成段是编译的前置步骤：`CodeGen.exe` 跑不起来 = 引擎编不了。libclang 由 CMake 变量
  `CALIB_CLANG_DIR` 指定（默认 `E:/Projects/CACodeGen/ExternalLibraries/LLVM`，那里要有
  `bin/x64/libclang.dll`）；该文件不在时独立工程会打印一条 WARNING 并且**不产出** `CodeGen.exe`，
  构建随即在生成段失败并说清原因，**不会**掉成一句光秃秃的引擎报错。
- **新克隆第一次构建之前，IDE 找不到那些生成头。** 生成头由构建现场生成在
  `out/build/<预设>/codegen/<模块>/`，仓库里没有它们；先跑一次 `python build.py --config Debug`，
  IDE 的跳转与补全才会正常（在那之前 IntelliSense 会报一堆找不到 `*.Reflection.h`）。

「生成用的程序没建起来」与「引擎编译失败」在输出里是**两种不同的失败**，一眼能分开：前者是生成段的
`CODEGEN FAILED (…)` 块（点名 `codegen sidecar not found` / libclang 路径），后者是编译器的
`FAILED: …obj` + `ninja: build stopped`。

## 一、它做什么 / 不做什么

**做**：

- 从**预处理器真正展开过**的标注宏调用里确定"哪些类型被标注"（见第三节），并把它绑定到该调用之后第一个
  文件级 / 命名空间级的类型；注释、字符串、被禁用的 `#if` 分支、没用到的 `#define` 体**都不算标注**。
- 输出**确定性的**代码片段：同一输入产生同一份字节（工具内部不做排序，字段按声明顺序）。
- 三种发射模式：`--emit-reflection`（`TypeDescriptor<T>` 特化）、`--emit-serialization`
  （`CodeGenSerializer<T>` 特化）、`--emit-hash`（哈希/相等特化）。
- 不带任何发射标志时打印**字段表**（供人读、也供脚本解析）。
- 环境残缺时**拒发**：给出的编译环境让 `<string>` 之类的标准头不可达时，它不会交出"字段全 0"的产物，
  而是非零退出并指名哪个 include 没解析、哪些字段的类型退化了。

**不做 / 已知限制**（与能力同等醒目）：

- **不接引擎 target、不在引擎头文件里注册任何类型**：本工具的构建期清单目前指向自包含夹具
  `Tools/CodeGen/TestEmit.h` 的 `EmitSample`（`build.py` 的 `CODEGEN_ANNOTATED`）。把清单指向哪由使用者决定。
- **含私有成员的类型**：标记可以写在类体里（`CA_REFLECT(...)` 紧跟要标注的那个类型定义，嵌套类型也行），
  但**发射仍然拒绝含私有成员的类型**（退出码 5）—— 生成的代码直接按成员名访问，拿不到私有成员的访问权。
  要发这类类型，得先给生成的代码开一个 friend 入口；在那之前它只能提取字段表（提取不受影响）。
- **两条通道的共同要求**：标记必须是一次**预处理器真的展开过**的宏调用（`CA_REFLECT` / `CA_CODEGEN_REGISTER`）。
  写在注释、字符串、`#if 0` 分支或没被用到的 `#define` 体里都不算标注。
- **不保证"任意 C++ 都能处理"**：字段的偏移由 libclang 按你喂进去的编译环境求值；环境不真实
  （缺 include、`/MD` 与 `/MDd` 混用）时偏移会与真实构建不同 —— 工具会在能判定时拒发（退出码 6）。
- 枚举、函数、基类**没有提取**（本 change 只取字段）。

## 二、接口

```
CodeGen.exe <file.h> <TypeName> [发射标志] [-o <输出路径>] [交给 libclang 的参数 ...]
CodeGen.exe <file.h> <TypeName> --emit-all --out-dir <目录> [交给 libclang 的参数 ...]
```

| 参数 | 含义 |
|---|---|
| `<file.h>` | 要解析的头文件（相对路径按当前工作目录解析，通常从仓库根跑） |
| `<TypeName>` | 目标类型名；限定名可按非限定尾部匹配（`graphics_backend::ImageHandle` 与 `ImageHandle` 等价） |
| `--emit-reflection` | 发射 `cacore::codegen::TypeDescriptor<T>` 特化 |
| `--emit-serialization` | 发射序列化特化（要求该类型的标注带 `serializable` 选项） |
| `--emit-hash` | 发射哈希/相等特化 |
| `--emit-all` | 一次调用把上面三种产物**全部**发射出来（配 `--out-dir`）。同一份头只解析一遍，产物与逐个发射逐字节相同。它还会把这次**真正读过的文件**逐行打到标准输出（`CodeGen deps: <路径>`）、把**真正写出的产物**也逐行打出来（`CodeGen wrote: <路径>`），供 `build.py` 记账并确认三份都写出去了 |
| `--qualified-name=<qn>` 或 `--qualified-name <qn>` | 覆盖发射进代码里的限定名 |
| `-o <路径>` | 写文件（先写同名 `.tmp`，成功后改名覆盖；失败时**不留残渣**）。省略则写标准输出 |
| `--out-dir <目录>` 或 `--out-dir=<目录>` | 与 `--emit-all` 搭配：写 `<目录>/<TypeName>.Reflection.h`、`<目录>/<TypeName>.Serialization.h`、`<目录>/<TypeName>.Hash.h` 三个文件（逐个先写 `.tmp`，成功后改名；目录必须已存在） |
| 其余参数 | 原样交给 libclang，例如 `-I CACore/header`、`-std=c++20`、`-D...` |

**发射标志互斥**：`--emit-reflection` / `--emit-serialization` / `--emit-hash` 在**一次调用里只允许给一个**；
要一次拿齐三种就用 `--emit-all --out-dir <目录>`（它不能与那三个同时给）。三个都不给 = 打印字段表。

**`--emit-all` 不是"逐个发射"的同义词：它把三份产物的拒绝条件全部打开。** 一次调用要出三份，工具就得对三份
都能出码，所以标注没带 `serializable` 的类型在这里会以**退出码 9**被拒（`vector<bool>` 是 13、含指针成员是 15），
而同一个类型**只请求反射**时照样出码。归档夹具 `uisample.h` 的 `NoSer` 正是这种类型（它的标注是 `CA_REFLECT(ui)`，
故意不带 `serializable`）：

```bash
CodeGen.exe openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/fixtures/uisample.h NoSer -I CACore/header -I Tools/CodeGen --emit-reflection -o .review_tmp/readme/no-ser/NoSer.Reflection.h
```
```
[exit 0]
```
第一条成功：产物写在 `-o` 给的路径上（`NoSer.Reflection.h`，879 字节）。

```bash
CodeGen.exe openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/fixtures/uisample.h NoSer -I CACore/header -I Tools/CodeGen --emit-all --out-dir .review_tmp/readme/no-ser
```
```
[exit 9]
CodeGen: refusing --emit-serialization for 'audit_ns::NoSer' — the type's CA_REFLECT marker does not carry
the `serializable` option (marker options seen: 'ui'). Add `serializable` to the marker, or emit reflection
only.
```
第二条被拒，**一份产物都不写**。所以：只能出反射的类型进不了 `build.py` 的接入清单（清单每条固定跑
`--emit-all`，任一条非零退出即整体构建失败）。

### 类型名怎么写（命令行与接入清单同一口径）

**解析口径：按最后一个非限定名匹配。** 命令行写短名就够 —— `CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample`
找到的是 `codegen_demo::EmitSample`。反过来，**带上外层前缀找不到**：嵌套类型写最里面那一层，写
`RenderState::Stencil` 会以退出码 3 拒绝（实测原始输出见下）。发射进代码里的限定名取自类型游标
（`game::RenderState::Stencil`），不是你在命令行敲的那个词；要覆盖它才用 `--qualified-name=<限定名>`。
`build.py` 接入清单（`CODEGEN_ANNOTATED`）的第二列同一口径，写短名即可。

`Tools/CodeGen/examples/nested.h` 是这条口径的样板：外层 `game::RenderState` 没被标注，只有里面的
`game::RenderState::Stencil` 带类内标记。

```bash
CodeGen.exe Tools/CodeGen/examples/nested.h Stencil -I CACore/header  # 字段表（exit 0）
CodeGen.exe Tools/CodeGen/examples/nested.h Stencil -I CACore/header --emit-reflection  # 反射（exit 0）
CodeGen.exe Tools/CodeGen/examples/nested.h RenderState::Stencil -I CACore/header  # 带外层前缀 → 找不到（exit 3）
```

字段表（`[exit 0]`，第二行起的形状与别处相同，末尾那一长串是容器标志位）：

```
Stencil 2
  field compareOp 0 int 17 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
  field ref 4 int 17 …（同上）
# options: serializable
```

反射产物的头两行（`[exit 0]`）—— 名字取自游标，带上了外层与命名空间：

```cpp
// Generated by CodeGen reflection emitter for game::RenderState::Stencil.
// Requires: the reflected type is fully defined (for offsetof) and CodeGenMeta.h is on
```

带外层前缀（`[exit 3]`）：

```
libclang: type 'RenderState::Stencil' not found in 'Tools/CodeGen/examples/nested.h'
```

## 三、两条标注通道：每条一份可直接复制的实例

下面两个头文件就在 `Tools/CodeGen/examples/` 里，可以原样复制改动；每一段后面的命令都在本机跑过，输出照抄。
两条通道的共同规则：**只有预处理器真的执行过这次宏调用，才算标注**（实现：解析时带
`CXTranslationUnit_DetailedPreprocessingRecord`，只认 libclang 报出来的 `CXCursor_MacroExpansion`，选项文本
取自这次调用的字节区间）。所以标注写在注释、字符串、`#if 0`、没用到的 `#define` 里**一律不算**。

### 通道一：`CA_REFLECT(...)` —— 标记写在类型定义之前（支持三个发射器）

`Tools/CodeGen/examples/reflect.h`：

```cpp
#pragma once

#include "CodeGenMeta.h"   // the descriptor/serializer interface the generated code depends on

namespace game
{
    CA_REFLECT(serializable, min = 0)
    struct Player
    {
        int    id;
        float  health;
    };
}
```

```bash
CodeGen.exe Tools/CodeGen/examples/reflect.h Player -I CACore/header                      # 字段表（exit 0）
CodeGen.exe Tools/CodeGen/examples/reflect.h Player -I CACore/header --emit-reflection     # 反射（exit 0）
CodeGen.exe Tools/CodeGen/examples/reflect.h Player -I CACore/header --emit-serialization  # 序列化（exit 0）
CodeGen.exe Tools/CodeGen/examples/reflect.h Player -I CACore/header --emit-hash           # 哈希（exit 0）
```

字段表（`[exit 0]`）：

```
Player 2
  field id 0 int 17 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
  field health 4 float 21 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
# options: serializable, min = 0
```

`--emit-reflection` 的**完整**产物（`[exit 0]`）——这就是"生成物长什么样"的样板，另两个发射器同形：

```cpp
// Generated by CodeGen reflection emitter for game::Player.
// Requires: the reflected type is fully defined (for offsetof) and CodeGenMeta.h is on
// the include path, before this fragment. Deterministic: same input => same bytes.
#pragma once

namespace cacore::codegen
{
template<>
struct TypeDescriptor<game::Player>
{
    static constexpr const char*              name       = "game::Player";
    static constexpr std::size_t              fieldCount = 2;

    static constexpr const CodeGenFieldInfo* fields() { return arr; }
    static constexpr CodeGenFieldInfo arr[2] = {
        { "id", "int", offsetof(game::Player, id), TypeKind::Int, FieldFlags::HashInclude, nullptr, nullptr },
        { "health", "float", offsetof(game::Player, health), TypeKind::Float, FieldFlags::HashInclude, nullptr, nullptr },
    };
};
} // namespace cacore::codegen
```

`--emit-serialization` 的产物（`[exit 0]`，写进 `-o` 给的路径；这里为了省地方省掉了注释头）：

```cpp
// Generated by CodeGen serialization emitter for game::Player.
// Requires: the type is defined + CodeGenSerialization.h on the include path.
// Deterministic: declaration-order field traversal, same input => same bytes.
// Unordered containers are flattened and sorted before writing, so the bytes depend on the
// CONTENT only - never on the insertion history of the container.
#pragma once

#include "CodeGenSerialization.h"

namespace cacore::codegen
{
template<>
struct CodeGenSerializer<game::Player>
{
    using Obj = game::Player;
    template<typename ByteBuffer>
    static void serialize(Obj const& obj, ByteBuffer& buffer)
    {
        ::cacore::codegen::writer<ByteBuffer> w(buffer);
        cgWriteValue(w, obj.id);            // 每个成员一行
        cgWriteValue(w, obj.health);
    }
    // The read position is an explicit parameter: this entry can be called in the middle of a
    // stream, and it leaves `position` where it stopped so the caller keeps reading.
    template<typename ByteBuffer>
    static bool deserialize(Obj& obj, ByteBuffer const& buffer, std::size_t& position)
    {
        ::cacore::codegen::reader_t<ByteBuffer> r(buffer, position);
        if (!cgReadValue(r, obj.id)) return false;
        if (!cgReadValue(r, obj.health)) return false;
        return r.ok();
    }
};
} // namespace cacore::codegen
```

`--emit-hash` 的产物（`[exit 0]`）——**一份文件里两个特化**：先相等、再哈希（`Hasher.h` 那条老路已经不在产物里）：

```cpp
// Generated by CodeGen hash/equality emitter for game::Player.
// Field-wise (padding/layout independent), floats normalized (-0.0=+0.0, NaN canonical).
// Requires: type defined + CodeGenHash.h on the include path. Deterministic (declaration order).
#pragma once

#include "CodeGenHash.h"

namespace cacore::codegen
{
template<>
struct CodeGenEqual<game::Player>
{
    using Obj = game::Player;
    static bool equal(Obj const& a, Obj const& b)
    {
        if (!cgEqualValue(a.id, b.id)) return false;
        if (!(a.health == b.health || (a.health != a.health && b.health != b.health))) return false;
        return true;
    }
};

template<>
struct custom_hash_trait<game::Player>
{
    using Obj = game::Player;
    template<typename hasher_t>
    static void hash(Obj const& obj, hasher_t& h)
    {
        cgHashContainerValue(h, obj.id);
        cgHashContainerValue(h, obj.health);
    }
    static bool equal(Obj const& a, Obj const& b) { return CodeGenEqual<game::Player>::equal(a, b); }
};
} // namespace cacore::codegen
```

### 通道二：`CA_CODEGEN_REGISTER("限定名");` —— 不改被标注的类型

`Tools/CodeGen/examples/register.h`（**结尾的分号必须有**：宏体里是 `static_assert`，自己不带分号）：

```cpp
#pragma once

#include "CodeGenMeta.h"

namespace game
{
    struct Inventory { int slots; int used; };
}

CA_CODEGEN_REGISTER("game::Inventory");
```

```bash
CodeGen.exe Tools/CodeGen/examples/register.h Inventory -I CACore/header --emit-reflection
```

`[exit 0]`，产物里的名字**取自字符串**（会带上命名空间，不用你在命令行写限定名）：

```cpp
template<>
struct TypeDescriptor<game::Inventory>
{
    static constexpr const char*              name       = "game::Inventory";
    static constexpr std::size_t              fieldCount = 2;
    static constexpr CodeGenFieldInfo arr[2] = {
        { "slots", "int", offsetof(game::Inventory, slots), TypeKind::Int, FieldFlags::HashInclude, nullptr, nullptr },
        { "used",  "int", offsetof(game::Inventory, used),  TypeKind::Int, FieldFlags::HashInclude, nullptr, nullptr },
    };
};
```

### 生成物怎么被消费

生成的是一段**片段**：消费它的翻译单元里，**类型定义和 `CodeGenMeta.h` 必须先于它出现**（序列化产物还
要求 `CodeGenSerialization.h`，哈希产物要求 `CodeGenHash.h`）。形状就是这样：

```cpp
#include "game/Player.h"                        // 类型定义
#include "CodeGenMeta.h"                        // 描述符接口
#include "generated/Player.Reflection.h"        // 生成物（放最后）
```

真实例子见 `Test/CoreTests/EmitReflectionTest.cpp`、`GeneratedSerializerTest.cpp`、`GeneratedHashTest.cpp`
——它们 `#include "generated/EmitSample.*.h"`，并对生成的表做运行期断言。

## 四、退出码

| 码 | 含义 | 谁的错 |
|---|---|---|
| 0 | 成功；或"该文件**解析干净**且没有任何被标注的类型"（此时**不写任何产物**） | — |
| 1 | libclang 解析失败 | 输入/参数 |
| 2 | 用法错误（参数不够、`--emit-all` 没给 `--out-dir`、`--out-dir` 没配 `--emit-all`、`-o` 与 `--out-dir` 同时给、`--out-dir=` 或 `-o ''` 给了空值）、读不到输入、打不开输出 | 调用方 |
| 3 | 在这个翻译单元里找不到该类型 | 输入 |
| 4 | 该类型没有被标注（而文件里有别的被标注类型） | 输入 |
| 5 | 拒绝发射：类型含非公有成员，而类外通道只能服务全公有类型 | 输入 |
| 6 | 拒绝提取：解析不可信 —— 环境残缺导致 AST 被截断，**或文件本身有错误级诊断（语法错误、include 找不到、标记宏未定义）**。报错会指名未解析的 include 与诊断条数 | 环境/输入 |
| 7 | 输出没能提交（`.tmp` 写不进或改名失败） | 环境 |
| 9 | 请求了 `--emit-serialization`，但标注没带 `serializable` 选项 | 输入 |
| 10 | 一次调用给了不止一个具体发射标志，或 `--emit-all` 与某个具体发射标志同时给 | 调用方 |
| 11 | 只找到该类型的**声明**、没有定义（成员表会是空的，所以拒发） | 输入 |
| 13 | 成员是**按位存储**的容器（`vector<bool>`）：元素取不到引用，旧运行时的元素循环绑不上 | 输入 |
| 14 | 成员是**重复键容器**（`multimap` / `multiset` / 无序变体）：等键之间没有规范顺序 | 输入 |
| 15 | 容器的元素是**裸指针或托管指针**：旧运行时不写指针元素，写出的流会静默丢数据 | 输入 |
| 16 | 容器的**键或元素没有可用的默认构造函数**：生成的读取代码会先 `T t{}` 再读入 | 输入 |
| 17 | 无序容器的**参与排序的一侧没有全序**（既没有 `operator<` 也没有 `operator<=>`） | 输入 |

> ⚠ 退出码 0 的第二种情况（"没有标注"）在旧版本里**掩盖过解析失败**：文件有语法错误、include 找不到、
> 或标记宏没定义时，标注列表同样是空的，旧版本照样返回 0 并打印 `no annotated types`，于是错误的编译
> 环境看起来和"干净的文件"一模一样。现在诊断检查跑在"空标注"这条提前返回**之前**：只要有错误级诊断或
> 未解析的 include，就是退出码 6。

## 五、容器：识别规则、磁盘形状与拒绝条件

（本节的规则由 `codegen-container-semantics` 落地；工具的行为与这里的每一句一一对应。）

### 识别规则

- 容器按**规范化后的类型基名**识别：`vector` / `array` / `string` / `basic_string` / `deque` / `list` /
  `map` / `multimap` / `set` / `multiset` / `unordered_map` / `unordered_multimap` / `unordered_set` /
  `unordered_multiset`。`castl::` 与 `std::` 的别名都归一到同一个基名。
- **C 数组按维度识别**：`int a[2][3]` 是一个容器，它的元素描述是 `int[3]`，`int[3]` 的元素是 `int`。
- **包装器只记录、不拒绝**：`NameHash` / `PathHash` / `HashObj` 会在字段表里置 `Wrapper` 位
  （`HasOpaqueElement` 保持为假），照常出码。
- 字段表逐成员给出 `key` 与 `element` 两个指向同构描述的指针，所以嵌套容器能表达出多层。

### 标量与字符串的字节契约（逐条）

生成物写出的每一个字节都由这张表决定。新运行时（`CACore/header/CodeGenSerialization.h` 的写对象）与
旧运行时（`cacore::serializer`，迁移期还在）在这五条上**逐字节相同** ——
`Test/CoreTests/CodeGenRuntimeTest.cpp` 里每个格式规则都有一条「新旧两套写同一个对象、比字节」的对拍，
本节每条规则后面括号里就是它跑出来的读数。

- **整数**：按类型自身的宽度写，`int` / `unsigned` 4 字节，`long long` / `unsigned long long` / `uint64_t`
  8 字节；字节序是**本机字节序**（x64 上是小端），不做任何转换。（`write == old runtime bytes = 1 (bytes=17)`）
- **`bool`**：1 字节（`sizeof(bool)`），`true` 写 1、`false` 写 0。（同上那条 17 字节里第 9 个字节）
- **枚举**：按它的**底层类型**的宽度写，写的是枚举值的位表示。`enum class E : uint8_t` 是 1 字节；
  没写底层类型的 `enum class E` 是 4 字节（底层是 `int`）。
- **浮点**：按位写，`float` 4 字节、`double` 8 字节；**不做任何规范化**，`-0.0f` 与 `+0.0f` 写出的字节不同。
  （哈希那一侧相反：算哈希时 `-0.0` 归一成 `+0.0`、NaN 归一成一个稳定值，见 `CodeGenHash.h`。）
- **字符串**：先写 `unsigned long long` 的元素个数，再写这些字节，**没有结尾的 `\0`**；空字符串写 8 个 0 字节。
  （`string bytes == old = 1 (bytes=15, 载荷长度 = 8 + 文本长度 = 15)`，文本是 7 个字符）

结构体成员按**声明顺序**逐个写；`map` / `set` 的元素是「键、值」各写一遍，写法与上面的标量契约相同。

### 磁盘形状契约

- 每个容器**先写元素个数**（`unsigned long long`），再逐个写元素。
- `map` / `unordered_map`：每个元素先写**键**、再写**值**；`set` / `unordered_set`：逐个写元素本身。
- **无序容器先排成规范顺序再写**：`map` 按**键**排序，`set` 按**元素本身**排序，`map` 的值不参与排序。
  因此"内容相同、插入顺序不同"的两个对象写出**逐字节相同**的流。
- **排序用的比较**是元素自己的 `operator<`；只有 `operator<=>` 也可以（编译器会合成 `<`）。
  参与排序的那一侧两者都没有时走拒绝路径（退出码 17）—— 这条是刻意放宽的：引擎里真实的键类型
  只有 `operator<=>`。
- **多维 C 数组按维度嵌套**：`int a[2][3]` 写成 `2`，然后每个 `int[3]` 写成 `3` 加三个 `int`。
- 读取时，有 `clear()` 的容器**先清空再按载荷重建**，所以读回来的内容只由载荷决定，与目标里原有的元素无关；
  **定长数组**（`castl::array` / C 数组 —— 它们没有 `clear()`）按位置逐个覆写，并在读之前校验载荷里的长度与
  extent 一致：不一致就是格式错，读取失败并报出两个长度（`被测的 extent` 与 `载荷里的长度`）。

### 形状总表：哪些必须支持、哪些必须拒绝

每一行都带探针读数（退出码）。探针都在仓库里，命令形如
`CodeGen.exe <头> <类型> -I CACore/header --emit-serialization`；少数几行要额外的 include 参数，表里注明。

**必须支持**（序列化与哈希两个发射器都退出码 0）：

| 形状 | 探针 | 读数 |
|---|---|---|
| 全公有成员：标量 / 字符串 / 结构体 / `vector` / `map` / `unordered_map` / 定长数组 | `Tools/CodeGen/TestShapes.h` 的 `Supported` | exit 0 |
| 类内标记的嵌套类型 | `Tools/CodeGen/examples/nested.h` 的 `Stencil` | exit 0（见第二节） |
| 包装类型（`NameHash` / `PathHash` / `HashObj`） | `CACore/private/TestWrapper.h` 的 `Holder` | 构建期逐条生成 + 逐字节对拍，exit 0 |
| 无序容器的键只有 `operator<=>`（引擎里的 `cahash::sha256_hash::result_type` 就是） | 两条 `ShaderLibrary` 数据链 | 同上，exit 0 |
| 容器的元素是内含无序容器的结构体（原退出码 12，已撤除） | `Interface/RenderInterface/private/TestContainer.h` 的 `Inner` / `Outer` | 同上，exit 0 |

**必须拒绝**：

| 形状 | 探针 | 退出码 | 报什么 |
|---|---|---|---|
| 成员本身就是裸指针 | `TestShapes.h` 的 `RawPointerMember` | 15 | 点名成员 `p` 与 `int *` |
| 成员本身就是托管指针 | `TestShapes.h` 的 `ManagedPointerMember` | 15 | 点名成员 `p` 与 `std::shared_ptr<int>` |
| 容器的元素是裸 / 托管指针 | `TestShapes.h` 的 `PointerElement` | 15 | 点名成员 `items` |
| 成员是重复键容器 | `TestShapes.h` 的 `MultiKey` | 14 | 点名成员 `m` 与容器类型 |
| 成员是按位存储的容器（`vector<bool>`） | `TestShapes.h` 的 `BitPacked` | 13 | 点名成员 `v` |
| 容器的元素不可默认构造 | `TestShapes.h` 的 `NoDefaultElement` | 16 | 点名成员 `v` 与元素类型 |
| 无序容器参与排序的一侧没有全序 | `TestShapes.h` 的 `NoOrderKeyMap` | 17 | 点名成员 `m` |
| 类型没被标注 | `Tools/CodeGen/TestNested.h` 的 `CodeGenNested` | 4 | 打印可用的两个标记 |
| 非公有成员 | `Tools/CodeGen/TestPrivate.h` 的 `PrivateLike` | 5 | 说明成员是按名字从类外访问的、要公开或开 friend 入口 |
| 编译环境残缺（要加 `-nostdinc++ -nostdinc`） | `Tools/CodeGen/TestStdLayout.h` 的 `StdLayout` | 6 | 点名未解析的 include 与退化的字段 |
| 标注没带 `serializable` | 归档夹具 `uisample.h` 的 `NoSer` | 9 | 回显看到的选项 |
| 一次给两个发射标志 | `Tools/CodeGen/TestEmit.h` 的 `EmitSample` | 10 | 说明三个互斥 |
| 只有声明没有定义 | 归档夹具 `only_declared_registered.h` 的 `OnlyDeclared` | 11 | 说明成员表会是空的 |

**指针 / 托管指针成员该怎么办（替代做法）**：把要落盘的部分提成值语义的结构体 —— 句柄写名字或索引，
另配一张随对象一起落盘的旁表；不要让序列化器去写进程内地址（地址在下次启动后没有意义，
而老运行时对指针是"直接跳过"，等于静默丢数据）。仓库里的实例是
`D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h` 的 `ShaderSetData`（一个非拥有的反向指针成员），
它的处置是**不进接入清单、不生成代码**。

只请求 `--emit-reflection` 时以上都不受影响（字段表照出）。注意 `build.py` 的接入清单**没有**按条目
选择发射器的能力：每条清单条目固定跑三个发射器，任一次非零退出即整体构建失败，所以只能出反射的类型
不能进清单。

## 五、调用示例（每条都在本机实跑过，输出逐字照抄）

### 1. 打印字段表（不带发射标志）

```
CodeGen.exe Tools/CodeGen/TestStruct.h CodeGenSample -I CACore/header
```
```
[exit 0]
CodeGenSample 4
  field a 0 int 17 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
  field b 4 float 21 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
  field c 8 bool 3 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
  field d 16 double 22 container=0 unordered=0 map=0 set=0 multikey=0 bitpacked=0 key= keyKind=0 element= elementKind=0 elemContainer=0 elemOpaque=0 elemManagedPtr=0
# options: serializable, min = 0
```

最后一行是标注里带的选项（原样回显）。字段行格式：`field <名字> <字节偏移> <类型拼写> <CXTypeKind 数值>`
后面再跟 13 项容器标志（`container=` / `unordered=` / `map=` / `set=` / `multikey=` / `bitpacked=` /
`key=` / `keyKind=` / `element=` / `elementKind=` / `elemContainer=` / `elemOpaque=` / `elemManagedPtr=`），
每一项**每条字段都会打**，不是只在有容器时才出现。

### 2. 发射反射描述符到标准输出

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-reflection
```
```
[exit 0]
// Generated by CodeGen reflection emitter for codegen_demo::EmitSample.
// Requires: the reflected type is fully defined (for offsetof) and CodeGenMeta.h is on
// the include path, before this fragment. Deterministic: same input => same bytes.
#pragma once

namespace cacore::codegen
{
template<>
struct TypeDescriptor<codegen_demo::EmitSample>
{
    static constexpr const char*              name       = "codegen_demo::EmitSample";
    static constexpr std::size_t              fieldCount = 4;
    ...（此处截断，完整产物由 build.py 与入库 golden 逐字节对拍）
```

### 3. 发射序列化特化到文件

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-serialization -o .review_tmp/readme/EmitSample.Serialization.h
```
```
[exit 0]                     （无 stdout / stderr；产物写在 -o 给的那个路径）
```
产物里每个字段一行 `cgWriteValue(w, obj.<成员>);`，`EmitSample` 有 4 个成员，所以恰好 4 行。
`build.py` 会把这份产物与入库的 `Tools/CodeGen/generated/EmitSample.Serialization.h` **逐字节对拍**，
不一致即构建失败。

### 4. 类型没被标注 → 4

```
CodeGen.exe Tools/CodeGen/TestNested.h CodeGenNested -I CACore/header
```
```
[exit 4]
CodeGen: type 'CodeGenNested' is not annotated (use CA_REFLECT or CA_CODEGEN_REGISTER); refusing to extract
```

### 5. 类型含非公有成员 → 5（不发编译不过的代码）

```
CodeGen.exe Tools/CodeGen/TestPrivate.h PrivateLike -I CACore/header --emit-reflection
```
```
[exit 5]
CodeGen: refusing to emit for 'codegen_private_test::PrivateLike' — it has non-public members, and the
generated code reads members by name from outside the class (an in-class marker does not change that
access check). Make them public, or give the generated code friend access through the private-member
entry point.
```
同一条命令**去掉** `--emit-reflection`（只取字段表）是 exit 0 —— 提取与发射的区分成立。

### 6. 一次给两个发射标志 → 10

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-serialization --emit-hash
```
```
[exit 10]
CodeGen: refusing — 2 emitter flags in one run; --emit-reflection / --emit-serialization / --emit-hash
are mutually exclusive per invocation. Invoke the tool once per emitter, or ask for all three at once
with --emit-all --out-dir <dir>.
```

### 7. 标注没带 `serializable` → 9

```
CodeGen.exe openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/fixtures/uisample.h NoSer -I CACore/header -I Tools/CodeGen --emit-serialization
```
```
[exit 9]
CodeGen: refusing --emit-serialization for 'audit_ns::NoSer' — the type's CA_REFLECT marker does not
carry the `serializable` option (marker options seen: 'ui'). Add `serializable` to the marker, or emit
reflection only.
```

### 8. 编译环境残缺 → 6（点名 include 与退化的字段）

```
CodeGen.exe Tools/CodeGen/TestStdLayout.h StdLayout -I CACore/header -nostdinc++ -nostdinc
```
```
[exit 6]
CodeGen: refusing to extract from 'Tools/CodeGen/TestStdLayout.h' — the compile environment fed to this
tool is incomplete, so the parsed AST is truncated and the type table would be silently wrong. Fidelity
cannot be judged by the exit code.
  unresolved include (fatal): CACore/header\CodeGenMeta.h:14:10: fatal error: 'cstddef' file not found
  前几条错误（不在被解析的头里，多半来自它 include 的文件）:
    CACore/header\CodeGenMeta.h:14:10: fatal error: 'cstddef' file not found
  degraded members of 'StdLayout' (reported offset 0 although not the first member): key, tail
```
判据是**属性**：只看 `<string>` 是否可达，不看命令行里出现了什么参数。含 `-nostdinc` 但用 `-isystem`
把 STL 各根喂满 → exit 0。

### 9. 类型只有声明没有定义 → 11

```
CodeGen.exe openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/fixtures/only_declared_registered.h OnlyDeclared -I CACore/header --emit-reflection
```
```
[exit 11]
CodeGen: refusing to emit for 'OnlyDeclared' — only a DECLARATION of it exists in
'openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/fixtures/only_declared_registered.h', not a
definition, so its member list would be emitted as empty. Include the header that defines the type (or
pass that header as the input).
```

### 10. 一次调用出三份产物 → 0（`--emit-all`）

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-all --out-dir .review_tmp/readme/emit-all
```
```
[exit 0]
```

`--out-dir` 下出现三个文件，名字就是 `build.py` 期望的 `<类型名>.<发射器>.h`：

```
EmitSample.Reflection.h      1176
EmitSample.Serialization.h   1547
EmitSample.Hash.h            1310
```

这三份与"分别跑 `--emit-reflection` / `--emit-serialization` / `--emit-hash` 三次、各写一个 `-o`"的产物
**逐字节相同**（实测三份 SHA256 两两相等，大小就是上面三个数）—— `--emit-all` 改的是"同一份头解析几遍"，
不是出码规则。`build.py` 的接入清单就是按它一条一次调用。

四种用法错误各自拒绝（退出码即上表）：`--emit-all` 没配 `--out-dir` → **2**；`--out-dir` 没配 `--emit-all`
→ **2**；`--emit-all` 与某个具体发射标志同时给 → **10**；一次给两个具体发射标志 → **10**（见示例 6）。

## 六、构建期怎么用它

`build.py` 的 `codegen()` 步骤（排在所有编译之前）：先构建 `CodeGen` 目标，再按**接入清单**逐条生成，
把产物与**该条目自己的入库目录**逐字节对拍，任一不一致或工具非零退出即**构建失败**。清单为空是合法配置
（不阻断构建）。**输入与入库副本都没变的条目会跳过**（省掉一次 libclang 解析），见下一节。

### 构建开关与「跳过没变的条目」

| 开关 | 作用 |
|---|---|
| `--configure` | 强制重新 configure。默认不强制：只要所有 `CMakeLists.txt` / `*.cmake` / `CMakePresets.json` 都比 `out/build/<预设>/build.ninja` 旧，configure 就整段跳过 |
| `--no-configure` | 完全不 configure（从前就有） |
| `--codegen-all` | 忽略账本，清单里每一条都重跑（等价于从前的行为，排查用） |

**已删除的两个开关**：`--no-codegen`（跳过整个生成段）与 `--commit-generated`（把新生成物写进入库目录）
随「生成物入库」一起没了 —— 生成物只落构建目录、每次构建现场生成，既没有「只编译、不生成」这条快路，
也没有「写回仓库」这个动作。**传了会直接报错退出**（退出码 1），不会被当成普通构建悄悄跑过去。

**账本**：`out/build/<预设>/codegen.stamp.json`。清单每一行按**这些输入**算指纹 —— 工具二进制、被解析的
那个头、**生成器这一次真正读过的每一个文件**（`CodeGen deps:` 那一串，逐个算内容哈希）、该模块推导出的
编译环境（include 与宏）、**包含目录的名字清单**（防"新加一个同名头把旧头挡住"）、上一轮的三份产物。
全部相同、且三份产物都在，这一行才跳过；任何一样变了（包括工具被重新编译过）就重跑。只在这一行
**没有任何失败**时才记账，所以失败不会因为下一轮被跳过而消失。

### 构建耗时：三档，照着跑就能复现

同一个机器、同一个构建目录，跑下面三条命令，看末尾那行 `[耗时]`（本机 Debug 预设，2026-10-05 实测）：

1. **全量**（忽略账本，63 条全跑）：`python build.py --config Debug --codegen-all`
   → `codegen: 重跑 63 条、跳过 0 条`，`[耗时] configure 0.0s / codegen 49.1s / 编译 49.9s / 合计 99.0s`
2. **改一个被标注的头**（例如给 `Interface/RenderInterface/header/GPUBuffer.h` 加一行注释）：
   `python build.py --config Debug`
   → `codegen: 重跑 1 条、跳过 62 条`，`[耗时] configure 0.0s / codegen 5.4s / 编译 55.2s / 合计 60.6s`
   （大头是**重编受影响的那些编译单元**，不是生成）
3. **什么都不改**：`python build.py --config Debug`
   → `codegen: 重跑 0 条、跳过 63 条`，`[耗时] configure 0.0s / codegen 1.5s / 编译 2.8s / 合计 4.3s`

对照（同一台机器、同一天，成对读数）：

- 全量：**49.1 秒**（并发 8 条）对 **149.0 秒**（把 `CODEGEN_MAX_WORKERS` 临时压到 1）—— 同一份代码、同一批输入，差 99.9 秒；
- 调用次数：现在每个类型**一次**（63 次），从前是三次（189 次）；
- 从前行与行之间是**串行**的：`git show HEAD:build.py` 的内层 `for emit_flag, suffix in CODEGEN_EMITTERS:` 一行跑完三次再跑下一行，任何时刻只有 1 个进程在跑。

### 接入清单：四列

```python
CODEGEN_ANNOTATED = [
    # (头文件, 类型名, 归属模块目录, 可选的限定名)
    ("Tools/CodeGen/TestEmit.h", "EmitSample", "Test/CoreTests"),
    ("Interface/RenderInterface/header/GPUBuffer.h", "GPUBufferDescriptor", "Interface/RenderInterface"),
]
```

- **第三列**（归属模块目录）决定**用谁的编译环境解析这个头**，也决定**产物落在构建目录的哪一层**：
  `out/build/<预设>/codegen/<归属目录，`/` 换成 `_`>/`。它按路径段匹配仓库相对路径，构建树 `out/`
  （FetchContent 拉下来的第三方）整体排除。
- **归属写谁**：写**真正编译（消费）这个类型的模块**。两个自包含夹具的头文件放在 `Tools/CodeGen/` 下，
  但产物是被 `Test/CoreTests` 的编译单元 include 的，所以归属写 `Test/CoreTests` —— 写"头文件所在的目录"
  会推不出编译环境（生成器已经是独立工程，引擎的 `compile_commands.json` 里没有它的编译单元）。
- **第四列**（可选）是**限定名**：工具按**最后一个非限定名**查找类型，同一个 TU 里同名的类型很常见，
  带上它可以让生成物里写上完整的限定名。

### 编译环境怎么来的（不手写任何头文件清单）

从 `compile_commands.json` 推导，规则三条：

1. **include 目录取该模块全部编译单元的并集**。单取一条会漏：实测 `Interface` 单条最多 12 个、并集 13 个，
   `Test` 21 / 22。
2. **宏只允许来自单一条编译单元**。同一模块里出现多套宏时**拒绝构建**并列出这些宏，不替调用方挑一套
   （实测 `Test` 有两套，其中一套带 Jolt 系列宏）。
3. **保留预编译头的强制包含**：编译单元用了 `/FI` + `/Yu` 时，它们指向的头文件会作为 `-include <文件>`
   传给 libclang（实测 `Interface` 的 `cmake_pch.hxx`）。另外 `/MDd` 会转成
   `-D_DEBUG -D_ITERATOR_DEBUG_LEVEL=2`（否则 STL 布局按 release 规则算，`std::string` 之后的偏移全错）。

### 已接入的真实类型（可照着加第二个）

`graphics_backend::GPUBufferDescriptor`（`Interface/RenderInterface/header/GPUBuffer.h`）是清单里第一个真实
引擎类型，接线一共四处：

1. **头文件**：加 `#include "CodeGenMeta.h"`，并在类型定义**之前**写 `CA_REFLECT(serializable)`
   （标记必须是真实宏调用 —— 工具靠预处理器判定，不扫源码文本）。
2. **清单**：按上面四列加一条。
3. **生成物入库**：`Interface/RenderInterface/generated/GPUBufferDescriptor.{Reflection,Serialization,Hash}.h`，
   生成后复制进去（`build.py` 只写 build 目录，**从不改源码树**）。
4. **消费与断言**：`Interface/RenderInterface/private/GPUBufferDescriptorReflection.cpp` include 生成头，
   写 `CA_REFLECTION(graphics_backend::GPUBufferDescriptor, count, stride);`，并断言字段数、每个字段的
   `offsetof`、类型名与成员名，最后实例化框架自带的 `cacore::codegen::static_assert_abi_consistency<T>()`。
   该目录通过 `target_include_directories(${PROJECT_NAME} PRIVATE generated)` 进 include 路径。

这四处做完之后有三种失败都会**在构建期**报出来：删掉入库的生成头（对拍或编译失败）、改了类型没重新生成
（逐字节不一致）、入库文件被换成别的类型的产物（编译期断言失败）。

libclang 的位置由 CMake 变量 `CALIB_CLANG_DIR` 给出（默认指向 vendored LLVM）；该目录下没有
`bin/x64/libclang.dll` 时 CodeGen 目标不会构建。

## 七、验证与证据在哪儿

- **本文档的示例本身是可核的**：`python Tools/CodeGen/verify_readme.py` 会逐条读出上面那些命令、原样跑一遍，
  并把它与文档里标注的退出码对比（0 处不符即通过）。改了工具行为就顺手跑它，别让文档烂掉。
- 第三节那两段示例头文件本体在 `Tools/CodeGen/examples/`（`reflect.h` / `register.h`），可以直接复制改动、
  也可以直接拿来跑上面的命令；第二节那条类型名口径的样板是 `Tools/CodeGen/examples/nested.h`。
- 单元测试（运行期断言，消费入库的生成头）：`Test/CoreTests/` —— `EmitReflectionTest.cpp`（字段名逐字）、
  `GeneratedSerializerTest.cpp`（序列化往返）、`GeneratedHashTest.cpp`（哈希/相等）、`CodeGenMetaTest.cpp`
  （字节偏移的独立数值）。
- 工具行为的实测证据：`openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/` —— 其中
  `regen_codegen_evidence.py` 按固定用例组生成 `r26-gates.txt` / `r28-gates.txt` / `r31-950.txt`，
  `regen_annotation_matrix.py` 生成 `r35-matrix.txt`（"不是代码的写法" × 三条通道的矩阵）。
- 证据里的输入路径必须能在仓库里找到，由 `python Tools/spin_check.py evidence` 强制。

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
  IDE 的跳转与补全才会正常（在那之前 IntelliSense 会报一堆找不到 `*.codegen.h`）。

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
| `--emit-all` | 一次调用把上面三节**全部**发射出来，写进**一份** `<TypeName>.codegen.h`（配 `--out-dir`）。同一份头只解析一遍；每一节的字节与单发射器逐字节相同（实测 228 次逐节对拍全同）。它还会把这次**真正读过的文件**逐行打到标准输出（`CodeGen deps: <路径>`）、把**真正写出的那一份产物**也打出来（`CodeGen wrote: <路径>`，恰好一行），供 `build.py` 记账并确认那一份真的写出去了 |
| `--qualified-name=<qn>` 或 `--qualified-name <qn>` | 覆盖发射进代码里的限定名 |
| `-o <路径>` | 写文件（先写同名 `.tmp`，成功后改名覆盖；失败时**不留残渣**）。省略则写标准输出 |
| `--out-dir <目录>` 或 `--out-dir=<目录>` | 与 `--emit-all` 搭配：写**一份** `<目录>/<TypeName>.codegen.h`（先写 `.tmp`，全部写完才改名覆盖；目录必须已存在）。节顺序固定为**反射 → 序列化 → 哈希**，文件头注释 / `#pragma once` / 运行时头的 `#include` 各只出现一次。单独给某个发射标志时不能用它 —— 那种情况用 `-o <路径>`，文件名由调用方自己取 |
| `--emit-binding` | 绑定发射器：按头里的 `CA_BIND` 标注，把整份头里所有被标注接口投影成 `<接口>.Bindings.cpp` + `<接口>.Bindings.cs`（一次调用处理一个头，不需要 `<TypeName>`）。见「十四、绑定发射器」 |
| `--audit-script <路径>` | 只配 `--emit-binding`：审计脚本路径（默认取**输入头所在目录**下的 `check_bindable_audit.py`）。生成前会以子进程调它，接口不在审计表覆盖清单里时打"跳过审计核对" |
| `--handle-index-bits <n>` | 只配 `--emit-binding`：句柄里**序号位**的位数，默认 **48**。合法范围：至少 1 位，且与 `--handle-generation-bits` 之和不超过 **64**；越界时退出码 **2** 并说明 |
| `--handle-generation-bits <n>` | 只配 `--emit-binding`：句柄里**代际位**的位数，默认 **16**。合法范围同上。`release` 后代际号进位，旧句柄立刻失效 |
| `--registry-part <标签>` | 只配 `--emit-binding`：声明**本头是登记表发射输入**（见「十四、登记表发射」）。要求该头里每个被 `CA_BIND` 标注的接口都带编号；缺号 / 同号 ⇒ 非零退出并点名，且**不写任何产物**。成功时把 `<标签>.registry.part` 分片写进 `--registry-part-dir` |
| `--registry-part-dir <目录>` | 只配 `--registry-part`：分片落点（目录必须已存在）；缺省 = `--out-dir` |
| `--emit-registry` | **合并模式**（不给头文件、不再解析 C++）：读若干个 `--registry-part-file` 分片，产出登记表三份产物。见「十四、登记表发射」 |
| `--registry-part-file <路径>` | 只配 `--emit-registry`：一个输入分片；可重复。编号跨分片重复 / 跳号 / 从非 1 开始时非零退出并点名 |
| 其余参数 | 原样交给 libclang，例如 `-I CACore/header`、`-std=c++20`、`-D...` |

**发射标志互斥**：`--emit-reflection` / `--emit-serialization` / `--emit-hash` 在**一次调用里只允许给一个**；
要一次拿齐三节、写进一份 `<类型名>.codegen.h`，就用 `--emit-all --out-dir <目录>`（它不能与那三个同时给）。
三个都不给 = 打印字段表。

**`--emit-all` 不是"逐个发射"的同义词：它把三节的拒绝条件全部打开。** 一次调用要出三节，工具就得对三节
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
#include "Player.codegen.h"                     // 生成物（一个类型一份；放最后；它住在构建目录的
                                                // codegen/<归属目录>/ 下，那个目录由 CMake 的
                                                // target_include_directories 加进包含路径）
```

真实例子见 `Test/CoreTests/EmitReflectionTest.cpp`、`GeneratedSerializerTest.cpp`、`GeneratedHashTest.cpp`
——它们 `#include "EmitSample.codegen.h"` 这一份生成物，并对生成的表做运行期断言。

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

**包装类型的成员是空指针时写什么**（`wrapper_traits<T>::get_data` 的实现决定，实测于 2026-10-05）：
`ComPtr<ID3DBlob>` 那类包装成员在**空指针**时写一段**空载荷**（`uint64 0`，8 字节），读回来是一个 0 字节的
对象，不是"什么都没写" —— 于是"写→读→再写"的字节稳定。**注意这与老的一侧不同**：老运行时把指针成员整段
跳过（一个字节都不写），所以含这类成员的类型的磁盘字节与老格式**不一致**，这是本 change 有意为之
（7.2 / D8：含指针成员不许静默丢数据）。实现包装特化时**必须判空**：不判空就是解引用空指针，
实测会让进程访问违例（退出码 `0xC0000005`）。

只请求 `--emit-reflection` 时以上都不受影响（字段表照出）。注意 `build.py` 的接入清单**没有**按条目
选择发射器的能力：每条清单条目固定跑一次 `--emit-all`（三节一起、写一份产物），任一次非零退出即整体构建失败，所以只能出反射的类型
不能进清单。

## 六、调用示例（每条都在本机实跑过，输出逐字照抄）

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
    ...（此处截断；完整产物由 build.py 每次构建现场生成并核对名单）
```

### 3. 发射序列化特化到文件

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-serialization -o .review_tmp/readme/EmitSample.Serialization.h
```
```
[exit 0]                     （无 stdout / stderr；产物写在 -o 给的那个路径）
```
产物里每个字段一行 `cgWriteValue(w, obj.<成员>);`，`EmitSample` 有 4 个成员，所以恰好 4 行。
`build.py` 每次构建现场生成这份产物（落在构建目录），并核对工具报出的 `CodeGen wrote:` 名单 —— 少了哪一份
或工具非零退出，构建就失败。**生成物不进源码树**：仓库里没有 `generated/` 目录，也没有"入库副本"可对拍。

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
CodeGen: refusing to emit for 'codegen_private_test::PrivateLike' — it has non-public members, and the generated code reads members by name from outside the class (an in-class marker does not change that access check). Make them public, or put `CA_CODEGEN_FRIEND(PrivateLike);` inside the class body (it friends the four generated entry points — the equivalent of the old `CA_PRIVATE_REFLECTION`).
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

### 10. 一次调用出一份产物（三节）→ 0（`--emit-all`）

```
CodeGen.exe Tools/CodeGen/TestEmit.h EmitSample -I CACore/header --emit-all --out-dir .review_tmp/readme/emit-all
```
```
[exit 0]
（stdout 里先是若干行 `CodeGen deps: <路径>`，最后是那一行写出名单：）
CodeGen wrote: .review_tmp/readme/emit-all/EmitSample.codegen.h
```

`--out-dir` 下只出现**一份** `EmitSample.codegen.h`（4,312 字节）。文件头是文件级注释 + 一次 `#pragma once` +
`CodeGenMeta.h` / `CodeGenSerialization.h` / `CodeGenHash.h` 各一次 include；三节顺序固定为
**反射 → 序列化 → 哈希**，每节保留自己的 `// Generated by CodeGen <某个> emitter for <类型>` 注释行。

这一份里每一节的字节与"跑 `--emit-reflection` / `--emit-serialization` / `--emit-hash` 三次、各写一个 `-o`"
的产物**逐字节相同**（实测 76 个类型 × 3 节 = **228 次对拍全同**）—— `--emit-all` 改的是"同一份头解析几遍"
与"写几个文件"，不是出码规则。`build.py` 的接入清单就是按它一条一次调用。

> 那三个后缀名（`.Reflection.h` / `.Serialization.h` / `.Hash.h`）作为**单节 `-o` 的输出名**仍然可用
> （示例 1 / 3 就是这么取的），工具不认名字、也不再用它们命名产物：`--out-dir` 只写 `<类型名>.codegen.h`。

四种用法错误各自拒绝（退出码即上表）：`--emit-all` 没配 `--out-dir` → **2**；`--out-dir` 没配 `--emit-all`
→ **2**；`--emit-all` 与某个具体发射标志同时给 → **10**；一次给两个具体发射标志 → **10**（见示例 6）。

## 七、构建期怎么用它

`build.py` 的 `codegen()` 步骤（排在所有编译之前）：先构建 `CodeGen` 目标，再按**接入清单**逐条生成，
逐条核对工具报出的产物名单，任一不一致或工具非零退出即**构建失败**。产物落在构建目录
（`out/build/<预设>/codegen/<归属目录>/`），**源码树里没有生成物**。清单为空是合法配置（不阻断构建）。
**输入没变的条目会跳过**（省掉一次 libclang 解析），见下一节。

### 构建开关与「跳过没变的条目」

| 开关 | 作用 |
|---|---|
| `--configure` | 强制重新 configure。默认不强制：只要所有 `CMakeLists.txt` / `*.cmake` / `CMakePresets.json` 都比 `out/build/<预设>/build.ninja` 旧，configure 就整段跳过 |
| `--no-configure` | 完全不 configure（从前就有） |
| `--codegen-all` | 忽略账本，清单里每一条都重跑（等价于从前的行为，排查用） |

**已删除的两个开关**：`--no-codegen`（跳过整个生成段）与 `--commit-generated`（把新生成物写回源码树的入库目录）
随「生成物改为只落构建目录」一起没了 —— 生成物只落构建目录、每次构建现场生成，既没有「只编译、不生成」这条快路，
也没有「写回仓库」这个动作。**传了会直接报错退出**（退出码 1），不会被当成普通构建悄悄跑过去。

**账本**：`out/build/<预设>/codegen.stamp.json`。清单每一行按**这些输入**算指纹 —— 工具二进制、被解析的
那个头、**生成器这一次真正读过的每一个文件**（`CodeGen deps:` 那一串，逐个算内容哈希）、该模块推导出的
编译环境（include 与宏）、**包含目录的名字清单**（防"新加一个同名头把旧头挡住"）、上一轮的那一份产物。
全部相同、且那一份产物在，这一行才跳过；任何一样变了（包括工具被重新编译过）就重跑。只在这一行
**没有任何失败**时才记账，所以失败不会因为下一轮被跳过而消失。

生成段在跑工具**之前**还会清掉旧命名的产物（`out/build/*/codegen/**/<类型名>.{Reflection,Serialization,Hash}.h`，
**所有预设**都清）：那些名字在源码树里已经没有消费者，留着只会在下一次构建别的预设时被 `#include` 到 ——
"改了新文件、编的却是旧文件"的假绿。

### 构建耗时：三档，照着跑就能复现

同一个机器、同一个构建目录，跑下面三条命令，看末尾那行 `[耗时]`（本机 Debug 预设；**2026-10-05 收尾时重测**，清单这时是 **76 条** —— 比上一轮多出四个键类型：`ViewRectData`、`GPUPipelineStateKey`、`VertexInputBindingData`、`InputElementDesc`，它们原来因为解析门只能手写哈希特化）：

1. **全量**（忽略账本，76 条全跑）：`python build.py --config Debug --codegen-all`
   → `codegen: 重跑 76 条、跳过 0 条`，`[耗时] configure 0.0s / codegen 40.6s / 编译 42.9s / 合计 83.5s`
   （同一天更早一次、清单还是 63 条时是 `codegen 49.1s / 合计 99.0s` —— 条数多了、生成段反而更快，因为那次编译段要重编的单元更多。）
2. **从零**（清掉构建目录里除 `_deps/` 之外的全部内容，再 `python build.py --config Debug --offline-fetch`）：
   → `codegen: 重跑 76 条、跳过 0 条`，`[耗时] configure 52.3s / codegen 39.3s / 编译 97.5s / 合计 189.1s`
   （configure 那 52 秒里大部分是主配置与各外部依赖的再生成；`--offline-fetch` 见 `CLAUDE.md` 的「构建」节。）
3. **改一个被标注的头**（例如给 `Interface/RenderInterface/header/GPUBuffer.h` 加一行注释）：`python build.py --config Debug`
   → `codegen: 重跑 1 条、跳过 75 条`，耗时大头是**重编受影响的那些编译单元**，不是生成。
   ⚠ 这一档的秒数没有在收尾时重测（改头文件要动工作区，测完还得逐字节还原）；上一次实测（同日、别的东西不变）是 `codegen 5.4s / 编译 55.2s / 合计 60.6s` —— 引用时请连日期一起引。
4. **什么都不改**：`python build.py --config Debug`
   → `codegen: 重跑 0 条、跳过 76 条`，`[耗时] configure 0.0s / codegen 1.3s / 编译 2.1s / 合计 3.5s`

**2026-10-11 补一组读数**（"一个类型一份产物"落地之后，同一台机器）：全量
`python build.py --config Debug --codegen-all` → `codegen: 重跑 76 条、跳过 0 条`，
`[耗时] configure 0.0s / codegen 40.5s / 编译 62.7s / 合计 103.2s`。编译段比上面那组大是因为这一次
26 个消费文件的 include 行变了、受影响的编译单元要重编；生成段与上面同量级（写出的文件从 228 份降到 76 份，
省下的磁盘写入在总耗时里看不出来）。

对照（同一台机器、同一天，成对读数）：

- 全量：**49.1 秒**（并发 8 条）对 **149.0 秒**（把 `CODEGEN_MAX_WORKERS` 临时压到 1）—— 同一份代码、同一批输入，差 99.9 秒；
- 调用次数：现在每个类型**一次**（76 次），从前是三次（原来 63 条时是 189 次）；
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
引擎类型。接线一共四处（**生成物不进源码树**：它们落在构建目录的 `codegen/<归属目录>/` 下）：

1. **头文件**：加 `#include "CodeGenMeta.h"`，并在类型定义**之前**写 `CA_REFLECT(serializable)`
   （标记必须是真实宏调用 —— 工具靠预处理器判定，不扫源码文本）。成员是私有的就再在类体里写
   `CA_CODEGEN_FRIEND(类型名)`：它把四个生成入口（`TypeDescriptor` / `CodeGenSerializer` / `CodeGenEqual` /
   `custom_hash_trait`）加成友元 —— 入口在类内时不用写这一行。
2. **接入清单**：`build.py` 的 `CODEGEN_ANNOTATED` 按四列加一条（头文件、类型名、归属目录、产出目录）；
   归属目录同时决定"用谁的编译环境解析"和"产物落在构建目录的哪一格"。
3. **消费方要看得见生成物**：在**类型自己的头**末尾（或消费方的编译单元里）include 那一份产物 ——
   `#include "<类型>.codegen.h"`（一个类型一份，反射 / 序列化 / 哈希三节都在里面；一次调用出的就是它）。
   `cacore::HashObj<T>` 与
   `std::unordered_map<…, cacore::hash<T>>` 在**实例化**时就要看到哈希特化，少一个类型的那一份那一层就会退回
   "整块字节"或直接编不过。归属模块的 `codegen` 目录必须在 `target_include_directories(... PUBLIC)` 里，
   别的模块才拿得到（`Interface/RenderInterface/CMakeLists.txt` 就是这么写的）。
4. **消费单元里断言**：字段条数、每个字段的 `offsetof` 偏移、容器标志与元素描述、以及
   `cacore::codegen::has_custom_hash_func<T, cacore::codegen::hasher<>>`（"既有哈希入口看得到生成的特化"）。
   样板：`Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`、
   `D3D12RenderBackend/private/ShaderLibrary/ShaderLibraryCodeGenAssertions.cpp`。

**加不进去清单的类型，用两份手写特化顶上**（设计里给的第二条路）：

- **指针 / 托管指针成员**（`castl::shared_ptr`、`ComPtr<T>`…）：序列化侧按 7.2 的替代做法改结构（句柄存
  名字或索引 + 一张随对象落盘的旁表），**不要**让生成代码去写进程内地址。真要保留这个类型当哈希键，
  就手写 `cacore::codegen::custom_hash_trait<T>`：只算值语义的那几个字段，指针按**地址**或干脆不算
  （地址是进程内的东西，进了哈希只会让同一个对象在不同进程落到不同的桶）。实例：
  `Interface/RenderInterface/header/ShaderResourceHandle.h` 的 `ImageHandle` / `BufferHandle`。
- **头本身过不了解析门**（文件里有错误级诊断：语法错误、找不到的 include、老宏判不出常量表达式…）：
  同样手写 `custom_hash_trait`，把"能不能当键"与"能不能落盘"分开。实例：
  `D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.h` 的三个键类型、`Interface/RenderInterface/
  header/GPUGraph.h` 的 `ViewRectData`。

这四处做完之后有三种失败都会**在构建期**报出来：生成物没写出来（`CodeGen wrote:` 名单对不上）、改了类型
没重新生成（逐字节不一致）、消费方看不到特化（编译期断言失败或直接编不过）。

libclang 的位置由 CMake 变量 `CALIB_CLANG_DIR` 给出（默认指向 vendored LLVM）；该目录下没有
`bin/x64/libclang.dll` 时 CodeGen 目标不会构建。

## 八、验证与证据在哪儿

- **本文档的示例本身是可核的**：`python Tools/CodeGen/verify_readme.py` 会逐条读出上面那些命令、原样跑一遍，
  并把它与文档里标注的退出码对比（0 处不符即通过）。改了工具行为就顺手跑它，别让文档烂掉。
- 第三节那两段示例头文件本体在 `Tools/CodeGen/examples/`（`reflect.h` / `register.h`），可以直接复制改动、
  也可以直接拿来跑上面的命令；第二节那条类型名口径的样板是 `Tools/CodeGen/examples/nested.h`。
- 单元测试（运行期断言，消费生成的表头）：`Test/CoreTests/` —— `EmitReflectionTest.cpp`（字段名逐字）、
  `GeneratedSerializerTest.cpp`（序列化往返）、`GeneratedHashTest.cpp`（哈希/相等）、`CodeGenMetaTest.cpp`
  （字节偏移的独立数值）。
- 工具行为的实测证据：`openspec/changes/archive/2026-09-20-engine-codegen-framework/evidence/` —— 其中
  `regen_codegen_evidence.py` 按固定用例组生成 `r26-gates.txt` / `r28-gates.txt` / `r31-950.txt`，
  `regen_annotation_matrix.py` 生成 `r35-matrix.txt`（"不是代码的写法" × 三条通道的矩阵）。
- 证据里的输入路径必须能在仓库里找到，由 `python Tools/spin_check.py evidence` 强制。

## 九、收尾扫描：旧序列化 / 哈希设施的符号与指纹

`Tools/CodeGen/check_legacy_symbols.py` 是**给"删旧设施"这类收尾用的**扫描器：它回答"旧设施在产物里是不是
真的没了"。三条判据一次跑完，也可以分开跑：

```
python Tools/CodeGen/check_legacy_symbols.py                 # 三条一起跑
python Tools/CodeGen/check_legacy_symbols.py --symbols       # 只扫符号
python Tools/CodeGen/check_legacy_symbols.py --fingerprints  # 只扫旧实现指纹
python Tools/CodeGen/check_legacy_symbols.py --files-exist   # 只查三份旧头在不在
```

- **扫描范围**：引擎源码（后缀白名单 `.h/.hpp/.hh/.hxx/.inl/.cpp/.cxx/.cc/.c/.py`）**加上构建目录里的生成物**
  （`out/build/*/codegen/**/*.h`）；跳过 `openspec/`、`out/`（生成物那份另算）、`_deps/`、`.review_tmp/`、
  `.claude/`、`build/` 与脚本自己。
- **判的是什么**：一份写死的符号表（旧设施对外露出的限定名，如 `cacore::serializer`、`cacore::aggregateHasher`、
  `CATypeDescriptor`）与一份旧实现指纹表（旧运行时实现里那几句特征话：`visit_members`、`CATypeMemberDesc`、
  `containerInfo`、`Submit Count:`、`Object must be trivially copyable`、`hash_one`、`managed_pointer_traits`）。
  注释里出现限定名**照样算命中** —— 判据是"这些名字在产物里彻底消失"。
- **退出码**：`0` = 三条判据都为零（打印 `扫描 N 个文件、命中 0 行`）；`1` = 有命中没被白名单放行（逐条打印
  `文件:行号: [符号] 原文`）；`2` = 用法错 / 找不到仓库根。
- **白名单**：脚本顶部 `ABI_GUARD_WHITELIST`，每条 =（仓库相对路径，该行去掉首尾空白后的原文）。终点是**空表**：
  需要临时放行时加一条并写清原因，别让判据静默通过。

本机实跑（2026-10-05，本 change 收尾时）：

```
$ python Tools/CodeGen/check_legacy_symbols.py
== 逐符号扫描（源码 + 构建目录里的生成物） ==
扫描 505 个文件、命中 0 行
== 旧实现指纹 ==
扫描 505 个文件、命中 0 行
== 三份旧头必须不存在 ==
  CACore/header/Reflection.h: 不存在
  CACore/header/Serialization.h: 不存在
  CACore/header/Hasher.h: 不存在

扫描 505 个文件、命中 0 行
$ echo $?
0
```

命中时它会变成这样（同一天、删旧头之前的一次实跑，退出码 1）：

```
== 逐符号扫描（源码 + 构建目录里的生成物） ==
扫描 606 个文件、命中 63 行
  CACore/header/CodeGenMeta.h:128: [CATypeDescriptor] // 私有成员的友元入口。老系统里对应的是 …
== 旧实现指纹 ==
扫描 606 个文件、命中 64 行
== 三份旧头必须不存在 ==
  CACore/header/Reflection.h: 仍然存在
  …
命中 130 行（白名单 0 条）
```

> ⚠ 一条实测出来的坑：指纹表里**不要**放接口自己的方法名（例如 `SubmitAndWait` 是 `ca_io::WBatch` / `IOBatch`
> 的方法，随接口一起留着），否则判据永远清不到 0。要放就放旧实现独有的东西 —— 旧序列化器那句
> `Submit Count:` 调试打印就是这样一条：全仓只有它的 `finalize()` 里有。

## 十、解析期垫片与解析环境里的两个额外开关

工具解析引擎头时，`build.py` 会在模块自己的编译环境之外再喂两样东西：额外 include 根
（`CODEGEN_EXTRA_INCLUDES`）与额外宏（`CODEGEN_EXTRA_DEFINES`）。**两者都只作用于解析**，
不进入引擎的编译，也不改变任何生成物的文本。

### 垫片 `Tools/CodeGen/clang_shim/cpuid.h`

| 项 | 内容 |
|---|---|
| 替代的头 | `cpuid.h` —— 它是 **clang 自带的资源头**（跟着 LLVM 装在 `lib/clang/<版本>/include` 里） |
| 为什么仓库里需要它 | 本仓库 vendored 的那份 libclang（`CALIB_CLANG_DIR` 指向的目录）只带了 `bin/` + `lib/` + `include/clang-c`，**没有资源目录**；而 Windows SDK 的 `DirectXMath.h` 在 clang 身份下会 `#include <cpuid.h>`。缺了它，解析 D3D12 模块自己的头会以 `fatal error: 'cpuid.h' file not found` 失败（退出码 6） |
| 边界 | **只让 include 过得去**：里面只有"把名字补上"的最小定义，**不参与任何布局计算** —— 生成字段表里的偏移仍然由被解析的真实类型决定。引擎本体只用 MSVC 编译，永远看不到这个头 |
| 不许做什么 | **不许定义编译器自带的函数**：`__cpuid` 在 MSVC 目标上是 clang 的内建，定义它会报 `definition of builtin function '__cpuid'`。需要 GCC 形状的名字时用**宏**，且参数个数按**真实调用点**给 —— `DirectXMathMisc.inl` 在 clang 身份下调用的是 6 个参数的 `__cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3])` |

### `-D_CRT_USE_BUILTIN_OFFSETOF`

UCRT 的 `stddef.h` 在 `_MSC_VER` 下把 `offsetof` 展开成空指针解引用形式
（`#if defined _MSC_VER && !defined _CRT_USE_BUILTIN_OFFSETOF`），而 clang 在 MSVC 目标上**也定义 `_MSC_VER`**。
MSVC 自己会折叠那个模式，clang 不会 —— 于是生成物里的 `static constexpr CodeGenFieldInfo arr[…]` 会报
`constexpr variable … must be initialized by a constant expression`。定义这个宏之后 `offsetof` 展开成
`__builtin_offsetof`（clang 自带的 `stddef.h` 走的正是这一支），解析即干净。实测：不加它 `exit=6`
（`constexpr variable 'offA' …` 与 `constexpr variable 'oneWithOffset' …` 两条），加了 `exit=0`。

### 红线：不许用"把门调松"的开关换通过

解析环境里 MUST NOT 出现 `-ferror-limit`（抬高错误上限）、`-w` / `-Wno-error`（把错误降级）这类开关。
"链上干净"的判据**不是**工具退出码为 0：include 链里的错误单独不触发拒收，而且在门没被触发时一条都不打印。
可复核的做法是自己给工具传 `-ferror-limit=1`（链上任何一条错误都会升级成致命诊断，原文就会被打印出来），
要求**一条错误级诊断都没有**；把错误数从 20 压到 19 也能让那句致命消失，但链上还是脏的。

## 十一、哈希键的来源盘点：`check_hash_sources.py`

### 它做什么 / 不做什么

**做**：扫出引擎里"当哈希键用"的类型，逐个打印它的**算法来源**；有来源不明的就非零退出并逐个点名。
这是 `require-explicit-hash-source` 那次改动的配套检查 —— 那次把运行时里"平凡可拷贝就悄悄按整块字节"
这条隐式兜底删掉了，从那以后每个会被求哈希的类型都必须有一个**显式**来源：

| 来源 | 是什么 |
|---|---|
| 生成物 | 类型名在 `build.py` 的接入清单 `CODEGEN_ANNOTATED` 里（构建期由 CodeGen 出字段级特化） |
| 手写声明 | 源码里看得见 `custom_hash_trait<该类型>` 或 `CA_HASH_BY_BYTES(该类型)` |
| 内置档 | 标量 / 枚举、指针与句柄、容器、包装类型，或该名字在某个模块的 `*Includes.h` 里出现过（第三方类型在那里声明） |
| 无 | **来源不明** —— 这正是要抓的 |

**不做**（边界，必须一起看）：

- 它是**按写法扫**的：只认 `HashObj<X>` / `cacore::hash<X>` / `cacore::hash_256<X>` / `unordered_map<X,` /
  `unordered_set<X,` / `shared_dic<X,` / `HashDictionary<X,` 这几种拼写。**扫不到**：容器元素位置上的类型
  （`castl::vector<X>`）、手写特化体内部 `hasher.hash(obj.member)` 里的成员类型、别名与宏包一层的写法、
  以及"写了但拼错"的名字。
- 它**不编译**，所以也判不出"这个类型到底会不会被实例化"。真正的保证来自编译器：漏了来源的类型会在
  **用到它的那个编译单元**里当场静态断言失败（错误信息里点名类型与三条出路）。
- 注释里的写法不算数（行尾 `//` 之后整段忽略）：**例外清单**要能被"把某一行注释掉"验证成红色。

### 参数

| 参数 | 作用 |
|---|---|
| 无 | 只打印汇总与来源不明的那些（CI 用的就是这一档） |
| `--list-all` | 把每个键类型与它的来源都列出来（排查用） |

### 退出码

| 码 | 含义 |
|---|---|
| 0 | 每个键类型都有来源（例外清单里的条目会**逐条打印**出来） |
| 1 | 有来源不明的键类型：逐个打印类型名与它出现在哪个文件 |
| 2 | 判据自身失效：找不到 `build.py` 里的接入清单，或一个源码文件都扫不到 |

### 例外清单 `hash_source_exceptions.txt`

一行一个 `类型名  # 理由`。**往这里加一行等于当众宣布"这个类型今天仍然没有来源"**，所以每条都要写清
为什么、谁来收尾；每次运行都会把它逐条打印出来。当前只有一条（`PipelineStateDesc`：它唯一的用户
`PipelineStatesDic` 是死代码，补它要连带三个没有特化、也没有清单行的类型）。

### 直接复制运行的示例（本机实跑，输出逐字照抄）

```
> python Tools/CodeGen/check_hash_sources.py
扫描 270 个文件、命中 0 个来源不明的键类型
例外 1 个（hash_source_exceptions.txt，逐条列出）：
  PipelineStateDesc：例外：唯一的用户 PipelineStatesDic 是死代码；补它要连带 ShaderStateDescriptor / CRenderPassInfo / D3DVertexBindingSlot，另立改动（出现在 D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.h）
清单 74 个类型；键类型共 35 个
> echo %ERRORLEVEL%
0
```

`--list-all` 的表长这样（前几行）：

```
> python Tools/CodeGen/check_hash_sources.py --list-all
扫描 270 个文件、命中 0 个来源不明的键类型
  手写声明     BufferHandle                                 6 处
  生成物      CPipelineStateObject                         4 处
  手写声明     ComPtr                                       1 处
  内置档      ResourceHandleKey                            1 处
```

把某个声明注释掉（例如 `Interface/RenderInterface/header/GPUTexture.h` 里那份
`custom_hash_trait<::graphics_backend::GPUTextureView>`）之后，它立刻变红 —— 这条自检是
**本 change 的判据之一**，实测输出：

```
> python Tools/CodeGen/check_hash_sources.py
扫描 270 个文件、命中 1 个来源不明的键类型
  来源不明: GPUTextureView（出现在 D3D12RenderBackend/private/ResourceManagment/GPUResourceStates.h, VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.h）
例外 1 个（hash_source_exceptions.txt，逐条列出）：
  PipelineStateDesc：…
清单 74 个类型；键类型共 35 个
> echo %ERRORLEVEL%
1
```

## 十二、单节发射开关与旧符号守卫：为什么留着（**不许当旧账删**）

旧反射 / 序列化 / 哈希设施已经退场（`Reflection.h` / `Serialization.h` / `Hasher.h` 三个路径都不存在，
`CA_REFLECTION` / `CATypeDescriptor` 在源码树里 0 处）。收拾那段过渡期时，下面两样**看着像旧账、其实不是**，
它们各有职责 —— 删掉会让某条判据当场失效：

**① 单节发射开关**：`--emit-reflection` / `--emit-serialization` / `--emit-hash` 与 `-o <文件>`。

- 它承载的判据：`openspec/specs/codegen-framework/spec.md` 里那条「一个类型一次调用出一份产物（三节）」
  要求三节由**同一段发射代码**产出，判据就是"`-o` 的单节输出与改造前那三份同名产物逐字节相同
  （76 个类型 × 3 节 = 228 次）"。
- 删了会怎样：这条"合并没有改内容"的判据没有执行装置了 —— 剩下只能比对合并后那一份文件的整体字节，
  分不清"哪一节变了"。
- 现状：`build.py` 走的是 `--emit-all --out-dir`（一次解析出一份 `<类型名>.codegen.h`），单节开关只在
  复核与文档示例里用；第二节与第六节里的示例就是它。

**② 旧符号守卫**：`Tools/CodeGen/check_legacy_symbols.py`。

- 它做什么：对**生成物**（构建目录 `out/build/<预设>/codegen/`）与**引擎源码**逐符号扫描
  `cacore::serializer` / `cacore::deserializer` / `batch_deserializer` / `aggregateHasher` /
  `custom_hash_trait` / `has_custom_hash_func` / `careflection::containerStates` /
  `managed_wrapper_traits` / `CATypeDescriptor` / `CA_REFLECTION`，命中必须 **0 行**；另有一套旧实现指纹。
- 三个退出码：**0** = 每条判据都为 0（正常）；**1** = 有命中且没被白名单放行（红）；**2** = 用法错或
  找不到仓库根（判据自身失效，不当成通过）。
- 删了会怎样：旧设施"回流"没有任何东西拦着 —— 谁再把 `custom_hash_trait` 或 `CA_REFLECTION` 写回源码树，
  构建照样绿。

**自查**：这两样都不是"迁移期的临时装置"（那种是 `build.py` 里按后缀清旧产物的那段，它已经在
`retire-transitional-leftovers` 里删掉了，因为源码树与构建目录里都没有旧命名产物可清）。判断标准是
**"删掉它，有没有一条判据会失去执行装置"** —— 有就留着，没有才是旧账。

## 十三、可绑定面：C++ 接口形状 → C ABI 的投影规则

**它做什么**：把"哪些接口方法能给 C# 用、用什么形状过界"写成一张**逐方法可核**的表，并配一个对账脚本。
**它不做什么**：不生成任何绑定代码（绑定生成器是后续 change 的事），也**不改任何接口签名** —— 投影由生成器承担，
C++ 接口保持易用（`castl::vector` / `castl::string` / `castl::shared_ptr` / `castl::function` 照写）。
把复杂结构翻成 blittable 是生成代码的活，不是接口作者的活。

### 边界上只允许四类形状

blittable 标量与递归纯标量记录、`T const* + 数量`（视图）、不透明句柄（定宽整数 + 代际号）、
“真实签名的原生函数指针 + 不透明 `void* user`”回调形状（注册号与注销伴生留在内部层）。
**容器类型与模板类型永不出现在 C ABI 面**：`castl::vector` / `map` / `unordered_map` / `set` / `string` 只在 C++ 接口面存在。

### 投影矩阵

| C++ 接口里写的 | 投影成 | 拷贝 | 谁分配 |
|---|---|---|---|
| 定宽整数 / `bool` / `enum class` / flags | 同名定宽类型（`bool` → `int32`） | 无 | — |
| blittable 结构体（值或 `const&`） | `<T>C const*`（布局镜像 + 清单自检） | 无 | 调用方 |
| `castl::string const&` / 返回 `castl::string` | `char const* + uint64 len`（UTF-8，不要求 NUL） | 有 | 写入方 |
| `castl::string_view const&`（入参） | `char const* + uint64 len`，thunk 里造临时 `string_view` 再传给实现 | 有 | 调用方 |
| `castl::string_view`（返回） | 两段式 `get_len()` + `copy_to(dst, cap)` | 有 | 调用方给缓冲区 |
| `castl::vector<T> const&`，T blittable | `TC const* + count` | 一次 | 调用方 |
| `castl::vector<T>&`（输出） | `TC* dst + cap` → 返回所需数（两段式） | 一次 | 调用方 |
| `castl::vector<T>`，T 非 blittable | `uint8_t const* + len`（序列化块） | 一次 | 调用方 |
| `map` / `unordered_map` / `set` | 摊平成 `(K*, V*, count)` 或序列化块 | 一次 | 调用方 |
| `castl::shared_ptr<T>` / `std::unique_ptr<T>`（返回） | `uint64` 句柄（句柄表持智能指针）+ 配对 `release`（独占语义释放即作废） | 无 | 生成器 |
| `castl::shared_ptr<T>`（参数） | `uint64` 句柄 → 反查 | 无 | 生成器 |
| `castl::weak_ptr<T>` | `uint64` **弱句柄**（句柄表存弱引用，不抬高 `use_count`；对象析构后旧句柄按 1001 判失效） | 无 | 生成器（只观察） |
| `T const&` / `T&` 出参 | `TC const*` / `TC*` | 视 T | — |
| `castl::function<Ret(Args...)>` 回调 | 真实签名的函数指针 + `void* user`、内部注册号、注销伴生；回调期间借用的接口对象 | 无 | 生成器（注册表 + 托管 GCHandle） |
| 会抛异常的方法 | 生成器 try/catch → 错误码 + 文本 | — | — |
| 公开数据记录（平坦 / 嵌套 / `bool` / 窄枚举 / 记录级 `EnumFlags`） | `<T>C` 按值返回、`<T>C const*` 入参；字段按真实 `sizeof` / `offsetof` 镜像，附标准布局、默认可构造与 CLR 布局自检 | 无 | 调用方（入参）/ 生成器（返回临时量） |
| 方法级普通枚举 | 仍按 `int32_t` 投影；公开 C# 用生成的枚举类型 | 无 | — |
| 方法级 `uenum::EnumFlags<T>` | 按 `MaskType` 的定宽整数（如 `uint16_t`）；公开 C# 用带 `[Flags]` 的 `T` | 无 | — |
| 独立 `cacore::NameHash` / `PathHash const&` | `char const* + uint64 len`；原生侧用现有构造函数重建（UTF-8），名称哈希与路径规范化不在生成器里另算 | 有 | 调用方 |
| 独立 `cacore::HashObj<T> const&` | 按 `T` 的记录镜像（`TC const*`），原生侧 `HashObj<T>{value}` 重算；`T` 必须有消费翻译单元可见的显式哈希来源 | 有 | 调用方 |
| `castl::vector<uint8_t> const&` | `uint8_t const* + count`，公开 C# `byte[]`（`fixed` 覆盖整个调用）；空数组合法、空指针配非零长度按 1002 拒绝且不进入实现 | 一次 | 调用方 |
| `castl::shared_ptr<T>` 入参 | `uint64` 句柄 → 按目标类型反查并重建 `shared_ptr<T>`；强返回句柄复用强引用、弱返回句柄调用期间保活，借用句柄与错误类型按 1001 拒绝且实现调用次数为 0 | 无 | 生成器 |
| 强 / 弱 / 借用返回（含跨头文件目标） | `uint64` 句柄；表项同时记录发行者与目标对象的完整限定名。公开 C# 用**目标接口**的 `ResolveTargetHandle` / `ReleaseTargetHandle`（追加在函数表末尾，ABI v2 不变）；旧发行者入口按发行者角色保持不变 | 无 | 生成器 |

统一规则：**元素里含 `string` / 容器 / 指针 ⇒ 一律降到序列化块**，并在生成期日志里逐条说明。

### 四类不可投影（必须点名，不许静默生成）

① 模板方法 / 泛型接口；② 所有权不明的裸指针；③ 依赖隐式转换的重载；④ **语义越界**（技术上绑得了，但脚本不该调：
模块初始化期的依赖注入、销毁整个后端之类）。前三类是"绑不了"，第四类是"不许绑"，都必须写进审计表并给出改法建议。

### 热路径：加法式快路重载

每帧多次调用、且参数是容器的方法，允许在接口上**另加**一个零拷贝重载（`...Span(T const* , count)`），
默认实现包装成容器调易用版本；**不许反过来删掉易用版本**。

### 对账脚本：`check_bindable_audit.py`

做什么：解析被审计接口头里的**纯虚方法**（按类体、剥注释、跳过纯虚析构），与审计表逐条对账。
审计表在 `openspec/changes/**/evidence/bindable-audit.md`（归档后仍在 `**/evidence/` 下，脚本用通配找）。

`--list` 打印逐条对账结果。

退出码：**0** = 对账通过；**1** = 有对不上的地方（缺审计行 / 僵尸行 / 不可绑却没有理由，逐条打印并点名）；**2** = 判据自身失效（找不到审计表或某个接口头）。

```powershell
python Tools/CodeGen/check_bindable_audit.py
```
```
扫描 12 个接口、对账 90 条方法、命中 0 处问题（审计表：openspec\changes\archive\2026-10-18-csharp-bindable-surface\evidence\bindable-audit.md）
[exit 0]
```

**已知限制（醒目）**：

- 审计范围现在是 12 个接口（`CRenderBackend` / `IReadbackToken` / `IWindow` / `IWindowSystem` / `IShaderCompilerManager` / `IOManager` / `IResource` / `IScriptTexture` / `IScriptRenderTarget` / `IScriptRenderGraph` / `IScriptRenderBackend` / `IScriptShaderStruct`）。其中后五个是脚本渲染适配面：`csharp-render-backend-triangle` 的 3 + 6 + 6 条方法，加上 `csharp-render-shader-constants` 的参数结构 3 条与两个接口各新增的方法（图 6 → 8、后端 6 → 7），再加上 `csharp-render-texture-sampling` 的纹理接口 2 条与三个接口各新增的方法（图 8 → 9、后端 7 → 8、参数结构 4 → 6）；`CRenderBackend` 的两个元数据查询也各登记一行（不可绑 + 理由），所以它是 10 → 12 条。汇总数字必须与头文件实算一致、重复行会让脚本非零。
  `GPUGraph` / `IMGUIContext` / `IOBatch` / `WBatch` / `IShaderCompiler` **尚未审计** —— 每帧热路径恰恰在这几个上面，所以"需快路"清单目前是**空的**（不是规则失效）。
- 脚本按**纯虚方法名**对账，**不看签名**：某个方法改了参数类型、脚本不会红；签名一致性要靠绑定生成器落地时的逐方法 conformance 测试。
- 脚本的前提是"接口头是结构化 C++"：宏展开出来的方法、`#if` 条件编译出来的方法，脚本看不见，也不会报。
## 十四、绑定发射器（`--emit-binding`）

**做什么**：按头里的 `CA_BIND` 标注，把每个被标注接口投影成两份产物 —— `<接口>.Bindings.cpp`（thunk、版本化函数表、`GetApi(version, size, out)`、共用句柄表、错误通道、布局清单的编译期断言）与 `<接口>.Bindings.cs`（`delegate* unmanaged<…>` 声明、偏移常量、以及**给使用者用的托管表面** `<接口>` 类）。投影规则见 `openspec/specs/csharp-bindable-surface/spec.md` 与本文第十三节、下文的两张矩阵。

**不做什么**：不生成模板/泛型，也不生成"所有权不明裸指针"的绑定（这类必须用 `CA_BIND_SKIP(方法, "理由")` 排除，工具只打印、退出码 0）；`castl::function<void(...)>` 回调已实现（见下），带这种参数的方法不再被拒绝；不改任何接口签名；不负责把函数表送进托管侧（那是消费方的事）。

**回调（本 change 之后）**：`castl::function<void(Args...)>` 参数投影成**真实签名的原生函数指针 + 不透明 `void* user`**，安装入口返回**内部注册号**，另发一个通用注销伴生 `<接口>_UnregisterCallback(void* self, uint64_t registration)`。注册号、函数指针、用户令牌与注销伴生只留在 native / `internal` 层，公开 C# setter 仍是 `void SetX(Action<...>)`。托管注册表用 `GCHandle` 保活委托：替换先装新的再注销旧的，置空与表面对象释放同时清原生注册与保活项；旧注册号注销只作用于仍匹配 current 的那一项，不会误删后来替换的新回调。回调参数里的已绑定接口裸指针还原成**回调期间借用的表面对象**，返回即撤销借用（再调用抛 1001）；非注册线程的回调被拒绝（1005），回调执行期间替换/清空同一个注册被活动标记挡下（1004），用户委托异常在托管桥内捕获（1003）并在下一次主线程调用原样上报。

**弱句柄、释放与解析（第十四节规则，`csharp-window-creation` 之后）**：`castl::weak_ptr<T>` 返回**不再被拒绝**，它投影成 `uint64` 弱句柄 —— 句柄表里存弱引用、**不抬高** `use_count()`，对象析构后 `lock()` 失败即按 1001 判失效。返回 `T*`（`T` 是另一个已 `CA_BIND` 的接口）投影成**借用句柄**（kind 4）：只存裸指针、不持有，存活见证者是发行它的那个对象。每个被绑定接口另外导出两个泛用入口：`<接口>_ReleaseHandle(void* self, uint64_t handle)` 释放、`<接口>_ResolveHandle(void* self, uint64_t handle)` 把句柄解回对象指针（失败返回空并置 1001）；两者都在函数表里（`ResolveHandle` / `ReleaseHandle` 两个槽），托管侧靠它们做 `Dispose`。**句柄表是进程级唯一一张**（不是每接口一张）：句柄 = 代际 + 序号，而每个接口的序号都从 1 开始，两张独立的表会发出数字完全相同的句柄 —— 把 A 表的句柄交给 B 表的释放入口，B 表会命中同号的活项并销毁一个无关对象。表项带接口标签，标签不符一律按 1001 拒。释放时表项会同时放掉强引用与弱引用并让代际进位：**只清表项、不放强引用**会让对象永不析构（`use_count() == 1` 永远不成立，窗口永不回收）。

### 跨头文件清单、共享值类型与双角色句柄（csharp-render-binding-foundations）

**构建期接口清单（任务 3.1）**：`CODEGEN_BINDINGS` 的第三列只检查"期望产物前缀是否存在"，**不是**接口清单。
`build.py` 先对每个生成输入跑

```powershell
CodeGen.exe <头文件> --list-bind-interfaces -ICACore/header -ITools/CodeGen/clang_shim -xc++
```

从主头实际的 `CA_BIND` 形状收集 `<短名>=<完整限定名>`，汇总去重后再以重复的
`--known-interface <短名>=<完整限定名>` 传给正式 `--emit-binding`。清单只用于类型投影与诊断，
不会让未在主头标注的接口额外生成产物。同短名不同完整限定名 = 清单冲突，构建失败并点名双方；
方法返回的裸指针目标不在已知清单里也按"未登记抽象接口"拒绝，不会留下引用不存在 C# 类型的产物。

**共享公开值类型（任务 2.3）**：公开记录与枚举按**限定类型**各发一份 `<类型短名>.Bindings.cs`，
接口产物里只留 `<接口>Bindings` 的 C 镜像与 `FromC` / `ToC`。文件头写 `// 限定名：<完整限定名>`；
同一短名对应不同限定名、或同名文件内容不一致时，`build.py` 用每个文件的内容哈希对账并让构建失败。
`Scripting.Managed.csproj` 的 `*.Bindings.cs` 通配符会把这些文件一起编进程序集。

**发行者与目标两类句柄入口（任务 3.2）**：旧的 `<接口>_ResolveHandle` / `<接口>_ReleaseHandle` /
`<接口>_Release<方法名>` 保持**发行者角色**（继续服务原生调用、非接口数据对象与既有窗口断言）；
每个接口在函数表**末尾追加** `<接口>_ResolveTargetHandle` / `<接口>_ReleaseTargetHandle`，按表项里的
**目标对象完整限定名**校验，供公开 C# 的资源表面解析和释放。目标解析借用句柄时要求发行者见证存在
且仍存活；目标释放即使目标已死亡或见证已过期也必须能清表项并进位。ABI 版本仍是 2，旧槽不重排。
表项现在记录 `issuerType` / `targetType` / `witnessHandle`，见证按"发行者在表里的那个活表项（序号+代际）"
判，而不是按对象弱引用（别的持有者会让弱引用一直不过期）。

**本 change 的明确限制**：不支持 `std::span`；不支持包装值（`NameHash` / `PathHash` / `HashObj<T>`）
作为返回值、嵌入记录或容器元素；不支持 `weak_ptr<T>` 入参（弱返回句柄仍然支持）；字节入参只承诺
同步一次复制，原生实现若异步保存输入指针，需要后续专门的回读 / 生命周期 change；不支持容器出参、
映射容器、联合体、定长数组与通用序列化块；非接口数据对象的智能指针返回仍只提供原生发行者入口，
不生成伪造的公开 C# 资源对象。



### 两张矩阵

绑定要过两道投影，各有一张矩阵。（本文第十三节那张是**第一张**：C++ 接口形状 → C ABI。）

**第二张：C++ 声明 → 托管表面**（六行，`csharp-window-creation` 任务 5.7 的口径；每行都有一个
conformance 夹具，判据在 `Tools/CodeGen/check_bindings.py` 的 `check_surface_matrix()` 里，
断的是**生成出来的整条签名**）：

| C++ 接口里写的 | 托管表面（`public sealed class <接口>`）上长什么样 | 夹具 |
|---|---|---|
| `bool` 返回 | `bool`（C ABI 是 `int32_t`，表面还原回来） | `IBindRow16::GetFlag` |
| `T&` 出参 | 元组返回值，例如 `(int A, float B) GetPair()` | `IBindRow17::GetPair` |
| `castl::string const&` 入参 | `string`（C ABI 是 `char const* + uint64_t` 两段） | `IBindRow18::SetTitle` |
| `castl::string_view` 返回 | `string`（两段式伴生拼回来） | `IBindRow19::GetLabel` |
| `void*` | `IntPtr`（C ABI 是不透明 `uint64_t` 令牌） | `IBindRow20::GetToken` |
| `castl::weak_ptr<T>` 返回 | 同名接口引用（`T`），句柄已失效即抛 | `IBindRow21::GetFlagOwner` |

`void` / `float` / `int` 这类"本来就同名"的形状不单列行，由上面第 ②③ 行的夹具覆盖（`void` 返回、
`int&` / `float&` 出参）。**错误码不是矩阵行**：它不进表面（表面方法没有返回值可判，失败就是抛
`<接口>Exception`，异常带的 `Code` 见下面那张码表）。

**表面里不许出现的东西**（判据是 `check_surface()`，按**类作用域**数，不按整文件 grep）：

- `delegate*`（函数指针）、`void*`、`ulong`（句柄）—— 这些**只许**待在 `internal` 的
  `<接口>Bindings` / `<接口>Lease` / `<接口>Calls` 三层里；
- `public` 类型里也不许出现它们：`<接口>Handle` 结构体因此是 `internal` 的。

**错误码（唯一来源是生成器里的 `kBindErrorCodes`，生成物只发具名常量）**：

| 码 | 生成物里的常量 | 谁会写它 |
|---|---|---|
| 1001 | `<接口>_ERROR_STALE_HANDLE` | `ResolveHandle` / `ReleaseHandle`（句柄失效、或不属于本接口）、弱句柄背后对象已析构、借用句柄的见证者已销毁、回调借用对象在回调返回后被使用 |
| 1002 | `<接口>_ERROR_NULL_POINTER` | 登记强/弱/借用句柄时实现返回了空指针 |
| 1003 | `<接口>_ERROR_CALLBACK_EXCEPTION` | 托管桥捕获用户委托异常、原生回调桥抛 C++ 异常；下一次 `UpdateSystem` 或回调注册调用原样上报 |
| 1004 | `<接口>_ERROR_CALLBACK_REENTRANT` | 回调执行期间替换 / 清空 / 注销**同一个**回调注册（活动标记挡下，不销毁正在执行的回调） |
| 1005 | `<接口>_ERROR_CALLBACK_WRONG_THREAD` | 原生回调来自非注册线程：桥拒绝进入托管委托，只记录错误 |
| 1006 | `<接口>_ERROR_INVALID_MONITOR` | `GetMonitor` 的编号为负或不小于 `GetMonitorCount()`：绑定层在调用原生实现**之前**拦下，不让它进 clamp |

生成物里**不许出现裸字面量**；`check_bindings.py` 的 `check_error_codes()` 把产物里所有
`<接口>_ERROR_*` 常量与所有 `_setError` 的码逐个对这张表，对不上即红。新增错误标识时，
`Tools/CodeGen/CodeGenMain.cpp` 的 `kBindErrorCodes`、`check_bindings.py` 的 `ERROR_CODE_TABLE`
与这张表**三处一起改** —— 只改一侧会被 `check_error_codes()` 判红。

**退出码**：`0` = 全部生成（含"所有方法都被带理由排除"这种空产物的情况）；`1` = 有拒绝（① 模板类、③ 依赖隐式转换的重载、②/④ 的排除没写理由、缺审计结论、审计表本身不绿）；`2` = 参数问题（缺 `--out-dir`、句柄位宽非法）；`3` = libclang 解析失败或解析出错误诊断。

**示例一：一个被带理由排除的接口（退出码 0，不出产物）**

```powershell
CodeGen.exe Tools/CodeGen/BindableShapes.Reject.h --emit-binding --out-dir .review_tmp --audit-script Tools/CodeGen/check_bindable_audit.py -ICACore/header -ITools/CodeGen/clang_shim -xc++
```

真实输出（节选，顺序与实际一致）：

```text
CodeGen: 跳过审计核对 IBindRow11Reject —— 它不在审计表覆盖的接口清单里
CodeGen: 排除 IBindRow11Reject::PeekRawThing —— "② 所有权不明裸指针：返回裸指针且没有所有权说明（不投影）"
绑定：接口 1 个 / 方法 0 条 / 产物 0 个 / 拒绝 0 条
```

**示例二：隐式转换重载（退出码 1，什么都不写）**

```powershell
CodeGen.exe Tools/CodeGen/BindableShapes.RejectOverload.h --emit-binding --out-dir .review_tmp --audit-script Tools/CodeGen/check_bindable_audit.py -ICACore/header -ITools/CodeGen/clang_shim -xc++
```

真实输出（节选）：

```text
CodeGen: 拒绝 IBindRowOverloadReject::Set 与 Set —— ③ 依赖隐式转换的重载：两者投影成同一个 C ABI 签名
```

**三条实测前提**（不满足会得到看着莫名其妙的错）：

1. **必须传 `-xc++`** —— `.h` 不传会被 libclang 当 C 解析，直接 `libclang parse failed (error=4)`；
2. **`-I` 要覆盖这个头依赖的全部目录**：`CACore/header` 只够最简的夹具，真实接口（`IWindow`）还要它模块自己的头目录与第三方目录。`build.py` 不手写这些 —— 它从编译数据库里借一个已经能编这个头的编译单元；
3. **`--out-dir` 必须已存在**（工具不建目录），且产物会**覆盖**同名文件。

### 返回值投影的五条规则（tunk 里怎么调实现）

这五条是"生成的 thunk 到底有没有真调实现"的分界线，写错一条就会得到"签名对、调用恒返回零"的桩：

1. **`bool` 走 `? 1 : 0`** —— `bool` 投影成 `int32_t`，不能直接 `return true/false`，也不能靠"类型文本相等"判断能否直出（`bool` 与 `int32_t` 永不相等，旧判据把 8 个 `bool` 方法全判成了桩）。thunk 生成 `return object->M() ? 1 : 0;`。
2. **枚举走 `static_cast<int32_t>`** —— `enum class` 不能隐式转 `int32_t`；**参数方向**也要转回去（`static_cast<ca_bind_fixture::EBindMode>(mode)`），而且 libclang 给的是**非限定**拼写，生成物在全局作用域，必须按接口自己的命名空间补前缀。
3. **`void*` 投影成 `uint64_t` 不透明令牌** —— thunk 里 `reinterpret_cast<uint64_t>(object->M())`。只认 `void*` 这一种拼写：其它 `T*` 返回必须由作者写 `CA_BIND_SKIP` 排除，不许被静默绑成裸地址。
4. **字符串入参从 `char const* + len` 造临时量** —— 实现要 `castl::string` 就 `castl::string(ptr, len)`，要 `string_view` 就 `castl::string_view(ptr, len)`，再把临时量传给实现。
5. **两段式只发两个伴生** —— `_Len` 返回 `object->M().size()`，`_Copy` 自己再取一次实现的值、按 `min(cap - 1, size)` 截断并补零；**容量 0 时一个字节都不写**（无符号下溢会写满整段）。这类方法**不进函数表、不发 `extern "C"` 直出入口、不发直出 thunk** —— 三个都不发才自洽。

**夹具覆盖现状（三个形状仍留桩）**（2026-11-01 更新：回调也修好了；桩数仍是 3，因为下面三个是另一种形状）：

| 形状 | 现在什么状态 | 为什么 |
|---|---|---|
| 容器**入参**（`IBindRow05::SetThings`） | **已实现**：thunk 按 `(T const* + count)` 逐元素搭回 `castl::vector<T>` 再调实现 | — |
| 镜像结构体**按值返回**（`IBindRow02::GetThing`） | **已实现**：thunk 按镜像的字段名逐成员从 `<T>C` 搭出 `T`、调实现、再逐成员搬回 | — |
| 容器**出参**（`IBindRow06::GetThings`） | 仍留桩 | C ABI 是 `dst + cap` 两段式，"容量不够怎么办"没有定协议（截断＝静默降级、抛错＝C ABI 没有异常通道）；定协议要连错误码一起设计 |
| 元素含 `castl::string` 的容器（`IBindRow07::SetNested`） | 仍留桩 | C ABI 是序列化块 `(uint8_t const* + len)`，要先有容器序列化格式 |
| `unordered_map`（`IBindRow08::SetMapping`） | 仍留桩 | 摊平成 `(K*, V*, count)`，但遍历顺序不保证、重复键的覆盖语义也没定 |
| 回调（`IBindRow13::SetCallback`） | **已实现**：函数指针 + `void* user` + 内部注册号 + 注销伴生；夹具覆盖注册、参数顺序、替换、旧注册号注销、空指针、重入与异常上报 | — |

`check_bindings.py` 对**夹具**只打印桩数、不判定，对**被绑定到托管侧的真实接口**桩数必须为 0。`build.py` 每次构建会把那一行夹具桩数打进日志（现在是 `桩数 3 个`），别让它悄悄涨。

### 登记表发射（`--emit-registry`，render-interface-merge）

**做什么**：把"编号 → 接口"从各接口头的 `CA_BIND(编号)` 标注收集成分片，再在**不解析 C++** 的合并步里产出三份跨接口产物：

| 产物 | 内容 |
|---|---|
| `BindingRegistry.Bindings.h` | `kBindingInterfaceId<接口>` 编号常量、`EngineApi` struct 与 14 字段的 `static_assert(sizeof(EngineApi) == 80u`、宿主固定名钩子声明 |
| `BindingRegistry.Bindings.cpp` | `get_interface_api` 路由（编号 → `<接口>_GetApi`、默认实例 / 目标类型表角色）、布局查询路由、`FillEngineApi` 装配（按固定名字调宿主工厂/错误通道钩子） |
| `EngineApi.Bindings.cs` | 与 C++ struct 同一次生成的托管逐字段镜像 `EngineApiMirror` + `EngineApiBindings`（`AbiVersion` / `Size` / `TableVersion` / 逐接口编号常量 / 绑定清单 `Tables`） |

**编号规则**：进登记表的接口必须写 `CA_BIND(编号)`；编号全局唯一、只在现有最大编号之后追加，主登记表必须恰为 `1..N` 连续（夹具登记表按同规则连续）。缺号 / 同号 / 跳号 ⇒ 分片步或合并步非零退出并点名；拒绝的那一轮**不写任何登记产物**（先校验后写）。

**确定性**：**同一输入重跑**，三份产物逐字节相同。不同调用方式把不同的输入头路径文本写进"登记输入"注释时产物会差这一行注释 —— 那是输入不同，不是非确定性。

**构建期用法**：`build.py` 对 `CODEGEN_REGISTRIES` 的每个登记输入头跑一次 `--emit-binding ... --registry-part <标签> --registry-part-dir <目录>`（分片），再用 `--emit-registry --out-dir <目录>` 加重复的 `--registry-part-file <分片>` 汇总。主登记表产物落 `codegen/bindings/`，夹具登记表落 `codegen/bindings/fixture/`（不进宿主编译面）。

**退出码**：0 = 成功；1 = 编号问题（缺号 / 重号 / 跳号）或分片缺失/读不到（都在写产物之前拒绝）；2 = 参数问题（缺 `--out-dir`）或写文件失败。

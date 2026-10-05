## RENAMED Requirements

- FROM: `### Requirement: 统一 CodeGen 流水线从 annotated 头产出可读生成代码`
- TO: `### Requirement: 生成物不再调用旧运行时（原「统一 CodeGen 流水线从 annotated 头产出可读生成代码」的能力边界收窄）`
- FROM: `### Requirement: 会静默出错的容器组合必须被拒绝`
- TO: `### Requirement: 拒绝清单收窄（原「会静默出错的容器组合必须被拒绝」）`
- FROM: `### Requirement: 反射标注用标记宏自动爬取 + 未迁移/外部走非侵入通道`
- TO: `### Requirement: 反射标注通道支持嵌套类型（原「反射标注用标记宏自动爬取」）`

> 这三条改名与下面的 MODIFIED 配对。归档打包（`openspec archive`）按**标题精确匹配**，顺序是
> RENAMED → REMOVED → MODIFIED → ADDED，而且"有改名时 MODIFIED 必须引用**新**标题"（`specs-apply.js` 会校验）。
> 所以旧标题只写在这一节里，下面三段的标题是新标题。（2026-10-05 归档时补的：原先只写了带"（原「…」）"
> 的 MODIFIED 标题，打包器找不到旧标题、整次归档被中止且没有改任何文件 —— 见 `tasks.md` 第 11 节的补记。）

## MODIFIED Requirements

### Requirement: 生成物不再调用旧运行时（原「统一 CodeGen 流水线从 annotated 头产出可读生成代码」的能力边界收窄）

生成的序列化与哈希代码 MUST 由 CodeGen 自己的运行时驱动：读写自带游标、容器判据与哈希钩子都来自新运行时，MUST NOT 引用 `cacore::serializer`、`cacore::deserializer`、`careflection::containerStates`、`cacore::custom_hash_trait`、`cacore::has_custom_hash_func`。

序列化入口 MUST 接收缓冲区（写）或缓冲区加显式读位置（读），使生成代码能在流中间调用另一个类型的生成代码。

#### Scenario: 生成物零引用旧符号

- **WHEN** 对仓库里所有生成物逐个搜上面五个符号
- **THEN** 零命中（带文件限定）；现状每个符号都被引用，故该判据在改动前为红

#### Scenario: 递归形状不再被拒绝

- **WHEN** 容器的元素是另一个已标注的结构体，且该结构体内部含无序容器
- **THEN** 出码成功、往返一致、字节与插入顺序无关；`3.2` 里那条"元素是内含无序容器的结构体必须拒绝（退出码 12）"的场景**作废**

### Requirement: 拒绝清单收窄（原「会静默出错的容器组合必须被拒绝」）

下列拒绝 MUST 保留：参与排序的一侧没有全序（退出码 17）、元素是裸指针或托管指针（15）、重复键容器（14）、键或元素不可默认构造（16）、按位存储容器（13）。每条拒绝的非零退出 MUST 在 stderr 里**点名成员名、涉事的类型（元素类型 / 键类型 / 容器类型）与原因** —— 这句是从基线里那条被收窄的要求搬过来的，不许在收窄时丢掉。

**撤除**：元素是"内部含无序容器的结构体"（原退出码 12）—— 递归读写做完后这个形状由生成代码支持。

**作废基线场景**：基线 `openspec/specs/codegen-framework/spec.md` 里那条「元素是内含无序容器的结构体时必须拒绝 / 生成序列化与哈希的调用 MUST 非零退出」的 Scenario，**在本 change 生效后作废**；收尾时必须回到基线文件里把它标注为作废（不能只在 change 工件里说一句），否则第三方照基线读仍会认为必须拒绝。

**新增**：含非 public 成员的类型要么提供 friend 入口（照 `Tools/CodeGen/README.md` 的写法），要么保持拒绝（5）；含指针成员的类型保持拒绝，README 里写明替代做法。

#### Scenario: 拒绝集合逐条与 README 的退出码表一致

- **WHEN** 对每个拒绝形状跑一次探针，并把退出码与 `Tools/CodeGen/README.md` 的退出码表逐条对照
- **THEN** 逐条一致；退出码 12 从表里删除，且已有一条"同样的夹具现在出码成功"的读数

#### Scenario: 撤除 12 之后，原来那条夹具要改成能通过

- **WHEN** 用改前触发退出码 12 的那个夹具（`struct Inner { castl::unordered_map<unsigned,unsigned> m; }; struct Outer { castl::vector<Inner> items; };` 这一形状）在改动后再跑一次
- **THEN** 出码成功、往返一致，且两个只有内层插入顺序不同的对象写出的字节逐字节相同；"改前 exit 12 + stderr 点名"与"改后 exit 0 + 往返一致"两个读数都要留档

### Requirement: 反射标注通道支持嵌套类型（原「反射标注用标记宏自动爬取」）

标记 MUST 能标注命名空间作用域之外的嵌套类型（`DepthStencilStates::StencilStates`、`cahash::sha256_hash::result_type`）。类型名的解析口径（短名或全名）MUST 在 README 里写明并与命令行、接入清单一致。

#### Scenario: 嵌套类型三个发射器都成功

- **WHEN** 对上述两个嵌套类型请求三个发射器
- **THEN** 全部退出码 0；现状是 4（类型未标注），故判据在改动前为红

#### Scenario: 环境能解析 D3D12 自己的头

- **WHEN** 用推导环境解析 `D3D12RenderBackend/private/ShaderLibrary/ShaderLibrary.h`
- **THEN** 退出码 0；现状是 6（`directx/d3dx12.h` 找不到）

## ADDED Requirements

### Requirement: 容器读写不再要求容器有 `clear()`

无序容器仍然 MUST 在读之前清空并按规范顺序重建；但**定长数组**（没有 `clear()` 的类型，如 `castl::array`）MUST 按位置逐个覆写，并在读之前校验载荷长度与 extent 一致 —— MUST NOT 因为没有 `clear()` 就拒绝出码。

（本条是从基线里那条「读进非空目标时先清空（**已定死，不留选择**）」的 Scenario 收窄出来的：那条场景把"清空"写成了所有容器读路径的前提，而定长数组没有 `clear()`；收窄之后"先清空"只对无序容器成立，定长数组走按位置覆写。它是一条**新增**要求，不是改名 —— 基线里没有同名的 Requirement 可改。）

#### Scenario: 含 `castl::array` 的真实类型可出码

- **WHEN** 对含 `castl::array<T, N>` 成员的类型请求序列化并在真实目标里实例化读路径
- **THEN** 编译通过、往返一致；不再走"没有 `clear()` 就拒绝"这条路

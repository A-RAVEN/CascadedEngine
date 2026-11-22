# csharp-reverse-interface Specification

## Purpose

托管实现原生接口的反向投影能力：C ABI 面第五类"函数表 + user"形状、C++ 代理 / C# 抽象基类 / 注册 glue / releaseUser 三件产物、单一强所有者 + 借用句柄所有权、注册线程 + 失败哨兵、错误通道，以及检查管线的反向集成。

## Requirements

### Requirement: 反向接口必须生成代理、抽象基类与注册 glue 三件产物
对标注为反向的 C++ 接口，生成器 MUST 产出：① C++ 代理 `<I>Proxy : public <I>`（每**被绑定**纯虚方法体调对应函数指针槽）；② C# 公开抽象基类（去 I 前缀加 Base，IResource→ResourceBase、IResourceFactory→ResourceFactoryBase），每**被绑定**虚方法对应 abstract 托管方法；③ glue `<I>_CreateImpl(void* user, <每被绑定虚方法投影 fnptr>..., void (*releaseUser)(void*)) → 强句柄` + `<I>_ReleaseImpl`。C ABI 面 MUST 用**独立第五类**形状"函数表 + 不透明 user"（MUST NOT 表述成第 4 类扩展，第 4 类仍是单回调）；**一虚方法 MAY 投影成多条函数指针**（两段式 Len/Copy），对齐 MUST 按"每被绑定虚方法的投影槽集合"逐条对，MUST NOT 用"函数指针总数=虚方法数"。每条函数指针参数 MUST 仍在前四类内。生成物 MUST 落构建目录 codegen/bindings（经 CascadedBindingDir 编进程序集），MUST NOT 写回 Scripting/Managed/（csproj 明文不放生成绑定）。

#### Scenario: 三件产物按被绑定虚方法投影集合对齐
- **WHEN** 对含一个两段式方法 + 一个 CA_BIND_SKIP 方法的反向夹具跑生成器
- **THEN** 代理虚方法数/基类 abstract 数 MUST 与**被绑定**虚方法数逐条对齐（跳过 CA_BIND_SKIP 的）；两段式方法 MUST 投影两条函数指针都生成；缺任一产物即失败

#### Scenario: C# 实现经代理被 C++ 调到（行为判据）
- **WHEN** C# 继承基类覆写经 CreateImpl 注册，C++ 调代理虚方法
- **THEN** C# 覆写 MUST 被真实调用收投影参数，C++ MUST 读到 C# 返回值；代理改空体时行为判据 MUST 变红（MUST NOT 用 object-> 结构判据，反向代理无此名）

### Requirement: 反向产物必须单一强所有者且 C# 持借用句柄
代理强引用 MUST 只在注册表（工厂注册表/资源注册表，单一强所有者），句柄表对代理 MUST 是借用项（kind 4，MUST NOT 另持强引用造成双重所有）。glue releaseUser(void* user) 指向托管桥释放方法（GCHandle.FromIntPtr(user).Free()）；代理析构 MUST 调 releaseUser → 托管桥 Free（唯一 Free 在托管侧，C++ MUST NOT 直接调 GCHandle.Free）。C# 实例 GCHandle.Alloc 成 user；C# 句柄是借用，Dispose 释放借用（句柄失效报 1001）但注册表仍持。ReverseKeepAliveCount MUST 每实例一个 + 原生 getter（C++ 可读；MUST NOT 全局静态——其它夹具拖漂；MUST NOT 复用窗口 LiveKeepAliveCount）。releaseUser Free 后置空；重复释放按代际拒；C# 重复 Dispose 幂等。

#### Scenario: Dispose 只掉借用一份（两个观测钉死）
- **WHEN** 注册表持代理（ReverseKeepAliveCount=1），C# Dispose 释放借用句柄
- **THEN** 同一句柄解析 MUST 报 1001（借用已释放）且 按路径 MUST 仍取回同一实例（注册表仍持）且 ReverseKeepAliveCount MUST 不归零；纯泄漏（Dispose 什么都不做）则句柄不报 1001 → 红

#### Scenario: 注册表释放后计数归零
- **WHEN** 注册表释放代理（注销工厂/卸载资源）
- **THEN** 代理析构调 releaseUser → 托管桥 Free，ReverseKeepAliveCount MUST 1→0（原生 getter 精确读数）；重复 Dispose MUST 幂等不下溢不崩

### Requirement: 反向实现只能在注册线程被调且失败必须可观测
桥检查注册线程不符 MUST 按 1005 落槽 + 非 void 槽返失败哨兵（Serialize len UINT64_MAX、Create nullptr）。代理收哨兵 MUST 中止。工厂 Create() 失败 MUST 抛异常（C++ 原生工厂直接抛、C# 工厂异常经桥转码；MUST NOT 用 nullptr+错误槽——C++ 原生路径无错误槽对象）。C# 覆写异常 MUST 桥内捕获转反向错误码不跨原生边界，错误表/ERROR_CODE_TABLE/README 同步含标识。

#### Scenario: 非注册线程调用被拒
- **WHEN** 注册线程注册，另一线程调代理 Serialize
- **THEN** MUST 1005 落槽 + len 哨兵 + 代理中止 + C# 覆写计数 0，下次注册线程调用上报 1005

#### Scenario: Create 失败抛异常不注册空（可观测）
- **WHEN** 工厂 Create() 抛异常（或 C# 返回 null 经桥转异常）
- **THEN** 系统 MUST 捕获/检查 nullptr，MUST NOT 包 shared_ptr 解引用、MUST NOT 注册空资源；HasResource(path) MUST ==false（或等价公开查询）；第二次 Load MUST 重新调 Create（计数 2，证明未缓存空）；拆分抛异常/返回 null 两场景

### Requirement: 反向产物必须与检查管线集成（替代判据非放松）
反向产物 MUST 有显式类别标记，检查脚本据此区分正向/反向。现有四检查 MUST 反向感知（对反向产物用替代判据，正向判据不动，改前/改后对照入档）：check_surface 反向产物改判 check_reverse_base（public abstract 基类干净 + abstract 数=被绑定虚方法数，正向仍要 IDisposable 表面类）；check_alignment 反向对齐 abstract 基类；check_stubs 反向用行为判据；check_shared_value_types 反向按 .cs/.cpp 成对接口类处理。check_bindings.py MUST 增 --only 单检查入口 + check_reverse_base 报扫描命中数≥1（避免空扫恒真）。check_bindable_audit.py INTERFACES MUST 增反向接口（现硬编码 9 真实接口）；审计表（全仓唯一 archive/2026-10-18-csharp-bindable-surface/evidence/）MUST 增"谁实现"列 + 反向行，MUST NOT 新建（新建 exit 2）。build.py 期望产物闸门 MUST 认反向两件套。

#### Scenario: 反向基类干净绿塞 void* 红（可独立跑）
- **WHEN** check_bindings.py --only check_reverse_base 扫干净反向基类，再扫塞 public void* 的
- **THEN** 干净态 MUST 绿（exit 0，命中数≥1）；塞 void* MUST 红点名；既有 check_surface/check_alignment/check_stubs/check_shared_value_types 对反向产物 MUST NOT 误红（反向感知）

#### Scenario: 反向接口漏审计行即红
- **WHEN** 反向接口（已加进 INTERFACES）加被绑定虚方法但不补归档审计表行
- **THEN** check_bindable_audit.py MUST 非零退出点名缺哪条

### Requirement: 反向投影必须有 conformance 夹具且行为可失败
投影矩阵 MUST 增反向行（函数表+user，名字标识），指到反向夹具接口（进 interface-ids.lock 夹具段，不进宿主路由）。conformance MUST 断言第五类签名 + 六条行为：C# 实现经代理被调收返回值/替换 supersede/释放失效/非注册线程拒/异常转码/double-free 守卫。结构断言（虚方法对齐/签名/句柄 kind）CoreTests，C#→C++ 行为 tester（hostfxr；CoreTests 不加载 C#）。每条缺陷态变红。

#### Scenario: 反向夹具六条行为断言
- **WHEN** 跑 conformance 反向行（结构 CoreTests、行为 tester）
- **THEN** 六条 MUST 全过；代理改空体/注销保留保活项/无守卫时对应断言 MUST 非零退出点名

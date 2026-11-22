# csharp-bindings 增量

## MODIFIED Requirements

### Requirement: 生成的 C ABI 面只允许审计表规定的形状
生成器 MUST 只按 `csharp-bindable-surface` 的投影矩阵出形状：blittable 标量与结构体、`T const* + 数量`、不透明句柄、`char const* + len`、**函数指针 + `void* user`**（回调那一行）、以及**反向接口的函数表（每被绑定虚方法投影出的函数指针，可多条）+ `void* user`**。`user` 只作不透明令牌，**原生生成代码（C++ thunk/代理）MUST NOT 解引用它**；托管桥经 `GCHandle.FromIntPtr(user).Target` 解回托管对象是既有语义（见 csharp-bindable-surface 回调生命周期条）。生成的 C ABI 签名里 MUST NOT 出现容器类型（`castl::vector` / `map` / `string` 等）、模板、`shared_ptr` / `unique_ptr` / `weak_ptr`、C++ 引用。

反向接口（C# 实现 C++）的产物 MUST 含 C++ 代理类（每虚方法调其投影函数指针槽，一虚方法可多槽）、C# 公开抽象基类（只含托管类型）、注册 glue（CreateImpl 收 user + 每虚方法投影函数指针 + releaseUser 释放回调返回强句柄、ReleaseImpl 释放）。公开抽象基类 MUST NOT 出现 `void*`/`delegate*`/原生句柄类型（原生细节藏 internal 桥），经**新增 check_reverse_base 检查**（递归 glob）判定——MUST NOT 复用 check_surface（它要求 public sealed IDisposable 表面类，反向抽象基类不满足）。GCHandle 强根随代理，Free 唯一发生在托管桥（代理析构调 releaseUser 函数指针 → 托管桥 GCHandle.Free；C++ MUST NOT 直接调 GCHandle.Free）。

#### Scenario: 签名里出现容器类型即算不合格
- **WHEN** 扫描生成的 `<接口>.Bindings.cpp` 里所有 `extern "C"` 声明
- **THEN** 容器类型名、模板尖括号、`shared_ptr`/`unique_ptr`/`weak_ptr`、C++ `&` 引用的命中数 MUST 全为 0（脚本逐类给命中数与命中行）

#### Scenario: 反向公开抽象基类无原生细节
- **WHEN** check_reverse_base（新增，递归 glob）扫反向接口生成的 C# 公开抽象基类签名
- **THEN** `void*`/`delegate*`/原生句柄类型命中数 MUST 为 0（干净态绿）；塞一个 public void* MUST 判红点名该基类（红绿两半构造得出，原生细节只允许在 internal 桥）

#### Scenario: 反向代理虚方法真实调到 C# 实现
- **WHEN** C# 继承抽象基类覆写方法经 CreateImpl 注册，C++ 调代理虚方法
- **THEN** 代理 MUST 经函数指针槽调到托管桥再到 C# 覆写（非空体），C++ 读到 C# 返回值；把代理改成空体时行为判据 MUST 变红
#pragma once
// 绑定发射器的 conformance 夹具：投影矩阵 14 行，一行一个夹具接口。
// 矩阵本身在 Tools/CodeGen/README.md 第十三节（14 行数据）。这里只放形状，不放实现 ——
// 生成器对每个接口产出 <接口>.Bindings.cpp / .cs，期望的 C ABI 签名逐字写在
// Tools/CodeGen/BindableShapes.expected.txt（每行前缀矩阵行号）。
//
// 注意：这份头只被 CodeGen 解析、以及被生成的 shim 编译，不参与引擎运行。

#include <stdint.h>
#include <CASTL/CAString.h>
#include <CASTL/CAStringView.h>
#include <CASTL/CAVector.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAFunctional.h>
#include <CodeGenMeta.h>

namespace ca_bind_fixture
{
	// —— 供多行共用的元素类型 ——
	struct BindScalarThing
	{
		uint32_t id;
		float weight;
	};

	// 元素里含字符串 ⇒ 非 blittable（矩阵第 7 行）
	struct BindNestedWithString
	{
		castl::string name;
		uint32_t value;
	};

	enum class EBindMode : uint8_t
	{
		None = 0,
		One = 1,
		Two = 2,
	};

	// 第 1 行：定宽整数 / bool / enum / flags
	CA_BIND()
	class IBindRow01
	{
	public:
		virtual int32_t GetInt32(int32_t value) = 0;
		virtual bool GetBool(bool value) = 0;
		virtual EBindMode GetMode(EBindMode mode) = 0;
		virtual ~IBindRow01() = default;
	};

	// 第 2 行：blittable 结构体（值或 const&）
	CA_BIND()
	class IBindRow02
	{
	public:
		virtual BindScalarThing GetThing(BindScalarThing const& value) = 0;
		virtual ~IBindRow02() = default;
	};

	// 第 3 行：castl::string const& / 返回 castl::string
	CA_BIND()
	class IBindRow03
	{
	public:
		virtual void SetName(castl::string const& name) = 0;
		virtual castl::string GetName() const = 0;
		virtual ~IBindRow03() = default;
	};

	// 第 4 行：castl::string_view（返回）⇒ 两段式（get_len + copy_to）
	CA_BIND()
	class IBindRow04
	{
	public:
		virtual castl::string_view GetNameView() const = 0;
		virtual ~IBindRow04() = default;
	};

	// 第 5 行：castl::vector<T> const&（T blittable）⇒ ptr + count
	CA_BIND()
	class IBindRow05
	{
	public:
		virtual void SetThings(castl::vector<BindScalarThing> const& things) = 0;
		virtual ~IBindRow05() = default;
	};

	// 第 6 行：castl::vector<T>&（输出）⇒ dst + cap 两段式
	CA_BIND()
	class IBindRow06
	{
	public:
		virtual void GetThings(castl::vector<BindScalarThing>& outThings) = 0;
		virtual ~IBindRow06() = default;
	};

	// 第 7 行：castl::vector<T>，T 非 blittable ⇒ 序列化块（uint8_t const* + len）
	CA_BIND()
	class IBindRow07
	{
	public:
		virtual void SetNested(castl::vector<BindNestedWithString> const& items) = 0;
		virtual ~IBindRow07() = default;
	};

	// 第 8 行：map / unordered_map / set ⇒ 摊平成 (K*, V*, count)
	// 本夹具用 unordered_map 代表；set 那一档同样摊平。
	CA_BIND()
	class IBindRow08
	{
	public:
		virtual void SetMapping(castl::unordered_map<uint32_t, uint32_t> const& mapping) = 0;
		virtual ~IBindRow08() = default;
	};

	// 第 9 行：智能指针返回 ⇒ 句柄 + 配对释放
	CA_BIND()
	class IBindRow09
	{
	public:
		virtual castl::shared_ptr<BindScalarThing> AcquireThing() = 0;
		virtual ~IBindRow09() = default;
	};

	// 第 10 行：智能指针参数 ⇒ 句柄反查
	CA_BIND()
	class IBindRow10
	{
	public:
		virtual void ConsumeThing(castl::shared_ptr<BindScalarThing> thing) = 0;
		virtual ~IBindRow10() = default;
	};

	// 第 11 行（castl::weak_ptr ⇒ 不投影）放在 BindableShapes.Reject.h 里：那份头要被拒绝，
	// 这份头要能整体生成成功，两者不能混在一个文件里。

	// 第 12 行：T const& / T& 出参 ⇒ 指针
	CA_BIND()
	class IBindRow12
	{
	public:
		virtual void GetPair(int32_t& outFirst, float& outSecond) const = 0;
		virtual ~IBindRow12() = default;
	};

	// 第 13 行：castl::function<Ret(Args...)> 回调 ⇒ 函数指针 + void* user + 内部注册号 + 注销伴生
	CA_BIND()
	class IBindRow13
	{
	public:
		virtual void SetCallback(castl::function<void(uint32_t, float)> callback) = 0;
		virtual ~IBindRow13() = default;
	};

	// 第 15 行（新增）：`castl::string_view const&` 入参 —— `IWindowSystem::NewWindow` 的名字参数就是这种形状
	CA_BIND()
	class IBindRow15
	{
	public:
		virtual void SetTitle(castl::string_view const& title) = 0;
		virtual ~IBindRow15() = default;
	};

	// 第 11 行：`castl::weak_ptr` 返回 ⇒ **弱句柄**（不抬高引用计数，对象析构后旧句柄报 1001）
	CA_BIND()
	class IBindRow11
	{
	public:
		virtual castl::weak_ptr<BindScalarThing> PeekThing() = 0;
		virtual ~IBindRow11() = default;
	};

	// 第 14 行：会抛异常的方法 ⇒ thunk 里 try/catch，返回"错误码 + 文本"
	CA_BIND()
	class IBindRow14
	{
	public:
		virtual void MayThrow(int32_t value) = 0;
		virtual ~IBindRow14() = default;
	};

	// ---- 第二张矩阵（C++ 声明 → 托管表面）的六行 conformance 夹具，任务 5.7 ----
	// 前五行与「C++ → C ABI」那张表共用接口，但**判据在生成出来的 `.cs` 上**：每行断言一条
	// 具体的表面签名（`public bool GetFlag()` 之类）。第六行额外要求被指向的接口本身也可绑。

	// 行 ①：`bool` 返回 → 托管侧还是 `bool`（C ABI 是 `int32_t`，表面要还原回来）
	CA_BIND()
	class IBindRow16
	{
	public:
		virtual bool GetFlag() = 0;
		virtual ~IBindRow16() = default;
	};

	// 行 ②：`T&` 出参 → 元组返回值；顺带覆盖 `void` 返回与 `int`/`float` 这两个"本来就同名"的形状
	CA_BIND()
	class IBindRow17
	{
	public:
		virtual void GetPair(int& outA, float& outB) = 0;
		virtual ~IBindRow17() = default;
	};

	// 行 ③：`castl::string const&` 入参 → `string`（C ABI 是 `char const* + uint64_t` 两段）
	CA_BIND()
	class IBindRow18
	{
	public:
		virtual void SetTitle(castl::string const& title) = 0;
		virtual ~IBindRow18() = default;
	};

	// 行 ④：`castl::string_view` 返回 → `string`（两段式伴生拼回来）
	CA_BIND()
	class IBindRow19
	{
	public:
		virtual castl::string_view GetLabel() const = 0;
		virtual ~IBindRow19() = default;
	};

	// 行 ⑤：`void*` → `IntPtr`（C ABI 是不透明 `uint64_t` 令牌）
	CA_BIND()
	class IBindRow20
	{
	public:
		virtual void* GetToken() = 0;
		virtual ~IBindRow20() = default;
	};

	// 行 ⑥：`castl::weak_ptr<T>` 返回 → 同名接口引用；`T` 本身必须是可绑接口（这里指向 ①的接口）
	CA_BIND()
	class IBindRow21
	{
	public:
		virtual castl::weak_ptr<IBindRow16> GetFlagOwner() = 0;
		virtual ~IBindRow21() = default;
	};

	// ---- 嵌套纯标量记录（矩阵第 2 行「blittable 结构体」的递归形态）----
	// `BindFloatRect` 带 defaulted `operator<=>`：它不改变数据布局，生成器 MUST 仍按数据记录处理；
	// 换成自定义方法时必须拒绝（拒绝夹具在 `BindableShapes.RejectNested.h`）。
	struct BindFloatRect
	{
		float x;
		float y;
		float width;
		float height;

		auto operator<=>(BindFloatRect const&) const = default;
	};

	// 外层记录里嵌套两个 `BindFloatRect`：布局清单必须递归记录嵌套类型与字段偏移，
	// 不能把 `m_MonitorRect` 这一层摊平成匿名字节。
	struct BindMonitorInfo
	{
		BindFloatRect monitorRect;
		BindFloatRect workAreaRect;
		float dpiScale;
	};

	// 按值返回 / 按 const& 入参各一条：镜像搬运要覆盖两个方向。
	CA_BIND()
	class IBindRow22
	{
	public:
		virtual BindMonitorInfo GetMonitorInfo(int32_t monitorId) const = 0;
		virtual void SetMonitorInfo(BindMonitorInfo const& info) = 0;
		virtual ~IBindRow22() = default;
	};
}

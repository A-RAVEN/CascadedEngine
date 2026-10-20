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

	// 第 13 行：castl::function<Ret(Args...)> 回调 ⇒ 函数指针 + void* user + 保活表
	CA_BIND()
	class IBindRow13
	{
	public:
		virtual void SetCallback(castl::function<void(uint32_t, float)> callback) = 0;
		virtual ~IBindRow13() = default;
	};

	// 第 14 行：会抛异常的方法 ⇒ thunk 里 try/catch，返回"错误码 + 文本"
	CA_BIND()
	class IBindRow14
	{
	public:
		virtual void MayThrow(int32_t value) = 0;
		virtual ~IBindRow14() = default;
	};
}

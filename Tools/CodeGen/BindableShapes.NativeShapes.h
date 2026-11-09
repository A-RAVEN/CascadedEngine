#pragma once
// 原生形态夹具（codegen-native-binding-prereqs 任务 2.1）。
//
// 这份头只被 CodeGen 解析、被生成物编译，不参与引擎运行。它覆盖本批要给发射器补齐的
// 原生面形态（每一项落到后面哪条任务见行内注释）：
//   · 非纯虚 virtual 成员（接口里带实现体）—— 任务 2.2；
//   · 非虚公开成员函数 —— 任务 2.2；
//   · 返回自身引用的链式方法 —— 任务 2.2（C ABI 返回 void，C# 返回同一表面对象）；
//   · 一个模板方法 —— 本批维持拒绝（① 类，用 CA_BIND_SKIP 显式排除并写理由）；
//   · 纯标量记录静态工厂 + 默认构造的类 —— 任务 2.3；
//   · 同名两签重载 —— 任务 2.4（第 1 槽保持现名、第 2 槽 _2）；
//   · 资源引用值类 / 序列化块记录 / 参数注入 —— 任务 3.1 / 3.2 / 3.3 按需追加。
//
// 第一段必须留在所有 include 之前：UCRT 的 `stddef.h` 在定义了 `_MSC_VER` 时把 `offsetof`
// 展开成"空指针解引用取地址"的宏，libclang 解析生成物里的 `constexpr CodeGenFieldInfo arr[]`
// 时会因此报 "must be initialized by a constant expression"（照抄 RenderFoundation 的处置）。
#ifndef _CRT_USE_BUILTIN_OFFSETOF
#define _CRT_USE_BUILTIN_OFFSETOF
#endif
#include <stdint.h>
#include <type_traits>

#include <CASTL/CAString.h>
#include <CASTL/CAStringView.h>
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>

#include <HashKeys.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 纯标量记录（任务 2.3 的静态工厂返回）：公开字段、标准布局、可默认构造、无虚函数。
	struct NativeCount
	{
		uint32_t calls;
		uint32_t lastValue;
	};

	// 资源引用值类（任务 3.1）：kind 枚举 + 按对象句柄 / 按名两态构造。它不是 CA_BIND 类，
	// 自身成员方法不进表面；只作为参数过界（C ABI 是 kind + 句柄 + 名称四段）。
	class IBindNativeShape;

	// 注入钩子（任务 3.3）：固定名 `<类>_<方法>_<参数>`，返回引擎默认提供的指针。
	extern "C" void* CAInject_IBindNativeShape_SetInjectedPointer_externalState();

	CA_BIND_VALUE(NativeResourceRef)
	class NativeResourceRef
	{
	public:
		// kind 枚举在类体里（与真实 ImageHandle / BufferHandle 同形）；生成器从类体源码读它。
		enum class ENativeRefKind : uint32_t
		{
			None = 0,
			Object = 1,
			Named = 2,
		};

		NativeResourceRef() = default;
		explicit NativeResourceRef(castl::shared_ptr<IBindNativeShape> const& object)
			: m_Object(object), m_Kind(ENativeRefKind::Object)
		{
		}
		explicit NativeResourceRef(cacore::NameHash const& name)
			: m_Name(name), m_Kind(ENativeRefKind::Named)
		{
		}
		ENativeRefKind GetReferenceKind() const { return m_Kind; }
		castl::shared_ptr<IBindNativeShape> const& GetReferencedObject() const { return m_Object; }
		cacore::NameHash const& GetReferencedName() const { return m_Name; }

	private:
		castl::shared_ptr<IBindNativeShape> m_Object;
		cacore::NameHash m_Name;
		ENativeRefKind m_Kind = ENativeRefKind::None;
	};

	// 序列化块记录（任务 3.2）：公开字段 + 私有字段（经 CA_CODEGEN_FRIEND），
	// 成员含一层 `castl::vector<标量记录>`、一层 `castl::vector<枚举>` 与文本（NameHash）。
	struct PassAttachment
	{
		uint32_t index;
		float weight;
	};

	// 图形状序列化夹具（本批 1.5 / design D11）：把真实 `RenderPass`/`DrawCallBatch`/`DrawCall`
	// 里出现的三类新元素类别收在一张记录里，逐字段往返核对：
	//   ① 值类容器元素：`castl::vector<CA_BIND_VALUE 值类>`（真实形状：`vector<ImageHandle> m_Attachments`）；
	//   ② 命名映射成员：`unordered_map<cacore::NameHash, 标量|记录|值类>`（真实形状：
	//      `unordered_map<NameHash, BufferHandle> m_BoundVertexBuffers`）；
	//   ③ 接口对象引用成员：`shared_ptr<已挂 CA_BIND 的接口>`，直接成员与映射值两种（真实形状：
	//      `ShaderStructDic = unordered_map<NameHash, shared_ptr<ShaderStruct>>`）。
	struct GraphLikeRecord
	{
		castl::vector<NativeResourceRef> m_Refs;                                            // ①
		castl::unordered_map<cacore::NameHash, uint32_t> m_Counts;                          // ② 标量值
		castl::unordered_map<cacore::NameHash, PassAttachment> m_Attachments;               // ② 记录值
		castl::unordered_map<cacore::NameHash, NativeResourceRef> m_RefsByName;             // ② 值类值
		castl::shared_ptr<IBindNativeShape> m_Object;                                       // ③ 直接成员
		castl::unordered_map<cacore::NameHash, castl::shared_ptr<IBindNativeShape>> m_Objects;  // ③ 映射值
	};

	enum class EPassMode : uint16_t
	{
		Fill = 0,
		Wire = 1,
	};

	class PassLike
	{
		CA_CODEGEN_FRIEND(PassLike);

	public:
		PassLike() = default;

		castl::vector<PassAttachment> m_Attachments;
		castl::vector<EPassMode> m_Modes;
		cacore::NameHash m_Name;

		void SetPrivateCount(uint32_t value) { m_PrivateCount = value; }
		uint32_t GetPrivateCount() const { return m_PrivateCount; }

	private:
		uint32_t m_PrivateCount = 0u;
	};

	CA_BIND()
	class IBindNativeShape
	{
	public:
		virtual ~IBindNativeShape() = default;

		// 现行生成器已经能绑定的纯虚面：改造前的绿态锚，改造后产物必须仍然是这两条槽。
		virtual uint32_t GetCounter() const = 0;
		virtual void SetCounter(uint32_t value) = 0;

		// 非纯虚 virtual（任务 2.2）：接口里带实现体，派生类可以覆盖。
		virtual uint32_t GetVirtualValue() const { return 7u; }

		// 非虚公开成员函数（任务 2.2）：原生便利方法，调纯虚钩子。
		uint32_t GetDoubled() const { return GetCounter() * 2u; }

		// 纯标量记录静态工厂（任务 2.3）：返回记录值，作为值记录构造通道。
		static NativeCount MakeTally(uint32_t value)
		{
			return NativeCount{ value, value + 1u };
		}

		// 资源引用过界（任务 3.1）：kind + 对象句柄 / 名称两通道，原生实现按构造通道收到值类。
		// 形参名用 resourceRef 而不是 ref：C++ 合法，但 `ref` 是 C# 关键字，出码到托管表面会编不过。
		virtual void ConsumeRef(NativeResourceRef const& resourceRef, uint32_t channel) = 0;

		// 序列化块记录（任务 3.2）：输入方向、一层容器/文本记录，过界是 uint8_t const* + count。
		virtual void ConsumePass(PassLike const& pass) = 0;

		// 图形状序列化（本批 1.5 / design D11）：值类容器 / 命名映射 / 接口对象引用三类元素一次过界。
		virtual void ConsumeGraphShape(GraphLikeRecord const& shape) = 0;

		// 参数注入（任务 3.3）：第二个参数是所有权不明的裸指针，由引擎钩子提供；
		// C ABI 与托管表面都看不到它（CA_BIND_INJECT 写在类后面）。
		virtual void SetInjectedPointer(uint32_t token, int32_t* externalState) = 0;

		// 内存窗参数投影（csharp-native-direct-binding 任务 1.1 / design D2）：
		// `const void*` + 紧随其后的 `uint64_t` 长度 + 其余标量（offset / copy）。
		// 托管表面收 `byte[]`（长度由托管算），thunk 以固定内存传指针与长度；
		// 原生实现必须收到与托管字节逐字节一致的内容与正确长度，其余参数原样过界。
		virtual void UploadWindow(void const* data, uint64_t size, uint64_t offset, bool copy) = 0;

		// 记录两段式（csharp-native-direct-binding 任务 1.2 / design D8）：返回位置指向
		// **可序列化记录**的非拥有裸指针。命中返回记录地址、未命中返回空指针（托管得 null）；
		// 托管拿到的是记录的序列化只读副本，原生签名不动。
		virtual PassLike const* FindPass(cacore::NameHash const& passName) = 0;

		// 返回位置标量容器两段式（csharp-native-direct-binding 任务 1.6 / design D13）：
		// `vector<标量> const&` 走 `Count` + `Copy` 两个伴生；托管表面得到元素数组。
		// 空容器：Count=0 且 Copy 不许在零长度上解引用。记录容器 / 含句柄或文本的容器维持不可绑。
		virtual castl::vector<uint32_t> const& GetPassIndices() const = 0;

		// 模板显式实例化声明（csharp-native-direct-binding 任务 1.3 / design D9）：模板成员本身
		// 不自动采集；类后的 `CA_BIND_INSTANTIATE(TemplatedValue, uint32_t, float)` 声明要绑的
		// 两个形态。两个形态各自出具体槽、托管侧是同名重载；未声明的形态（如 double）不进任何表面。
		// 模板体在头文件内定义 —— thunk 自己完成实例化，C++ 侧不新增函数、不改签名。
		template<typename T>
		uint32_t TemplatedValue(T const& value)
		{
			if constexpr (std::is_same_v<T, uint32_t>)
			{
				++s_TemplateUintCalls;
				return value + 100u;
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				++s_TemplateFloatCalls;
				return static_cast<uint32_t>(value * 2.0f);
			}
			else
			{
				return 0u;
			}
		}
		static inline uint32_t s_TemplateUintCalls = 0u;
		static inline uint32_t s_TemplateFloatCalls = 0u;

		// 返回自身引用的链式方法（任务 2.2）：C ABI 投影成 void，C# 表面返回 this。
		virtual IBindNativeShape& ChainAdd(uint32_t value)
		{
			SetCounter(GetCounter() + value);
			return *this;
		}

		// 同名两签重载（任务 2.4）：无隐式转换歧义；第 1 槽保持 Overloaded、第 2 槽 Overloaded_2。
		// 两个实现各带一个计数，CoreTests 用它证明"分别调用、互不冒充"。
		virtual uint32_t Overloaded(uint32_t value)
		{
			++s_OverloadOneCalls;
			return value + 1u;
		}
		virtual uint32_t Overloaded(uint32_t value, uint32_t add)
		{
			++s_OverloadTwoCalls;
			return value + add;
		}
		static inline uint32_t s_OverloadOneCalls = 0u;
		static inline uint32_t s_OverloadTwoCalls = 0u;

		// ① 模板方法：本批维持拒绝 —— 显式排除并写理由（不能静默漏掉）。
		template<typename T>
		T GetAs() const;
	};
	CA_BIND_SKIP(GetAs, "① 模板方法：本批不做模板单态化绑定（spec csharp-bindings 的①类）")
	CA_BIND_INJECT(SetInjectedPointer, externalState)
	// 模板显式实例化（D9）：只绑 uint32_t 与 float 两个形态；double 等未声明形态不进任何表面。
	CA_BIND_INSTANTIATE(TemplatedValue, uint32_t, float)

	// 带默认构造的绑定类（任务 2.3）：生成 `<类>_Create` 槽与 C# `Create` 入口。
	// 本类没有纯虚方法；改造前的生成器一条方法都收不到，改造后才会有方法 + Create。
	CA_BIND()
	class BindNativeObject
	{
	public:
		BindNativeObject() = default;

		uint32_t GetValue() const { return m_Value; }
		void SetValue(uint32_t value) { m_Value = value; }
		uint32_t DoubleValue() const { return m_Value * 2u; }

	private:
		uint32_t m_Value = 0u;
	};
}

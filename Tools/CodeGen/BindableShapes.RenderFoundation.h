#pragma once
// 渲染基础形状夹具（csharp-render-binding-foundations 任务 1.1）。
//
// 覆盖：平坦/嵌套公开数据记录、bool、窄枚举、方法级与记录级 `EnumFlags`、静态工厂与非虚
// 便利查询、独立 `NameHash` / `PathHash` / `HashObj<T>`、字节与标量/记录/文本/枚举/标志位/
// 句柄的混合参数、以及带复杂实参的智能指针返回。
//
// 注意：这个头只被 CodeGen 解析、以及被生成的 shim 编译，不参与引擎运行。
//
// 第一段必须留在所有 include 之前：UCRT 的 `stddef.h` 在定义了 `_MSC_VER` 时把
// `offsetof` 展开成"空指针解引用取地址"的宏，那个表达式不是常量表达式；libclang 解析
// 生成物 `RenderHashable.codegen.h` 里的 `constexpr CodeGenFieldInfo arr[]` 时会因此报
// "must be initialized by a constant expression"。`_CRT_USE_BUILTIN_OFFSETOF` 是 UCRT
// 自己提供的开关，打开后 `offsetof` 展开成 `__builtin_offsetof`（MSVC 与 clang 都支持），
// 编译期语义不变。
#ifndef _CRT_USE_BUILTIN_OFFSETOF
#define _CRT_USE_BUILTIN_OFFSETOF
#endif
#include <stdint.h>

#include <CASTL/CAString.h>
#include <CASTL/CAStringView.h>
#include <CASTL/CAVector.h>
#include <CASTL/CASharedPtr.h>

#include <HashKeys.h>

#include "BindableShapes.RenderTypes.h"
// `RenderHashable` 的显式哈希来源：由 build.py 的 CODEGEN_ANNOTATED 在绑定生成之前生成。
#include "RenderHashable.codegen.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindFoundation
	{
	public:
		// 平坦记录：按值返回 + `const&` 入参。
		virtual RenderDesc GetDesc(RenderDesc const& desc) = 0;
		// 嵌套记录：外层字段偏移必须逐层保留。
		virtual RenderNested GetNested(RenderNested const& nested) = 0;
		// bool 双向投影：C ABI 是 int32_t，公开表面仍是 bool。
		virtual bool GetBool(bool value) = 0;
		// 普通枚举：方法级继续按 int32_t 投影。
		virtual ERenderMode GetMode(ERenderMode mode) = 0;
		// 方法级标志位：按 MaskType（uint16_t）投影，公开表面用带 [Flags] 的枚举入参。
		virtual uint16_t GetFlags(ERenderFlags flags) = 0;
		// 独立包装值：按 UTF-8 文本投影，原生侧用现有构造函数重建。
		virtual void SetName(cacore::NameHash const& name) = 0;
		virtual void SetPath(cacore::PathHash const& path) = 0;
		// `HashObj<T>`：只传所含值，原生侧重新构造哈希对象。
		virtual void ConsumeHash(cacore::HashObj<RenderHashable> const& value) = 0;
		// 字节 + 标量/记录/文本/枚举/标志位/句柄的混合参数。
		virtual int64_t MixBytes(castl::vector<uint8_t> const& data, int32_t count, RenderDesc const& desc,
			castl::string const& label, ERenderMode mode, ERenderFlags flags, void* token) = 0;
		// 智能指针返回 + 复杂实参组合：返回分支不得漏掉实参转换。
		virtual castl::shared_ptr<RenderDesc> AcquireDesc(castl::vector<uint8_t> const& data,
			RenderDesc const& desc, castl::string const& label, ERenderMode mode, ERenderFlags flags) = 0;
		// 智能指针入参（非接口数据对象）：由发行者入口反查重建。
		virtual void ConsumeDesc(castl::shared_ptr<RenderDesc> desc) = 0;
		virtual ~IBindFoundation() = default;
	};
}

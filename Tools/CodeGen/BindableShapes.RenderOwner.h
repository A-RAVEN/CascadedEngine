#pragma once
// 跨头文件发行接口夹具（csharp-render-binding-foundations 任务 1.2）。
//
// 发行接口返回另一个头里声明的资源接口（强 / 弱 / 借用），并接收资源接口的
// `shared_ptr<T>` 入参。资源接口与发行接口分属两个生成输入，用来验证
// `--list-bind-interfaces` 聚合出的完整限定名清单、目标接口入口与 `shared_ptr`
// 实参重建在跨头文件场景下真的成立。
#include <stdint.h>

#include <CASTL/CASharedPtr.h>

#include "CodeGenMeta.h"
#include "BindableShapes.RenderResource.h"

namespace ca_bind_fixture
{
	CA_BIND()
	class IBindRenderOwner
	{
	public:
		// 强返回：句柄表持有强引用，并配一个发行者角色的 Release 伴生。
		virtual castl::shared_ptr<IBindRenderResource> CreateResource(uint32_t id) = 0;
		// 弱返回：句柄表只存弱引用，没有配对释放伴生。
		virtual castl::weak_ptr<IBindRenderResource> PeekResource() = 0;
		// 借用返回：发行者是 self，目标入口解析要求发行者见证存活。
		virtual IBindRenderResource* BorrowResource() = 0;
		// 智能指针入参：按句柄反查并重建目标类型的 shared_ptr。
		virtual void UseResource(castl::shared_ptr<IBindRenderResource> resource) = 0;
		// 错误目标类型：同一个句柄交到这里必须失败且不进入实现。
		virtual void UseAltResource(castl::shared_ptr<IBindRenderResourceAlt> resource) = 0;
		virtual castl::shared_ptr<IBindRenderResourceAlt> CreateAltResource(uint32_t id) = 0;
		virtual uint32_t LastUsedId() const = 0;
		virtual ~IBindRenderOwner() = default;
	};
}

#pragma once
// 跨头文件资源接口夹具（csharp-render-binding-foundations 任务 1.2）。
//
// 这个头只声明**资源**接口；发行接口 `IBindRenderOwner` 在同一 change 的
// `BindableShapes.RenderOwner.h` 里，两者由 build.py 分两次 `--emit-binding` 生成。
// 资源接口返回发行接口的弱指针与借用裸指针，用来验证：
//   · 目标接口入口能按目标类型解析跨头文件句柄；
//   · 目标解析借用句柄要求发行者见证存在且仍存活；
//   · 两类释放都能清理表项并进位。
#include <stdint.h>

#include <CASTL/CAString.h>
#include <CASTL/CASharedPtr.h>

#include "CodeGenMeta.h"

namespace ca_bind_fixture
{
	// 发行接口在另一个头里声明；弱指针 / 借用裸指针的返回只需要这个不完整类型。
	class IBindRenderOwner;

	CA_BIND()
	class IBindRenderResource
	{
	public:
		virtual uint32_t GetResourceId() const = 0;
		virtual castl::string GetResourceName() const = 0;
		// 弱返回：句柄表只存弱引用，不抬高 use_count。
		virtual castl::weak_ptr<IBindRenderOwner> GetOwnerWeak() = 0;
		// 借用返回：发行者是 `self`，目标解析要求发行者见证存活。
		virtual IBindRenderOwner* GetOwnerBorrowed() = 0;
		virtual ~IBindRenderResource() = default;
	};

	// 第二个资源接口：用来喂"错误目标类型"（把资源句柄传给 Alt 目标入口 / Alt 参数）。
	CA_BIND()
	class IBindRenderResourceAlt
	{
	public:
		virtual uint32_t GetAltId() const = 0;
		virtual ~IBindRenderResourceAlt() = default;
	};
}

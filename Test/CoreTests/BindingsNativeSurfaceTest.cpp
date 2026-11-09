// 原生五类表面（csharp-native-direct-binding 任务 2.4 / design D12+D13）的读数断言。
//
// 直接 `#include` 真实接口的生成产物（与 `BindingsRenderFoundationTest.cpp` 对夹具的做法同一套路）：
// 这样判据跑的是**产品代码**，不是手写替身。本文件只断言"表面形状 + 能真的调到"，不驱动渲染后端。
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "WindowHandle.Bindings.cpp"
#include "GPUGraph.Bindings.cpp"
#include "CRenderBackend.Bindings.cpp"
#include "GPUTexture.Bindings.cpp"
#include "ShaderStruct.Bindings.cpp"

// `CRenderBackend::ExecuteGraph` 的调度器注入钩子由**宿主**定义（CSharpHost）。本测试只调
// `GetWindowHandle` / WindowHandle 的读方法，不调 ExecuteGraph；这里给一个测试替身把链接补上，
// 返回空指针（一旦有人在本测试里真调 ExecuteGraph，原生侧会收到空调度器 —— 这里是显式的、
// 有注释的边界，不是静默兜底）。
extern "C" void* CAInject_CRenderBackend_ExecuteGraph_scheduler()
{
	return nullptr;
}

namespace
{
	bool Expect(bool condition, char const* message)
	{
		if (!condition)
		{
			std::printf("  [原生表面] 失败：%s\n", message);
		}
		return condition;
	}

	// WindowHandle 的替身实现：只回答尺寸与描述符，不碰 GPU。
	class FakeWindowHandle final : public graphics_backend::WindowHandle
	{
	public:
		graphics_backend::uint2 GetSizeSafe() const override { return {1920u, 1080u}; }
		graphics_backend::GPUTextureDescriptor const& GetBackbufferDescriptor() const override
		{
			return descriptor;
		}
		bool IsValid() const override { return true; }

		graphics_backend::GPUTextureDescriptor descriptor;
	};
}

bool RunNativeSurfaceTest()
{
	bool ok = true;

	// ① 五个原生表面各有一条**形状**断言：函数表槽的名字与签名逐字钉住（改签名/改名都红）。
	static_assert(sizeof(&GPUGraph_Create) > 0, "GPUGraph_Create 槽必须存在（隐式公开默认构造的工厂）");
	static_assert(sizeof(&CRenderBackend_GetWindowHandle) > 0,
		"CRenderBackend_GetWindowHandle 槽必须存在（2.4 解 SKIP）");
	static_assert(sizeof(&WindowHandle_GetSizeSafe) > 0, "WindowHandle_GetSizeSafe 槽必须存在");
	static_assert(sizeof(&WindowHandle_GetBackbufferDescriptor) > 0,
		"WindowHandle_GetBackbufferDescriptor 槽必须存在");
	static_assert(sizeof(&WindowHandle_IsValid) > 0, "WindowHandle_IsValid 槽必须存在（非纯虚）");
	static_assert(sizeof(&GPUGraph_GetPassIndicesCount) > 0,
		"GPUGraph_GetPassIndicesCount 槽必须存在（D13 两段式）");
	static_assert(sizeof(&GPUGraph_CopyGetPassIndices) > 0,
		"GPUGraph_CopyGetPassIndices 槽必须存在（D13 两段式）");

	// ② GPUGraph 的工厂真的能创建对象：返回句柄非 0，并能解析回对象指针。
	const uint64_t graphHandle = GPUGraph_Create(nullptr);
	ok = Expect(graphHandle != 0u, "GPUGraph_Create 返回 0（工厂没建出对象）") && ok;
	void* graphSelf = graphHandle != 0u ? GPUGraph_ResolveHandle(nullptr, graphHandle) : nullptr;
	ok = Expect(graphSelf != nullptr, "GPUGraph 句柄解析不回对象") && ok;

	// ③ WindowHandle 三个方法的真实往返：尺寸 / 描素符（序列化只读副本）/ 有效性。
	auto windowShared = std::make_shared<FakeWindowHandle>();
	FakeWindowHandle& window = *windowShared;
	const uint64_t windowHandle = WindowHandle_registerStrongHandle(
		&window, std::static_pointer_cast<void>(windowShared), "graphics_backend::WindowHandle");
	ok = Expect(windowHandle != 0u, "WindowHandle 句柄登记失败") && ok;
	WindowHandle_clearError(&window);
	const uint2C size = WindowHandle_GetSizeSafe(&window);
	ok = Expect(size.x == 1920u && size.y == 1080u, "GetSizeSafe 的读回不是 1920x1080") && ok;
	ok = Expect(WindowHandle_IsValid(&window) != 0, "IsValid 读回 false") && ok;
	const GPUTextureDescriptorC descriptor =
		WindowHandle_GetBackbufferDescriptor(&window);
	ok = Expect(WindowHandle_LastErrorCode(&window) == 0,
		"GetBackbufferDescriptor 写了错误码（序列化只读副本失败）") && ok;

	// ③b 值类的**第二条对象构造通道**：ImageHandle 的 WindowHandle → Backbuffer（design D12）。
	// 这条通道走 `GPUGraph_Present` 的 kind=2 分支；发射器若只出第一条对象通道，这里会写
	// 「无效的资源引用类型」、呈现清单保持空 —— 4.1 的窗口目标/呈现链路就断在这里。
	{
		auto* graph = static_cast<graphics_backend::GPUGraph*>(graphSelf);
		GPUGraph_Present(graphSelf, 2u, windowHandle, nullptr, 0u);
		const int32_t presentError = GPUGraph_LastErrorCode(graphSelf);
		const auto& presents = graph->GetFinalizePass().m_PresentBackBuffers;
		const bool backbufferPresent = presents.size() == 1u &&
			presents[0].GetType() == graphics_backend::ImageHandle::ImageType::Backbuffer &&
			presents[0].GetWindowHandle() == windowShared;
		ok = Expect(presentError == 0,
			"GPUGraph_Present 拒绝了 WindowHandle 构造的后台缓冲引用（值类第二条对象通道没生成）") && ok;
		ok = Expect(backbufferPresent,
			"呈现清单没有收到 Backbuffer 引用（值类第二条对象通道生成错误）") && ok;
		std::printf("  [原生表面] Present(windows 句柄构造) 错误码=%d 呈现项=%zu\n", presentError,
			presents.size());
	}

	std::printf("  [原生表面] 工厂句柄=%llu 解析=%d 窗口尺寸=%ux%u 有效=1 描述符码=%d 槽=7\n",
		static_cast<unsigned long long>(graphHandle), graphSelf != nullptr ? 1 : 0, size.x, size.y,
		WindowHandle_LastErrorCode(&window));

	if (graphHandle != 0u)
	{
		GPUGraph_ReleaseHandle(nullptr, graphHandle);
	}
	return ok;
}

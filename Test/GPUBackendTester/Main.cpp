// editor-window-panel 3.1：进程内最小 WebSocket 客户端（Winsock 手写 RFC6455 客户端握手 + 掩码文本帧，
// 零新依赖）。`winsock2.h` 必须先于 `windows.h`，否则 windows.h 会先拉进 winsock1 的声明、两边冲突；
// 链接用 `#pragma comment` 补齐（0.1⑤ 实测 CMakeLists/build.ninja 里没有 ws2_32）。
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// editor-window-panel 3.2 ⑩：桌面验证的第二条捕获通道 —— DXGI Desktop Duplication 能抓到
// independent-flip 交换链真实上屏内容（屏幕 DC 的 BitBlt 在 flip 呈现下可能只抓到窗口背景）。
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
// editor-window-panel 3.2 ⑩ 主通道：Windows.Graphics.Capture（按窗口抓内容，不依赖桌面变化，
// 也不依赖 BitBlt 能否看到 flip 呈现）。C++/WinRT 头随 Windows SDK 提供（INCLUDE 已含 cppwinrt）。
#include <mutex>
#include <atomic>
#include <memory>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#pragma comment(lib, "windowsapp.lib")
#include<mimalloc.h>
#include <ScriptHost/IScriptHost.h>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <windows.h>
#include <ThreadManager.h>
#include <Compiler.h>
#include <iostream>
#include <library_loader.h>
#include <CRenderBackend.h>
#include <RenderInterfaceManager.h>
#include <CNativeRenderPassInfo.h>
#include <CCommandList.h>
#include <ShaderBindingBuilder.h>
#include <FileLoader.h>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <stb_image_write.h>
#include <thread>
#include <CASTL/CAString.h>
#include <CASTL/CAChrono.h>
#include <CAResource/ResourceSystemFactory.h>
#include <GPUGraph.h>
// vulkan-gpu-queries：一次图执行的逐 pass GPU 计时（`gpu-graph-timing` 能力）。
#include <GPUTiming.h>
#include <TextureSampler.h>
#include <CAWindow/WindowSystem.h>
#include <CATimer/Timer.h>
#include <TimerSystemEditor/TimerSystem_Impl.h>
#include <IOManager/IOManager.h>
#include <filesystem>
#include <system_error>
#include <magic_enum/magic_enum.hpp>
#include <CAResource/ResourceSystemFactory.h>
#include <stb_image.h>
#include <CACore/CAModuleManager.h>
#include <IMGUIContext/IMGUIContext.h>
#include <crtdbg.h>
#include <windows.h>
#include "private/MiniDump.h"
// 生成的 C ABI 入口已经从这个文件里退场（`csharp-window-creation` 任务 5.5）：端到端那一圈
// 由托管侧走**生成的表面**（`IWindow` / `IWindowSystem`）完成，C ABI 入口只在
// `Test/CoreTests` 的绑定用例里直接对着产物验。
#include "FakeWindowSystem.h"
// csharp-reverse-interface-binding 8.1：反向接口的**函数表**（跨模块唯一正确入口：表里的函数指针
// 指向创建句柄的那个模块）。产物里的 C++ 类型只用来解释表结构 —— 绝不调用 tester 自己那份
// `<线形>__*` 函数（句柄表是 inline+函数级 static，EXE 与 DLL 各一份，查错表就等于查不到）。
#include "resource_management__IResourceFactory.Bindings.cpp"
#include "resource_management__IResource.Bindings.cpp"
// 编号常量来自登记表头（唯一来源仍是 interface-ids.lock）。
#include "BindingRegistry.Bindings.h"
// 脚本渲染用例的原生核对通道（csharp-native-direct-binding 4.1）：桥退役后不再 include 适配层私有头，
// 改为读托管侧读数行（`[托管 读数]`：原生对象地址 + 计数）+ 图附件的资源引用（helper 在
// TestCSharpTriangle 之前）。

// 实例名（`csharp-window-creation` 任务 5.4）：登记与查找都带这一维 —— 同一个接口可以并存
// 多份实现。真实的窗口系统实例名必须与 `CA_MODULE_INSTANCE(cawindow::IWindowSystem,
// cawindow::WindowSystem, WindowSystem_GLFW)` 的第三个参数逐字一致。
namespace
{
	constexpr char const* kWindowInstanceName = "Window";
	constexpr char const* kRealWindowSystemInstanceName = "WindowSystem_GLFW";
	constexpr char const* kFakeWindowSystemInstanceName = "FakeWindowSystem";
}

using namespace thread_management;
using namespace resource_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace cawindow;
using namespace catimer;
using namespace ca_io;

// ---- CRT assert / error auto-dismiss hook ----
// MessageBoxTimeout is exported by user32.dll (widely available on Windows); it
// shows a message box that closes itself after `dwMilliseconds`, so a debug
// assert/error is visible but never needs a human click to dismiss.
extern "C" WINUSERAPI int WINAPI MessageBoxTimeoutA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType, WORD wLanguageId, DWORD dwMilliseconds);

static int __cdecl AutoDismissAssertHook(int reportType, char* message, int* /*returnValue*/)
{
	if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR)
	{
		const char* title = (reportType == _CRT_ASSERT)
			? "Assertion Failed (auto-dismiss)"
			: "Runtime Error (auto-dismiss)";
		MessageBoxTimeoutA(nullptr,
			message ? message : "(no message)",
			title,
			MB_OK | MB_ICONERROR | MB_SYSTEMMODAL,
			0 /* languageId */,
			4000 /* ms -> auto-close */);
		// Mirror to stderr so headless runs keep a log record.
		fprintf(stderr, "[CRT %s] %s\n", title, message ? message : "");
	}
	// Return TRUE = handled; suppress the default modal-and-wait dialog. The CRT
	// still aborts afterwards (MiniDump::EnableAutoDump captures the crash).
	return TRUE;
}

// ---- TestContext ----

struct TestContext
{
	cacore::CAModuleManager moduleManager;
	cacore::IModuleManager* pModuleManager = nullptr;
	CThreadManager* pThreadManager = nullptr;
	CRenderBackend* pGPUBackend = nullptr;
	IWindowSystem* pWindowSystem = nullptr;
	imgui_display::IMGUIContext* pIMGUIContext = nullptr;
	ResourceManagingSystem* pResourceManagingSystem = nullptr;
	ResourceImportingSystem* pImportingSystem = nullptr;
	castl::string editorConfigPath;
	castl::string assetPath;
	castl::string resourcePath;
	int headlessFrames = 0;
	int captureFrame = -1; // add-render-readback: read back this frame (index into headless loop) to PNG
	int captureWrites = 0; // 本测试成功写出的 capture PNG 数（任务 2.3：0 = 没写成，帧循环结束后警告）
	castl::string backendName = "vulkan"; // which backend is running; tags capture PNG filenames
};

// ---- Capture helper (add-render-readback extension) ----
// Create a caller-owned (External) offscreen RT sized to a render target; it is the only legal
// readback source for the visual-assertion path (a presented swapchain backbuffer has no eTransferSrc
// usage and its content is undefined post-present — see design D2/D5).
static ImageHandle CreateCaptureRT(TestContext& ctx, uint32_t w, uint32_t h)
{
	auto tex = ctx.pGPUBackend->CreateGPUTexture(
		GPUTextureDescriptor::Create(w, h, ETextureFormat::E_R8G8B8A8_UNORM),
		ETextureAccessType::eTransferSrc | ETextureAccessType::eTransferDst | ETextureAccessType::eRT);
	return ImageHandle(tex);
}

// Read back an external offscreen RT and export it to a backend-tagged PNG so vulkan/d3d12 outputs
// coexist: <baseName>_<backend>.png. Returns false if the backend rejected the readback.
static bool WriteReadbackPNG(TestContext& ctx, ImageHandle const& rt, int w, int h, char const* baseName)
{
	// Carrier size = the SOLE public source GetImageByteSize (do NOT derive W*H*4 by hand).
	std::vector<uint8_t> px(static_cast<size_t>(GetImageByteSize(w, h, ETextureFormat::E_R8G8B8A8_UNORM)));
	auto token = ctx.pGPUBackend->Readback(rt, px);
	if (!token)
	{
		std::cerr << "[capture] " << baseName << ": Readback returned null token" << std::endl;
		return false;
	}
	token->Wait();
	bool nonEmpty = false;
	for (auto b : px) { if (b != 0) { nonEmpty = true; break; } }
	castl::string pngPath = castl::string("test_output/") + baseName + "_" + ctx.backendName + ".png";
	stbi_write_png(pngPath.c_str(), w, h, 4, px.data(), w * 4);
	std::cout << baseName << ": wrote " << pngPath.c_str()
		<< " pixelBytes=" << px.size()
		<< " nonEmpty=" << (nonEmpty ? "true" : "false") << std::endl;
	if (!nonEmpty)
	{
		std::cerr << "[capture] " << baseName << ": capture is BLANK (nonEmpty=false) - render likely broken" << std::endl;
		return false;
	}
	++ctx.captureWrites;
	return true;
}

// ---- Test Functions ----

void TestSimpleTriangle(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "Hello Triangle");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());

	// 端到端（csharp-bindings-generator 任务 3.2）：把窗口登记给 CSharpHost，并打出 C++ 侧的
	// GetWindowSize 读数 —— 托管侧在第一次 Tick 时会取同一张表、调同一个方法，两处尺寸必须逐字一致。
	if (auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>())
	{
		pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, newWindow.lock().get());
		// 编号 2 也必须登记（`csharp-window-creation` 任务 3.3 实测发现）：登记表是进程级 static，
		// 跑全量时这个测试先登记了编号 1，之后 TestScriptHost 的 Tick 就会一路走到托管侧的编号 2
		// 探针上；那里取不到实例就会写 `s_lastError`，把 TestScriptHost 判红。
		pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);
		int cppWidth = 0;
		int cppHeight = 0;
		newWindow.lock()->GetWindowSize(cppWidth, cppHeight);
		CA_LOG("C++ 侧窗口尺寸 = {} x {}（托管侧稍后会用同一张表读一次）", cppWidth, cppHeight);
	}
	struct VertexStruct
	{
		std::array<float, 3> pos;
		std::array<float, 3> color;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(VertexStruct, color), VertexInputFormat::eR32G32B32_SFloat, CANAME("COLOR")),
		}, false);

	std::vector<VertexStruct> testBuffer = {
		{{-0.25f, -0.25f, 0.25f }, {1.0f, 0.0f, 0.0f}},
		{{0.25f, -0.25f, 0.25f }, {0.0f, 1.0f, 0.0f}},
		{{0.0f, 0.5f, 0.5f }, {0.0f, 0.0f, 1.0f}},
	};

	ImageHandle windowBackBuffer(windowHandle);
	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	// add-render-readback: render the same content to an external offscreen RT so we can screenshot it.
	ImageHandle captureRT;
	int capW = 0, capH = 0;
	if (ctx.captureFrame >= 0)
	{
		auto backDesc = windowHandle->GetBackbufferDescriptor();
		capW = backDesc.width; capH = backDesc.height;
		captureRT = CreateCaptureRT(ctx, capW, capH);
	}
	auto renderContent = [&](ImageHandle target) {
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 0.5f, 0, 1)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestSimpleTriangle") })
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
					.Draw(testBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.Rast(renderContent(windowBackBuffer));
	if (ctx.captureFrame >= 0)
		newGraph->Rast(renderContent(captureRT));
	GPUFrame newFrame;
	newFrame.pGraph = newGraph;

	if (ctx.headlessFrames > 0)
	{
		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT.IsValid())
				WriteReadbackPNG(ctx, captureRT, capW, capH, "TestSimpleTriangle");
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
		}
		ctx.pGPUBackend->WaitIdle();
	}
}

// add-render-readback: closed-loop validation — render a triangle to a SELF-BUILT offscreen RT
// (External/caller-owned, design D2/D5: readback source must be an offscreen RT, not the
// presented backbuffer) then Readback it, wait, export PNG + assert non-empty.
void TestReadback(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "TestReadback");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
		std::array<float, 3> color;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(VertexStruct, color), VertexInputFormat::eR32G32B32_SFloat, CANAME("COLOR")),
		}, false);

	std::vector<VertexStruct> testBuffer = {
		{{-0.9f, -0.9f, 0.25f }, {1.0f, 0.0f, 0.0f}},
		{{0.9f, -0.9f, 0.25f }, {0.0f, 1.0f, 0.0f}},
		{{0.0f, 0.9f, 0.25f }, {0.0f, 0.0f, 1.0f}},
	};

	// Self-built offscreen RT: caller-owned (External) + eTransferSrc so it's readable via readback.
	uint32_t const RT_W = 256, RT_H = 256;
	auto offscreenRT = ctx.pGPUBackend->CreateGPUTexture(
		GPUTextureDescriptor::Create(RT_W, RT_H, ETextureFormat::E_R8G8B8A8_UNORM),
		ETextureAccessType::eTransferSrc | ETextureAccessType::eTransferDst | ETextureAccessType::eRT);
	ImageHandle rtImage(offscreenRT);

	ImageHandle windowBackBuffer(windowHandle);
	BufferHandle vbuffer(CANAME("ReadbackVertBuffer"));
	// Render the triangle to BOTH the backbuffer (so the present semaphore signals normally —
	// VUID-vkQueuePresentKHR-pWaitSemaphores-03268 requires signaled present semaphores) and the
	// offscreen RT (the readback source). The offscreen pass is what we read back.
	auto makeTrianglePass = [&](ImageHandle target) {
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0.2f, 0.0f, 0.2f, 1.0f)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestSimpleTriangle") })
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
					.Draw(testBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.Rast(makeTrianglePass(windowBackBuffer))
		.Rast(makeTrianglePass(rtImage));
	GPUFrame newFrame;
	newFrame.pGraph = newGraph;

	bool captured = false;
	int frames = ctx.headlessFrames > 0 ? ctx.headlessFrames : 1;
	for (int i = 0; i < frames; ++i)
	{
		ctx.pWindowSystem->UpdateSystem();
		auto scheduler = ctx.pThreadManager->NewScheduler();
		ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
		if (i == ctx.captureFrame)
		{
			captured = WriteReadbackPNG(ctx, rtImage, RT_W, RT_H, "readback");
		}
	}
	ctx.pGPUBackend->WaitIdle();
	if (ctx.captureFrame >= 0 && !captured)
	{
		// closed-loop validation: a requested readback that produced a null/blank frame is a real failure.
		throw std::runtime_error("TestReadback: requested capture frame " + std::to_string(ctx.captureFrame) + " but readback was null/blank");
	}

	// add-render-readback hardening: Readback(image) must REJECT an unsupported subresource (return a
	// null token) instead of silently truncating to mip0/layer0 — the headline behavioral change. A
	// multi-mip image was previously copy-truncated to mip0; now it is explicitly rejected. (Other
	// reject categories — multi-layer / MSAA / non-2D / depth-stencil multi-aspect — are gated by the
	// same descriptor checks and verified statically; constructing those RTs risks backend creation
	// pitfalls orthogonal to the readback logic.) Run unconditionally so it is covered in non-capture runs.
	{
		auto mmRT = ctx.pGPUBackend->CreateGPUTexture(
			GPUTextureDescriptor::Create(64, 64, ETextureFormat::E_R8G8B8A8_UNORM, ETextureType::e2D, 1, 2),
			ETextureAccessType::eTransferSrc | ETextureAccessType::eTransferDst | ETextureAccessType::eRT);
		// Carrier size = GetImageByteSize (sole public source), even for a reject case.
		std::vector<uint8_t> mmPx(static_cast<size_t>(GetImageByteSize(64, 64, ETextureFormat::E_R8G8B8A8_UNORM)));
		auto mmTok = ctx.pGPUBackend->Readback(ImageHandle(mmRT), mmPx);
		if (mmTok)
		{
			// closed-loop validation: a multi-mip image MUST be rejected (null token), never silently
			// truncated to mip0. If the subresource gate regresses, this is a real failure (hard-fail,
			// mirroring the capture throw at the top of this function) — a soft stderr print would let
			// the gate regress unnoticed because runTestWithDiagnostics still returns pass.
			throw std::runtime_error("TestReadback: multi-mip readback should return null token but got a valid token (subresource gate regressed)");
		}
		std::cout << "[reject] multi-mip readback -> null token OK" << std::endl;
	}
}

void TestTriangleWithConstantColor(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "Hello Triangle With Constant Color");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
		}, false);

	std::vector<VertexStruct> testBuffer = {
		{{-0.25f, -0.25f, 0.25f }},
		{{0.25f, -0.25f, 0.25f }},
		{{0.0f, 0.5f, 0.5f }},
	};

	castl::shared_ptr<ShaderStruct> pConstantColor = ctx.pGPUBackend->CreateShaderStruct(CANAME("ConstantColor"));

	castl::array<castl::shared_ptr<ShaderStruct>, 3> colorStructs;
	for (int i = 0; i < colorStructs.size(); ++i)
	{
		colorStructs[i] = ctx.pGPUBackend->CreateShaderStruct(CANAME("ColorSubStruct"));
		pConstantColor->SetStruct(CANAME("colorStruct"), colorStructs[i], i);
	}

	ImageHandle windowBackBuffer(windowHandle);
	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	ImageHandle depthBuffer(CANAME("WindowDepth"));
	GPUTextureDescriptor depthTextureDesc = windowHandle->GetBackbufferDescriptor();
	depthTextureDesc.format = ETextureFormat::E_D32_SFLOAT;
	ImageHandle captureRT;
	int capW = 0, capH = 0;
	if (ctx.captureFrame >= 0)
	{
		auto backDesc = windowHandle->GetBackbufferDescriptor();
		capW = backDesc.width; capH = backDesc.height;
		captureRT = CreateCaptureRT(ctx, capW, capH);
	}
	auto renderContent = [&](ImageHandle target) {
		return RenderPass::New(target, depthBuffer, AttachmentConfig::Clear(), AttachmentConfig::ClearDepthStencil())
			.SetAttachmentConfig(0, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 0, 1, 1)))
			.SetDepthAttachmentConfig(AttachmentConfig::ClearDepthStencil())
			.SetParam(CANAME("constantColorBlock"), pConstantColor)
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithConstantColor") })
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
					.Draw(testBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocImage(depthBuffer, depthTextureDesc)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.Rast(renderContent(windowBackBuffer));
	if (ctx.captureFrame >= 0)
		newGraph->Rast(renderContent(captureRT));
	GPUFrame newFrame;
	newFrame.pGraph = newGraph;
	castl::chrono::high_resolution_clock timer;
	auto beginTime = timer.now();

	if (ctx.headlessFrames > 0)
	{
		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);

			auto currentTime = timer.now();
			auto duration = castl::chrono::duration_cast<castl::chrono::milliseconds>(currentTime - beginTime).count();
			float elapsedTime = duration / 1000.0f;

			for (int j = 0; j < colorStructs.size(); ++j)
			{
				colorStructs[j]->SetValue(CANAME("color"), glm::vec3(1.0f, j * 0.5f, 0.0f) * (castl::cos(elapsedTime) * 0.5f + 0.5f));
			}
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT.IsValid())
				WriteReadbackPNG(ctx, captureRT, capW, capH, "TestTriangleWithConstantColor");
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);

			auto currentTime = timer.now();
			auto duration = castl::chrono::duration_cast<castl::chrono::milliseconds>(currentTime - beginTime).count();
			float elapsedTime = duration / 1000.0f;

			for (int j = 0; j < colorStructs.size(); ++j)
			{
				colorStructs[j]->SetValue(CANAME("color"), glm::vec3(1.0f, j * 0.5f, 0.0f) * (castl::cos(elapsedTime) * 0.5f + 0.5f));
			}
		}
		ctx.pGPUBackend->WaitIdle();
	}
}

void TestTriangleWithStructuredBufferColor(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "Hello Triangle With Structured Color");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
		}, false);

	std::vector<VertexStruct> testBuffer = {
		{{-0.25f, -0.25f, 0.25f }},
		{{0.25f, -0.25f, 0.25f }},
		{{0.0f, 0.5f, 0.5f }},
	};

	BufferHandle structuredColorBuffer(CANAME("StructuredColorBuffer"));
	castl::shared_ptr<ShaderStruct> pStructuredColor = ctx.pGPUBackend->CreateShaderStruct(CANAME("StructuredColor"));
	pStructuredColor->SetBuffer(CANAME("colorStructuredBuffer"), structuredColorBuffer);

	glm::vec3 testColor = glm::vec3(1.0f, 0.0f, 1.0f);

	ImageHandle windowBackBuffer(windowHandle);
	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	ImageHandle captureRT;
	int capW = 0, capH = 0;
	if (ctx.captureFrame >= 0)
	{
		auto backDesc = windowHandle->GetBackbufferDescriptor();
		capW = backDesc.width; capH = backDesc.height;
		captureRT = CreateCaptureRT(ctx, capW, capH);
	}
	auto renderContent = [&](ImageHandle target) {
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 1, 0, 1)))
			.SetParam(CANAME("structuredColorBlock"), pStructuredColor)
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithStructuredBufferColor") })
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
					.Draw(testBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(testBuffer.size(), sizeof(testBuffer[0])))
		.ScheduleData(vbuffer, testBuffer.data(), testBuffer.size() * sizeof(testBuffer[0]))
		.AllocBuffer(structuredColorBuffer, GPUBufferDescriptor::Create(1, sizeof(glm::vec3)))
		.ScheduleData(structuredColorBuffer, &testColor, sizeof(testColor))
		.Rast(renderContent(windowBackBuffer));
	if (ctx.captureFrame >= 0)
		newGraph->Rast(renderContent(captureRT));
	GPUFrame newFrame;
	newFrame.pGraph = newGraph;

	if (ctx.headlessFrames > 0)
	{
		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT.IsValid())
				WriteReadbackPNG(ctx, captureRT, capW, capH, "TestTriangleWithStructuredBufferColor");
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
		}
		ctx.pGPUBackend->WaitIdle();
	}
}


void TestTriangleWithImageBuffer(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "Texture Sampling");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
		std::array<float, 2> texcoord;
	};
	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(VertexStruct, texcoord), VertexInputFormat::eR32G32B32_SFloat, CANAME("TEXCOORD")),
		}, false);
	//We Choose Vulkan & DX12 Convention, in which texcoord starts at top-left of texture
	std::vector<VertexStruct> testBuffer = {
	{{-0.5f, -0.5f, 0.25f }, {0.0f, 1.0f}},
	{{0.5f, -0.5f, 0.25f }, {1.0f, 1.0f}},
	{{0.5f, 0.5f, 0.25f }, {1.0f, 0.0f}},
	{{-0.5f, 0.5f, 0.25f }, {0.0f, 0.0f}},
	};

	std::vector<uint32_t> indicesBuffer = {
		0, 1, 2,
		2, 3, 0
	};

	ImageHandle windowBackBuffer(windowHandle);

	castl::shared_ptr<GPUTexture> testTexture;
	castl::shared_ptr<GPUBuffer> testVertexBuffer;
	castl::shared_ptr<GPUBuffer> testIndexBuffer;

	//Submit
	{
		castl::string texturFile = (std::filesystem::path(ctx.resourcePath.c_str()) / "Images" / "test.png").string();
		int width, height, channels;
		auto data = stbi_load(texturFile.c_str(), &width, &height, &channels, 4);
		testTexture = ctx.pGPUBackend->CreateGPUTexture(GPUTextureDescriptor::Create(width, height
			, ETextureFormat::E_R8G8B8A8_UNORM)
			, ETextureAccessType::eTransferDst | ETextureAccessType::eSampled);

		uint32_t textureSize = width * height * 4;

		uint32_t color = (0 << 16) | (0 << 8) | (255);
		castl::vector<uint32_t> colorData(256 * 256, color);

		testVertexBuffer = ctx.pGPUBackend->CreateGPUBuffer(
			EBufferUsage::eDataDst | EBufferUsage::eVertexBuffer
			, testBuffer.size()
			, sizeof(testBuffer[0])
		);

		testIndexBuffer = ctx.pGPUBackend->CreateGPUBuffer(
			EBufferUsage::eDataDst | EBufferUsage::eIndexBuffer
			, indicesBuffer.size()
			, sizeof(indicesBuffer[0])
		);

		castl::shared_ptr<GPUGraph> submitGraph = castl::make_shared<GPUGraph>();
		submitGraph->ScheduleData(testTexture, data, textureSize);
		submitGraph->ScheduleData(testVertexBuffer, testBuffer);
		submitGraph->ScheduleData(testIndexBuffer, indicesBuffer);
		submitGraph->Finalize(testTexture, ETextureAccessType::eSampled);
		auto scheduler = ctx.pThreadManager->NewScheduler();
		ctx.pGPUBackend->ExecuteGraph(scheduler.get(), submitGraph);
		stbi_image_free(data);
	}

	auto imageStruct = ctx.pGPUBackend->CreateShaderStruct(CANAME("SamplingTextureData"));
	imageStruct->SetImage(CANAME("testTexture"), testTexture);
	imageStruct->SetSampler(CANAME("testSampler"), TextureSamplerDescriptor::LinearClamp());

	BufferHandle vbuffer(CANAME("TestVertBuffer"));
	BufferHandle ibuffer(CANAME("TestIndicesBuffer"));
	ImageHandle captureRT;
	int capW = 0, capH = 0;
	if (ctx.captureFrame >= 0)
	{
		auto backDesc = windowHandle->GetBackbufferDescriptor();
		capW = backDesc.width; capH = backDesc.height;
		captureRT = CreateCaptureRT(ctx, capW, capH);
	}
	auto renderContent = [&](ImageHandle target) {
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 1, 0, 1)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithTextureSampling") })
			.SetParam(CANAME("textureData"), imageStruct)
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), testVertexBuffer)
					.SetIndexBuffer(EIndexBufferType::e32, testIndexBuffer)
					.DrawIndexed(indicesBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocAndUploadBuffer(vbuffer, testBuffer)
		.AllocAndUploadBuffer(ibuffer, indicesBuffer)
		.Rast(renderContent(windowBackBuffer));
	if (ctx.captureFrame >= 0)
		newGraph->Rast(renderContent(captureRT));

	if (ctx.headlessFrames > 0)
	{
		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT.IsValid())
				WriteReadbackPNG(ctx, captureRT, capW, capH, "TestTriangleWithImageBuffer");
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
		}
		ctx.pGPUBackend->WaitIdle();
	}
}



void TestDoublePass(TestContext& ctx)
{
	int windowWidth = 1024;
	int windowHeight = 512;
	auto newWindow = ctx.pWindowSystem->NewWindow(windowWidth, windowHeight, "Hello Double Pass");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());
	struct VertexStruct
	{
		std::array<float, 3> pos;
		std::array<float, 2> texcoord;
	};
	cacore::HashObj<VertexInputsDescriptor> blitpassDescs = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(VertexStruct, texcoord), VertexInputFormat::eR32G32B32_SFloat, CANAME("TEXCOORD")),
		}, false);
	//We Choose Vulkan & DX12 Convention, in which texcoord starts at top-left of texture
	std::vector<VertexStruct> testBuffer = {
	{{-0.5f, -0.5f, 0.25f }, {0.0f, 1.0f}},
	{{0.5f, -0.5f, 0.25f }, {1.0f, 1.0f}},
	{{0.5f, 0.5f, 0.25f }, {1.0f, 0.0f}},
	{{-0.5f, 0.5f, 0.25f }, {0.0f, 0.0f}},
	};

	std::vector<VertexStruct> testBuffer1 = {
	{{-1.0f, -1.0f, 0.25f }, {0.0f, 1.0f}},
	{{1.0f, -1.0f, 0.25f }, {1.0f, 1.0f}},
	{{1.0f, 1.0f, 0.25f }, {1.0f, 0.0f}},
	{{-1.0f, 1.0f, 0.25f }, {0.0f, 0.0f}},
	};

	std::vector<uint32_t> indicesBuffer = {
		0, 1, 2,
		2, 3, 0
	};

	ImageHandle windowBackBuffer(windowHandle);

	castl::shared_ptr<GPUTexture> testTexture;
	castl::shared_ptr<GPUBuffer> testVertexBuffer;
	castl::shared_ptr<GPUBuffer> testVertexBuffer1;
	castl::shared_ptr<GPUBuffer> testIndexBuffer;

	//Submit
	{
		castl::string texturFile = (std::filesystem::path(ctx.resourcePath.c_str()) / "Images" / "vulkanlogo.png").string();
		int width, height, channels;
		auto data = stbi_load(texturFile.c_str(), &width, &height, &channels, 4);
		testTexture = ctx.pGPUBackend->CreateGPUTexture(GPUTextureDescriptor::Create(width, height
			, ETextureFormat::E_R8G8B8A8_UNORM)
			, ETextureAccessType::eTransferDst | ETextureAccessType::eSampled);

		uint32_t textureSize = width * height * 4;

		uint32_t color = (0 << 16) | (0 << 8) | (255);
		castl::vector<uint32_t> colorData(256 * 256, color);

		testVertexBuffer = ctx.pGPUBackend->CreateGPUBuffer(
			EBufferUsage::eDataDst | EBufferUsage::eVertexBuffer
			, testBuffer.size()
			, sizeof(testBuffer[0])
		);

		testVertexBuffer1 = ctx.pGPUBackend->CreateGPUBuffer(
			EBufferUsage::eDataDst | EBufferUsage::eVertexBuffer
			, testBuffer1.size()
			, sizeof(testBuffer1[0])
		);

		testIndexBuffer = ctx.pGPUBackend->CreateGPUBuffer(
			EBufferUsage::eDataDst | EBufferUsage::eIndexBuffer
			, indicesBuffer.size()
			, sizeof(indicesBuffer[0])
		);

		castl::shared_ptr<GPUGraph> submitGraph = castl::make_shared<GPUGraph>();
		submitGraph->ScheduleData(testTexture, data, textureSize);
		stbi_image_free(data);
		submitGraph->ScheduleData(testVertexBuffer, testBuffer);
		submitGraph->ScheduleData(testVertexBuffer1, testBuffer1);
		submitGraph->ScheduleData(testIndexBuffer, indicesBuffer);
		auto scheduler = ctx.pThreadManager->NewScheduler();
		ctx.pGPUBackend->ExecuteGraph(scheduler.get(), submitGraph);
	}

	auto imageStruct = ctx.pGPUBackend->CreateShaderStruct(CANAME("SamplingTextureData"));
	imageStruct->SetImage(CANAME("testTexture"), testTexture);
	imageStruct->SetSampler(CANAME("testSampler"), TextureSamplerDescriptor::LinearClamp());



	//BufferHandle vbuffer(CANAME("TestVertBuffer"));
	//BufferHandle ibuffer(CANAME("TestIndicesBuffer"));
	ImageHandle pass0RT(CANAME("Pass0"));

	auto blitStruct = ctx.pGPUBackend->CreateShaderStruct(CANAME("SamplingTextureData"));
	blitStruct->SetImage(CANAME("testTexture"), pass0RT, GPUTextureView::CreateDefaultForRenderTarget());
	blitStruct->SetSampler(CANAME("testSampler"), TextureSamplerDescriptor::LinearClamp());

	ImageHandle captureRT;
	int capW = 0, capH = 0;
	if (ctx.captureFrame >= 0)
	{
		auto backDesc = windowHandle->GetBackbufferDescriptor();
		capW = backDesc.width; capH = backDesc.height;
		captureRT = CreateCaptureRT(ctx, capW, capH);
	}
	// Bug-fix regression (pipeline-cache surface): two passes share the SAME shader
	// ("TestNaiveTriangle") but use DIFFERENT rasterization (CullFront vs CullBack). The cache-key fix
	// must give each pass its OWN VkPipeline so culling is honored independently. A single triangle is
	// rendered twice; CullFront and CullBack are complementary (one keeps the face, the other culls it),
	// so the two captures DIFFER when the fix works. Without the fix both passes would bind the
	// first-built pipeline (same cache key → CullFront) and the captures would MATCH.
	struct CullVertex { std::array<float, 3> pos; };
	cacore::HashObj<VertexInputsDescriptor> cullDescs = VertexInputsDescriptor::Create(sizeof(CullVertex),
		{ VertexAttribute::Create(offsetof(CullVertex, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")) }, false);
	std::vector<CullVertex> cullTri = {
		{{-0.5f, -0.5f, 0.0f}},
		{{ 0.5f, -0.5f, 0.0f}},
		{{ 0.0f,  0.5f, 0.0f}},
	};
	BufferHandle cullBuffer(CANAME("CullTestBuffer"));

	ImageHandle cullFrontRT, cullBackRT;
	if (ctx.captureFrame >= 0)
	{
		cullFrontRT = CreateCaptureRT(ctx, capW, capH);
		cullBackRT = CreateCaptureRT(ctx, capW, capH);
	}
	auto cullTriangle = [&](ImageHandle target, RasterizerStates const& rast) {
		CPipelineStateObject pso;
		pso.rasterizationStates = rast;
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 0, 1, 1)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestNaiveTriangle") })
			.SetPipelineState(pso)
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), cullDescs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), cullBuffer)
					.Draw(cullTri.size())
				)
			);
	};
	// The visible content is the final blit of pass0RT to the target; blitPass(target) can be
	// emitted to both the backbuffer (present) and the captureRT (readback), sampling the same pass0RT.
	auto blitPass = [&](ImageHandle target) {
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 1, 0, 1)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestBlitToScreenPass") })
			.SetParam(CANAME("textureData"), blitStruct)
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), blitpassDescs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), testVertexBuffer1)
					.SetIndexBuffer(EIndexBufferType::e32, testIndexBuffer)
					.DrawIndexed(indicesBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		//.AllocAndUploadBuffer(vbuffer, testBuffer)
		//.AllocAndUploadBuffer(ibuffer, indicesBuffer)
		.AllocBuffer(cullBuffer, GPUBufferDescriptor::Create(cullTri.size(), sizeof(cullTri[0])))
		.ScheduleData(cullBuffer, cullTri.data(), cullTri.size() * sizeof(cullTri[0]))
		.AllocImage(pass0RT, GPUTextureDescriptor::Create(windowWidth, windowHeight, ETextureFormat::E_R8G8B8A8_UNORM))
		.Rast(
			RenderPass::New(pass0RT, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 1, 0, 1)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestTriangleWithTextureSampling") })
			.SetParam(CANAME("textureData"), imageStruct)
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), blitpassDescs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), testVertexBuffer)
					.SetIndexBuffer(EIndexBufferType::e32, testIndexBuffer)
					.DrawIndexed(indicesBuffer.size())
				)
			)
		)
		.Rast(blitPass(windowBackBuffer));
	if (ctx.captureFrame >= 0)
	{
		newGraph->Rast(blitPass(captureRT));
		newGraph->Rast(cullTriangle(cullFrontRT, RasterizerStates::CullFront()));
		newGraph->Rast(cullTriangle(cullBackRT, RasterizerStates::CullBack()));
	}

	if (ctx.headlessFrames > 0)
	{
		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT.IsValid())
				WriteReadbackPNG(ctx, captureRT, capW, capH, "TestDoublePass");
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && cullFrontRT.IsValid())
				WriteReadbackPNG(ctx, cullFrontRT, capW, capH, "TestDoublePass_cullFront");
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && cullBackRT.IsValid())
				WriteReadbackPNG(ctx, cullBackRT, capW, capH, "TestDoublePass_cullBack");
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
		}
		ctx.pGPUBackend->WaitIdle();
	}
}


void TestComputeBuffer(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "Compute Modify Vertex Position");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());

	cacore::HashObj<VertexInputsDescriptor> descs = VertexInputsDescriptor::Create(
		sizeof(float) * 3,
		{
			VertexAttribute::Create(0, VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
		}, false);

	std::vector<uint32_t> indicesBuffer = {
		0, 1, 2,
		2, 3, 0
	};


	struct VertexStruct
	{
		std::array<float, 3> pos;
		std::array<float, 3> color;
	};
	cacore::HashObj<VertexInputsDescriptor> descs1 = VertexInputsDescriptor::Create(sizeof(VertexStruct),
		{
			VertexAttribute::Create(offsetof(VertexStruct, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(VertexStruct, color), VertexInputFormat::eR32G32B32_SFloat, CANAME("COLOR")),
		}, false);

	std::vector<VertexStruct> triangleBuffer1 = {
		{{-0.25f, -0.25f, -0.25f }, {1.0f, 0.0f, 0.0f}},
		{{0.25f, -0.25f, -0.25f }, {0.0f, 1.0f, 0.0f}},
		{{0.0f, 0.5f, 0.0f }, {0.0f, 0.0f, 1.0f}},
	};


	ImageHandle windowBackBuffer(windowHandle);

	castl::shared_ptr<GPUBuffer> testIndexBuffer;

	//Submit Index Buffer
	{
		testIndexBuffer = ctx.pGPUBackend->CreateGPUBuffer(
			EBufferUsage::eDataDst | EBufferUsage::eIndexBuffer
			, indicesBuffer.size()
			, sizeof(indicesBuffer[0])
		);

		castl::shared_ptr<GPUGraph> submitGraph = castl::make_shared<GPUGraph>();
		submitGraph->ScheduleData(testIndexBuffer, indicesBuffer);
		auto scheduler = ctx.pThreadManager->NewScheduler();
		ctx.pGPUBackend->ExecuteGraph(scheduler.get(), submitGraph);
	}

	//Set Vertex Buffer As Compute Buffer
	BufferHandle vbuffer(CANAME("ComputeVertexBuffer"));
	BufferHandle vbuffer1(CANAME("ParallelGeometryBuffer"));
	auto computeParams = ctx.pGPUBackend->CreateShaderStruct(CANAME("TestComputeBufferParams"));
	computeParams->SetBuffer(CANAME("RWVertexBuffer"), vbuffer);
	// Bug-fix regression (compute surface): the SAME shader path ("TestComputeVertexBuffer") is used by
	// TWO dispatches, each carrying a DIFFERENT per-dispatch shader struct (computeParams vs
	// computeParams2) that binds a DIFFERENT RW buffer and a DIFFERENT `time` constant. The dedup-key
	// fix must give each dispatch its own binding instance so dispatch1's constant (and buffer) does
	// NOT leak into dispatch2. Without the fix, the second dispatch collapses onto the first instance
	// and writes vbuffer (dispatch1's buffer) instead of vbuffer1, leaving vbuffer1 undefined.
	auto computeParams2 = ctx.pGPUBackend->CreateShaderStruct(CANAME("TestComputeBufferParams"));
	computeParams2->SetBuffer(CANAME("RWVertexBuffer"), vbuffer1);

	ImageHandle captureRT;
	ImageHandle captureRT2;
	int capW = 0, capH = 0;
	if (ctx.captureFrame >= 0)
	{
		auto backDesc = windowHandle->GetBackbufferDescriptor();
		capW = backDesc.width; capH = backDesc.height;
		captureRT = CreateCaptureRT(ctx, capW, capH);
		// captureRT2 mirrors dispatch2's output (vbuffer1) so we can verify it was written independently
		// with its own `time` constant.
		captureRT2 = CreateCaptureRT(ctx, capW, capH);
	}
	// The compute passes run once and fill vbuffer (dispatch1) + vbuffer1 (dispatch2); the backbuffer
	// and each capture RT raster their respective buffer.
	auto renderContent = [&](ImageHandle target, BufferHandle const& vb) {
		return RenderPass::New(target, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 0, 1, 1)))
			.SetShaderInfo({ CAPATH("Shaders/Test/TestNaiveTriangle") })
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), descs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vb)
					.SetIndexBuffer(EIndexBufferType::e32, testIndexBuffer)
					.DrawIndexed(indicesBuffer.size())
				)
			);
	};
	castl::shared_ptr<GPUGraph> newGraph = castl::make_shared<GPUGraph>();
	newGraph->Present(windowBackBuffer)
		.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(4, sizeof(float) * 3))
		.AllocBuffer(vbuffer1, GPUBufferDescriptor::Create(4, sizeof(float) * 3))
		.Comp(ComputeBatch::New(true)
			.Dispatch(ComputeDispatch::Create({ CAPATH("Shaders/Test/TestComputeVertexBuffer") }, 1, 1, 1)
				.SetParam(CANAME("computeParams"), computeParams))
			.Dispatch(ComputeDispatch::Create({ CAPATH("Shaders/Test/TestComputeVertexBuffer") }, 1, 1, 1)
				.SetParam(CANAME("computeParams"), computeParams2))
		)
		.Rast(renderContent(windowBackBuffer, vbuffer));
	if (ctx.captureFrame >= 0)
	{
		newGraph->Rast(renderContent(captureRT, vbuffer));
		newGraph->Rast(renderContent(captureRT2, vbuffer1));
	}

	castl::chrono::high_resolution_clock timer;
	auto startTime = timer.now();

	if (ctx.headlessFrames > 0)
	{
		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
auto elapsedTime = timer.now() - startTime;
			auto duration = castl::chrono::duration_cast<castl::chrono::duration<float>>(elapsedTime).count();
			computeParams->SetValue(CANAME("time"), duration);
			computeParams2->SetValue(CANAME("time"), duration + 1.0f);
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT.IsValid())
				WriteReadbackPNG(ctx, captureRT, capW, capH, "TestComputeBuffer");
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame && captureRT2.IsValid())
				WriteReadbackPNG(ctx, captureRT2, capW, capH, "TestComputeBuffer_d1");
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			auto elapsedTime = timer.now() - startTime;
			auto duration = castl::chrono::duration_cast<castl::chrono::duration<float>>(elapsedTime).count();
			computeParams->SetValue(CANAME("time"), duration);
			computeParams2->SetValue(CANAME("time"), duration + 1.0f);
			ctx.pWindowSystem->UpdateSystem();
			auto scheduler = ctx.pThreadManager->NewScheduler();
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), newGraph);
		}
		ctx.pGPUBackend->WaitIdle();
	}
}


void TestIMGUI(TestContext& ctx)
{
	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "IMGUI Test Window");

	{
		auto scheduler = ctx.pThreadManager->NewScheduler();
		castl::shared_ptr<GPUGraph> initializeGraph = castl::make_shared<GPUGraph>();
		ctx.pIMGUIContext->Initialize(ctx.editorConfigPath, newWindow.lock(), initializeGraph.get());
		ctx.pGPUBackend->ExecuteGraph(scheduler.get(), initializeGraph);
	}



	// Per-window capture: at the capture frame we mirror EVERY drawn window's IMGUI content into a
	// same-size readable RT (the real windows are still opened + presented), then Readback each so the
	// secondary-viewport offset becomes measurable pixel data. The RT is sized to ITS OWN window so the
	// NDC->framebuffer mapping matches the presented surface — the observed offset is a vertex-positioning
	// artifact, and a same-size mirror reproduces it (the engine can't Readback a swapchain backbuffer:
	// D2/D5 — no eTransferSrc).

	if (ctx.headlessFrames > 0)
	{
		// capture-frame bookkeeping: captureRTs[k] <-> drawn viewport #k, in the same order
		// GetWindowHandles() built them (which is the order Draw() mirrors per-viewport).
		castl::vector<ImageHandle> captureRTs;
		std::vector<std::pair<int, int>> captureDims;

		for (int i = 0; i < ctx.headlessFrames; ++i)
		{
ctx.pWindowSystem->UpdateSystem();

			ctx.pIMGUIContext->UpdateIMGUI();

			auto scheduler = ctx.pThreadManager->NewScheduler();
			castl::shared_ptr<GPUGraph> frameGraph = castl::make_shared<GPUGraph>();
			ctx.pIMGUIContext->PrepareDrawData(frameGraph.get());

			auto& contexts = ctx.pIMGUIContext->GetTextureViewContexts();

			if (ctx.captureFrame >= 0 && i == ctx.captureFrame)
			{
				// All platform viewports now exist (UpdateIMGUI/UpdatePlatformWindows ran); dump their geometry.
				std::cerr << "[imgui-diag] === viewport layout at capture frame ===" << std::endl;
				ctx.pIMGUIContext->LogViewports();

				captureRTs.clear(); captureDims.clear();
				auto& surfaces = ctx.pIMGUIContext->GetWindowHandles();
				for (auto& surface : surfaces)
				{
					auto const& d = surface->GetBackbufferDescriptor();
					captureRTs.push_back(CreateCaptureRT(ctx, d.width, d.height));
					captureDims.push_back(std::make_pair((int)d.width, (int)d.height));
				}
				std::cerr << "[imgui-diag] capture frame: " << captureRTs.size() << " window(s)" << std::endl;
			}

			ctx.pIMGUIContext->Draw(frameGraph.get(), captureRTs);

			auto& presentSurfaces = ctx.pIMGUIContext->GetWindowHandles();
			for (auto& surface : presentSurfaces)
			{
				frameGraph->Present(ImageHandle(surface));
			}
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), frameGraph);
			if (ctx.captureFrame >= 0 && i == ctx.captureFrame)
			{
				// One PNG per window: TestIMGUI_viewport<N>_<backend>.png (N=0 is the main window);
				// WriteReadbackPNG appends "_<backend>", so the base name carries the viewport index.
				for (size_t v = 0; v < captureRTs.size(); ++v)
				{
					char base[64];
					snprintf(base, sizeof(base), "TestIMGUI_viewport%zu", v);
					WriteReadbackPNG(ctx, captureRTs[v], captureDims[v].first, captureDims[v].second, base);
				}
			}
		}
		ctx.pGPUBackend->WaitIdle();
	}
	else
	{
		while (!newWindow.lock()->WindowShouldClose())
		{
			ctx.pWindowSystem->UpdateSystem();

			ctx.pIMGUIContext->UpdateIMGUI();

			auto scheduler = ctx.pThreadManager->NewScheduler();
			castl::shared_ptr<GPUGraph> frameGraph = castl::make_shared<GPUGraph>();
			ctx.pIMGUIContext->PrepareDrawData(frameGraph.get());

			auto& contexts = ctx.pIMGUIContext->GetTextureViewContexts();

			ctx.pIMGUIContext->Draw(frameGraph.get());

			auto& presentSurfaces = ctx.pIMGUIContext->GetWindowHandles();
			for (auto& surface : presentSurfaces)
			{
				frameGraph->Present(ImageHandle(surface));
			}
			ctx.pGPUBackend->ExecuteGraph(scheduler.get(), frameGraph);
		}
		ctx.pGPUBackend->WaitIdle();
	}
}


// 失败详情放在**静态缓冲**里（任务 4.4）：这个可执行文件里长 std::string 的堆缓冲在
// 异常抛出与捕获之间会被释放掉（实测 e.what() 拿到 32 个 0xDD = MSVC debug 堆的"已释放"填充，
// 而短于 SSO 阈值的消息完全正常），根因是 mimalloc 与 ucrtbase 两个堆同时存在：
//   mimalloc-redirect: error: mimalloc-redirect.dll seems to be initialized after ucrtbase.dll
// 所以跨 catch 边界只传一句短码，详细文字走这块静态内存，appendJsonResult 直接按 char* 写出。
// （声明放在 `TestIOErrorChannel` 之前：那段用例的失败细节也走这条通道，见下面的判据汇总。）
static char g_errorDetail[512] = {};

// ---- TestIOErrorChannel（io-error-channel 任务 1.2 / 1.3 / 1.4 / 1.5 / 5.1）----
// 批式读写的失败通道，以及资源加载失败的透出。每条判据各自打印一行读数、各自记判定，最后统一判红绿 ——
// 这样改动前的一次运行里 1.2 / 1.3 / 1.4① / 1.4② / 1.5 / 5.1 的**真实读数逐条可辨**，不会被前一条
// 失败挡住。临时文件全部落在 `%TEMP%\io_error_channel_probe\` 下，用完删除（异常路径也删，见 TempDirGuard）；
// 5.1 用一个资源字典里没有、磁盘上也不存在的名字，不碰 `CAAssets` 里的真实资产。
namespace
{
	// 5.1 的探针资源类型：`GetOrLoadResource` 的模板便利入口要求一个可默认构造的 `IResource` 派生类型。
	// 路径不存在时 `Batch()` 在反序列化之前就抛 `io_error`，所以下面两个成员不该被调到；
	// 一旦被调到（说明批创建期的探测没拦住），这里抛的异常会把 5.1 判红。
	class MissingAssetResourceProbe : public IResource
	{
	public:
		void Serialize(ca_io::WBatch* /*inWriter*/) override { throw std::runtime_error("E_IO_PROBE_SERIALIZE"); }
		void Deserialize(ca_io::IOBatch* /*inReader*/) override { throw std::runtime_error("E_IO_PROBE_DESERIALIZE"); }
	};

	// 一条「必须抛 ca_io::io_error 且消息点名路径」的判据读数。
	struct IoThrowReading
	{
		bool threw = false;     // 抛的是不是 ca_io::io_error
		bool namedPath = false; // 消息里有没有那个路径
		char actual[320] = {};  // 实际发生的事（失败时直接看这一行）

		bool Passed() const { return threw && namedPath; }
	};

	// 跑一个应当失败的调用，把「有没有抛、抛的是什么、消息点没点名路径」都变成读数，不向外抛异常 ——
	// 好让同一轮里其余判据照常跑。
	template <typename Fn>
	IoThrowReading ProbeThrow(char const* expectedPath, Fn&& action)
	{
		IoThrowReading reading;
		try
		{
			action();
			std::snprintf(reading.actual, sizeof(reading.actual), "没有抛异常（静默成功）");
		}
		catch (ca_io::io_error const& e)
		{
			reading.threw = true;
			// 直接对 `what()` 的字符缓冲做子串查找：本进程里 std::string 的堆缓冲在跨 catch 边界
			// 有已知问题（见 `g_errorDetail` 的注释），这里不再多复制一份 std::string。
			reading.namedPath = std::strstr(e.what(), expectedPath) != nullptr;
			std::snprintf(reading.actual, sizeof(reading.actual), "ca_io::io_error：%s", e.what());
		}
		catch (std::exception const& e)
		{
			std::snprintf(reading.actual, sizeof(reading.actual), "非 ca_io::io_error 的异常：%s", e.what());
		}
		catch (...)
		{
			// 这个分支也会接住**模块 DLL 抛来的 std 异常**：本可执行文件的目标带
			// `-D_HAS_EXCEPTIONS=0`（从 Jolt 的接口定义继承），那个模式下 `std::exception` 是
			// `<exception>` 里的 `stdext::exception`（名字不同、布局也不同），所以
			// `catch (std::exception const&)` 匹配不上 DLL 抛的 `std::filesystem::filesystem_error`。
			// `ca_io::io_error` 不受影响：它自己的类型名两边一致，上面的第一个 catch 按名字就能接住。
			std::snprintf(reading.actual, sizeof(reading.actual), "未知类型的异常（不是 ca_io::io_error）");
		}
		return reading;
	}

	// 写一个指定字节数的文件：内容按序号填（第 i 个字节 = i），便于核对读到的位置对不对。
	bool WriteProbeFile(std::filesystem::path const& path, std::size_t size)
	{
		std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!out.is_open())
			return false;
		for (std::size_t i = 0; i < size; ++i)
		{
			const char byte = static_cast<char>(i);
			out.write(&byte, 1);
		}
		out.close();
		return true;
	}

	// 判据失败时也要把临时文件删掉：抛异常退出时由析构负责。
	struct TempDirGuard
	{
		std::filesystem::path root;
		~TempDirGuard()
		{
			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
		}
	};
} // namespace

void TestIOErrorChannel(TestContext& ctx)
{
	auto* pIOManager = ctx.pModuleManager->GetInstance<ca_io::IOManager>();
	if (pIOManager == nullptr)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "取不到 ca_io::IOManager（IOManager_FS 模块没加载成功）");
		throw std::runtime_error("E_IO_NO_MODULE");
	}
	if (ctx.pResourceManagingSystem == nullptr)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "取不到 ResourceManagingSystem（资源模块没加载成功）");
		throw std::runtime_error("E_IO_NO_RESOURCE_SYSTEM");
	}

	struct SubCheck
	{
		char const* id = "";
		char const* subject = "";
		bool passed = false;
		char reading[384] = {};
	};
	std::vector<SubCheck> checks;
	auto addCheck = [&checks](char const* id, char const* subject, bool passed, char const* reading) {
		SubCheck check;
		check.id = id;
		check.subject = subject;
		check.passed = passed;
		std::snprintf(check.reading, sizeof(check.reading), "%s", reading);
		checks.push_back(check);
	};

	TempDirGuard tempGuard;
	tempGuard.root = std::filesystem::temp_directory_path() / "io_error_channel_probe";
	std::error_code ignored;
	std::filesystem::remove_all(tempGuard.root, ignored);
	std::filesystem::create_directories(tempGuard.root, ignored);

	// ---- 1.2 创建期探测：不存在的路径必须在 `Batch()` 就抛 `io_error` 并点名路径 ----
	{
		std::string const missingText = (tempGuard.root / "missing_input.bin").string();
		const IoThrowReading reading = ProbeThrow(missingText.c_str(), [&]() {
			auto batch = pIOManager->Batch(missingText);
		});
		addCheck("1.2", "不存在的路径在 IOManager::Batch 抛 ca_io::io_error 且点名路径", reading.Passed(),
		         reading.actual);
	}

	// ---- 1.3 越界读：拒绝并报告 false，且批内位置不推进（用「拒绝之后再读一段必须仍被接受」核位置）----
	{
		std::filesystem::path const filePath = tempGuard.root / "sixteen_bytes.bin";
		bool firstAccepted = false;
		bool overrunRejected = false;
		bool positionKept = false;
		bool headCorrect = false;
		bool tailCorrect = false;
		char readingText[384] = {};
		if (!WriteProbeFile(filePath, 16))
		{
			std::snprintf(readingText, sizeof(readingText), "构造失败：写不出 16 字节临时文件 %s",
			              filePath.string().c_str());
		}
		else
		{
			std::string const fileText = filePath.string();
			try
			{
				auto batch = pIOManager->Batch(fileText);
				unsigned char head[8] = {};
				unsigned char overrunTarget[16] = {}; // 越界那次的目标单独一块，旧实现真写进来也不越界
				unsigned char tail[8] = {};
				firstAccepted = batch->Read(8, head);
				overrunRejected = !batch->Read(16, overrunTarget);
				positionKept = batch->Read(8, tail);
				batch->SubmitAndWait();
				headCorrect = true;
				tailCorrect = true;
				for (std::size_t i = 0; i < 8; ++i)
				{
					headCorrect = headCorrect && head[i] == static_cast<unsigned char>(i);
					tailCorrect = tailCorrect && tail[i] == static_cast<unsigned char>(i + 8);
				}
				std::snprintf(readingText, sizeof(readingText),
				              "首读 8 字节被接受=%d、越界读 16 字节被拒=%d、拒绝后再读 8 字节仍被接受（位置未推进）=%d、"
				              "头 8 字节内容符合=%d、尾 8 字节内容符合=%d",
				              firstAccepted ? 1 : 0, overrunRejected ? 1 : 0, positionKept ? 1 : 0,
				              headCorrect ? 1 : 0, tailCorrect ? 1 : 0);
			}
			catch (std::exception const& e)
			{
				std::snprintf(readingText, sizeof(readingText), "意外抛出异常：%s", e.what());
			}
			catch (...)
			{
				std::snprintf(readingText, sizeof(readingText), "意外抛出未知类型的异常");
			}
		}
		addCheck("1.3",
		         "越界读返回 false、批内位置不推进、已接受的读仍读得到正确字节",
		         firstAccepted && overrunRejected && positionKept && headCorrect && tailCorrect, readingText);
	}

	// ---- 1.4① 提交期打不开：先排队一次读，再删掉源文件，`SubmitAndWait` 必须在调用线程抛 `io_error` ----
	{
		std::filesystem::path const filePath = tempGuard.root / "deleted_after_batch.bin";
		std::string const fileText = filePath.string();
		bool const created = WriteProbeFile(filePath, 16);
		if (!created)
		{
			addCheck("1.4①", "排队读之后删除源文件，SubmitAndWait 抛 ca_io::io_error 点名路径", false,
			         "构造失败：写不出 16 字节临时文件");
		}
		else
		{
			const IoThrowReading reading = ProbeThrow(fileText.c_str(), [&]() {
				auto batch = pIOManager->Batch(fileText);
				unsigned char buffer[16] = {};
				batch->Read(16, buffer); // 必须先排队：空批在 SubmitAndWait 首行就返回
				std::error_code removeError;
				std::filesystem::remove(filePath, removeError);
				batch->SubmitAndWait();
			});
			addCheck("1.4①", "排队读之后删除源文件，SubmitAndWait 抛 ca_io::io_error 点名路径", reading.Passed(),
			         reading.actual);
		}
	}

	// ---- 1.4② 提交期短读（TOCTOU）：创建期尺寸 16，提交前截短到 8，`gcount()` 检查必须抓到 ----
	{
		std::filesystem::path const filePath = tempGuard.root / "truncated_after_batch.bin";
		std::string const fileText = filePath.string();
		bool const created = WriteProbeFile(filePath, 16);
		if (!created)
		{
			addCheck("1.4②", "排队读之后把源文件截短到 8 字节，SubmitAndWait 抛 ca_io::io_error 点名路径", false,
			         "构造失败：写不出 16 字节临时文件");
		}
		else
		{
			const IoThrowReading reading = ProbeThrow(fileText.c_str(), [&]() {
				auto batch = pIOManager->Batch(fileText);
				unsigned char buffer[16] = {};
				batch->Read(16, buffer); // 创建期尺寸 16，越界检查放行
				std::error_code resizeError;
				std::filesystem::resize_file(filePath, 8, resizeError);
				batch->SubmitAndWait();
			});
			addCheck("1.4②", "排队读之后把源文件截短到 8 字节，SubmitAndWait 抛 ca_io::io_error 点名路径",
			         reading.Passed(), reading.actual);
		}
	}

	// ---- 1.5 写目标打不开：父目录不存在，`SubmitAndWait` 必须在调用线程抛 `io_error` 点名路径 ----
	{
		std::string const unwritableText = (tempGuard.root / "no_such_dir" / "output.bin").string();
		const IoThrowReading reading = ProbeThrow(unwritableText.c_str(), [&]() {
			auto batch = pIOManager->WriteBatch(unwritableText);
			unsigned char payload[16] = {};
			batch->Write(16, payload); // 必须先排队：空批在 SubmitAndWait 首行就返回
			batch->SubmitAndWait();
		});
		addCheck("1.5", "写目标打不开（父目录不存在）时 SubmitAndWait 抛 ca_io::io_error 点名路径",
		         reading.Passed(), reading.actual);
	}

	// ---- 5.1 资源层透出：资源字典里没有、磁盘上也不存在的资产路径，加载必须抛 `io_error` 点名路径 ----
	{
		char const* const kMissingAssetName = "__io_error_channel_missing__.res";
		const IoThrowReading reading = ProbeThrow(kMissingAssetName, [&]() {
			cacore::PathHash const key{ castl::string(kMissingAssetName) };
			ctx.pResourceManagingSystem->GetOrLoadResource<MissingAssetResourceProbe>(key);
		});
		addCheck("5.1", "经 GetOrLoadResource 加载缺失资产路径抛 ca_io::io_error 点名该路径", reading.Passed(),
		         reading.actual);
	}

	// ---- 汇总：逐条打印读数，再统一判红绿 ----
	std::string failedIds;
	for (auto const& check : checks)
	{
		CA_LOG("[I/O 错误通道 {}] {}：{} ⇒ {}", check.id, check.subject,
		       check.passed ? "判据成立" : "判据不成立", check.reading);
		if (!check.passed)
		{
			failedIds += check.id;
			failedIds += " ";
		}
	}
	if (!failedIds.empty())
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail),
		              "I/O 错误通道判据不成立：%s（逐条读数见 test_output/TestIOErrorChannel.log）",
		              failedIds.c_str());
		throw std::runtime_error("E_IO_ERROR_CHANNEL");
	}
	CA_LOG("TestIOErrorChannel 通过：{} 条判据全部成立（1.2 / 1.3 / 1.4① / 1.4② / 1.5 / 5.1）", checks.size());
}

// ---- Helpers (added for diagnostics) ----

static void createDirectoryIfNeeded(const char* path)
{
	CreateDirectoryA(path, NULL);
}

// 任务 2.1（design D3）：headless 跑前只清 `test_output/*.log` —— 同名日志跨运行 / 跨后端串味是病根；
// json/png 是跨运行累积产物（C# 像素判据多 run 对账、跨后端 PNG 对比依赖它们），不能删。
static int CleanupStaleLogs(char const* path)
{
	int removed = 0;
	std::error_code ec;
	std::filesystem::directory_iterator entry(path, ec);
	if (ec)
	{
		return 0;
	}
	for (auto const& item : entry)
	{
		if (!item.is_regular_file(ec))
		{
			ec.clear();
			continue;
		}
		if (item.path().extension() != ".log")
		{
			continue;
		}
		std::error_code removeError;
		if (std::filesystem::remove(item.path(), removeError) && !removeError)
		{
			++removed;
		}
	}
	return removed;
}

// `g_errorDetail`（失败细节的静态缓冲，理由见它自己的声明处）声明在 `TestIOErrorChannel` 之前，
// 这里直接用；`appendJsonResult` 按 `char*` 把它写进结果文件。
static void appendJsonResult(std::ofstream& jsonFile, bool& isFirst,
	const char* testName, const char* status,
	long long durationMs, int exitCode, const char* error)
{
	if (!jsonFile.is_open()) return;

	if (isFirst)
	{
		jsonFile << "[\n";
		isFirst = false;
	}
	else
	{
		jsonFile << ",\n";
	}

	jsonFile << "  {\"name\":\"" << testName
		<< "\",\"status\":\"" << status
		<< "\",\"duration_ms\":" << durationMs
		<< ",\"exit_code\":" << exitCode;
	if (error)
	{
		jsonFile << ",\"error\":\"" << error << "\"";
	}
	jsonFile << "}";
	jsonFile.flush();
}

static void closeJsonArray(std::ofstream& jsonFile, bool isFirst)
{
	if (!jsonFile.is_open()) return;
	if (isFirst)
	{
		jsonFile << "[\n";
	}
	jsonFile << "\n]\n";
	jsonFile.close();
}

// ---- Entry Point ----

// 层 ②：C# 宿主可用性（docs/testing.md 的两层测试里的第 ② 层）。
// 只断言进程内状态；"坏的时候会不会响"由 Tools/check_csharp_host_failures.py 在层 ③ 覆盖。
// 怎么把它弄红：把 Host.cs 里 Tick 的 `return s_tickCount;` 改成 `return 0;` —— 只有这一条会红。
void TestScriptHost(TestContext& ctx)
{
	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		throw std::runtime_error("IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		throw std::runtime_error(std::string("托管侧不可用：") + pScriptHost->LastError());
	}
	if (pScriptHost->LastError()[0] != '\0')
	{
		throw std::runtime_error(std::string("托管侧可用但 LastError 非空：") + pScriptHost->LastError());
	}

	int32_t first = pScriptHost->Tick(0.016);
	if (first != 1)
	{
		throw std::runtime_error("第一次跨边界 Tick 期望回值 1，实际 " + std::to_string(first));
	}

	int32_t second = pScriptHost->Tick(0.016);
	if (second != 2)
	{
		throw std::runtime_error("第二次跨边界 Tick 期望回值 2（证明状态在托管侧保留了），实际 " +
			std::to_string(second));
	}

	if (pScriptHost->LastError()[0] != '\0')
	{
		throw std::runtime_error(std::string("两次 Tick 之后 LastError 非空：") + pScriptHost->LastError());
	}

	CA_LOG("TestScriptHost 通过：IsAvailable=true、Tick 回值 1 → 2、LastError 为空");
}

// 端到端：C# 侧建窗（`csharp-window-creation` 任务 2.7）。
// 与 TestScriptHost 的区别是它**真的建一个窗口**并把两个实例都登记给 CSharpHost：
// 编号 1 = IWindow（IWindow 的方法表）、编号 2 = IWindowSystem（NewWindow / GetWindowCount 那张表）。
// 不登记实例时 `OnGetInterfaceApi` 一律返回 0，托管侧只能读到「写入 0 字节、实例 0」——
// 所以「登记过」本身就是这条测试要证明的东西。
void TestCSharpWindow(TestContext& ctx)
{
	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		throw std::runtime_error("IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		throw std::runtime_error(std::string("托管侧不可用：") + pScriptHost->LastError());
	}

	// 基线必须在建窗**之前**读，否则「比基线 +1」这句话没有对照。
	int32_t const countBaseline = ctx.pWindowSystem->GetWindowCount();
	auto newWindow = ctx.pWindowSystem->NewWindow(800, 600, "CSharp Window");
	if (newWindow.expired())
	{
		throw std::runtime_error("IWindowSystem::NewWindow 返回了空弱引用");
	}
	auto window = newWindow.lock();

	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, window.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	// 任务 3.2：显示器读数的两侧探针。C++ 侧每次读都打一行固定格式；托管侧在同一个 Tick 里用
	// 生成的 `GetMonitor` 表面读同一个编号并打自己那行，`Tools/check_window_system_bindings.py`
	// 逐字段对拍（两个矩形 + DPI）；负数与不小于数量的编号必须抛 `IWindowSystemException`（1006）。
	{
		// 显示器列表在 `UpdateSystem` 里刷新（`UpdateMonitors`）；不先泵一帧，两侧都只会读到 0 个显示器。
		ctx.pWindowSystem->UpdateSystem();
		auto logMonitor = [](char const* tag, int monitorId, cawindow::MonitorInfo const& info) {
			CA_LOG("[C++ GetMonitor] {} id={} rect=({},{},{},{}) work=({},{},{},{}) dpi={}", tag, monitorId,
			       info.m_MonitorRect.x, info.m_MonitorRect.y, info.m_MonitorRect.width, info.m_MonitorRect.height,
			       info.m_WorkAreaRect.x, info.m_WorkAreaRect.y, info.m_WorkAreaRect.width,
			       info.m_WorkAreaRect.height, info.m_DPIScale);
		};
		const int monitorCount = ctx.pWindowSystem->GetMonitorCount();
		CA_LOG("[C++ GetMonitor] count={}", monitorCount);
		if (monitorCount > 0)
		{
			logMonitor("probe=1", 0, ctx.pWindowSystem->GetMonitor(0));
		}
		else
		{
			CA_LOG("[C++ GetMonitor] 没有显示器，跳过 0 号读数");
		}
		// 原生实现对越界编号用 `castl::clamp`，这里把它的真实行为记下来：绑定层 MUST 在调用原生
		// 实现之前拦下 -1，所以下面这行读数只用于证明"原生确实会返回另一个显示器"，不是期望结果。
		logMonitor("clamp", -1, ctx.pWindowSystem->GetMonitor(-1));
	}

	int cppWidth = 0;
	int cppHeight = 0;
	window->GetWindowSize(cppWidth, cppHeight);
	CA_LOG("C++ 侧窗口尺寸 = {} x {}（托管侧随后用同一张表读一次；不登记实例这一段读不到）", cppWidth,
	       cppHeight);
	// 任务 5.4：这一份实现归属哪个窗口系统，必须在窗口还活着的时候读（下面 `window.reset()` 之后就没了）
	void* const realWindowOwner = window->GetWindowSystem();

	// 任务 2.3：脚本在**进程的第一次成功绑定之后**建一个常驻窗口，只建一次。
	// 所以这个加号分两种情况（全量跑时脚本早在更早的测试里就建过窗了）：
	//   - 这一 Tick 是进程的第一次 Tick（回值 1）⇒ 脚本在这里建窗，计数必须 +1；
	//   - 回值 > 1 ⇒ 脚本已经在更早的 Tick 里建过，这里**不该**再 +1（+1 才说明它重复建窗了）。
	int32_t const countBeforeScript = ctx.pWindowSystem->GetWindowCount();

	int32_t const scriptTickValue = pScriptHost->Tick(0.016);
	if (pScriptHost->LastError()[0] != '\0')
	{
		// 先把跨边界的文字拷成**具名** std::string 再拼、再抛（任务 4.4）：
		// 直接写成 `std::runtime_error(std::string("…") + pScriptHost->LastError())` 时，
		// 异常里的消息在出口处已经变成 0xDD（MSVC debug 堆的"已释放"填充），
		// 实测 `e.what()` 与结果文件里的 error 字段都是乱码，而 LastError() 自己返回的
		// 62 字节是对的（`E5 8F 96 …` = "取 IWin…"）。分开写让临时量的生命周期明确。
		// 实测：短于 SSO 阈值的消息正常显示，长的变成 0xDD —— 根因是 mimalloc 与 ucrtbase
		// 两个堆共存（启动时那句 `mimalloc-redirect.dll seems to be initialized after ucrtbase.dll`）。
		// 所以跨 catch 边界只抛一句短码，详细文字走静态缓冲 `g_errorDetail`，报告直接按 char* 写出。
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "建窗 + Tick 之后 LastError 非空：%s",
		              pScriptHost->LastError());
		throw std::runtime_error("E_HANDLE_TABLE");
	}

	// 任务 2.3：脚本只建一次窗。这一 Tick 是进程第一次 Tick（回值 1）时计数必须 +1；
	// 否则说明脚本已在更早的 Tick 里建过，这里再加就说明它重复建窗了。两种情况都不许回落。
	{
		int32_t const countAfterScript = ctx.pWindowSystem->GetWindowCount();
		ctx.pWindowSystem->UpdateSystem();
		int32_t const countAfterScriptUpdate = ctx.pWindowSystem->GetWindowCount();
		bool const firstTick = scriptTickValue == 1;
		int32_t const expected = firstTick ? countBeforeScript + 1 : countBeforeScript;
		CA_LOG("脚本建窗计数：建窗前 {} → Tick(回值 {}) 之后 {} / UpdateSystem 之后 {}（{}）", countBeforeScript,
		       scriptTickValue, countAfterScript, countAfterScriptUpdate,
		       firstTick ? "本 Tick 是进程第一次 Tick，期望 +1" : "脚本已在更早的 Tick 建过，期望不涨");
		if (countAfterScript != expected)
		{
			std::snprintf(g_errorDetail, sizeof(g_errorDetail),
			              "脚本建窗计数不符：Tick 回值 %d（%s）、Tick 之后 = %d、建窗前 = %d（期望 %d）", scriptTickValue,
			              firstTick ? "进程第一次 Tick，期望 +1" : "脚本早建过，期望不涨", countAfterScript,
			              countBeforeScript, expected);
			throw std::runtime_error("E_SCRIPT_WINDOW");
		}
		if (countAfterScriptUpdate != countAfterScript)
		{
			std::snprintf(g_errorDetail, sizeof(g_errorDetail),
			              "脚本建的窗口被回收了：UpdateSystem 之后 = %d、建窗后 = %d（脚本没关窗，不许回落）",
			              countAfterScriptUpdate, countAfterScript);
			throw std::runtime_error("E_SCRIPT_KEEP");
		}
	}

	CA_LOG("TestCSharpWindow 通过：窗口已登记（编号 1 = IWindow、编号 2 = IWindowSystem），"
	       "两侧尺寸读数见 test_output/TestCSharpWindow.log");

	// 任务 2.8：关窗与回收。回收**只发生在 `UpdateSystem` 里**，不显式调它就永远回不到基线。
	int32_t const countAfterCreate = ctx.pWindowSystem->GetWindowCount();
	window->CloseWindow();
	bool const shouldCloseCpp = window->WindowShouldClose();
	// 回收条件是 `use_count() == 1 && WindowShouldClose()` —— 测试自己手里这条 shared_ptr
	// 就是"多出来的那一个"：不放掉它，计数永远回不到基线（实测：不放掉 → 建窗后 1、回收后仍 1）。
	window.reset();
	CA_LOG("关窗：建窗后计数 = {}、C++ 侧 WindowShouldClose = {}（泵帧前，两侧都应为真）",
	       countAfterCreate, shouldCloseCpp ? 1 : 0);
	pScriptHost->Tick(0.016);
	int32_t const countAfterPump = ctx.pWindowSystem->GetWindowCount();
	ctx.pWindowSystem->UpdateSystem();
	int32_t const countAfterUpdate = ctx.pWindowSystem->GetWindowCount();
	CA_LOG("关窗后计数：基线 {} → 建窗后 {} / 泵帧后 {} / UpdateSystem 之后 {}", countBaseline, countAfterCreate, countAfterPump,
	       countAfterUpdate);
	if (!shouldCloseCpp)
	{
		throw std::runtime_error("CloseWindow 之后 C++ 侧 WindowShouldClose 仍为假");
	}
	if (countAfterUpdate >= countAfterCreate)
	{
		throw std::runtime_error("UpdateSystem 之后窗口计数没有回落（建窗后 " +
			std::to_string(countAfterCreate) + "、回收后 " + std::to_string(countAfterUpdate) + "）");
	}

	// 任务 5.5：**表面建的窗口**走一整圈 —— 托管侧在第三次 Tick 里 `NewWindow` → 读尺寸 → 关窗 →
	// `Dispose`，引擎侧只看得到这里的计数：建出来的那一个让计数 +1，泵帧 + UpdateSystem 之后回基线。
	// 这一段刻意不再裸调 C ABI 入口（建窗 / 解句柄 / 读尺寸 / 关窗 / 读错误码 全部改由托管表面做）——
	// 那几条入口由 `Test/CoreTests` 的绑定用例直接对着产物验，这里只验"两侧读数一致"。
	//
	// `csharp-visible-window` 任务 1.1：脚本（`UserScript.OnEngineReady`）会建一个**常驻**窗口，从第一次
	// Tick 起就一直在计数里。所以下面两条断言的基线必须**就地重读**，而且不能再取"第一次 Tick 之后"
	// —— 那时引擎自己那个 800×600 窗口还在（它要到下面这次 `UpdateSystem()` 之后才被回收），基线会把
	// 它算进去，`+1` 就永远差 1。取在这里：引擎窗口已回收、脚本窗（若已建）已在计数里。
	int32_t const countSurfaceBaseline = ctx.pWindowSystem->GetWindowCount();
	pScriptHost->Tick(0.016);
	int32_t const countAfterSurfaceTick = ctx.pWindowSystem->GetWindowCount();
	ctx.pWindowSystem->UpdateSystem();
	int32_t const countAfterSurfaceUpdate = ctx.pWindowSystem->GetWindowCount();
	CA_LOG("表面建窗计数：基线 {} → Tick #3 之后 {} / UpdateSystem 之后 {}（托管侧在这一次 Tick 里建窗 + 读尺寸 + 关窗 + 释放）",
	       countSurfaceBaseline, countAfterSurfaceTick, countAfterSurfaceUpdate);
	if (pScriptHost->LastError()[0] != '\0')
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "表面建窗那一次 Tick 之后 LastError 非空：%s",
		              pScriptHost->LastError());
		throw std::runtime_error("E_SURFACE_WINDOW");
	}
	if (countAfterSurfaceTick != countSurfaceBaseline + 1)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail),
		              "托管侧通过表面建的窗口没有出现在计数里：Tick #3 之后 = %d、基线 = %d（期望 +1）",
		              countAfterSurfaceTick, countSurfaceBaseline);
		throw std::runtime_error("E_SURFACE_COUNT");
	}
	if (countAfterSurfaceUpdate != countSurfaceBaseline)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail),
		              "表面建的窗口没有被回收：UpdateSystem 之后计数 = %d、基线 = %d（期望回基线）",
		              countAfterSurfaceUpdate, countSurfaceBaseline);
		throw std::runtime_error("E_SURFACE_RECYCLE");
	}

	// 任务 5.4：同一个接口登记**两份实现**，按实例名各取各的，互不影响。
	// 判据三条：① 按名取到的指针就是各自那一份；② 第二份上的窗口 `GetWindowSystem()` 回它自己
	// （指针相等）；③ 第一份不受影响 —— 按第一个实例名取到的仍是它，默认那一份也是它。
	// 失败细节一律写进 `g_errorDetail`、只抛短码（同上）。
	{
		fake_window::System fakeSystem;
		pScriptHost->SetInterfaceInstance(2, kFakeWindowSystemInstanceName, &fakeSystem);
		void* const realByName = pScriptHost->GetInterfaceInstance(2, kRealWindowSystemInstanceName);
		void* const fakeByName = pScriptHost->GetInterfaceInstance(2, kFakeWindowSystemInstanceName);
		if (realByName != ctx.pWindowSystem || fakeByName != &fakeSystem)
		{
			std::snprintf(g_errorDetail, sizeof(g_errorDetail),
			              "按实例名取实例取错了：真实那份 = %p（期望 %p）、第二份 = %p（期望 %p）", realByName,
			              static_cast<void*>(ctx.pWindowSystem), fakeByName, static_cast<void*>(&fakeSystem));
			throw std::runtime_error("E_INSTANCE_NAME");
		}
		if (pScriptHost->GetInterfaceInstance(2, nullptr) != ctx.pWindowSystem)
		{
			std::snprintf(g_errorDetail, sizeof(g_errorDetail),
			              "默认那一份被第二份登记影响了（应仍是最先登记的真实窗口系统 %p，实际 %p）",
			              static_cast<void*>(ctx.pWindowSystem), pScriptHost->GetInterfaceInstance(2, nullptr));
			throw std::runtime_error("E_INSTANCE_DEFAULT");
		}
		auto fakeWindow = fakeSystem.NewWindow(320, 240, "Fake Window", true, true, true, false).lock();
		if (!fakeWindow)
		{
			throw std::runtime_error("E_FAKE_WINDOW");
		}
		if (fakeWindow->GetWindowSystem() != &fakeSystem)
		{
			std::snprintf(g_errorDetail, sizeof(g_errorDetail),
			              "第二份上的窗口，GetWindowSystem() 回的是 %p，期望第二份 %p",
			              static_cast<void*>(fakeWindow->GetWindowSystem()), static_cast<void*>(&fakeSystem));
			throw std::runtime_error("E_FAKE_OWNER");
		}
		if (realWindowOwner != ctx.pWindowSystem)
		{
			std::snprintf(g_errorDetail, sizeof(g_errorDetail),
			              "第一份上的窗口，GetWindowSystem() 回的是 %p，期望第一份 %p —— 两份实现串了",
			              realWindowOwner, static_cast<void*>(ctx.pWindowSystem));
			throw std::runtime_error("E_REAL_OWNER");
		}
		CA_LOG("多实例读数：按名取到真实 = {} / 第二份 = {}；第二份窗口的 GetWindowSystem() 指向第二份，"
		       "第一份与默认那一份都不受影响",
		       static_cast<void const*>(realByName), static_cast<void const*>(fakeByName));
	}

	// 任务 4.2 的第二条：**未知接口编号不许静默返回 0**。
	// 造一次 `interfaceId = 9999` 的取表调用：返回值必须是 0，且 `LastError()` 里必须说明原因。
	// （原来用 9：那时 9 还没接线；2.3/2.4 把原生五类接到 8–12 之后 9 = GPUGraph 已是登记过的目标类型表，
	//   探针会撞上"版本/角色不符"的另一条错误文本。9999 不在登记表里，判据保持"未知编号必须点名"。）
	{
		uint32_t const rejected = pScriptHost->GetInterfaceApiForTest(9999u, nullptr, 1u, 0u, nullptr, nullptr);
		if (rejected != 0u)
		{
			throw std::runtime_error("未知接口编号 9 竟然取表成功（返回 " + std::to_string(rejected) + "）");
		}
		std::string const hostError = pScriptHost->LastError();
		if (hostError.find("未知接口编号") == std::string::npos)
		{
			throw std::runtime_error("未知接口编号 9 被静默拒绝：LastError() 里没有说明，实际内容 = \"" + hostError +
			                         "\"");
		}
		CA_LOG("未知接口编号读数：返回 0、LastError = {}", pScriptHost->LastError());
	}
}

// ---- 托管读数行与绑定对象句柄解析（csharp-native-direct-binding 4.1）----
// 桥退役后 C++ 用例不能再 dynamic_cast 适配器对象；改为读托管侧打出的机器可读读数行
// （`[托管 读数] ...`），并按行里的对象句柄从**进程级强句柄表**解析出真实的 `GPUGraph` /
// `GPUTexture` / `WindowHandle` —— 与托管侧持有的是同一个对象（同一张表、同一代际），
// 结构断言（pass 数、参数表、呈现清单、顶点流）与像素断言的强度不变。
namespace
{
	struct HostProbeReadings
	{
		std::map<std::string, std::string> values;
		std::string reject;
		bool found = false;

		bool Has(char const* key) const
		{
			return values.find(key) != values.end();
		}

		std::string const& Get(char const* key) const
		{
			static const std::string empty;
			auto it = values.find(key);
			return it == values.end() ? empty : it->second;
		}

		uint64_t U64(char const* key) const
		{
			return std::strtoull(Get(key).c_str(), nullptr, 10);
		}

		int64_t I64(char const* key) const
		{
			return std::strtoll(Get(key).c_str(), nullptr, 10);
		}
	};

	// 从本次用例自己的日志里读最后一条 `probe=<名字>` 的读数行（stdout 已被
	// runTestWithDiagnostics 重定向到 test_output/<用例名>.log）。
	bool ReadHostProbeReadings(char const* logPath, char const* probe, HostProbeReadings* out)
	{
		std::fflush(stdout);
		std::ifstream file(logPath);
		if (!file)
		{
			return false;
		}
		const std::string marker = std::string(" probe=") + probe + " ";
		std::string line;
		while (std::getline(file, line))
		{
			if (line.find("[托管 读数]") == std::string::npos || line.find(marker) == std::string::npos)
			{
				continue;
			}
			out->found = true;
			out->values.clear();
			out->reject.clear();
			const std::size_t rejectAt = line.find(" | reject=");
			const std::string head = rejectAt == std::string::npos ? line : line.substr(0, rejectAt);
			if (rejectAt != std::string::npos)
			{
				out->reject = line.substr(rejectAt + std::strlen(" | reject="));
			}
			std::istringstream stream(head);
			std::string token;
			while (stream >> token)
			{
				const std::size_t eq = token.find('=');
				if (eq == std::string::npos)
				{
					continue;
				}
				out->values[token.substr(0, eq)] = token.substr(eq + 1);
			}
		}
		return out->found;
	}

	// 从本次用例自己的日志里读 `[编辑器 读数] probe=editor ...` 行（editor-window-panel 1.2）：
	// 与上面的渲染读数同一套做法，只是标记行是编辑器自己的读数行。
	bool ReadEditorReadings(char const* logPath, HostProbeReadings* out)
	{
		std::fflush(stdout);
		std::ifstream file(logPath);
		if (!file)
		{
			return false;
		}
		const std::string marker = " probe=editor ";
		std::string line;
		out->found = false;
		while (std::getline(file, line))
		{
			if (line.find("[编辑器 读数]") == std::string::npos || line.find(marker) == std::string::npos)
			{
				continue;
			}
			out->found = true;
			out->values.clear();
			out->reject.clear();
			std::istringstream stream(line);
			std::string token;
			while (stream >> token)
			{
				const std::size_t eq = token.find('=');
				if (eq == std::string::npos)
				{
					continue;
				}
				out->values[token.substr(0, eq)] = token.substr(eq + 1);
			}
		}
		return out->found;
	}

	// 在本次用例自己的日志里找一行子串（M3：监听线程入队标记；M14/M11：服务禁用/关闭标记）。
	bool LogContains(char const* logPath, std::string const& needle)
	{
		std::fflush(stdout);
		std::ifstream file(logPath);
		if (!file)
		{
			return false;
		}
		std::string line;
		while (std::getline(file, line))
		{
			if (line.find(needle) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	// 取日志 / 读数文件里 `标记：值` 的行尾值（标记之后到行尾，去掉行尾空白）。
	// 用途：判据的期望值必须来自**服务自己解析出来的**那个值，而不是测试端另算一遍。
	// 背景（editor 批 1.11 遗留）：静态根从 CWD 相对改成绝对路径（程序集 / 进程目录下的 bin/editor-web）后，
	// `TestEditorWindowPanel` 仍用 `exists("editor-web/index.html")` 探测 —— cwd=仓库根时探测 false、
	// 服务却返 200，判据与服务实际行为脱节（全量回归里表现为 E_EDITOR_HTTP_PAGE）。改成读这条读数。
	bool ReadLoggedValue(char const* logPath, char const* marker, std::string* out)
	{
		std::fflush(stdout);
		std::ifstream file(logPath);
		if (!file)
		{
			return false;
		}
		const std::string needle = marker;
		std::string line;
		while (std::getline(file, line))
		{
			const std::size_t at = line.find(needle);
			if (at == std::string::npos)
			{
				continue;
			}
			std::string value = line.substr(at + needle.size());
			while (!value.empty() &&
			       (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
			{
				value.pop_back();
			}
			if (value.empty())
			{
				continue;
			}
			*out = value;
			return true;
		}
		return false;
	}

	// 句柄 = (代际 << 48) | 序号；测试进程里**没有**生成产物的句柄表（它在 CSharpHost 模块里），
	// 所以对象的核对走另一个口：托管读数行给出图的原生对象地址（对象由 C# 租约与图内引用保活），
	// 再用图里的附件 `ImageHandle` 拿到离屏纹理与窗口后台缓冲的 `shared_ptr` —— 与 C# 侧提交的
	// 是同一个对象（附件里的句柄在反序列化时从同一张对象表解析）。
	struct ProbeNativeObjects
	{
		graphics_backend::GPUGraph* graph = nullptr;
		graphics_backend::ImageHandle windowImage;
		graphics_backend::ImageHandle captureImage;
	};

	ProbeNativeObjects ResolveProbeObjects(HostProbeReadings const& readings)
	{
		ProbeNativeObjects objects;
		objects.graph = reinterpret_cast<graphics_backend::GPUGraph*>(
			static_cast<uintptr_t>(readings.U64("graphSelf")));
		if (objects.graph == nullptr)
		{
			return objects;
		}
		for (auto const& pass : objects.graph->GetRenderPasses())
		{
			for (auto const& attachment : pass.GetAttachments())
			{
				if (attachment.GetType() == graphics_backend::ImageHandle::ImageType::Backbuffer &&
					!objects.windowImage.IsValid())
				{
					objects.windowImage = attachment;
				}
				if (attachment.GetType() == graphics_backend::ImageHandle::ImageType::External &&
					!objects.captureImage.IsValid())
				{
					objects.captureImage = attachment;
				}
			}
		}
		return objects;
	}
}

// ---- TestCSharpTriangle（csharp-native-direct-binding 4.1）----
// C# 用自己的顶点字节、着色器路径、窗口与离屏目标建图并提交；原生侧按托管读数行里的句柄解析出
// 同一份 `GPUGraph` / `GPUTexture` / `WindowHandle`，读 pass 数、呈现清单与像素。
//
// Debug 自检模式（进程环境变量 `CASCADED_CSHARP_RENDER_SELFTEST`，C# 侧读同一份值）：
//   skip-draw 在调用层省略两次 `AddTrianglePass`（图 pass 数 2 → 0），以 `E_TRIANGLE_DRAW` 判红；
//   bad-vertex / skip-present / skip-wait 分别以 `E_TRIANGLE_PIXEL` / `E_TRIANGLE_PRESENT` /
//   `E_TRIANGLE_WAIT` 判红。
// `Tools/check_csharp_triangle.py` 逐后端对账正常（pass）与负向（fail + 对应错误码）。
void TestCSharpTriangle(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};

	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		fail("E_TRIANGLE_HOST", "IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		fail("E_TRIANGLE_HOST", castl::string("托管侧不可用：") + pScriptHost->LastError());
	}

	char modeBuffer[64] = {};
	::GetEnvironmentVariableA("CASCADED_CSHARP_RENDER_SELFTEST", modeBuffer, sizeof(modeBuffer));
	const std::string mode = modeBuffer;
	const bool skipDraw = mode == "skip-draw";
	const bool badVertex = mode == "bad-vertex";
	const bool skipPresent = mode == "skip-present";
	const bool skipWait = mode == "skip-wait";
	if (!(mode.empty() || skipDraw || badVertex || skipPresent || skipWait))
	{
		fail("E_TRIANGLE_MODE", castl::string("未知的自检模式：") + mode.c_str());
	}

	struct TriangleEnvGuard
	{
		~TriangleEnvGuard() { ::SetEnvironmentVariableA("CASCADED_CSHARP_RENDER_PROBE", nullptr); }
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_RENDER_PROBE", "1");

	// 窗口接口实例：托管探针用 `IWindowSystem.NewWindow` 建自己的窗口并从它取窗口目标。
	auto newWindow = ctx.pWindowSystem->NewWindow(320, 240, "C# Triangle");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_TRIANGLE_HOST", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	const std::string baselineError = pScriptHost->LastError();
	const int32_t tickValue = pScriptHost->Tick(0.016);
	::SetEnvironmentVariableA("CASCADED_CSHARP_RENDER_PROBE", nullptr);
	const std::string hostError = pScriptHost->LastError();
	if (hostError != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "Tick(回值 %d) 之后 LastError 非空：%s", tickValue,
		              hostError.c_str());
		fail("E_TRIANGLE_HOST_ERROR", detail);
	}
	if (tickValue <= 0)
	{
		fail("E_TRIANGLE_TICK", "Tick 回值不是正数（探针没有执行）");
	}

	HostProbeReadings readings;
	if (!ReadHostProbeReadings("test_output/TestCSharpTriangle.log", "triangle", &readings))
	{
		fail("E_TRIANGLE_READINGS", "托管读数行 `[托管 读数] probe=triangle` 没有出现在日志里");
	}

	// 本用例是**第一个**渲染探针用例，之前跑的 `TestScriptHost` / `TestCSharpWindow` 都没有设置
	// 任何渲染开关 —— 探针开始前的原生调用计数必须全为 0。
	{
		const int64_t preExec = readings.I64("preExec");
		const int64_t preCapture = readings.I64("preCapture");
		const int64_t preStruct = readings.I64("preStruct");
		const int64_t preAddBuffer = readings.I64("preAddBuffer");
		const int64_t preCreateTexture = readings.I64("preCreateTexture");
		const int64_t preUpload = readings.I64("preUpload");
		const int64_t preImage = readings.I64("preImage");
		const int64_t preSampler = readings.I64("preSampler");
		CA_LOG("非探针阶段读数（TestScriptHost / TestCSharpWindow 之后）：captureTarget={} "
		       "createShaderStruct={} addBuffer={} executeGraph={} createTexture={} 上传执行={} "
		       "setImage={} setSampler={}",
		       preCapture, preStruct, preAddBuffer, preExec, preCreateTexture, preUpload, preImage,
		       preSampler);
		if (preExec != 0 || preCapture != 0 || preStruct != 0 || preAddBuffer != 0 ||
			preCreateTexture != 0 || preUpload != 0 || preImage != 0 || preSampler != 0)
		{
			char detail[384] = {};
			std::snprintf(detail, sizeof(detail),
			              "非探针测试创建了 GPU 工作：captureTarget=%lld createShaderStruct=%lld "
			              "addBuffer=%lld executeGraph=%lld createTexture=%lld 上传执行=%lld "
			              "setImage=%lld setSampler=%lld",
			              static_cast<long long>(preCapture), static_cast<long long>(preStruct),
			              static_cast<long long>(preAddBuffer), static_cast<long long>(preExec),
			              static_cast<long long>(preCreateTexture), static_cast<long long>(preUpload),
			              static_cast<long long>(preImage), static_cast<long long>(preSampler));
			fail("E_RENDER_NONPROBE_WORK", detail);
		}
	}

	ProbeNativeObjects objects = ResolveProbeObjects(readings);
	if (objects.graph == nullptr)
	{
		fail("E_TRIANGLE_GRAPH", "读数行里的 graphSelf 解析不出一张原生 GPUGraph（图没有保留或被提前释放）");
	}
	const uint32_t passCount = static_cast<uint32_t>(objects.graph->GetRenderPasses().size());
	const auto& presents = objects.graph->GetFinalizePass().m_PresentBackBuffers;
	if (skipDraw)
	{
		// `skip-draw` 在调用层省略了两次 `AddTrianglePass`：图 pass 数必须为 0（不是零面积三角形），
		// 托管前置校验拒绝空图提交、原生执行计数 0。
		if (passCount != 0u || readings.I64("exec") != 0)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail),
			              "skip-draw 没有省略 AddTrianglePass：图 pass 数 %u、原生执行 %lld（期望 0 / 0）",
			              passCount, static_cast<long long>(readings.I64("exec")));
			fail("E_TRIANGLE_DRAW", detail);
		}
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-draw：图 pass 数 0、原生执行 0（正常模式 2），按预期失败");
		fail("E_TRIANGLE_DRAW", detail);
	}
	// 图里有 pass 才能从附件里拿到离屏纹理与窗口后台缓冲（skip-draw 的 0 pass 在上面的分支已经判红）。
	if (!objects.captureImage.IsValid() || !objects.windowImage.IsValid())
	{
		fail("E_TRIANGLE_TARGET", "图附件的资源引用里解析不出离屏纹理 / 窗口后台缓冲");
	}

	// 目标快照：离屏目标与窗口同尺寸、RGBA8；离屏引用是外部纹理、窗口引用是后台缓冲。
	const auto& captureDesc = objects.captureImage.GetExternalManagedTexture()->GetDescriptor();
	const auto& windowDesc = objects.windowImage.GetWindowHandle()->GetBackbufferDescriptor();
	const bool captureExternal =
		objects.captureImage.GetType() == ImageHandle::ImageType::External;
	const bool windowBackbuffer =
		objects.windowImage.GetType() == ImageHandle::ImageType::Backbuffer;
	if (windowDesc.width != 320u || windowDesc.height != 240u || captureDesc.width != 320u ||
		captureDesc.height != 240u || captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM ||
		!captureExternal || !windowBackbuffer)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_TRIANGLE_TARGET", detail);
	}

	constexpr uint32_t kExpectedPassCount = 2u;
	if (passCount != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              passCount, kExpectedPassCount);
		fail("E_TRIANGLE_DRAW", detail);
	}
	if (readings.I64("exec") != 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "原生执行入口调用增量 %lld，期望 1",
		              static_cast<long long>(readings.I64("exec")));
		fail("E_TRIANGLE_EXEC", detail);
	}

	// skip-wait：负向模式必须让等待计数不涨。
	if (skipWait)
	{
		ctx.pGPUBackend->WaitIdle(); // 清理仍在执行的 GPU 工作，再报负向失败
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "skip-wait：由本次提交引起的 WaitIdle 增量 %lld（期望 0）",
		              static_cast<long long>(readings.I64("wait")));
		fail("E_TRIANGLE_WAIT", detail);
	}
	if (readings.I64("wait") != 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：由本次提交引起的 WaitIdle 增量 %lld（期望 1）",
		              static_cast<long long>(readings.I64("wait")));
		fail("E_TRIANGLE_WAIT", detail);
	}

	const bool presentMatches = presents.size() == 1u &&
		presents[0] == objects.windowImage &&
		presents[0].GetType() == ImageHandle::ImageType::Backbuffer;
	if (skipPresent)
	{
		ctx.pGPUBackend->WaitIdle();
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-present：真实呈现清单 %zu 项（负向模式期望 0；%s）", presents.size(),
		              presents.empty() ? "呈现确实被省略" : "呈现没有被省略");
		fail("E_TRIANGLE_PRESENT", detail);
	}
	if (!presentMatches)
	{
		fail("E_TRIANGLE_PRESENT", "真实呈现清单不是唯一的窗口后台缓冲");
	}

	// 像素：既有 Readback 读取独立离屏目标；背景 (0,255,0,255)、中心 (255,0,0,255)。
	ctx.pGPUBackend->WaitIdle();
	const uint32_t width = captureDesc.width;
	const uint32_t height = captureDesc.height;
	std::vector<uint8_t> pixels(static_cast<size_t>(
		GetImageByteSize(width, height, ETextureFormat::E_R8G8B8A8_UNORM)));
	auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
	if (!token)
	{
		fail("E_TRIANGLE_READBACK", "离屏目标的 Readback 返回空 token");
	}
	token->Wait();
	auto pixelAt = [&pixels, width](uint32_t x, uint32_t y) {
		const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
		return std::array<uint8_t, 4>{ pixels[index], pixels[index + 1], pixels[index + 2],
			                          pixels[index + 3] };
	};
	const std::array<uint8_t, 4> background = pixelAt(1, 1);
	const std::array<uint8_t, 4> center = pixelAt(width / 2, height / 2);
	const castl::string pngPath = castl::string("test_output/TestCSharpTriangle_") + ctx.backendName + "_" +
		(mode.empty() ? "normal" : mode.c_str()) + ".png";
	stbi_write_png(pngPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(),
	               static_cast<int>(width * 4));
	CA_LOG("TestCSharpTriangle：backend={} mode={} size={}x{} wait={} exec={} presents={} "
	       "bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height,
	       static_cast<long long>(readings.I64("wait")),
	       static_cast<long long>(readings.I64("exec")), presents.size(), background[0], background[1],
	       background[2], background[3], center[0], center[1], center[2], center[3], pngPath.c_str());

	auto differs = [](std::array<uint8_t, 4> const& left, std::array<uint8_t, 4> const& right) {
		return left[0] != right[0] || left[1] != right[1] || left[2] != right[2] || left[3] != right[3];
	};
	if (badVertex)
	{
		const std::array<uint8_t, 4> pureRed{ 255, 0, 0, 255 };
		if (!differs(center, pureRed))
		{
			fail("E_TRIANGLE_PIXEL", "bad-vertex：中心仍是纯红（负向模式没有生效）");
		}
		fail("E_TRIANGLE_PIXEL", "bad-vertex：中心按预期不再是纯红");
	}
	const std::array<uint8_t, 4> expectedBackground{ 0, 255, 0, 255 };
	const std::array<uint8_t, 4> expectedCenter{ 255, 0, 0, 255 };
	if (differs(background, expectedBackground) || differs(center, expectedCenter))
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "像素不符：背景 (%u,%u,%u,%u)、中心 (%u,%u,%u,%u)", background[0], background[1],
		              background[2], background[3], center[0], center[1], center[2], center[3]);
		fail("E_TRIANGLE_PIXEL", detail);
	}
	CA_LOG("TestCSharpTriangle 通过：{} 后端背景与中心像素精确匹配", ctx.backendName);
}

// ---- TestCSharpConstantColor（csharp-native-direct-binding 4.1）----
// C# 用单 `POSITION` 顶点流 + `ConstantColor` 参数结构（3 个 `ColorSubStruct`）建图，把参数结构绑定到
// `constantColorBlock`，窗口与离屏各一个同逻辑 pass，提交并等待；原生侧按句柄解析同一份图，核对
// pass 数、每个 pass 的参数表、顶点流描述、呈现清单与像素（背景 (0,0,255,255)、中心 (255,0,0,255)）。
//
// Debug 负向（进程环境变量 `CASCADED_CSHARP_CONSTCOLOR_SELFTEST`，C# 侧读同一份值）：
//   `bad-struct-colors` 三个数组槽改成红/绿/蓝 → 中心不再是纯红，以 `E_CONSTCOLOR_PIXEL` 判红；
//   `skip-param` 完全不调 `SetPassParam` → 托管前置校验在进入后端执行前拒绝并点名 `constantColorBlock`，
//                原生以 `E_CONSTCOLOR_PARAM` 判红，**不读像素**（不对未定义内容编造期望）。
void TestCSharpConstantColor(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};

	// C++ 侧的独立夹具值：尺寸、pass 数、两个像素与块名都另写一份，不与 C# 的
	// `CSharpConstantColorProbe` 共享常量表 —— 一边改错不会两边一起绿。
	constexpr uint32_t kWindowWidth = 320u;
	constexpr uint32_t kWindowHeight = 240u;
	constexpr uint32_t kExpectedPassCount = 2u;
	const std::array<uint8_t, 4> expectedBackground{ 0u, 0u, 255u, 255u };
	const std::array<uint8_t, 4> expectedCenter{ 255u, 0u, 0u, 255u };
	char const* const kBlockName = "constantColorBlock";

	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		fail("E_CONSTCOLOR_HOST", "IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		fail("E_CONSTCOLOR_HOST", castl::string("托管侧不可用：") + pScriptHost->LastError());
	}

	char modeBuffer[64] = {};
	::GetEnvironmentVariableA("CASCADED_CSHARP_CONSTCOLOR_SELFTEST", modeBuffer, sizeof(modeBuffer));
	const std::string mode = modeBuffer;
	const bool badStructColors = mode == "bad-struct-colors";
	const bool skipParam = mode == "skip-param";
	if (!(mode.empty() || badStructColors || skipParam))
	{
		fail("E_CONSTCOLOR_MODE", castl::string("未知的自检模式：") + mode.c_str());
	}

	struct ConstantColorEnvGuard
	{
		~ConstantColorEnvGuard() { ::SetEnvironmentVariableA("CASCADED_CSHARP_CONSTCOLOR_PROBE", nullptr); }
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_CONSTCOLOR_PROBE", "1");

	// 托管探针用 `IWindowSystem.NewWindow` 建自己的窗口，并从它取窗口目标。
	auto newWindow = ctx.pWindowSystem->NewWindow(kWindowWidth, kWindowHeight, "C# ConstantColor");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_CONSTCOLOR_HOST", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	const std::string baselineError = pScriptHost->LastError();
	const int32_t tickValue = pScriptHost->Tick(0.016);
	::SetEnvironmentVariableA("CASCADED_CSHARP_CONSTCOLOR_PROBE", nullptr);
	const std::string hostError = pScriptHost->LastError();
	if (hostError != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "Tick(回值 %d) 之后 LastError 非空：%s", tickValue,
		              hostError.c_str());
		fail("E_CONSTCOLOR_HOST_ERROR", detail);
	}
	if (tickValue <= 0)
	{
		fail("E_CONSTCOLOR_TICK", "Tick 回值不是正数（探针没有执行）");
	}

	HostProbeReadings readings;
	if (!ReadHostProbeReadings("test_output/TestCSharpConstantColor.log", "constantcolor", &readings))
	{
		fail("E_CONSTCOLOR_READINGS", "托管读数行 `[托管 读数] probe=constantcolor` 没有出现在日志里");
	}

	ProbeNativeObjects objects = ResolveProbeObjects(readings);
	if (objects.graph == nullptr)
	{
		fail("E_CONSTCOLOR_GRAPH", "读数行里的 graph 句柄解析不出一张原生 GPUGraph");
	}
	if (!objects.captureImage.IsValid() || !objects.windowImage.IsValid())
	{
		fail("E_CONSTCOLOR_PROBE_MISSING", "读数行里的离屏纹理 / 窗口句柄解析不出原生对象");
	}

	// 窗口描述快照：离屏目标与窗口同尺寸、RGBA8；窗口目标是后台缓冲、离屏不是。
	const auto& windowDesc = objects.windowImage.GetWindowHandle()->GetBackbufferDescriptor();
	const auto& captureDesc = objects.captureImage.GetExternalManagedTexture()->GetDescriptor();
	if (windowDesc.width != kWindowWidth || windowDesc.height != kWindowHeight ||
		captureDesc.width != kWindowWidth || captureDesc.height != kWindowHeight ||
		captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM ||
		objects.captureImage.GetType() != ImageHandle::ImageType::External ||
		objects.windowImage.GetType() != ImageHandle::ImageType::Backbuffer)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_CONSTCOLOR_TARGET", detail);
	}

	const auto& passes = objects.graph->GetRenderPasses();
	const uint32_t passCount = static_cast<uint32_t>(passes.size());
	if (passCount != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              passCount, kExpectedPassCount);
		fail("E_CONSTCOLOR_GRAPH", detail);
	}
	size_t totalBoundParams = 0;
	uint32_t passesWithBlock = 0;
	for (auto const& pass : passes)
	{
		auto const& bound = pass.GetShaderStructs();
		totalBoundParams += bound.size();
		auto entry = bound.find(CANAME(kBlockName));
		if (entry != bound.end() && entry->second != nullptr)
		{
			++passesWithBlock;
		}
	}

	// 探针调用计数必须与 C# 的实际调用逐项对上（读数来自托管侧的进入原生实现之前的计数）。
	// 正常模式在像素判据之后还要跑两个**托管前置校验负例**（未知类型名 / 未登记着色器路径），
	// 其中"未登记路径"的负例要建一个真实参数结构 —— 所以正常模式比 skip-param 多 1 次创建。
	{
		const int64_t createStructDelta = readings.I64("createStruct");
		const int64_t setFloat3Delta = readings.I64("setFloat3");
		const int64_t setStructDelta = readings.I64("setStruct");
		const int64_t setPassParamDelta = readings.I64("setPassParam");
		const int64_t captureDelta = readings.I64("captureCount");
		const int64_t executeDelta = readings.I64("exec");
		const int64_t expectedCreateStruct = mode.empty() ? 5 : 4;
		const int64_t expectedPassParam = skipParam ? 0 : 2;
		const int64_t expectedExecute = skipParam ? 0 : 1;
		CA_LOG("TestCSharpConstantColor 调用计数：createStruct={} setFloat3={} setStruct={} "
		       "setPassParam={} captureTarget={} executeGraph={}",
		       static_cast<long long>(createStructDelta), static_cast<long long>(setFloat3Delta),
		       static_cast<long long>(setStructDelta), static_cast<long long>(setPassParamDelta),
		       static_cast<long long>(captureDelta), static_cast<long long>(executeDelta));
		if (createStructDelta != expectedCreateStruct || setFloat3Delta != 3 || setStructDelta != 3 ||
			setPassParamDelta != expectedPassParam || captureDelta != 1 ||
			executeDelta != expectedExecute)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "调用计数不符：createStruct=%lld/%lld setFloat3=%lld/3 setStruct=%lld/3 "
			              "setPassParam=%lld/%lld captureTarget=%lld/1 executeGraph=%lld/%lld",
			              static_cast<long long>(createStructDelta),
			              static_cast<long long>(expectedCreateStruct),
			              static_cast<long long>(setFloat3Delta),
			              static_cast<long long>(setStructDelta),
			              static_cast<long long>(setPassParamDelta),
			              static_cast<long long>(expectedPassParam),
			              static_cast<long long>(captureDelta),
			              static_cast<long long>(executeDelta),
			              static_cast<long long>(expectedExecute));
			fail("E_CONSTCOLOR_COUNTS", detail);
		}
	}

	if (skipParam)
	{
		// 提交前拒绝：参数表为空、托管错误文本点名块名、本次提交的原生执行增量为 0。
		const bool namedBlock = readings.reject.find(kBlockName) != std::string::npos;
		if (totalBoundParams != 0)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "skip-param：参数表仍有 %zu 条绑定", totalBoundParams);
			fail("E_CONSTCOLOR_PARAM", detail);
		}
		if (!namedBlock)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "skip-param：拒绝文本没有点名 %s，实际 = \"%s\"",
			              kBlockName, readings.reject.c_str());
			fail("E_CONSTCOLOR_PARAM", detail);
		}
		if (readings.I64("exec") != 0)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "skip-param：原生执行增量 %lld（期望 0）",
			              static_cast<long long>(readings.I64("exec")));
			fail("E_CONSTCOLOR_PARAM", detail);
		}
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-param：参数表 0 条、拒绝文本点名 %s、后端执行次数 0，按预期失败", kBlockName);
		fail("E_CONSTCOLOR_PARAM", detail);
	}

	if (totalBoundParams != kExpectedPassCount || passesWithBlock != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "参数表不符：%zu 条绑定、%u 个 pass 有 %s（期望 %u 个 pass 各 1 条）", totalBoundParams,
		              passesWithBlock, kBlockName, kExpectedPassCount);
		fail("E_CONSTCOLOR_PARAM", detail);
	}
	if (readings.I64("wait") != 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：WaitIdle 增量 %lld（期望 1）",
		              static_cast<long long>(readings.I64("wait")));
		fail("E_CONSTCOLOR_WAIT", detail);
	}
	// 顶点流：单 `POSITION` 布局。两个 pass 的顶点流描述都必须只有一条 POSITION 属性、偏移 0、步长 12。
	{
		uint32_t checkedBatches = 0;
		for (auto const& pass : passes)
		{
			for (auto const& batch : pass.GetDrawCallBatches())
			{
				++checkedBatches;
				if (batch.m_VertexInputDescs.size() != 1u)
				{
					fail("E_CONSTCOLOR_STREAM", "绘制批次里的顶点流不止一个");
				}
				auto const& streamDescriptor = batch.m_VertexInputDescs.begin()->second.Get();
				auto const& attributes = streamDescriptor.attributes;
				if (attributes.size() != 1u || attributes[0].semanticName != CANAME("POSITION") ||
					attributes[0].offset != 0u || streamDescriptor.stride != 12u)
				{
					char detail[256] = {};
					std::snprintf(detail, sizeof(detail),
					              "顶点流不是单 POSITION：属性 %zu、首个语义 %s、偏移 %u、步长 %u",
					              attributes.size(),
					              attributes.empty() ? "?" : attributes[0].semanticName.c_str(),
					              attributes.empty() ? 0u : attributes[0].offset, streamDescriptor.stride);
					fail("E_CONSTCOLOR_STREAM", detail);
				}
			}
		}
		if (checkedBatches != kExpectedPassCount)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "检查到 %u 个绘制批次，期望 %u", checkedBatches,
			              kExpectedPassCount);
			fail("E_CONSTCOLOR_STREAM", detail);
		}
	}
	const auto& presents = objects.graph->GetFinalizePass().m_PresentBackBuffers;
	if (presents.size() != 1u || presents[0] != objects.windowImage)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "真实呈现清单 %zu 项，期望唯一的窗口后台缓冲",
		              presents.size());
		fail("E_CONSTCOLOR_PRESENT", detail);
	}

	// 像素：既有 `Readback` 读取独立离屏目标。背景来自清除色 (0,0,1,1)、中心来自三槽同红。
	ctx.pGPUBackend->WaitIdle();
	const uint32_t width = captureDesc.width;
	const uint32_t height = captureDesc.height;
	std::vector<uint8_t> pixels(static_cast<size_t>(
		GetImageByteSize(width, height, ETextureFormat::E_R8G8B8A8_UNORM)));
	auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
	if (!token)
	{
		fail("E_CONSTCOLOR_READBACK", "离屏目标的 Readback 返回空 token");
	}
	token->Wait();
	auto pixelAt = [&pixels, width](uint32_t x, uint32_t y) {
		const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
		return std::array<uint8_t, 4>{ pixels[index], pixels[index + 1], pixels[index + 2],
			                          pixels[index + 3] };
	};
	const std::array<uint8_t, 4> background = pixelAt(1, 1);
	const std::array<uint8_t, 4> center = pixelAt(width / 2, height / 2);
	const castl::string pngPath = castl::string("test_output/TestCSharpConstantColor_") +
		ctx.backendName + "_" + (mode.empty() ? "normal" : mode.c_str()) + ".png";
	stbi_write_png(pngPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(),
	               static_cast<int>(width * 4));
	CA_LOG("TestCSharpConstantColor：backend={} mode={} size={}x{} wait={} presents={} passes={} "
	       "boundParams={} bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height,
	       static_cast<long long>(readings.I64("wait")), presents.size(), passCount, totalBoundParams,
	       background[0], background[1], background[2], background[3], center[0], center[1], center[2],
	       center[3], pngPath.c_str());

	auto differs = [](std::array<uint8_t, 4> const& left, std::array<uint8_t, 4> const& right) {
		return left[0] != right[0] || left[1] != right[1] || left[2] != right[2] || left[3] != right[3];
	};
	if (badStructColors)
	{
		// 三槽改成红/绿/蓝：中心不再是纯红，证明颜色确实来自 C# 写入的参数值。
		if (!differs(center, expectedCenter))
		{
			fail("E_CONSTCOLOR_PIXEL", "bad-struct-colors：中心仍是纯红（负向模式没有生效）");
		}
		fail("E_CONSTCOLOR_PIXEL", "bad-struct-colors：中心按预期不再是纯红");
	}
	if (differs(background, expectedBackground) || differs(center, expectedCenter))
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "像素不符：背景 (%u,%u,%u,%u)、中心 (%u,%u,%u,%u)", background[0], background[1],
		              background[2], background[3], center[0], center[1], center[2], center[3]);
		fail("E_CONSTCOLOR_PIXEL", detail);
	}
	CA_LOG("TestCSharpConstantColor 通过：{} 后端背景与中心像素精确匹配、两个 pass 各 1 条 {}", 
	       ctx.backendName, kBlockName);

	// 托管前置校验负例（design D6）：未知类型名与未登记着色器路径都必须在进入原生实现之前被拒绝。
	{
		const bool unknownOk = readings.Get("negUnknown") == "1";
		const bool pathOk = readings.Get("negPath") == "1";
		if (!unknownOk || !pathOk)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "负例没有被挡下：negUnknown=%s negPath=%s unknownText=\"%s\" pathText=\"%s\"",
			              readings.Get("negUnknown").c_str(), readings.Get("negPath").c_str(),
			              readings.Get("negUnknownText").c_str(), readings.Get("negPathText").c_str());
			fail("E_CONSTCOLOR_NEGATIVE_PATH", detail);
		}
		CA_LOG("托管前置校验负例：未知类型名 / 未登记着色器路径都被拒绝（原生创建计数 0）");
	}
}

// ---- TestCSharpStructuredBuffer（csharp-native-direct-binding 4.1）----
// C# 用单 `POSITION` 顶点流 + `StructuredColor` 参数结构（一个 `StructuredBuffer<float3>` 字段，经
// `SetBuffer` 按名绑定图内登记的缓冲）建图：窗口与离屏各一个同逻辑 pass、两次
// `SetPassParam("structuredColorBlock", …)`，提交并等待。原生侧按句柄解析同一份图，核对 pass 数、
// 每个 pass 的参数表、缓冲字段解析名、呈现清单与像素（背景 (0,255,0,255)、中心 (255,0,255,255)）。
//
// Debug 负向（进程环境变量 `CASCADED_CSHARP_SB_SELFTEST`，C# 侧读同一份值）：
//   `bad-buffer-color` 缓冲元素写成 (0,0,1) → 中心 MUST 精确等于 (0,0,255,255)，以 `E_SB_PIXEL` 判红；
//   `unbound-field`    完全不调 `SetBuffer` → 托管前置校验拒绝并点名 `colorStructuredBuffer`；
//   `bad-buffer-name`  绑未登记的 `sbColorsMissing` → 同样在提交前拒绝并点名缓冲名。
void TestCSharpStructuredBuffer(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};

	// C++ 侧的独立夹具值：尺寸、pass 数、两个像素、缓冲颜色与清除色都另写一份，不与 C# 的
	// `CSharpStructuredBufferProbe` 共享常量表 —— 一边改错不会两边一起绿。
	constexpr uint32_t kWindowWidth = 320u;
	constexpr uint32_t kWindowHeight = 240u;
	constexpr uint32_t kExpectedPassCount = 2u;
	const std::array<uint8_t, 4> expectedBackground{ 0u, 255u, 0u, 255u };
	const std::array<uint8_t, 4> expectedCenter{ 255u, 0u, 255u, 255u };
	const std::array<uint8_t, 4> expectedBadBufferCenter{ 0u, 0u, 255u, 255u };
	const std::array<float, 3> expectedBufferColor{ 1.0f, 0.0f, 1.0f };
	const std::array<float, 4> expectedClearColor{ 0.0f, 1.0f, 0.0f, 1.0f };
	char const* const kBlockName = "structuredColorBlock";
	char const* const kFieldName = "colorStructuredBuffer";
	char const* const kBufferName = "sbColors";
	char const* const kMissingBufferName = "sbColorsMissing";

	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		fail("E_SB_HOST", "IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		fail("E_SB_HOST", castl::string("托管侧不可用：") + pScriptHost->LastError());
	}

	char modeBuffer[64] = {};
	::GetEnvironmentVariableA("CASCADED_CSHARP_SB_SELFTEST", modeBuffer, sizeof(modeBuffer));
	const std::string mode = modeBuffer;
	const bool badBufferColor = mode == "bad-buffer-color";
	const bool unboundField = mode == "unbound-field";
	const bool badBufferName = mode == "bad-buffer-name";
	if (!(mode.empty() || badBufferColor || unboundField || badBufferName))
	{
		fail("E_SB_MODE", castl::string("未知的自检模式：") + mode.c_str());
	}

	struct StructuredBufferEnvGuard
	{
		~StructuredBufferEnvGuard() { ::SetEnvironmentVariableA("CASCADED_CSHARP_SB_PROBE", nullptr); }
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_SB_PROBE", "1");

	CA_LOG("TestCSharpStructuredBuffer 夹具期望：缓冲颜色=({},{},{}) 清除色=({},{},{},{}) "
	       "block={} field={} buffer={}",
	       expectedBufferColor[0], expectedBufferColor[1], expectedBufferColor[2], expectedClearColor[0],
	       expectedClearColor[1], expectedClearColor[2], expectedClearColor[3], kBlockName, kFieldName,
	       kBufferName);

	// 托管探针用 `IWindowSystem.NewWindow` 建自己的窗口，并从它取窗口目标。
	auto newWindow = ctx.pWindowSystem->NewWindow(kWindowWidth, kWindowHeight, "C# StructuredBuffer");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_SB_HOST", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	const std::string baselineError = pScriptHost->LastError();
	const int32_t tickValue = pScriptHost->Tick(0.016);
	::SetEnvironmentVariableA("CASCADED_CSHARP_SB_PROBE", nullptr);
	const std::string hostError = pScriptHost->LastError();
	if (hostError != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "Tick(回值 %d) 之后 LastError 非空：%s", tickValue,
		              hostError.c_str());
		fail("E_SB_HOST_ERROR", detail);
	}
	if (tickValue <= 0)
	{
		fail("E_SB_TICK", "Tick 回值不是正数（探针没有执行）");
	}

	HostProbeReadings readings;
	if (!ReadHostProbeReadings("test_output/TestCSharpStructuredBuffer.log", "structuredbuffer",
			&readings))
	{
		fail("E_SB_READINGS", "托管读数行 `[托管 读数] probe=structuredbuffer` 没有出现在日志里");
	}

	// 7.1 验收：后端直建缓冲（`CreateBuffer`）跨两张图复用 —— 第一张图上传、图 Dispose 后第二张图
	// 仍绑定**同一个**后端对象并提交成功。回退到图内 `AllocBuffer` 时第二次绑定的是另一个对象
	// （且第一张图销毁后不可达），这三条读数做不出来。
	if (mode.empty())
	{
		if (!readings.Has("bufferReuse") || readings.U64("bufferReuse") != 1 ||
			readings.U64("bufferSubmits") != 2 || readings.U64("bufferAfterDispose") != 1)
		{
			fail("E_SB_CREATEBUFFER",
			     castl::string("后端直建缓冲跨图复用读数不符：reuse=") + readings.Get("bufferReuse") +
			         " submits=" + readings.Get("bufferSubmits") +
			         " afterDispose=" + readings.Get("bufferAfterDispose") +
			         " createCount=" + readings.Get("bufferCreateCount") +
			         " err=" + readings.Get("bufferError"));
		}
	}

	ProbeNativeObjects objects = ResolveProbeObjects(readings);
	if (objects.graph == nullptr)
	{
		fail("E_SB_GRAPH", "读数行里的 graph 句柄解析不出一张原生 GPUGraph");
	}
	if (!objects.captureImage.IsValid() || !objects.windowImage.IsValid())
	{
		fail("E_SB_PROBE_MISSING", "读数行里的离屏纹理 / 窗口句柄解析不出原生对象");
	}

	// 窗口描述快照：离屏目标与窗口同尺寸、RGBA8；窗口目标是后台缓冲、离屏不是。
	const auto& windowDesc = objects.windowImage.GetWindowHandle()->GetBackbufferDescriptor();
	const auto& captureDesc = objects.captureImage.GetExternalManagedTexture()->GetDescriptor();
	if (windowDesc.width != kWindowWidth || windowDesc.height != kWindowHeight ||
		captureDesc.width != kWindowWidth || captureDesc.height != kWindowHeight ||
		captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM ||
		objects.captureImage.GetType() != ImageHandle::ImageType::External ||
		objects.windowImage.GetType() != ImageHandle::ImageType::Backbuffer)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_SB_TARGET", detail);
	}

	// 图结构与参数表：两个 pass（窗口 + 离屏），每个 pass 的参数表正好 1 条 `structuredColorBlock`。
	const auto& passes = objects.graph->GetRenderPasses();
	const uint32_t passCount = static_cast<uint32_t>(passes.size());
	if (passCount != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              passCount, kExpectedPassCount);
		fail("E_SB_GRAPH", detail);
	}
	size_t totalBoundParams = 0;
	uint32_t passesWithBlock = 0;
	for (auto const& pass : passes)
	{
		auto const& bound = pass.GetShaderStructs();
		totalBoundParams += bound.size();
		auto entry = bound.find(CANAME(kBlockName));
		if (entry != bound.end() && entry->second != nullptr)
		{
			++passesWithBlock;
		}
	}
	if (totalBoundParams != kExpectedPassCount || passesWithBlock != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "参数表不符：%zu 条绑定、%u 个 pass 有 %s（期望 %u 个 pass 各 1 条）", totalBoundParams,
		              passesWithBlock, kBlockName, kExpectedPassCount);
		fail("E_SB_PARAM", detail);
	}

	// 缓冲字段解析名读托管计划的登记表：`unbound-field` 必须是空、`bad-buffer-name` 必须是
	// `sbColorsMissing`、正常模式必须是 `sbColors`（两个 pass 各一次）。
	{
		const std::string expectedName = unboundField ? "-" : (badBufferName ? kMissingBufferName : kBufferName);
		for (uint32_t passIndex = 0; passIndex < kExpectedPassCount; ++passIndex)
		{
			const std::string resolved =
				readings.Get(passIndex == 0 ? "boundBuffer0" : "boundBuffer1");
			if (resolved != expectedName)
			{
				char detail[512] = {};
				std::snprintf(detail, sizeof(detail),
				              "pass %u 的缓冲字段解析不是预期名字：期望 %s，实际 %s", passIndex,
				              expectedName.c_str(), resolved.c_str());
				fail("E_SB_BIND", detail);
			}
			CA_LOG("TestCSharpStructuredBuffer pass={} 缓冲字段解析={}", passIndex, resolved.c_str());
		}
	}

	// 两种提交前拒绝：参数表 2 条、拒绝文本点名缺失项、本次提交执行增量 0、不读像素。
	if (unboundField || badBufferName)
	{
		char const* const requiredName = unboundField ? kFieldName : kMissingBufferName;
		const bool named = readings.reject.find(requiredName) != std::string::npos;
		if (!named)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "%s：拒绝文本没有点名 %s，实际 = \"%s\"",
			              mode.c_str(), requiredName, readings.reject.c_str());
			fail("E_SB_BIND", detail);
		}
		if (readings.I64("exec") != 0)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：本次提交执行增量 %lld（期望 0）", mode.c_str(),
			              static_cast<long long>(readings.I64("exec")));
			fail("E_SB_BIND", detail);
		}
		const int64_t expectedSetBuffer = unboundField ? 0 : 1;
		CA_LOG("TestCSharpStructuredBuffer 负向计数：setBuffer={}（期望 {}） addBuffer={}（期望 2）",
		       static_cast<long long>(readings.I64("setBuffer")),
		       static_cast<long long>(expectedSetBuffer),
		       static_cast<long long>(readings.I64("addBuffer")));
		if (readings.I64("setBuffer") != expectedSetBuffer || readings.I64("addBuffer") != 2)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：计数不符 setBuffer=%lld/%lld addBuffer=%lld/2",
			              mode.c_str(), static_cast<long long>(readings.I64("setBuffer")),
			              static_cast<long long>(expectedSetBuffer),
			              static_cast<long long>(readings.I64("addBuffer")));
			fail("E_SB_COUNTS", detail);
		}
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail),
		              "%s：参数表 %zu 条、拒绝文本点名 %s、后端执行次数 0，按预期失败", mode.c_str(),
		              totalBoundParams, requiredName);
		fail("E_SB_BIND", detail);
	}

	// 正常 / bad-buffer-color：调用计数逐项对上。
	{
		CA_LOG("TestCSharpStructuredBuffer 调用计数：createStruct={} setBuffer={} setFloat3={} setStruct={} "
		       "setPassParam={} addBuffer={} captureTarget={} executeGraph={}",
		       static_cast<long long>(readings.I64("createStruct")),
		       static_cast<long long>(readings.I64("setBuffer")),
		       static_cast<long long>(readings.I64("setFloat3")),
		       static_cast<long long>(readings.I64("setStruct")),
		       static_cast<long long>(readings.I64("setPassParam")),
		       static_cast<long long>(readings.I64("addBuffer")),
		       static_cast<long long>(readings.I64("captureCount")),
		       static_cast<long long>(readings.I64("exec")));
		if (readings.I64("createStruct") != 1 || readings.I64("setBuffer") != 1 ||
			readings.I64("setFloat3") != 0 || readings.I64("setStruct") != 0 ||
			readings.I64("setPassParam") != 2 || readings.I64("addBuffer") != 2 ||
			readings.I64("captureCount") != 1 || readings.I64("exec") != 1)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "调用计数不符：createStruct=%lld/1 setBuffer=%lld/1 setFloat3=%lld/0 "
			              "setStruct=%lld/0 setPassParam=%lld/2 addBuffer=%lld/2 captureTarget=%lld/1 "
			              "executeGraph=%lld/1",
			              static_cast<long long>(readings.I64("createStruct")),
			              static_cast<long long>(readings.I64("setBuffer")),
			              static_cast<long long>(readings.I64("setFloat3")),
			              static_cast<long long>(readings.I64("setStruct")),
			              static_cast<long long>(readings.I64("setPassParam")),
			              static_cast<long long>(readings.I64("addBuffer")),
			              static_cast<long long>(readings.I64("captureCount")),
			              static_cast<long long>(readings.I64("exec")));
			fail("E_SB_COUNTS", detail);
		}
	}

	if (readings.I64("wait") != 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：WaitIdle 增量 %lld（期望 1）",
		              static_cast<long long>(readings.I64("wait")));
		fail("E_SB_WAIT", detail);
	}
	const auto& presents = objects.graph->GetFinalizePass().m_PresentBackBuffers;
	if (presents.size() != 1u || presents[0] != objects.windowImage)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "真实呈现清单 %zu 项，期望唯一的窗口后台缓冲",
		              presents.size());
		fail("E_SB_PRESENT", detail);
	}

	// 像素：既有 `Readback` 读取同一次 C# 提交的独立离屏目标。背景来自清除色 (0,1,0,1)、
	// 中心来自缓冲颜色 (1,0,1)；bad-buffer-color 的缓冲是 (0,0,1)，中心必须精确变成 (0,0,255,255)。
	ctx.pGPUBackend->WaitIdle();
	const uint32_t width = captureDesc.width;
	const uint32_t height = captureDesc.height;
	std::vector<uint8_t> pixels(static_cast<size_t>(
		GetImageByteSize(width, height, ETextureFormat::E_R8G8B8A8_UNORM)));
	auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
	if (!token)
	{
		fail("E_SB_READBACK", "离屏目标的 Readback 返回空 token");
	}
	token->Wait();
	auto pixelAt = [&pixels, width](uint32_t x, uint32_t y) {
		const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
		return std::array<uint8_t, 4>{ pixels[index], pixels[index + 1], pixels[index + 2],
			                          pixels[index + 3] };
	};
	const std::array<uint8_t, 4> background = pixelAt(1, 1);
	const std::array<uint8_t, 4> center = pixelAt(width / 2, height / 2);
	const castl::string pngPath = castl::string("test_output/TestCSharpStructuredBuffer_") +
		ctx.backendName + "_" + (mode.empty() ? "normal" : mode.c_str()) + ".png";
	stbi_write_png(pngPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(),
	               static_cast<int>(width * 4));
	CA_LOG("TestCSharpStructuredBuffer：backend={} mode={} size={}x{} wait={} presents={} passes={} "
	       "boundParams={} bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height,
	       static_cast<long long>(readings.I64("wait")), presents.size(), passCount, totalBoundParams,
	       background[0], background[1], background[2], background[3], center[0], center[1], center[2],
	       center[3], pngPath.c_str());

	auto differs = [](std::array<uint8_t, 4> const& left, std::array<uint8_t, 4> const& right) {
		return left[0] != right[0] || left[1] != right[1] || left[2] != right[2] || left[3] != right[3];
	};
	if (badBufferColor)
	{
		// 缓冲元素 (0,0,1)：中心 MUST 精确等于 (0,0,255,255)。判据不能写成"中心非 magenta"或
		// "中心不等于背景" —— 空帧 / 黑帧都能通过那种弱判据。
		if (differs(center, expectedBadBufferCenter))
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail),
			              "bad-buffer-color：中心 (%u,%u,%u,%u) 不等于 (0,0,255,255)（负向模式没有生效）",
			              center[0], center[1], center[2], center[3]);
			fail("E_SB_PIXEL", detail);
		}
		fail("E_SB_PIXEL", "bad-buffer-color：中心精确等于 (0,0,255,255)，按预期失败");
	}
	if (differs(background, expectedBackground) || differs(center, expectedCenter))
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "像素不符：背景 (%u,%u,%u,%u)、中心 (%u,%u,%u,%u)", background[0], background[1],
		              background[2], background[3], center[0], center[1], center[2], center[3]);
		fail("E_SB_PIXEL", detail);
	}
	CA_LOG("TestCSharpStructuredBuffer 通过：{} 后端背景与中心像素精确匹配、两个 pass 各 1 条 {}",
	       ctx.backendName, kBlockName);

	// 托管前置校验负例（design D6）：缓冲表外字段名 `SetBuffer` 必须在进入原生实现之前被拒绝。
	{
		const bool negativeOk = readings.Get("negField") == "1";
		if (!negativeOk)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "负例没有被挡下：negField=%s 文本=\"%s\"",
			              readings.Get("negField").c_str(), readings.Get("negFieldText").c_str());
			fail("E_SB_NEGATIVE", detail);
		}
		CA_LOG("托管前置校验负例：缓冲表外字段名 SetBuffer 被拒绝（原生 SetBuffer 计数 0）");
	}
}

// ---- TestCSharpTextureSampling（csharp-native-direct-binding 4.1）----
// C# 建 2×2 RGBA8 纹理（一次调用完成创建 + 上传 + 转采样态），用位置 + 纹理坐标顶点流与
// `TextureSampleBlock` 参数结构提交窗口 / 离屏两个同逻辑 pass，原生侧按句柄解析同一份图与同一份
// 纹理对象，核对 pass 数、参数表条目、纹理字段对象同一性、调用计数（上传执行与提交执行分开读）
// 与像素（背景 (0,0,255,255)、中心 (255,255,0,255)）。
void TestCSharpTextureSampling(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};

	// C++ 侧的独立夹具值：尺寸、pass 数、两个像素、纹理字节与清除色都另写一份，不与 C# 的
	// `CSharpTextureProbe` 共享常量表 —— 一边改错不会两边一起绿。
	constexpr uint32_t kWindowWidth = 320u;
	constexpr uint32_t kWindowHeight = 240u;
	constexpr uint32_t kExpectedPassCount = 2u;
	constexpr uint32_t kTextureSize = 2u;
	const std::array<uint8_t, 4> expectedBackground{ 0u, 0u, 255u, 255u };
	const std::array<uint8_t, 4> expectedCenter{ 255u, 255u, 0u, 255u };
	const std::array<uint8_t, 4> expectedBadTextureCenter{ 0u, 255u, 255u, 255u };
	const std::array<uint8_t, 4> expectedTexel{ 255u, 255u, 0u, 255u };
	const std::array<uint8_t, 4> expectedBadTexel{ 0u, 255u, 255u, 255u };
	const std::array<float, 4> expectedClearColor{ 0.0f, 0.0f, 1.0f, 1.0f };
	char const* const kBlockName = "textureBlock";
	char const* const kTextureField = "testTexture";
	char const* const kSamplerField = "testSampler";
	char const* const kStructTypeName = "TextureSampleBlock";
	char const* const kTextureName = "texYellow2x2";
	char const* const kProbeLogPath = "test_output/TestCSharpTextureSampling.log";

	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		fail("E_TEX_HOST", "IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		fail("E_TEX_HOST", castl::string("托管侧不可用：") + pScriptHost->LastError());
	}

	char modeBuffer[64] = {};
	::GetEnvironmentVariableA("CASCADED_CSHARP_TEX_SELFTEST", modeBuffer, sizeof(modeBuffer));
	const std::string mode = modeBuffer;
	const bool badTextureColor = mode == "bad-texture-color";
	const bool unboundImage = mode == "unbound-image";
	const bool unboundSampler = mode == "unbound-sampler";
	if (!(mode.empty() || badTextureColor || unboundImage || unboundSampler))
	{
		fail("E_TEX_MODE", castl::string("未知的自检模式：") + mode.c_str());
	}

	struct TextureEnvGuard
	{
		~TextureEnvGuard() { ::SetEnvironmentVariableA("CASCADED_CSHARP_TEX_PROBE", nullptr); }
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_TEX_PROBE", "1");

	CA_LOG("TestCSharpTextureSampling 夹具期望：纹理 {}x{} 纹素=({},{},{},{}) 清除色=({},{},{},{}) "
	       "block={} field={} sampler={}",
	       kTextureSize, kTextureSize, expectedTexel[0], expectedTexel[1], expectedTexel[2],
	       expectedTexel[3], expectedClearColor[0], expectedClearColor[1], expectedClearColor[2],
	       expectedClearColor[3], kBlockName, kTextureField, kSamplerField);

	// 托管探针用 `IWindowSystem.NewWindow` 建自己的窗口，并从它取窗口目标。
	auto newWindow = ctx.pWindowSystem->NewWindow(kWindowWidth, kWindowHeight, "C# Texture");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_TEX_HOST", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	const std::string baselineError = pScriptHost->LastError();
	const int32_t tickValue = pScriptHost->Tick(0.016);
	::SetEnvironmentVariableA("CASCADED_CSHARP_TEX_PROBE", nullptr);
	const std::string hostError = pScriptHost->LastError();
	if (hostError != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "Tick(回值 %d) 之后 LastError 非空：%s", tickValue,
		              hostError.c_str());
		fail("E_TEX_HOST_ERROR", detail);
	}
	if (tickValue <= 0)
	{
		fail("E_TEX_TICK", "Tick 回值不是正数（探针没有执行）");
	}

	HostProbeReadings readings;
	if (!ReadHostProbeReadings(kProbeLogPath, "texture", &readings))
	{
		fail("E_TEX_READINGS", "托管读数行 `[托管 读数] probe=texture` 没有出现在日志里");
	}

	ProbeNativeObjects objects = ResolveProbeObjects(readings);
	if (objects.graph == nullptr)
	{
		fail("E_TEX_GRAPH", "读数行里的 graph 句柄解析不出一张原生 GPUGraph");
	}
	if (!objects.captureImage.IsValid() || !objects.windowImage.IsValid())
	{
		fail("E_TEX_PROBE_MISSING", "读数行里的离屏纹理 / 窗口句柄解析不出原生对象");
	}

	// 窗口描述快照：离屏目标与窗口同尺寸、RGBA8；窗口目标是后台缓冲、离屏不是。
	const auto& windowDesc = objects.windowImage.GetWindowHandle()->GetBackbufferDescriptor();
	const auto& captureDesc = objects.captureImage.GetExternalManagedTexture()->GetDescriptor();
	if (windowDesc.width != kWindowWidth || windowDesc.height != kWindowHeight ||
		captureDesc.width != kWindowWidth || captureDesc.height != kWindowHeight ||
		captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM ||
		objects.captureImage.GetType() != ImageHandle::ImageType::External ||
		objects.windowImage.GetType() != ImageHandle::ImageType::Backbuffer)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_TEX_TARGET", detail);
	}

	// 图结构与参数表：两个 pass（窗口 + 离屏），每个 pass 的参数表正好 1 条 `textureBlock`。
	const auto& passes = objects.graph->GetRenderPasses();
	const uint32_t passCount = static_cast<uint32_t>(passes.size());
	if (passCount != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              passCount, kExpectedPassCount);
		fail("E_TEX_GRAPH", detail);
	}
	size_t totalBoundParams = 0;
	uint32_t passesWithBlock = 0;
	for (auto const& pass : passes)
	{
		auto const& bound = pass.GetShaderStructs();
		totalBoundParams += bound.size();
		auto entry = bound.find(CANAME(kBlockName));
		if (entry != bound.end() && entry->second != nullptr)
		{
			++passesWithBlock;
		}
	}
	if (totalBoundParams != kExpectedPassCount || passesWithBlock != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "参数表不符：%zu 条绑定、%u 个 pass 有 %s（期望 %u 个 pass 各 1 条）", totalBoundParams,
		              passesWithBlock, kBlockName, kExpectedPassCount);
		fail("E_TEX_PARAM", detail);
	}

	// `TextureSampleBlock` 的元数据读数（复核 design 前提，不改规则）：纹理条目是只读 + 描述符数组
	// 长度 1，采样器表里有 `testSampler`。查询方法在 `CRenderBackend` 上，所以经后端对象读。
	{
		ShaderCompilerSlang::ShaderStructData const* structData =
			ctx.pGPUBackend->FindShaderStructData(CANAME(kStructTypeName));
		if (structData == nullptr)
		{
			fail("E_TEX_STRUCT", "后端查不到 TextureSampleBlock 的编译元数据");
		}
		ShaderCompilerSlang::ShaderTextureData const* textureEntry = nullptr;
		for (auto const& candidate : structData->m_Textures)
		{
			if (candidate.m_Name == CANAME(kTextureField))
			{
				textureEntry = &candidate;
				break;
			}
		}
		if (textureEntry == nullptr)
		{
			fail("E_TEX_STRUCT", "TextureSampleBlock 的元数据纹理表里没有 testTexture");
		}
		bool samplerListed = false;
		for (auto const& sampler : structData->m_TextureSamplers)
		{
			if (sampler == CANAME(kSamplerField))
			{
				samplerListed = true;
				break;
			}
		}
		CA_LOG("TestCSharpTextureSampling 元数据读数：type={} textures={} texField={} rw={} elements={} "
		       "samplers={} samplerListed={}",
		       kStructTypeName, structData->m_Textures.size(), textureEntry->m_Name.c_str(),
		       static_cast<int>(textureEntry->m_RWType), textureEntry->m_ElementCount,
		       structData->m_TextureSamplers.size(), samplerListed ? 1 : 0);
		if (textureEntry->m_RWType != ShaderCompilerSlang::EShaderResourceAccess::eReadOnly ||
			textureEntry->m_ElementCount != 1u || !samplerListed)
		{
			fail("E_TEX_STRUCT",
			     "TextureSampleBlock 的 testTexture 不是「只读 + 描述符数组长度 1」，或采样器表里没有 testSampler");
		}
	}

	// 探针那次 `CreateTexture` 返回的纹理对象：按读数行里的原生对象地址取（纹理由图内参数结构
	// 强引用保活，探针末尾的 Dispose 只失效托管表面、不销毁原生对象）。
	GPUTexture* probeTexture = reinterpret_cast<GPUTexture*>(
		static_cast<uintptr_t>(readings.U64("textureSelf")));
	if (probeTexture == nullptr)
	{
		fail("E_TEX_CREATE", "读数行里的 textureSelf 是空（CreateTexture 没建出对象）");
	}
	{
		const auto& textureDesc = probeTexture->GetDescriptor();
		if (textureDesc.width != kTextureSize || textureDesc.height != kTextureSize ||
			textureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "创建的纹理描述不符：%ux%u 格式 %u",
			              textureDesc.width, textureDesc.height, static_cast<unsigned>(textureDesc.format));
			fail("E_TEX_CREATE", detail);
		}
	}

	// 两种提交前拒绝：参数表 2 条、拒绝文本点名缺失项、本次提交执行增量 0（上传执行另计，必须仍是 1）、
	// 不读像素 —— 以 `E_TEX_BIND` 判红。
	if (unboundImage || unboundSampler)
	{
		char const* const requiredName = unboundImage ? kTextureField : kSamplerField;
		const bool named = readings.reject.find(requiredName) != std::string::npos;
		if (!named)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "%s：拒绝文本没有点名 %s，实际 = \"%s\"",
			              mode.c_str(), requiredName, readings.reject.c_str());
			fail("E_TEX_BIND", detail);
		}
		if (readings.I64("exec") != 0)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：本次提交执行增量 %lld（期望 0）", mode.c_str(),
			              static_cast<long long>(readings.I64("exec")));
			fail("E_TEX_BIND", detail);
		}
		if (readings.I64("upload") != 1)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：上传执行增量 %lld（期望 1，另计不进提交判据）",
			              mode.c_str(), static_cast<long long>(readings.I64("upload")));
			fail("E_TEX_COUNTS", detail);
		}
		const int64_t expectedSetImage = unboundImage ? 0 : 1;
		const int64_t expectedSetSampler = unboundSampler ? 0 : 1;
		CA_LOG("TestCSharpTextureSampling 负向计数：setImage={}（期望 {}） setSampler={}（期望 {}） "
		       "上传执行={}（期望 1） 本次提交执行增量={}（期望 0）",
		       static_cast<long long>(readings.I64("setImage")),
		       static_cast<long long>(expectedSetImage),
		       static_cast<long long>(readings.I64("setSampler")),
		       static_cast<long long>(expectedSetSampler),
		       static_cast<long long>(readings.I64("upload")),
		       static_cast<long long>(readings.I64("exec")));
		if (readings.I64("setImage") != expectedSetImage ||
			readings.I64("setSampler") != expectedSetSampler)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：计数不符 setImage=%lld/%lld setSampler=%lld/%lld",
			              mode.c_str(), static_cast<long long>(readings.I64("setImage")),
			              static_cast<long long>(expectedSetImage),
			              static_cast<long long>(readings.I64("setSampler")),
			              static_cast<long long>(expectedSetSampler));
			fail("E_TEX_COUNTS", detail);
		}
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail),
		              "%s：参数表 %zu 条、拒绝文本点名 %s、本次提交执行增量 0（上传执行 1 另计），按预期失败",
		              mode.c_str(), totalBoundParams, requiredName);
		fail("E_TEX_BIND", detail);
	}

	// 纹理字段解析读**托管计划的登记表**：两个 pass 都要解析到探针那次 CreateTexture 的同一个对象
	// （读数行里的 `texture` 句柄与 `boundTexture0/1` 必须相等）。这一块排在两种漏绑的拒绝之后：
	// `unbound-image` 里根本没有绑定，先跑同一性核对会把错误文本换成"不是同一对象"。
	{
		const uint64_t createdToken = readings.U64("texture");
		for (uint32_t passIndex = 0; passIndex < kExpectedPassCount; ++passIndex)
		{
			const uint64_t boundToken =
				readings.U64(passIndex == 0 ? "boundTexture0" : "boundTexture1");
			const bool sameObject = createdToken != 0u && boundToken == createdToken;
			CA_LOG("TestCSharpTextureSampling pass={} 纹理字段解析={} 同一对象={}", passIndex,
			       boundToken != 0u ? "非空" : "(空)", sameObject ? 1 : 0);
			if (!sameObject)
			{
				char detail[256] = {};
				std::snprintf(detail, sizeof(detail),
				              "pass %u 的纹理字段解析到的不是探针 CreateTexture 返回的同一对象", passIndex);
				fail("E_TEX_BIND", detail);
			}
		}
	}

	// 正常 / bad-texture-color：调用计数逐项对上（上传执行与提交执行分开读）。
	{
		CA_LOG("TestCSharpTextureSampling 调用计数：createTexture={} setImage={} setSampler={} "
		       "setPassParam={} setFloat3={} setStruct={} setBuffer={} captureTarget={} 上传执行={} "
		       "本次提交执行={}",
		       static_cast<long long>(readings.I64("createTexture")),
		       static_cast<long long>(readings.I64("setImage")),
		       static_cast<long long>(readings.I64("setSampler")),
		       static_cast<long long>(readings.I64("setPassParam")),
		       static_cast<long long>(readings.I64("setFloat3")),
		       static_cast<long long>(readings.I64("setStruct")),
		       static_cast<long long>(readings.I64("setBuffer")),
		       static_cast<long long>(readings.I64("captureCount")),
		       static_cast<long long>(readings.I64("upload")),
		       static_cast<long long>(readings.I64("exec")));
		if (readings.I64("createTexture") != 1 || readings.I64("setImage") != 1 ||
			readings.I64("setSampler") != 1 || readings.I64("setPassParam") != 2 ||
			readings.I64("setFloat3") != 0 || readings.I64("setStruct") != 0 ||
			readings.I64("setBuffer") != 0 || readings.I64("captureCount") != 1 ||
			readings.I64("upload") != 1 || readings.I64("exec") != 1)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "调用计数不符：createTexture=%lld/1 setImage=%lld/1 setSampler=%lld/1 "
			              "setPassParam=%lld/2 setFloat3=%lld/0 setStruct=%lld/0 setBuffer=%lld/0 "
			              "captureTarget=%lld/1 上传执行=%lld/1 本次提交执行=%lld/1",
			              static_cast<long long>(readings.I64("createTexture")),
			              static_cast<long long>(readings.I64("setImage")),
			              static_cast<long long>(readings.I64("setSampler")),
			              static_cast<long long>(readings.I64("setPassParam")),
			              static_cast<long long>(readings.I64("setFloat3")),
			              static_cast<long long>(readings.I64("setStruct")),
			              static_cast<long long>(readings.I64("setBuffer")),
			              static_cast<long long>(readings.I64("captureCount")),
			              static_cast<long long>(readings.I64("upload")),
			              static_cast<long long>(readings.I64("exec")));
			fail("E_TEX_COUNTS", detail);
		}
	}

	if (readings.I64("wait") != 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：WaitIdle 增量 %lld（期望 1）",
		              static_cast<long long>(readings.I64("wait")));
		fail("E_TEX_WAIT", detail);
	}
	const auto& presents = objects.graph->GetFinalizePass().m_PresentBackBuffers;
	if (presents.size() != 1u || presents[0] != objects.windowImage)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "真实呈现清单 %zu 项，期望唯一的窗口后台缓冲",
		              presents.size());
		fail("E_TEX_PRESENT", detail);
	}

	// 托管侧的 disposed 摘要行：先 flush 当前 stdout，再从本次运行自己的日志文件里读那一行 ——
	// 日志是这一层唯一能读到托管摘要的通道。
	{
		std::fflush(stdout);
		std::ifstream logFile(kProbeLogPath);
		std::string logText;
		bool disposedLine = false;
		std::string line;
		while (std::getline(logFile, line))
		{
			if (line.find("ObjectDisposedException") != std::string::npos &&
				line.find("原生 SetImage 调用 0") != std::string::npos)
			{
				disposedLine = true;
				logText = line;
			}
		}
		CA_LOG("TestCSharpTextureSampling disposed 摘要行={}", disposedLine ? logText.c_str() : "(未命中)");
		if (!disposedLine)
		{
			fail("E_TEX_DISPOSED",
			     "托管日志里没有 disposed 摘要行（ObjectDisposedException + 原生 SetImage 调用 0）");
		}
	}

	// 像素：既有 `Readback` 读取同一次 C# 提交的独立离屏目标。背景来自清除色 (0,0,1,1)，
	// 中心来自纹理采样色；bad-texture-color 的纹素是青色，中心必须精确变成 (0,255,255,255)。
	ctx.pGPUBackend->WaitIdle();
	const uint32_t width = captureDesc.width;
	const uint32_t height = captureDesc.height;
	std::vector<uint8_t> pixels(static_cast<size_t>(
		GetImageByteSize(width, height, ETextureFormat::E_R8G8B8A8_UNORM)));
	auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
	if (!token)
	{
		fail("E_TEX_READBACK", "离屏目标的 Readback 返回空 token");
	}
	token->Wait();
	auto pixelAt = [&pixels, width](uint32_t x, uint32_t y) {
		const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
		return std::array<uint8_t, 4>{ pixels[index], pixels[index + 1], pixels[index + 2],
			                          pixels[index + 3] };
	};
	const std::array<uint8_t, 4> background = pixelAt(1, 1);
	const std::array<uint8_t, 4> center = pixelAt(width / 2, height / 2);
	const castl::string pngPath = castl::string("test_output/TestCSharpTextureSampling_") +
		ctx.backendName + "_" + (mode.empty() ? "normal" : mode.c_str()) + ".png";
	stbi_write_png(pngPath.c_str(), static_cast<int>(width), static_cast<int>(height), 4, pixels.data(),
	               static_cast<int>(width * 4));
	CA_LOG("TestCSharpTextureSampling：backend={} mode={} size={}x{} wait={} presents={} passes={} "
	       "boundParams={} bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height,
	       static_cast<long long>(readings.I64("wait")), presents.size(), passCount, totalBoundParams,
	       background[0], background[1], background[2], background[3], center[0], center[1], center[2],
	       center[3], pngPath.c_str());

	auto differs = [](std::array<uint8_t, 4> const& left, std::array<uint8_t, 4> const& right) {
		return left[0] != right[0] || left[1] != right[1] || left[2] != right[2] || left[3] != right[3];
	};
	if (badTextureColor)
	{
		// 纹素 (0,255,255,255)：中心 MUST 精确等于它。判据不能写成"中心非黄"或"中心不等于背景" ——
		// 空帧 / 黑帧都能通过那种弱判据。
		if (differs(center, expectedBadTextureCenter))
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail),
			              "bad-texture-color：中心 (%u,%u,%u,%u) 不等于 (0,255,255,255)（负向模式没有生效）",
			              center[0], center[1], center[2], center[3]);
			fail("E_TEX_PIXEL", detail);
		}
		fail("E_TEX_PIXEL", "bad-texture-color：中心精确等于 (0,255,255,255)，按预期失败");
	}
	if (differs(background, expectedBackground) || differs(center, expectedCenter))
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "像素不符：背景 (%u,%u,%u,%u)、中心 (%u,%u,%u,%u)", background[0], background[1],
		              background[2], background[3], center[0], center[1], center[2], center[3]);
		fail("E_TEX_PIXEL", detail);
	}
	CA_LOG("TestCSharpTextureSampling 通过：{} 后端背景与中心像素精确匹配、两个 pass 各 1 条 {}",
	       ctx.backendName, kBlockName);

	// 托管前置校验负例（design D6）：字节数不符的创建与纹理表外字段 `SetImage` 都必须被拒绝，
	// 且原生计数不涨。
	{
		const bool bytesOk = readings.Get("negBytes") == "1";
		const bool fieldOk = readings.Get("negField") == "1";
		if (!bytesOk || !fieldOk)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "负例没有被挡下：negBytes=%s negField=%s bytesText=\"%s\" fieldText=\"%s\"",
			              readings.Get("negBytes").c_str(), readings.Get("negField").c_str(),
			              readings.Get("negBytesText").c_str(), readings.Get("negFieldText").c_str());
			fail("E_TEX_NEGATIVE", detail);
		}
		CA_LOG("托管前置校验负例：字节数不符创建 / 纹理表外字段 SetImage 都被拒绝（原生创建与写入计数 0）");
	}
	CA_LOG("TestCSharpTextureSampling 夹具纹理读数：badTexel=({},{},{},{}) 清除色=({},{},{},{})",
	       expectedBadTexel[0], expectedBadTexel[1], expectedBadTexel[2], expectedBadTexel[3],
	       expectedClearColor[0], expectedClearColor[1], expectedClearColor[2], expectedClearColor[3]);
}

// ---- editor-window-panel 3.1 / 3.2：进程内最小 WebSocket 客户端与像素/屏幕捕获助手 ----
// 全部自带，零新依赖：Winsock 手写 RFC6455（客户端握手 + 掩码文本帧）；像素读回走既有
// `ctx.pGPUBackend->Readback`（只读离屏捕获纹理，不碰已呈现的 swapchain backbuffer —— D12）。
namespace
{
	bool WsEnsureWinsock(std::string& error)
	{
		static bool started = false;
		static std::string startupError;
		if (started)
		{
			return true;
		}
		if (!startupError.empty())
		{
			error = startupError;
			return false;
		}
		WSADATA data{};
		const int code = ::WSAStartup(MAKEWORD(2, 2), &data);
		if (code != 0)
		{
			startupError = "WSAStartup 失败：" + std::to_string(code);
			error = startupError;
			return false;
		}
		started = true;
		return true;
	}

	class WsClient
	{
	public:
		~WsClient() { Close(); }

		bool Connect(int port, std::string& error)
		{
			Close();
			if (!WsEnsureWinsock(error))
			{
				return false;
			}
			m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (m_socket == INVALID_SOCKET)
			{
				error = "socket() 失败：" + std::to_string(::WSAGetLastError());
				return false;
			}
			sockaddr_in address{};
			address.sin_family = AF_INET;
			address.sin_port = ::htons(static_cast<u_short>(port));
			::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
			if (::connect(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
			{
				error = "connect 127.0.0.1:" + std::to_string(port) + " 失败：" +
					std::to_string(::WSAGetLastError());
				Close();
				return false;
			}
			const char* key = "dGhlIHNhbXBsZSBub25jZQ==";
			char request[512] = {};
			std::snprintf(request, sizeof(request),
				"GET /ws HTTP/1.1\r\nHost: localhost:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
				"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", port, key);
			if (!SendAll(request, std::strlen(request), error))
			{
				Close();
				return false;
			}
			std::string response;
			char byte = 0;
			while (response.find("\r\n\r\n") == std::string::npos)
			{
				if (!RecvExact(&byte, 1, 5000, error))
				{
					Close();
					return false;
				}
				response.push_back(byte);
				if (response.size() > 16384)
				{
					error = "WebSocket 握手响应头过长";
					Close();
					return false;
				}
			}
			if (response.find(" 101 ") == std::string::npos)
			{
				error = "WebSocket 握手失败（响应不是 101）：" + response.substr(0, 160);
				Close();
				return false;
			}
			return true;
		}

		bool SendText(std::string const& text, std::string& error)
		{
			std::vector<unsigned char> frame;
			frame.push_back(0x81); // FIN + 文本帧
			const size_t length = text.size();
			if (length < 126)
			{
				frame.push_back(static_cast<unsigned char>(0x80 | length));
			}
			else if (length <= 0xFFFF)
			{
				frame.push_back(static_cast<unsigned char>(0x80 | 126));
				frame.push_back(static_cast<unsigned char>((length >> 8) & 0xFF));
				frame.push_back(static_cast<unsigned char>(length & 0xFF));
			}
			else
			{
				frame.push_back(static_cast<unsigned char>(0x80 | 127));
				for (int shift = 7; shift >= 0; --shift)
				{
					frame.push_back(static_cast<unsigned char>((static_cast<uint64_t>(length) >> (8 * shift)) & 0xFF));
				}
			}
			const unsigned char mask[4] = { 0x37, 0xFA, 0x21, 0x3D };
			frame.insert(frame.end(), mask, mask + 4);
			for (size_t index = 0; index < length; ++index)
			{
				frame.push_back(static_cast<unsigned char>(text[index]) ^ mask[index % 4]);
			}
			return SendAll(reinterpret_cast<const char*>(frame.data()), frame.size(), error);
		}

		// 收一条完整文本消息；超时返回 false 且 error 里含"超时"（调用方据此推进主循环）。
		bool ReceiveText(std::string& out, int timeoutMs, std::string& error)
		{
			out.clear();
			for (;;)
			{
				unsigned char header[2] = {};
				if (!RecvExact(reinterpret_cast<char*>(header), 2, timeoutMs, error))
				{
					return false;
				}
				const bool finalFrame = (header[0] & 0x80) != 0;
				const int opcode = header[0] & 0x0F;
				const bool masked = (header[1] & 0x80) != 0;
				uint64_t length = header[1] & 0x7F;
				if (length == 126)
				{
					unsigned char extended[2] = {};
					if (!RecvExact(reinterpret_cast<char*>(extended), 2, timeoutMs, error))
					{
						return false;
					}
					length = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
				}
				else if (length == 127)
				{
					unsigned char extended[8] = {};
					if (!RecvExact(reinterpret_cast<char*>(extended), 8, timeoutMs, error))
					{
						return false;
					}
					length = 0;
					for (int index = 0; index < 8; ++index)
					{
						length = (length << 8) | extended[index];
					}
				}
				unsigned char mask[4] = {};
				if (masked && !RecvExact(reinterpret_cast<char*>(mask), 4, timeoutMs, error))
				{
					return false;
				}
				std::vector<unsigned char> payload(static_cast<size_t>(length));
				if (length != 0 && !RecvExact(reinterpret_cast<char*>(payload.data()),
					static_cast<size_t>(length), timeoutMs, error))
				{
					return false;
				}
				if (masked)
				{
					for (size_t index = 0; index < payload.size(); ++index)
					{
						payload[index] ^= mask[index % 4];
					}
				}
				if (opcode == 0x8)
				{
					error = "服务端关闭了连接";
					return false;
				}
				if (opcode == 0x9 || opcode == 0xA)
				{
					continue; // ping/pong：本测试的服务端不发，忽略即可
				}
				out.append(reinterpret_cast<const char*>(payload.data()), payload.size());
				if (finalFrame)
				{
					return true;
				}
			}
		}

		void Close()
		{
			if (m_socket != INVALID_SOCKET)
			{
				::closesocket(m_socket);
				m_socket = INVALID_SOCKET;
			}
		}

	private:
		bool SendAll(const char* data, size_t length, std::string& error)
		{
			size_t sent = 0;
			while (sent < length)
			{
				const int written = ::send(m_socket, data + sent, static_cast<int>(length - sent), 0);
				if (written <= 0)
				{
					error = "send() 失败：" + std::to_string(::WSAGetLastError());
					return false;
				}
				sent += static_cast<size_t>(written);
			}
			return true;
		}

		bool RecvExact(char* data, size_t length, int timeoutMs, std::string& error)
		{
			size_t received = 0;
			while (received < length)
			{
				fd_set readSet;
				FD_ZERO(&readSet);
				FD_SET(m_socket, &readSet);
				timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
				const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
				if (ready == 0)
				{
					error = "接收超时（" + std::to_string(timeoutMs) + " ms）";
					return false;
				}
				if (ready == SOCKET_ERROR)
				{
					error = "select() 失败：" + std::to_string(::WSAGetLastError());
					return false;
				}
				const int got = ::recv(m_socket, data + received, static_cast<int>(length - received), 0);
				if (got <= 0)
				{
					error = "连接已关闭（recv=" + std::to_string(got) + "）";
					return false;
				}
				received += static_cast<size_t>(got);
			}
			return true;
		}

		SOCKET m_socket = INVALID_SOCKET;
	};

	// 服务端用 System.Text.Json 紧凑序列化，token 形状固定；判据只认这些 token。
	bool JsonHas(std::string const& text, char const* token)
	{
		return text.find(token) != std::string::npos;
	}

	bool JsonIsResult(std::string const& text)
	{
		return text.find("\"type\":\"result\"") != std::string::npos;
	}

	bool JsonIsEvent(std::string const& text, char const* name)
	{
		return text.find("\"type\":\"event\"") != std::string::npos &&
			text.find(std::string("\"event\":\"") + name + "\"") != std::string::npos;
	}

	int JsonRequestId(std::string const& text)
	{
		const std::string key = "\"requestId\":";
		const std::size_t at = text.find(key);
		if (at == std::string::npos)
		{
			return -1;
		}
		return std::atoi(text.c_str() + at + key.size());
	}

	std::string JsonErrorCode(std::string const& text)
	{
		const std::string key = "\"code\":\"";
		const std::size_t at = text.find(key);
		if (at == std::string::npos)
		{
			return std::string();
		}
		const std::size_t begin = at + key.size();
		const std::size_t end = text.find('"', begin);
		return end == std::string::npos ? std::string() : text.substr(begin, end - begin);
	}

	struct Rect4
	{
		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
	};

	bool ParseRect(std::string const& text, Rect4& out)
	{
		return std::sscanf(text.c_str(), "%d,%d,%d,%d", &out.x, &out.y, &out.w, &out.h) == 4;
	}

	bool ParseSize(std::string const& text, int& width, int& height)
	{
		return std::sscanf(text.c_str(), "%dx%d", &width, &height) == 2;
	}

	// 等一条指定 requestId 的 result：期间交替"短暂收包 + 推进一帧主循环"；事件收进 events。
	bool PumpUntilResult(cs_script::IScriptHost* host, TestContext& ctx, WsClient& client, int requestId,
		std::string& result, std::vector<std::string>& events, int timeoutMs)
	{
		const auto startedAt = std::chrono::steady_clock::now();
		while (true)
		{
			const double elapsedMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
			if (elapsedMs > timeoutMs)
			{
				return false;
			}
			std::string message;
			std::string receiveError;
			if (client.ReceiveText(message, 50, receiveError))
			{
				if (JsonIsResult(message) && JsonRequestId(message) == requestId)
				{
					result = message;
					return true;
				}
				events.push_back(message);
				continue;
			}
			if (receiveError.find("超时") == std::string::npos)
			{
				return false;
			}
			host->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
		}
	}

	// 纯 socket 的 HTTP GET（用于"资源缺失错误页 / 正常页面"读数）。
	bool HttpGet(int port, std::string const& path, int& status, std::string& body, std::string& error)
	{
		if (!WsEnsureWinsock(error))
		{
			return false;
		}
		SOCKET socketHandle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socketHandle == INVALID_SOCKET)
		{
			error = "socket() 失败：" + std::to_string(::WSAGetLastError());
			return false;
		}
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = ::htons(static_cast<u_short>(port));
		::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
		if (::connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			error = "connect 失败：" + std::to_string(::WSAGetLastError());
			::closesocket(socketHandle);
			return false;
		}
		char request[512] = {};
		std::snprintf(request, sizeof(request),
			"GET %s HTTP/1.1\r\nHost: localhost:%d\r\nConnection: close\r\n\r\n", path.c_str(), port);
		std::string raw;
		if (::send(socketHandle, request, static_cast<int>(std::strlen(request)), 0) <= 0)
		{
			error = "send 失败";
			::closesocket(socketHandle);
			return false;
		}
		char buffer[4096];
		for (;;)
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(socketHandle, &readSet);
			timeval timeout{ 5, 0 };
			const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
			if (ready <= 0)
			{
				break;
			}
			const int got = ::recv(socketHandle, buffer, sizeof(buffer), 0);
			if (got <= 0)
			{
				break;
			}
			raw.append(buffer, static_cast<size_t>(got));
		}
		::closesocket(socketHandle);
		const std::size_t headerEnd = raw.find("\r\n\r\n");
		if (headerEnd == std::string::npos)
		{
			error = "HTTP 响应不完整";
			return false;
		}
		const std::string header = raw.substr(0, headerEnd);
		if (header.find("HTTP/1.1 ") != 0)
		{
			error = "HTTP 状态行格式不符：" + header.substr(0, 64);
			return false;
		}
		status = std::atoi(header.c_str() + std::strlen("HTTP/1.1 "));
		body = raw.substr(headerEnd + 4);
		return true;
	}

	bool DesktopSessionAvailable()
	{
		HDESK desktop = ::OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
		if (desktop == nullptr)
		{
			return false;
		}
		::CloseDesktop(desktop);
		return true;
	}

	// 从屏幕 DC 的 (screenX, screenY) 抓 1 个像素（D12：OS 级屏幕捕获，不读 backbuffer）。
	bool CaptureScreenPixel(int screenX, int screenY, unsigned char outRgba[4], std::string& error)
	{
		HDC screen = ::GetDC(nullptr);
		if (screen == nullptr)
		{
			error = "GetDC(NULL) 失败（没有 desktop 会话？）";
			return false;
		}
		HDC memory = ::CreateCompatibleDC(screen);
		HBITMAP bitmap = ::CreateCompatibleBitmap(screen, 1, 1);
		HGDIOBJ oldObject = ::SelectObject(memory, bitmap);
		bool ok = ::BitBlt(memory, 0, 0, 1, 1, screen, screenX, screenY, SRCCOPY) != 0;
		unsigned char pixel[4] = { 0, 0, 0, 0 };
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		info.bmiHeader.biWidth = 1;
		info.bmiHeader.biHeight = -1;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		if (ok)
		{
			ok = ::GetDIBits(memory, bitmap, 0, 1, pixel, &info, DIB_RGB_COLORS) != 0;
		}
		::SelectObject(memory, oldObject);
		::DeleteObject(bitmap);
		::DeleteDC(memory);
		::ReleaseDC(nullptr, screen);
		if (!ok)
		{
			error = "屏幕捕获失败（BitBlt/GetDIBits）";
			return false;
		}
		outRgba[0] = pixel[2]; // DIB 是 BGRA，转 RGBA
		outRgba[1] = pixel[1];
		outRgba[2] = pixel[0];
		outRgba[3] = pixel[3];
		return true;
	}

	// 屏幕捕获前把本进程其它可见顶层窗口藏起来（测试自己建的主机窗/脚本常驻窗可能压在目标窗口上，
	// 用 SetForegroundWindow 不一定能提到前面）。只影响本进程窗口，跑完 RestoreAll 还原。
	struct OtherWindowHider
	{
		std::vector<HWND> Hidden;

		void HideAllExcept(HWND keep)
		{
			Hidden.clear();
			Params params{ this, keep };
			::EnumWindows(&OtherWindowHider::EnumProc, reinterpret_cast<LPARAM>(&params));
		}

		void RestoreAll()
		{
			for (HWND window : Hidden)
			{
				::ShowWindow(window, SW_SHOW);
			}
			Hidden.clear();
		}

	private:
		struct Params
		{
			OtherWindowHider* Self;
			HWND Keep;
		};

		static BOOL CALLBACK EnumProc(HWND window, LPARAM param)
		{
			Params* params = reinterpret_cast<Params*>(param);
			DWORD processId = 0;
			::GetWindowThreadProcessId(window, &processId);
			if (processId != ::GetCurrentProcessId() || window == params->Keep)
			{
				return TRUE;
			}
			if (!::IsWindowVisible(window) || ::IsIconic(window))
			{
				return TRUE;
			}
			::ShowWindow(window, SW_HIDE);
			params->Self->Hidden.push_back(window);
			return TRUE;
		}
	};

	UINT WindowDpi(HWND hwnd)
	{
		typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
		HMODULE user32 = ::GetModuleHandleA("user32.dll");
		auto getDpi = user32 == nullptr
			? nullptr
			: reinterpret_cast<GetDpiForWindowFn>(::GetProcAddress(user32, "GetDpiForWindow"));
		return getDpi == nullptr ? 0u : getDpi(hwnd);
	}

	// 把整个窗口客户区从屏幕 DC 抓下来存 PNG（诊断用：看"屏幕捕获到底抓到了什么"）。
	bool SaveClientAreaPng(HWND hwnd, std::string const& path, int& outWidth, int& outHeight, std::string& error)
	{
		RECT client{};
		if (!::GetClientRect(hwnd, &client))
		{
			error = "GetClientRect 失败";
			return false;
		}
		outWidth = client.right - client.left;
		outHeight = client.bottom - client.top;
		if (outWidth <= 0 || outHeight <= 0)
		{
			error = "客户区尺寸为空";
			return false;
		}
		POINT origin{ 0, 0 };
		if (!::ClientToScreen(hwnd, &origin))
		{
			error = "ClientToScreen 失败";
			return false;
		}
		HDC screen = ::GetDC(nullptr);
		if (screen == nullptr)
		{
			error = "GetDC(NULL) 失败";
			return false;
		}
		HDC memory = ::CreateCompatibleDC(screen);
		HBITMAP bitmap = ::CreateCompatibleBitmap(screen, outWidth, outHeight);
		HGDIOBJ oldObject = ::SelectObject(memory, bitmap);
		const BOOL copied = ::BitBlt(memory, 0, 0, outWidth, outHeight, screen, origin.x, origin.y, SRCCOPY);
		std::vector<uint8_t> rgba(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u, 0);
		bool ok = copied != 0;
		if (ok)
		{
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = outWidth;
			info.bmiHeader.biHeight = -outHeight; // 自上而下，stbi 按行写
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			ok = ::GetDIBits(memory, bitmap, 0, static_cast<UINT>(outHeight), rgba.data(), &info,
			                 DIB_RGB_COLORS) != 0;
			if (ok)
			{
				for (size_t index = 0; index + 3 < rgba.size(); index += 4)
				{
					const uint8_t blue = rgba[index];
					rgba[index] = rgba[index + 2];
					rgba[index + 2] = blue;
					rgba[index + 3] = 255;
				}
			}
		}
		::SelectObject(memory, oldObject);
		::DeleteObject(bitmap);
		::DeleteDC(memory);
		::ReleaseDC(nullptr, screen);
		if (!ok)
		{
			error = "BitBlt/GetDIBits 失败";
			return false;
		}
		if (stbi_write_png(path.c_str(), outWidth, outHeight, 4, rgba.data(), outWidth * 4) == 0)
		{
			error = "stbi_write_png 失败";
			return false;
		}
		return true;
	}

	std::string HexHresult(HRESULT result)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(result));
		return std::string(buffer);
	}

	LRESULT CALLBACK CaptureStimulusProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		return ::DefWindowProcW(window, message, wParam, lParam);
	}

	// 抓屏刺激窗口：64×64 无边框弹窗，抓帧期间用 GDI 交替刷红/蓝，让 DWM 持续产出新帧
	//（桌面复制的 AcquireNextFrame 只在桌面内容变化时返回）。
	HWND CreateCaptureStimulusWindow(int size)
	{
		const wchar_t* className = L"CascadedEditorCaptureStimulus";
		static bool registered = false;
		HINSTANCE instance = ::GetModuleHandleW(nullptr);
		if (!registered)
		{
			WNDCLASSEXW windowClass{};
			windowClass.cbSize = sizeof(windowClass);
			windowClass.lpfnWndProc = CaptureStimulusProc;
			windowClass.hInstance = instance;
			windowClass.lpszClassName = className;
			// 刺激窗口只用来制造桌面更新，不需要光标；不写 hCursor 也避免 IDC_ARROW 的
			// ANSI/UNICODE 宏在个别包含顺序下解析成 LPSTR（实测 C2664）。
			windowClass.hCursor = nullptr;
			if (::RegisterClassExW(&windowClass) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			{
				return nullptr;
			}
			registered = true;
		}
		return ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, className,
			L"capture-stimulus", WS_POPUP | WS_VISIBLE, 0, 0, size, size, nullptr, nullptr, instance, nullptr);
	}

	// 用 DXGI Desktop Duplication 抓真实桌面输出（含 independent-flip 交换链）上的一点。
	// 用途（editor-window-panel 3.2 ⑩）：屏幕 DC 的 BitBlt 在 flip 呈现下可能只抓到窗口背景（白），
	// 这条通道拿到的是 DWM 真实合成输出；判据仍是"视口中心是三角形红"，不放松。
	bool CaptureDesktopPixelDxgi(int screenX, int screenY, HWND stimulusWindow, unsigned char outRgba[4],
		std::string& error, int acquireAttempts = 20, int acquireTimeoutMs = 200)
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		HRESULT result = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
		if (FAILED(result) || device == nullptr)
		{
			error = "D3D11CreateDevice 失败：" + HexHresult(result);
			return false;
		}
		IDXGIDevice* dxgiDevice = nullptr;
		IDXGIAdapter* adapter = nullptr;
		IDXGIOutput* output = nullptr;
		IDXGIOutput1* output1 = nullptr;
		IDXGIOutputDuplication* duplication = nullptr;
		bool success = false;
		do
		{
			result = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
			if (FAILED(result) || dxgiDevice == nullptr)
			{
				error = "IDXGIDevice 取不到：" + HexHresult(result);
				break;
			}
			result = dxgiDevice->GetAdapter(&adapter);
			if (FAILED(result) || adapter == nullptr)
			{
				error = "GetAdapter 失败：" + HexHresult(result);
				break;
			}
			DXGI_OUTPUT_DESC outputDesc{};
			for (UINT index = 0;; ++index)
			{
				IDXGIOutput* candidate = nullptr;
				if (adapter->EnumOutputs(index, &candidate) == DXGI_ERROR_NOT_FOUND || candidate == nullptr)
				{
					break;
				}
				DXGI_OUTPUT_DESC candidateDesc{};
				candidate->GetDesc(&candidateDesc);
				const RECT& rect = candidateDesc.DesktopCoordinates;
				if (screenX >= rect.left && screenX < rect.right && screenY >= rect.top && screenY < rect.bottom)
				{
					output = candidate;
					outputDesc = candidateDesc;
					break;
				}
				candidate->Release();
			}
			if (output == nullptr)
			{
				error = "找不到包含采样点的显示器输出";
				break;
			}
			result = output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&output1));
			if (FAILED(result) || output1 == nullptr)
			{
				error = "IDXGIOutput1 取不到：" + HexHresult(result);
				break;
			}
			result = output1->DuplicateOutput(device, &duplication);
			if (FAILED(result) || duplication == nullptr)
			{
				error = "DuplicateOutput 失败：" + HexHresult(result) +
					"（可能有别的程序正在抓屏，或当前会话不支持桌面复制）";
				break;
			}
			// 诊断读数（请求 K 要求）：DuplicateOutput 的 HRESULT 与选中 output 的桌面坐标，
			// 用于核对"duplication 抓的是不是窗口所在那块输出"。
			CA_LOG("[编辑器 测试] ⑩ DXGI 诊断：DuplicateOutput={} desktop=({},{},{},{}) 采样点=({},{}) "
			       "attached={}",
			       HexHresult(result).c_str(), outputDesc.DesktopCoordinates.left,
			       outputDesc.DesktopCoordinates.top, outputDesc.DesktopCoordinates.right,
			       outputDesc.DesktopCoordinates.bottom, screenX, screenY,
			       outputDesc.AttachedToDesktop ? 1 : 0);
			const int localX = screenX - outputDesc.DesktopCoordinates.left;
			const int localY = screenY - outputDesc.DesktopCoordinates.top;
			for (int attempt = 0; attempt < acquireAttempts && !success; ++attempt)
			{
				// 桌面复制只在桌面内容变化时递帧。同一个 duplication 会话里、每次 Acquire 之前用 GDI 在
				// 刺激窗口上刷一块交替色，保证 DWM 有持续更新（新建 duplication 后第一次 Acquire 也常超时）。
				if (stimulusWindow != nullptr)
				{
					HDC stimulusDc = ::GetDC(stimulusWindow);
					if (stimulusDc != nullptr)
					{
						RECT stimulusRect{};
						::GetClientRect(stimulusWindow, &stimulusRect);
						HBRUSH stimulusBrush =
							::CreateSolidBrush((attempt % 2 == 0) ? RGB(255, 0, 0) : RGB(0, 0, 255));
						::FillRect(stimulusDc, &stimulusRect, stimulusBrush);
						::DeleteObject(stimulusBrush);
						::ReleaseDC(stimulusWindow, stimulusDc);
					}
				}
				// 光标移动是桌面复制帧的固有部分（比 GDI 刺激更直接）：在采样点附近左右各 1px 移动。
				::SetCursorPos(screenX + ((attempt % 2 == 0) ? 1 : -1), screenY);
				::Sleep(30);
				DXGI_OUTDUPL_FRAME_INFO frameInfo{};
				IDXGIResource* resource = nullptr;
				result = duplication->AcquireNextFrame(static_cast<UINT>(acquireTimeoutMs), &frameInfo, &resource);
				if (result == DXGI_ERROR_WAIT_TIMEOUT)
				{
					continue;
				}
				if (FAILED(result) || resource == nullptr)
				{
					error = "AcquireNextFrame 失败：" + HexHresult(result);
					break;
				}
				ID3D11Texture2D* texture = nullptr;
				result = resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
				if (SUCCEEDED(result) && texture != nullptr)
				{
					D3D11_TEXTURE2D_DESC textureDesc{};
					texture->GetDesc(&textureDesc);
					D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
					stagingDesc.Usage = D3D11_USAGE_STAGING;
					stagingDesc.BindFlags = 0;
					stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					stagingDesc.MiscFlags = 0;
					stagingDesc.ArraySize = 1;
					stagingDesc.MipLevels = 1;
					stagingDesc.SampleDesc.Count = 1;
					stagingDesc.SampleDesc.Quality = 0;
					ID3D11Texture2D* staging = nullptr;
					if (SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)) && staging != nullptr)
					{
						context->CopyResource(staging, texture);
						D3D11_MAPPED_SUBRESOURCE mapped{};
						if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
						{
							if (localX >= 0 && localY >= 0 && localX < static_cast<int>(textureDesc.Width) &&
								localY < static_cast<int>(textureDesc.Height))
							{
								const unsigned char* row = static_cast<const unsigned char*>(mapped.pData) +
									static_cast<size_t>(localY) * mapped.RowPitch +
									static_cast<size_t>(localX) * 4u;
								outRgba[0] = row[2];
								outRgba[1] = row[1];
								outRgba[2] = row[0];
								outRgba[3] = row[3];
								success = true;
							}
							else
							{
								error = "采样点不在桌面纹理范围内";
							}
							context->Unmap(staging, 0);
						}
						else
						{
							error = "Map staging 纹理失败";
						}
						staging->Release();
					}
					else
					{
						error = "创建 staging 纹理失败";
					}
					texture->Release();
				}
				else
				{
					error = "桌面资源转 ID3D11Texture2D 失败";
				}
				resource->Release();
				duplication->ReleaseFrame();
				if (!success)
				{
					::Sleep(100);
				}
			}
			if (!success && error.empty())
			{
				error = "Desktop Duplication " + std::to_string(acquireAttempts) + " 次尝试内没有可用帧";
			}
		} while (false);
		if (duplication != nullptr)
		{
			duplication->Release();
		}
		if (output1 != nullptr)
		{
			output1->Release();
		}
		if (output != nullptr)
		{
			output->Release();
		}
		if (adapter != nullptr)
		{
			adapter->Release();
		}
		if (dxgiDevice != nullptr)
		{
			dxgiDevice->Release();
		}
		if (context != nullptr)
		{
			context->Release();
		}
		if (device != nullptr)
		{
			device->Release();
		}
		return success;
	}

	// Windows.Graphics.Capture（按窗口抓内容）：主通道。窗口内容由 DWM 直接交付，不依赖桌面变化递帧，
	// 也不依赖 BitBlt 能否看到 flip-model 呈现。采样点按「捕获帧尺寸 / 读数客户区尺寸」换算。
	bool CaptureItemPixelWgc(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item, int clientX,
		int clientY, int referenceClientWidth, int referenceClientHeight, unsigned char outRgba[4],
		std::string& error, int timeoutMs = 6000)
	{
		using namespace winrt;
		using namespace winrt::Windows::Graphics;
		using namespace winrt::Windows::Graphics::Capture;
		using namespace winrt::Windows::Graphics::DirectX;
		using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
		try
		{
			try
			{
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
			}
			catch (winrt::hresult_error const&)
			{
				// 线程已初始化过（公寓模式不同）：自由线程帧池不依赖公寓模式，继续。
			}
			com_ptr<ID3D11Device> device;
			com_ptr<ID3D11DeviceContext> context;
			D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
			check_hresult(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, device.put(), &featureLevel,
				context.put()));
			com_ptr<IDXGIDevice> dxgiDevice = device.as<IDXGIDevice>();
			com_ptr<::IInspectable> inspectableDevice;
			check_hresult(::CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectableDevice.put()));
			IDirect3DDevice direct3DDevice = inspectableDevice.as<IDirect3DDevice>();

			auto framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(direct3DDevice,
				DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
			auto session = framePool.CreateCaptureSession(item);

			struct WgcFrameState
			{
				std::mutex Mutex;
				bool Got = false;
				int Width = 0;
				int Height = 0;
				std::string Error;
				unsigned char Pixel[4] = { 0, 0, 0, 0 };
			};
			// 帧回调可能在函数返回后仍有 in-flight 一次：状态放 shared_ptr 里被回调按值持有，避免悬空引用。
			auto state = std::make_shared<WgcFrameState>();
			auto handler = [state, device, context, clientX, clientY, referenceClientWidth, referenceClientHeight](
				Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
				try
				{
					auto frame = sender.TryGetNextFrame();
					::IUnknown* rawSurface = winrt::get_unknown(frame.Surface());
					com_ptr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
					check_hresult(rawSurface->QueryInterface(
						__uuidof(::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
						access.put_void()));
					com_ptr<ID3D11Texture2D> texture;
					check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
					D3D11_TEXTURE2D_DESC description{};
					texture->GetDesc(&description);
					D3D11_TEXTURE2D_DESC stagingDescription = description;
					stagingDescription.Usage = D3D11_USAGE_STAGING;
					stagingDescription.BindFlags = 0;
					stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
					stagingDescription.MiscFlags = 0;
					stagingDescription.ArraySize = 1;
					stagingDescription.MipLevels = 1;
					stagingDescription.SampleDesc.Count = 1;
					stagingDescription.SampleDesc.Quality = 0;
					com_ptr<ID3D11Texture2D> stagingTexture;
					check_hresult(device->CreateTexture2D(&stagingDescription, nullptr, stagingTexture.put()));
					context->CopyResource(stagingTexture.get(), texture.get());
					D3D11_MAPPED_SUBRESOURCE mapped{};
					check_hresult(context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));
					const int width = static_cast<int>(description.Width);
					const int height = static_cast<int>(description.Height);
					int sampleX = clientX;
					int sampleY = clientY;
					if (referenceClientWidth > 0 && referenceClientHeight > 0)
					{
						sampleX = static_cast<int>(static_cast<long long>(clientX) * width / referenceClientWidth);
						sampleY = static_cast<int>(static_cast<long long>(clientY) * height / referenceClientHeight);
					}
					{
						std::lock_guard<std::mutex> guard(state->Mutex);
						state->Width = width;
						state->Height = height;
						if (sampleX >= 0 && sampleY >= 0 && sampleX < width && sampleY < height)
						{
							const unsigned char* row = static_cast<const unsigned char*>(mapped.pData) +
								static_cast<size_t>(sampleY) * mapped.RowPitch +
								static_cast<size_t>(sampleX) * 4u;
							state->Pixel[0] = row[2];
							state->Pixel[1] = row[1];
							state->Pixel[2] = row[0];
							state->Pixel[3] = row[3];
							state->Got = true;
						}
						else
						{
							state->Error = "采样点不在捕获帧内";
						}
					}
					context->Unmap(stagingTexture.get(), 0);
				}
				catch (winrt::hresult_error const& ex)
				{
					std::lock_guard<std::mutex> guard(state->Mutex);
					state->Error =
						"帧处理失败：" + winrt::to_string(ex.message()) + "（" + HexHresult(ex.code()) + "）";
				}
				catch (...)
				{
					std::lock_guard<std::mutex> guard(state->Mutex);
					state->Error = "帧处理失败：未知异常";
				}
			};
			auto token = framePool.FrameArrived(handler);
			session.StartCapture();
			for (int waited = 0; waited < timeoutMs; waited += 50)
			{
				{
					std::lock_guard<std::mutex> guard(state->Mutex);
					if (state->Got)
					{
						break;
					}
				}
				::Sleep(50);
			}
			session.Close();
			framePool.FrameArrived(token);
			framePool.Close();
			{
				std::lock_guard<std::mutex> guard(state->Mutex);
				if (!state->Got)
				{
					error = state->Error.empty() ? "Windows.Graphics.Capture 在超时内没有帧" : state->Error;
					return false;
				}
				outRgba[0] = state->Pixel[0];
				outRgba[1] = state->Pixel[1];
				outRgba[2] = state->Pixel[2];
				outRgba[3] = state->Pixel[3];
				CA_LOG("[编辑器 测试] ⑩ WGC 帧尺寸={}x{} 采样点=({},{})", state->Width, state->Height, clientX,
				       clientY);
			}
			return true;
		}
		catch (winrt::hresult_error const& ex)
		{
			error = "Windows.Graphics.Capture 失败：" + winrt::to_string(ex.message()) + "（" + HexHresult(ex.code()) +
				"）";
			return false;
		}
		catch (std::exception const& ex)
		{
			error = std::string("Windows.Graphics.Capture 异常：") + ex.what();
			return false;
		}
	}

	// WGC 按窗口捕获：采样点是客户区坐标，按「帧尺寸 / 读数客户区尺寸」换算（帧可能含边框）。
	bool CaptureWindowPixelWgc(HWND hwnd, int clientX, int clientY, int referenceClientWidth,
		int referenceClientHeight, unsigned char outRgba[4], std::string& error, int timeoutMs = 6000)
	{
		using namespace winrt;
		using namespace winrt::Windows::Graphics::Capture;
		try
		{
			auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
			GraphicsCaptureItem item{ nullptr };
			check_hresult(interop->CreateForWindow(hwnd, winrt::guid_of<GraphicsCaptureItem>(),
				winrt::put_abi(item)));
			return CaptureItemPixelWgc(item, clientX, clientY, referenceClientWidth, referenceClientHeight, outRgba,
				error, timeoutMs);
		}
		catch (winrt::hresult_error const& ex)
		{
			error = "CreateForWindow 失败：" + winrt::to_string(ex.message()) + "（" + HexHresult(ex.code()) + "）";
			return false;
		}
	}

	// WGC 按显示器捕获：DWM 合成后的真实屏幕内容（independent-flip 的窗口也应包含），是展示证明最强通道。
	bool CaptureMonitorPixelWgc(HMONITOR monitor, int screenX, int screenY, unsigned char outRgba[4],
		std::string& error, int timeoutMs = 6000)
	{
		using namespace winrt;
		using namespace winrt::Windows::Graphics::Capture;
		try
		{
			MONITORINFO monitorInfo{};
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (!::GetMonitorInfoW(monitor, &monitorInfo))
			{
				error = "GetMonitorInfo 失败";
				return false;
			}
			auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
			GraphicsCaptureItem item{ nullptr };
			check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(),
				winrt::put_abi(item)));
			const int monitorWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
			const int monitorHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
			const int localX = screenX - monitorInfo.rcMonitor.left;
			const int localY = screenY - monitorInfo.rcMonitor.top;
			return CaptureItemPixelWgc(item, localX, localY, monitorWidth, monitorHeight, outRgba, error, timeoutMs);
		}
		catch (winrt::hresult_error const& ex)
		{
			error = "CreateForMonitor 失败：" + winrt::to_string(ex.message()) + "（" + HexHresult(ex.code()) + "）";
			return false;
		}
	}

	// 渲染证明（⑨/⑪）：按读数行的 graphSelf 解析出离屏捕获纹理，Readback 它的中心像素并判红。
	// 容差与 D12 的屏幕捕获一致（R>200、G<60、B<60），背景是 (0,128,0) 的清除色，不会误判。
	bool ReadCaptureCenterRed(TestContext& ctx, HostProbeReadings const& readings, std::string& detail)
	{
		ProbeNativeObjects objects = ResolveProbeObjects(readings);
		if (objects.graph == nullptr || !objects.captureImage.IsValid())
		{
			detail = "读数行 graphSelf/capture 解析失败（graph=0x" +
				std::to_string(reinterpret_cast<uintptr_t>(objects.graph)) + "）";
			return false;
		}
		int captureWidth = 0;
		int captureHeight = 0;
		if (!ParseSize(readings.Get("capture"), captureWidth, captureHeight) || captureWidth <= 0 ||
			captureHeight <= 0)
		{
			detail = "读数行的捕获尺寸非法：" + readings.Get("capture");
			return false;
		}
		std::vector<uint8_t> pixels(static_cast<size_t>(
			GetImageByteSize(captureWidth, captureHeight, ETextureFormat::E_R8G8B8A8_UNORM)));
		auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
		if (!token)
		{
			detail = "Readback 返回空 token";
			return false;
		}
		token->Wait();
		const size_t index =
			(static_cast<size_t>(captureHeight / 2) * static_cast<size_t>(captureWidth) +
			 static_cast<size_t>(captureWidth / 2)) * 4u;
		const unsigned char r = pixels[index];
		const unsigned char g = pixels[index + 1];
		const unsigned char b = pixels[index + 2];
		const unsigned char a = pixels[index + 3];
		char text[256] = {};
		std::snprintf(text, sizeof(text), "capture=%dx%d center=(%u,%u,%u,%u)", captureWidth, captureHeight,
		              r, g, b, a);
		detail = text;
		// 判据（工件修订：与既有探针一致的精确像素，不放宽）：离屏捕获中心必须精确是三角形红。
		return r == 255 && g == 0 && b == 0 && a == 255;
	}

	std::string ReadWindowTitle(cawindow::IWindow* window)
	{
		if (window == nullptr)
		{
			return std::string();
		}
		castl::string_view title = window->GetWindowName();
		return std::string(title.data(), title.size());
	}
}

// ---- TestEditorWindowPanel（editor-window-panel 第 18 用例）----
// 覆盖（tasks 1.2/1.3/1.5/1.6/1.7 与 3.1/3.2）：服务门控与握手、对象往返（C++ 读回）、撤销、六个协议
// 错误码、对象失效、线程边界、单窗口分栏读数、scene view 两层像素证据、改尺寸后重建。
//
// 读数来源：托管侧每帧写 `[编辑器 读数]`（窗口/分栏/图对象句柄/执行计数/命令原生调用计数），C++ 侧读
// 自己的日志；像素由 C++ 侧按 `graphSelf` 解析出离屏捕获纹理后 Readback（1.7 修订：托管绑定面无
// Readback）。本用例必须用 `--headless` 跑（日志才会落盘到 test_output/TestEditorWindowPanel.log）。
void TestEditorWindowPanel(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};

	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		fail("E_EDITOR_HOST", "IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		fail("E_EDITOR_HOST", castl::string("托管侧不可用：") + pScriptHost->LastError());
	}

	struct EditorEnvGuard
	{
		~EditorEnvGuard()
		{
			::SetEnvironmentVariableA("CASCADED_EDITOR", nullptr);
			::SetEnvironmentVariableA("CASCADED_EDITOR_PORT", nullptr);
			::SetEnvironmentVariableA("CASCADED_EDITOR_PANEL_WIDTH", nullptr);
			::SetEnvironmentVariableA("CASCADED_EDITOR_WEBVIEW2_DATA", nullptr);
			::SetEnvironmentVariableA("CASCADED_EDITOR_STOP", nullptr);
			::SetEnvironmentVariableA("CASCADED_EDITOR_READINGS_FILE", nullptr);
		}
	} envGuard;
	// D9：测试自己设门控与**专用端口**（避开默认 18390，防止干扰本机其它实例），跑后由守卫清理。
	::SetEnvironmentVariableA("CASCADED_EDITOR", "1");
	::SetEnvironmentVariableA("CASCADED_EDITOR_PORT", "18391");

	// 编号 1 = 一个存活窗口（默认 IWindow 表面），编号 2 = 窗口系统；编辑器另建它**唯一**的目标窗口。
	auto newWindow = ctx.pWindowSystem->NewWindow(640, 480, "Editor Test Host Window");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_EDITOR_WINDOW", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	const std::string baselineError = pScriptHost->LastError();

	// 真实帧语义：每帧 Tick（主线程 drain + 渲染）+ UpdateSystem（glfwPollEvents 泵线程消息）。
	auto pumpFrames = [&](int frames) {
		for (int index = 0; index < frames; ++index)
		{
			pScriptHost->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	};

	// 读数行落盘文件（托管侧每帧追加）：不依赖 --headless 的 stdout 重定向 —— 可见窗口模式
	// （CASCADED_EDITOR_DISPLAY_PROOF=1，不带 --headless）也能读读数与服务标记行。
	const char* readingsPath = "test_output/editor_readings.txt";
	std::filesystem::create_directories("test_output");
	std::error_code readingsRemoveError;
	std::filesystem::remove(readingsPath, readingsRemoveError);
	::SetEnvironmentVariableA("CASCADED_EDITOR_READINGS_FILE", readingsPath);
	char displayProofEnv[16] = {};
	::GetEnvironmentVariableA("CASCADED_EDITOR_DISPLAY_PROOF", displayProofEnv, sizeof(displayProofEnv));
	const bool displayProofMode = std::string(displayProofEnv) == "1";
	pumpFrames(1); // 第一次 Tick：绑定表面 + 脚本建常驻窗
	const int baselineCount = ctx.pWindowSystem->GetWindowCount();

	// M14（工件修订）：Debug 自检开关 no-runtime 变体 —— 断言编辑器明确禁用、进程不崩，然后结束用例。
	{
		char selfTest[64] = {};
		::GetEnvironmentVariableA("CASCADED_EDITOR_SELFTEST", selfTest, sizeof(selfTest));
		if (std::string(selfTest) == "no-runtime")
		{
			pumpFrames(6);
			HostProbeReadings disabledReadings;
			if (ReadEditorReadings(readingsPath, &disabledReadings) && disabledReadings.Get("state") == "running")
			{
				fail("E_EDITOR_NO_RUNTIME", "自检开关 no-runtime 下编辑器仍进入运行态");
			}
			if (!LogContains(readingsPath, "禁用：") || !LogContains(readingsPath, "运行时缺失"))
			{
				fail("E_EDITOR_NO_RUNTIME", "no-runtime 自检没有产生明确的运行时缺失禁用日志");
			}
			if (!LogContains(readingsPath, "已关闭"))
			{
				fail("E_EDITOR_NO_RUNTIME", "no-runtime 禁用之后服务没有走关闭路径");
			}
			CA_LOG("[编辑器 测试] M14 no-runtime 自检通过：编辑器明确禁用（日志含运行时缺失原因）、服务已关闭，"
			       "进程未崩");
			windowKeepAlive->CloseWindow();
			windowKeepAlive.reset();
			ctx.pWindowSystem->UpdateSystem();
			return;
		}
	}

	// 等编辑器进入运行态：读数行 objects=1（目标窗口已登记）+ controller=1 + nav=1 + exec>0（已渲染过）。
	// 上限 40 秒（测试内常量，不是产品默认值）：WebView2 首启 + 渲染首帧；0.2 探针实测 22 帧内完成。
	HostProbeReadings readings;
	bool ready = false;
	{
		const auto startedAt = std::chrono::steady_clock::now();
		int frames = 0;
		while (true)
		{
			const double elapsedMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
			if (elapsedMs > 40000.0)
			{
				break;
			}
			++frames;
			pScriptHost->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
			if ((frames % 5) == 0 && ReadEditorReadings(readingsPath, &readings) &&
				readings.Get("objects") == "1" && readings.I64("exec") > 0 &&
				readings.Get("controller") == "1" && readings.Get("nav") == "1")
			{
				ready = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	}
	if (!ready)
	{
		ReadEditorReadings(readingsPath, &readings);
		char detail[768] = {};
		std::snprintf(detail, sizeof(detail),
		              "编辑器 40 秒内没有进入运行态：objects=%s controller=%s nav=%s exec=%s error=%s；LastError=%s",
		              readings.Get("objects").c_str(), readings.Get("controller").c_str(),
		              readings.Get("nav").c_str(), readings.Get("exec").c_str(), readings.Get("error").c_str(),
		              pScriptHost->LastError());
		fail("E_EDITOR_START", detail);
	}
	CA_LOG("[编辑器 测试] 启动读数：mode={} objects={} controller={} nav={} exec={} client={} panel={} "
	       "viewport={} windowSelf={}",
	       readings.Get("mode").c_str(), readings.Get("objects").c_str(), readings.Get("controller").c_str(),
	       readings.Get("nav").c_str(), readings.Get("exec").c_str(), readings.Get("client").c_str(),
	       readings.Get("panel").c_str(), readings.Get("viewport").c_str(), readings.Get("windowSelf").c_str());

	// ⑥ 单窗口分栏：窗口计数增量 1；读数行有控制器 + 导航 + 模式 + 面板矩形/视口区域，两者铺满客户区。
	const int countAfterStart = ctx.pWindowSystem->GetWindowCount();
	if (countAfterStart != baselineCount + 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "编辑器窗口计数不符：基线 %d → 运行态 %d（期望 +1，不许出现第二个壳窗口）", baselineCount,
		              countAfterStart);
		fail("E_EDITOR_WINDOW_COUNT", detail);
	}
	if (readings.Get("mode") != "main-thread")
	{
		fail("E_EDITOR_MODE", "UI 线程模式不是 main-thread：" + readings.Get("mode"));
	}
	int clientWidth = 0;
	int clientHeight = 0;
	Rect4 panelRect;
	Rect4 viewportRect;
	if (!ParseSize(readings.Get("client"), clientWidth, clientHeight) ||
		!ParseRect(readings.Get("panel"), panelRect) || !ParseRect(readings.Get("viewport"), viewportRect))
	{
		fail("E_EDITOR_LAYOUT_READINGS", "读数行缺少 client/panel/viewport：" + readings.Get("panel") + " / " +
			readings.Get("viewport"));
	}
	if (panelRect.x != viewportRect.w || panelRect.w + viewportRect.w != clientWidth ||
		viewportRect.h != clientHeight || panelRect.h != clientHeight)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "分栏没有铺满客户区：client=%dx%d viewport=(%d,%d,%d,%d) panel=(%d,%d,%d,%d)", clientWidth,
		              clientHeight, viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, panelRect.x,
		              panelRect.y, panelRect.w, panelRect.h);
		fail("E_EDITOR_LAYOUT", detail);
	}
	const uint64_t windowSelf = readings.U64("windowSelf");
	if (windowSelf == 0)
	{
		fail("E_EDITOR_WINDOW_SELF", "读数行 windowSelf 为 0");
	}
	auto* targetWindow = reinterpret_cast<cawindow::IWindow*>(static_cast<uintptr_t>(windowSelf));
	const std::string originalTitle = ReadWindowTitle(targetWindow);
	CA_LOG("[编辑器 测试] 目标窗口：windowSelf=0x{:x} 原标题=\"{}\" 初始尺寸={}x{}", windowSelf,
	       originalTitle.c_str(), clientWidth, clientHeight);

	// ⑨ 渲染证明（首帧）：读数行 exec>0 且离屏捕获中心像素是三角形红。
	{
		std::string pixelDetail;
		const bool red = ReadCaptureCenterRed(ctx, readings, pixelDetail);
		CA_LOG("[编辑器 像素] 渲染证明（首帧）：{}（exec={} capture={}）", pixelDetail.c_str(),
		       readings.Get("exec").c_str(), readings.Get("capture").c_str());
		if (!red)
		{
			fail("E_EDITOR_CAPTURE_PIXEL", "离屏捕获中心不是三角形红：" + pixelDetail);
		}
	}

	// M6（工件修订）视口正控：直接从原生图记录里读**批级与绘制级**的视口/剪裁矩形：
	//   · 捕获 pass（外部纹理附件、无参 AddTrianglePass）= 整张捕获目标；
	//   · 窗口 pass（后台缓冲附件、带视口重载）= 左侧视口区域。
	// 现有 17 用例不读这两个字段，所以这条是独立判据（默认视口不被零矩形/错值破坏）。
	{
		ProbeNativeObjects objects = ResolveProbeObjects(readings);
		if (objects.graph == nullptr)
		{
			fail("E_EDITOR_GRAPH", "读数行 graphSelf 解析失败（图对象为空）");
		}
		int captureWidth = 0;
		int captureHeight = 0;
		if (!ParseSize(readings.Get("capture"), captureWidth, captureHeight) || captureWidth <= 0 ||
			captureHeight <= 0)
		{
			fail("E_EDITOR_GRAPH", "读数行 capture 尺寸非法：" + readings.Get("capture"));
		}
		Rect4 viewportNow;
		if (!ParseRect(readings.Get("viewport"), viewportNow))
		{
			fail("E_EDITOR_GRAPH", "读数行 viewport 非法");
		}
		int checkedPasses = 0;
		for (RenderPass const& pass : objects.graph->GetRenderPasses())
		{
			ImageHandle attachment;
			for (ImageHandle const& candidate : pass.GetAttachments())
			{
				if (candidate.IsValid())
				{
					attachment = candidate;
					break;
				}
			}
			if (!attachment.IsValid())
			{
				fail("E_EDITOR_GRAPH", "图里的 pass 没有附件");
			}
			const bool backbuffer = attachment.GetType() == ImageHandle::ImageType::Backbuffer;
			const int expectedX = 0;
			const int expectedY = 0;
			const int expectedW = backbuffer ? viewportNow.w : captureWidth;
			const int expectedH = backbuffer ? viewportNow.h : captureHeight;
			for (DrawCallBatch const& batch : pass.GetDrawCallBatches())
			{
				if (!batch.m_ViewPort.Valid() || !batch.m_Sissor.Valid())
				{
					fail("E_EDITOR_DEFAULT_VIEWPORT", "pass 的批级视口/剪裁矩形无效（默认视口正控失败）");
				}
				ViewRectData const& batchViewport = batch.m_ViewPort.Get();
				ViewRectData const& batchScissor = batch.m_Sissor.Get();
				if (batchViewport.x != expectedX || batchViewport.y != expectedY ||
					batchViewport.width != expectedW || batchViewport.height != expectedH ||
					batchScissor.x != expectedX || batchScissor.y != expectedY ||
					batchScissor.width != expectedW || batchScissor.height != expectedH)
				{
					char detail[512] = {};
					std::snprintf(detail, sizeof(detail),
					              "批级矩形不符（backbuffer=%d）：viewport=(%d,%d,%d,%d) scissor=(%d,%d,%d,%d) "
					              "期望=(%d,%d,%d,%d)",
					              backbuffer ? 1 : 0, batchViewport.x, batchViewport.y, batchViewport.width,
					              batchViewport.height, batchScissor.x, batchScissor.y, batchScissor.width,
					              batchScissor.height, expectedX, expectedY, expectedW, expectedH);
					fail("E_EDITOR_DEFAULT_VIEWPORT", detail);
				}
				for (DrawCall const& call : batch.m_DrawCalls)
				{
					if (!call.GetViewPort().Valid() || !call.GetScissor().Valid())
					{
						fail("E_EDITOR_DEFAULT_VIEWPORT", "绘制级视口/剪裁矩形无效");
					}
					ViewRectData const& callViewport = call.GetViewPort().Get();
					ViewRectData const& callScissor = call.GetScissor().Get();
					if (callViewport.x != expectedX || callViewport.y != expectedY ||
						callViewport.width != expectedW || callViewport.height != expectedH ||
						callScissor.x != expectedX || callScissor.y != expectedY ||
						callScissor.width != expectedW || callScissor.height != expectedH)
					{
						fail("E_EDITOR_DEFAULT_VIEWPORT", "绘制级矩形与批级/期望不一致");
					}
				}
			}
			++checkedPasses;
		}
		if (checkedPasses != 2)
		{
			char detail[128] = {};
			std::snprintf(detail, sizeof(detail), "图的 pass 数 %d ≠ 2（窗口 + 捕获）", checkedPasses);
			fail("E_EDITOR_GRAPH", detail);
		}
		CA_LOG("[编辑器 测试] M6 视口正控通过：2 个 pass 的批级/绘制级视口与剪裁矩形分别等于窗口左侧视口"
		       "({}x{}) 与整张捕获目标({}x{})",
		       viewportNow.w, viewportNow.h, captureWidth, captureHeight);
	}

	// ⑩ 的诊断（初始布局，改尺寸之前）：把客户区整块抓成 PNG。这条不是断言，只是证据 ——
	// 屏幕捕获失败时用它区分"抓错区域"与"渲染没上屏"。
	if (DesktopSessionAvailable())
	{
		void* diagnosticHandle = targetWindow->GetNativeWindowHandle();
		HWND diagnosticWindow = diagnosticHandle == nullptr ? nullptr : *static_cast<HWND*>(diagnosticHandle);
		if (diagnosticWindow != nullptr)
		{
			::ShowWindow(diagnosticWindow, SW_SHOW);
			::SetForegroundWindow(diagnosticWindow);
			OtherWindowHider diagnosticHider;
			diagnosticHider.HideAllExcept(diagnosticWindow);
			::Sleep(300);
			ctx.pWindowSystem->UpdateSystem();
			int diagnosticWidth = 0;
			int diagnosticHeight = 0;
			std::string diagnosticError;
			const std::string diagnosticPath =
				std::string("test_output/editor_screen_capture_") + ctx.backendName + "_initial.png";
			if (SaveClientAreaPng(diagnosticWindow, diagnosticPath, diagnosticWidth, diagnosticHeight,
			                      diagnosticError))
			{
				CA_LOG("[编辑器 测试] 诊断 PNG（初始布局）：{} 客户区={}x{}", diagnosticPath.c_str(), diagnosticWidth,
				       diagnosticHeight);
			}
			else
			{
				CA_LOG("[编辑器 测试] 诊断 PNG 写入失败：{}", diagnosticError.c_str());
			}
			diagnosticHider.RestoreAll();
		}
	}

	// ⑥ 单窗口尺寸同步：C++ 侧直接 SetWindowSize → 读数行的客户区/视口/面板必须同步。
	targetWindow->SetWindowSize(1000, 640);
	pumpFrames(4);
	if (!ReadEditorReadings(readingsPath, &readings))
	{
		fail("E_EDITOR_LAYOUT_READINGS", "尺寸同步后没有读数行");
	}
	if (readings.Get("client") != "1000x640")
	{
		fail("E_EDITOR_LAYOUT", "C++ SetWindowSize(1000,640) 之后读数行 client=" + readings.Get("client"));
	}
	if (!ParseRect(readings.Get("viewport"), viewportRect) || !ParseRect(readings.Get("panel"), panelRect))
	{
		fail("E_EDITOR_LAYOUT_READINGS", "尺寸同步后读数行缺少矩形");
	}
	if (viewportRect.w != 680 || panelRect.x != 680 || panelRect.w != 320 || viewportRect.h != 640)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "改尺寸后分栏不对：viewport=(%d,%d,%d,%d) panel=(%d,%d,%d,%d)",
		              viewportRect.x, viewportRect.y, viewportRect.w, viewportRect.h, panelRect.x, panelRect.y,
		              panelRect.w, panelRect.h);
		fail("E_EDITOR_LAYOUT", detail);
	}
	CA_LOG("[编辑器 测试] ⑥ 单窗口分栏：计数 +1、控制器/导航就绪、C++ SetWindowSize 后读数同步（client=1000x640、"
	       "viewport=680x640、panel=680,0,320,640）");

	// 1.2 的静态页读数：资源存在 ⇒ 200 + 页面本体；资源缺失 ⇒ 503 + 说明"资源未构建"的错误页。
	// **期望值来自服务自己打出的「面板静态根：<绝对路径>」行**（读数文件，两条通道都在），
	// 不在测试端按 CWD 另算一遍 —— 1.11 把静态根从 CWD 相对改成绝对路径之后，原来的
	// `exists("editor-web/index.html")` 探测与服务实际行为脱节（cwd=仓库根 ⇒ 探测 false、服务 200），
	// 那是 editor 批遗留的判据 bug，本批全量回归里以 E_EDITOR_HTTP_PAGE 暴露。
	{
		int status = 0;
		std::string body;
		std::string httpError;
		if (!HttpGet(18391, "/", status, body, httpError))
		{
			fail("E_EDITOR_HTTP", httpError);
		}
		std::string staticRoot;
		if (!ReadLoggedValue(readingsPath, "面板静态根：", &staticRoot))
		{
			fail("E_EDITOR_HTTP_PAGE",
			     "读数文件里没有「面板静态根：」行，静态页判据的期望值无法确定（服务没走 ServeStatic？）");
		}
		const bool hasIndex = std::filesystem::exists(std::filesystem::path(staticRoot) / "index.html");
		// 判据只认 ASCII 标记（"editor-web" 出现在错误页正文里）：C++ 窄字符串与 UTF-8 的编码约定不写死，
		// 中文正文只用来看，不用来比。
		const bool hasErrorMarker = body.find("editor-web") != std::string::npos;
		CA_LOG("[编辑器 测试] 静态页：status={} bytes={} index存在={} 静态根={} editor-web标记={}", status,
		       static_cast<long long>(body.size()), hasIndex ? 1 : 0, staticRoot.c_str(), hasErrorMarker ? 1 : 0);
		if (hasIndex)
		{
			if (status != 200)
			{
				fail("E_EDITOR_HTTP_PAGE", "面板资源已构建但 GET / 返回 " + std::to_string(status));
			}
		}
		else if (status != 503 || !hasErrorMarker)
		{
			fail("E_EDITOR_HTTP_PAGE", "缺面板资源时没有返回说明原因的错误页：status=" + std::to_string(status));
		}
	}

	// ---- WebSocket 会话（3.1 正向 + 3.2 负向）----
	WsClient client;
	std::string wsError;
	if (!client.Connect(18391, wsError))
	{
		fail("E_EDITOR_WS", wsError);
	}
	std::vector<std::string> events;
	auto sendCommand = [&](std::string const& json) {
		std::string error;
		if (!client.SendText(json, error))
		{
			fail("E_EDITOR_WS_SEND", error);
		}
	};
	auto waitResult = [&](int requestId, std::string& result) {
		if (!PumpUntilResult(pScriptHost, ctx, client, requestId, result, events, 10000))
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "requestId=%d 在 10 秒内没有收到 result", requestId);
			fail("E_EDITOR_WS_RESULT", detail);
		}
	};

	// 握手：welcome 带 protocolVersion=1 与对象快照。
	{
		sendCommand("{\"type\":\"hello\",\"protocolVersion\":1}");
		std::string welcome;
		const auto startedAt = std::chrono::steady_clock::now();
		for (;;)
		{
			const double elapsedMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
			if (elapsedMs > 5000.0)
			{
				fail("E_EDITOR_WS_WELCOME", "5 秒内没有收到 welcome");
			}
			std::string message;
			std::string receiveError;
			if (client.ReceiveText(message, 100, receiveError))
			{
				welcome = message;
				break;
			}
			if (receiveError.find("超时") == std::string::npos)
			{
				fail("E_EDITOR_WS_CLOSED", "握手期间连接被关闭：" + receiveError);
			}
			pScriptHost->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
		}
		CA_LOG("[编辑器 测试] welcome：{}", welcome.c_str());
		if (welcome.find("\"type\":\"welcome\"") == std::string::npos ||
			welcome.find("\"protocolVersion\":1") == std::string::npos)
		{
			fail("E_EDITOR_WS_WELCOME", "welcome 形状不对：" + welcome);
		}
		if (welcome.find("\"objects\":[{\"id\":1") == std::string::npos)
		{
			fail("E_EDITOR_WS_OBJECTS", "welcome 的对象快照里没有编号 1：" + welcome);
		}
	}

	// ① set-title 唯一串 → C++ 侧 GetWindowName 逐字相同。
	const std::string uniqueTitle = std::string("editor-title-") + std::to_string(::GetCurrentProcessId()) + "-" +
		std::to_string(static_cast<long long>(::GetTickCount64()));
	{
		sendCommand("{\"type\":\"command\",\"requestId\":101,\"command\":\"set-title\",\"objectId\":1,"
		            "\"args\":{\"title\":\"" + uniqueTitle + "\"}}");
		std::string result;
		waitResult(101, result);
		if (!JsonHas(result, "\"ok\":true"))
		{
			fail("E_EDITOR_SET_TITLE", "set-title 结果不是 ok：" + result);
		}
		const std::string readBack = ReadWindowTitle(targetWindow);
		if (readBack != uniqueTitle)
		{
			fail("E_EDITOR_TITLE_READBACK", "C++ 读回标题与请求不一致：读回=\"" + readBack + "\" 请求=\"" +
				uniqueTitle + "\"");
		}
		bool sawChanged = false;
		for (std::string const& event : events)
		{
			if (JsonIsEvent(event, "object-changed") && event.find("\"title\"") != std::string::npos)
			{
				sawChanged = true;
			}
		}
		events.clear();
		if (!sawChanged)
		{
			fail("E_EDITOR_EVENT_CHANGED", "set-title 成功但没有收到 object-changed(title) 事件");
		}
		CA_LOG("[编辑器 测试] ① set-title 往返：唯一串 = \"{}\"（C++ 读回逐字相同）", uniqueTitle.c_str());
	}

	// M7（工件修订）计数器正控：先证明"真实命令会让 cmdCalls 增长"，③④ 的"增量 0"才有对照。
	{
		pumpFrames(1);
		if (!ReadEditorReadings(readingsPath, &readings))
		{
			fail("E_EDITOR_COUNTER", "set-title 之后没有读数行");
		}
		if (readings.I64("cmdRequest") != 101 || readings.I64("cmdCalls") < 2)
		{
			fail("E_EDITOR_COUNTER", "计数器正控失败：cmdRequest=" + readings.Get("cmdRequest") + " cmdCalls=" +
				readings.Get("cmdCalls") + "（set-title 至少应有 SetWindowName + 读回两次原生调用）");
		}
		CA_LOG("[编辑器 测试] M7 计数器正控：set-title 期间 cmdCalls={}（>0，后续零增量断言有对照）",
		       readings.Get("cmdCalls").c_str());
	}

	// ⑦ 撤销：标题回到原值；再撤销 → E_UNDO_EMPTY。
	{
		sendCommand("{\"type\":\"command\",\"requestId\":102,\"command\":\"undo\",\"objectId\":1}");
		std::string result;
		waitResult(102, result);
		if (!JsonHas(result, "\"ok\":true"))
		{
			fail("E_EDITOR_UNDO", "undo 标题结果不是 ok：" + result);
		}
		const std::string afterUndo = ReadWindowTitle(targetWindow);
		if (afterUndo != originalTitle)
		{
			fail("E_EDITOR_UNDO", "undo 之后标题没有回到原值：读回=\"" + afterUndo + "\"");
		}
		sendCommand("{\"type\":\"command\",\"requestId\":103,\"command\":\"undo\",\"objectId\":1}");
		waitResult(103, result);
		if (!JsonHas(result, "\"ok\":false") || JsonErrorCode(result) != "E_UNDO_EMPTY")
		{
			fail("E_EDITOR_UNDO_EMPTY", "空槽 undo 不是 E_UNDO_EMPTY：" + result);
		}
		CA_LOG("[编辑器 测试] ⑦ 撤销：标题恢复原值，再撤销 → E_UNDO_EMPTY");
	}

	// ② set-size 合法宽高 → C++ 侧 GetWindowSize 相等；⑪ 改尺寸后捕获目标按新视口重建且中心仍红。
	{
		sendCommand("{\"type\":\"command\",\"requestId\":104,\"command\":\"set-size\",\"objectId\":1,"
		            "\"args\":{\"width\":900,\"height\":560}}");
		std::string result;
		waitResult(104, result);
		if (!JsonHas(result, "\"ok\":true"))
		{
			fail("E_EDITOR_SET_SIZE", "set-size 结果不是 ok：" + result);
		}
		int width = 0;
		int height = 0;
		targetWindow->GetWindowSize(width, height);
		if (width != 900 || height != 560)
		{
			char detail[128] = {};
			std::snprintf(detail, sizeof(detail), "C++ 读回尺寸 %dx%d ≠ 请求 900x560", width, height);
			fail("E_EDITOR_SIZE_READBACK", detail);
		}
		pumpFrames(3);
		if (!ReadEditorReadings(readingsPath, &readings))
		{
			fail("E_EDITOR_READINGS", "set-size 之后没有读数行");
		}
		if (readings.Get("capture") != "580x560")
		{
			fail("E_EDITOR_CAPTURE_SIZE", "改尺寸后捕获尺寸不是新视口 580x560：" + readings.Get("capture"));
		}
		std::string pixelDetail;
		const bool red = ReadCaptureCenterRed(ctx, readings, pixelDetail);
		CA_LOG("[编辑器 像素] 改尺寸后：{}（exec={}）", pixelDetail.c_str(), readings.Get("exec").c_str());
		if (!red)
		{
			fail("E_EDITOR_RESIZE_PIXEL", "改尺寸后离屏捕获中心不是三角形红：" + pixelDetail);
		}
		CA_LOG("[编辑器 测试] ②⑪ set-size 往返 + 捕获重建：读回 900x560、capture={}、中心红",
		       readings.Get("capture").c_str());
	}

	// ⑤ 线程边界：命令发出后、下一次 Tick drain 之前，C++ 侧标题必须不变（网络线程不许直接动引擎）。
	{
		const std::string laterTitle = uniqueTitle + "-after-tick";
		const std::string titleBefore = ReadWindowTitle(targetWindow);
		sendCommand("{\"type\":\"command\",\"requestId\":105,\"command\":\"set-title\",\"objectId\":1,"
		            "\"args\":{\"title\":\"" + laterTitle + "\"}}");
		// 只泵引擎消息、**不调 Tick**：监听线程有时间收包入队，但主线程没有 drain —— 标题必须不动。
		bool changedTooEarly = false;
		const auto startedAt = std::chrono::steady_clock::now();
		while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count() <
			800.0)
		{
			ctx.pWindowSystem->UpdateSystem();
			if (ReadWindowTitle(targetWindow) != titleBefore)
			{
				changedTooEarly = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		if (changedTooEarly)
		{
			fail("E_EDITOR_THREAD_BOUNDARY",
			     "命令在 Tick 之前就改了引擎状态：网络路径直接调用了引擎（D1 边界被破坏）");
		}
		std::string result;
		waitResult(105, result);
		if (!JsonHas(result, "\"ok\":true"))
		{
			fail("E_EDITOR_THREAD_BOUNDARY", "drain 之后 set-title 没有成功：" + result);
		}
		if (ReadWindowTitle(targetWindow) != laterTitle)
		{
			fail("E_EDITOR_THREAD_BOUNDARY", "drain 之后标题没有变成新值");
		}
		CA_LOG("[编辑器 测试] ⑤ 线程边界：800ms 不 Tick 期间标题不变；Tick 之后变为 \"{}\"", laterTitle.c_str());
	}

	// ⑧ 协议版本不符：独立连接 hello=999 → E_PROTOCOL_VERSION 且连接被关闭。
	{
		WsClient badVersion;
		std::string error;
		if (!badVersion.Connect(18391, error))
		{
			fail("E_EDITOR_WS_VERSION", error);
		}
		if (!badVersion.SendText("{\"type\":\"hello\",\"protocolVersion\":999}", error))
		{
			fail("E_EDITOR_WS_VERSION", error);
		}
		std::string message;
		if (!badVersion.ReceiveText(message, 5000, error))
		{
			fail("E_EDITOR_WS_VERSION", "没有收到版本拒绝结果：" + error);
		}
		if (!JsonHas(message, "\"ok\":false") || JsonErrorCode(message) != "E_PROTOCOL_VERSION")
		{
			fail("E_EDITOR_WS_VERSION", "版本拒绝不是 E_PROTOCOL_VERSION：" + message);
		}
		std::string afterClose;
		std::string closeError;
		const bool gotMore = badVersion.ReceiveText(afterClose, 3000, closeError);
		if (gotMore)
		{
			fail("E_EDITOR_WS_VERSION", "版本拒绝之后连接没有关闭，还收到：" + afterClose);
		}
		CA_LOG("[编辑器 测试] ⑧ 协议版本 999 → E_PROTOCOL_VERSION，连接已关闭（{}）", closeError.c_str());
	}

	// ③ 未知编号：E_UNKNOWN_OBJECT；状态不变；最近一条命令的原生调用数 0。
	{
		const std::string titleBefore = ReadWindowTitle(targetWindow);
		sendCommand("{\"type\":\"command\",\"requestId\":106,\"command\":\"set-title\",\"objectId\":9999,"
		            "\"args\":{\"title\":\"should-not-apply\"}}");
		std::string result;
		waitResult(106, result);
		if (!JsonHas(result, "\"ok\":false") || JsonErrorCode(result) != "E_UNKNOWN_OBJECT")
		{
			fail("E_EDITOR_UNKNOWN_OBJECT", "未知编号结果不是 E_UNKNOWN_OBJECT：" + result);
		}
		if (ReadWindowTitle(targetWindow) != titleBefore)
		{
			fail("E_EDITOR_UNKNOWN_OBJECT", "未知编号的命令动了目标窗口状态");
		}
		pumpFrames(1);
		ReadEditorReadings(readingsPath, &readings);
		if (readings.I64("cmdRequest") != 106 || readings.I64("cmdCalls") != 0)
		{
			fail("E_EDITOR_UNKNOWN_CALLS", "未知编号命令的原生调用读数不是 0：" + readings.Get("cmdCalls"));
		}
		CA_LOG("[编辑器 测试] ③ 未知编号 9999 → E_UNKNOWN_OBJECT，cmdCalls={}", readings.Get("cmdCalls").c_str());
	}

	// 形状错（E_BAD_MESSAGE，两个变体）与非法参数（E_INVALID_ARG）：都不进引擎队列、原生调用 0。
	{
		// 缺 requestId：requestId 回填 0（PROTOCOL.md 第 3 节约定）。
		sendCommand("{\"type\":\"command\",\"command\":\"get-object\",\"objectId\":1}");
		std::string message;
		bool gotBadMessage = false;
		const auto startedAt = std::chrono::steady_clock::now();
		while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count() <
			10000.0)
		{
			std::string receiveError;
			if (client.ReceiveText(message, 100, receiveError))
			{
				if (JsonIsResult(message))
				{
					gotBadMessage = JsonErrorCode(message) == "E_BAD_MESSAGE";
					break;
				}
				events.push_back(message);
				continue;
			}
			if (receiveError.find("超时") == std::string::npos)
			{
				fail("E_EDITOR_BAD_MESSAGE", "连接被关闭：" + receiveError);
			}
			pScriptHost->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
		}
		if (!gotBadMessage)
		{
			fail("E_EDITOR_BAD_MESSAGE", "缺 requestId 的消息没有被 E_BAD_MESSAGE 拒绝：" + message);
		}

		sendCommand("{\"type\":\"command\",\"requestId\":107,\"command\":\"explode\",\"objectId\":1}");
		std::string result;
		waitResult(107, result);
		if (JsonErrorCode(result) != "E_BAD_MESSAGE")
		{
			fail("E_EDITOR_BAD_MESSAGE", "未知 command 没有被 E_BAD_MESSAGE 拒绝：" + result);
		}

		sendCommand("{\"type\":\"command\",\"requestId\":108,\"command\":\"set-size\",\"objectId\":1,"
		            "\"args\":{\"width\":0,\"height\":560}}");
		waitResult(108, result);
		if (JsonErrorCode(result) != "E_INVALID_ARG")
		{
			fail("E_EDITOR_INVALID_ARG", "width=0 没有被 E_INVALID_ARG 拒绝：" + result);
		}
		pumpFrames(1);
		ReadEditorReadings(readingsPath, &readings);
		if (readings.I64("cmdCalls") != 0)
		{
			fail("E_EDITOR_INVALID_ARG", "非法参数命令的原生调用读数不是 0：" + readings.Get("cmdCalls"));
		}
		// B2（工件修订）：宽度必须大于面板宽（默认 320）—— width=320 也必须 E_INVALID_ARG，
		// 否则左侧视口宽会被压成 0。
		sendCommand("{\"type\":\"command\",\"requestId\":110,\"command\":\"set-size\",\"objectId\":1,"
		            "\"args\":{\"width\":320,\"height\":560}}");
		waitResult(110, result);
		if (JsonErrorCode(result) != "E_INVALID_ARG")
		{
			fail("E_EDITOR_INVALID_ARG", "width=面板宽(320) 没有被 E_INVALID_ARG 拒绝：" + result);
		}
		CA_LOG("[编辑器 测试] 形状错（E_BAD_MESSAGE×2）/ 非法参数（E_INVALID_ARG，含 width=0 与 width=面板宽）"
		       "均零原生调用");
	}

	// 正控（工件修订 M6b 的呈现层诊断）：用**原生 C++ 路径**（与 TestSimpleTriangle 相同）直接建图、
	// 画绿底红三角并 Present 到一个新窗口，再用 WGC（窗口 + 显示器）抓窗口中心。
	// 若红 ⇒ 引擎呈现正常，问题在编辑器图/糖层（窗口 pass 没画窗口附件或 Present 目标不符）；
	// 若也白 ⇒ 本环境窗口呈现层本身有问题。
	if (DesktopSessionAvailable())
	{
		auto controlWindowWeak = ctx.pWindowSystem->NewWindow(320, 240, "Editor Capture Control");
		auto controlWindow = controlWindowWeak.lock();
		if (controlWindow)
		{
			auto controlHandle = ctx.pGPUBackend->GetWindowHandle(controlWindow);
			struct ControlVertex
			{
				std::array<float, 3> pos;
				std::array<float, 3> color;
			};
			cacore::HashObj<VertexInputsDescriptor> controlDescriptors = VertexInputsDescriptor::Create(
				sizeof(ControlVertex),
				{
					VertexAttribute::Create(offsetof(ControlVertex, pos), VertexInputFormat::eR32G32B32_SFloat,
						CANAME("POSITION")),
					VertexAttribute::Create(offsetof(ControlVertex, color), VertexInputFormat::eR32G32B32_SFloat,
						CANAME("COLOR")),
				},
				false);
			std::vector<ControlVertex> controlVertices = {
				{ { -0.25f, -0.25f, 0.25f }, { 1.0f, 0.0f, 0.0f } },
				{ { 0.25f, -0.25f, 0.25f }, { 1.0f, 0.0f, 0.0f } },
				{ { 0.0f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f } },
			};
			ImageHandle controlBackbuffer(controlHandle);
			BufferHandle controlBuffer(CANAME("EditorControlVertices"));
			castl::shared_ptr<GPUGraph> controlGraph = castl::make_shared<GPUGraph>();
			controlGraph->Present(controlBackbuffer)
				.AllocBuffer(controlBuffer,
					GPUBufferDescriptor::Create(controlVertices.size(), sizeof(ControlVertex)))
				.ScheduleData(controlBuffer, controlVertices.data(),
					controlVertices.size() * sizeof(ControlVertex))
				.Rast(RenderPass::New(controlBackbuffer,
						AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0, 0.5f, 0, 1)))
						.SetShaderInfo({ CAPATH("Shaders/Test/TestSimpleTriangle") })
						.Batch(DrawCallBatch::New()
							.VertexStream(CANAME("EditorControlVertices"), controlDescriptors)
							.DrawCall(DrawCall::New()
								.SetVertexBuffer(CANAME("EditorControlVertices"), controlBuffer)
								.Draw(controlVertices.size()))));
			for (int frame = 0; frame < 20; ++frame)
			{
				ctx.pWindowSystem->UpdateSystem();
				auto scheduler = ctx.pThreadManager->NewScheduler();
				ctx.pGPUBackend->ExecuteGraph(scheduler.get(), controlGraph);
			}
			ctx.pGPUBackend->WaitIdle();
			// 正控的呈现证明：登记窗口 backbuffer 呈现前捕获 → 再跑两帧（含 Present）→ 读中心精确红。
			// 这条验证"引擎的 C++ 原生呈现路径把三角形写进了窗口交换链表面"，不依赖屏幕合成。
			auto controlWindowHandle = ctx.pGPUBackend->GetWindowHandle(controlWindow);
			if (!controlWindowHandle)
			{
				fail("E_EDITOR_CONTROL_PIXEL", "正控窗口 GetWindowHandle 返回空");
			}
			auto const& controlDesc = controlWindowHandle->GetBackbufferDescriptor();
			const uint64_t controlBytes = GetImageByteSize(controlDesc.width, controlDesc.height, controlDesc.format);
			if (controlBytes == 0)
			{
				fail("E_EDITOR_CONTROL_PIXEL", "正控窗口 backbuffer 格式不支持紧凑回读");
			}
			std::vector<uint8_t> controlPixels(static_cast<size_t>(controlBytes), 0);
			auto controlToken = ctx.pGPUBackend->Readback(ImageHandle(controlWindowHandle), controlPixels);
			if (!controlToken)
			{
				fail("E_EDITOR_CONTROL_PIXEL", "正控窗口 Readback(backbuffer) 被拒绝（能力/格式/重复请求）");
			}
			for (int frame = 0; frame < 2; ++frame)
			{
				ctx.pWindowSystem->UpdateSystem();
				auto scheduler = ctx.pThreadManager->NewScheduler();
				ctx.pGPUBackend->ExecuteGraph(scheduler.get(), controlGraph);
			}
			ctx.pGPUBackend->WaitIdle();
			controlToken->Wait();
			if (!controlToken->IsReady())
			{
				fail("E_EDITOR_CONTROL_PIXEL", "正控窗口 backbuffer 捕获未完成（token 未 ready）");
			}
			bool controlRed = false;
			unsigned char controlBytes4[4] = { 0, 0, 0, 0 };
			const bool controlBgra = controlDesc.format == ETextureFormat::E_B8G8R8A8_UNORM;
			if (controlDesc.width > 0 && controlDesc.height > 0)
			{
				const size_t centerIndex =
					(static_cast<size_t>(controlDesc.height / 2) * controlDesc.width + controlDesc.width / 2) * 4u;
				controlBytes4[0] = controlPixels[centerIndex];
				controlBytes4[1] = controlPixels[centerIndex + 1];
				controlBytes4[2] = controlPixels[centerIndex + 2];
				controlBytes4[3] = controlPixels[centerIndex + 3];
				const unsigned char red = controlBgra ? controlBytes4[2] : controlBytes4[0];
				const unsigned char green = controlBytes4[1];
				const unsigned char blue = controlBgra ? controlBytes4[0] : controlBytes4[2];
				controlRed = red == 255 && green == 0 && blue == 0 && controlBytes4[3] == 255;
			}
			CA_LOG("[编辑器 测试] 正控（原生 C++ 渲染窗口）：backbuffer {}x{} format={} 中心字节=({},{},{},{}) "
			       "色序={} 红={}",
			       controlDesc.width, controlDesc.height, static_cast<int>(controlDesc.format), controlBytes4[0],
			       controlBytes4[1], controlBytes4[2], controlBytes4[3], controlBgra ? "BGRA" : "RGBA",
			       controlRed ? 1 : 0);
			if (!controlRed)
			{
				fail("E_EDITOR_CONTROL_PIXEL", "原生 C++ 呈现正控失败：窗口交换链中心不是精确三角形红");
			}
			controlWindow->CloseWindow();
			controlWindow.reset();
			ctx.pWindowSystem->UpdateSystem();
		}
	}

	// ⑩ 展示证明（工件修订方向）：读回窗口**真实 swapchain backbuffer** 中心像素。捕获发生在
	// Present 之前（PresentWindows 钩子），证明渲染后端把三角形写进了窗口交换链表面；不依赖屏幕
	// 合成/虚拟显示，headless 也可跑。判据：精确 (255,0,0,255)（按 backbuffer 格式判色序）。
	{
		if (!ReadEditorReadings(readingsPath, &readings))
		{
			fail("E_EDITOR_READINGS", "backbuffer 读回前没有读数行");
		}
		// 用编辑器图里的**窗口 backbuffer 附件**（不是裸 IWindow*）：与 C# 侧同一个 WindowHandle，
		// 也避免 GetWindowHandle 的 shared_ptr 参数要求（裸指针不能新建拥有型 shared_ptr）。
		ProbeNativeObjects backbufferObjects = ResolveProbeObjects(readings);
		if (backbufferObjects.graph == nullptr || !backbufferObjects.windowImage.IsValid())
		{
			fail("E_EDITOR_BACKBUFFER", "读数行 graphSelf 解析失败或图里没有窗口 backbuffer 附件");
		}
		auto windowHandle = backbufferObjects.windowImage.GetWindowHandle();
		if (!windowHandle)
		{
			fail("E_EDITOR_BACKBUFFER", "窗口 backbuffer 附件里没有 WindowHandle");
		}
		auto const& backbufferDesc = windowHandle->GetBackbufferDescriptor();
		const uint64_t backbufferBytes =
			GetImageByteSize(backbufferDesc.width, backbufferDesc.height, backbufferDesc.format);
		if (backbufferBytes == 0 || backbufferDesc.width == 0 || backbufferDesc.height == 0)
		{
			fail("E_EDITOR_BACKBUFFER", "窗口 backbuffer 描述非法/格式不支持紧凑回读");
		}
		// 采样点 = **左侧视口区域中心**（不是整张 backbuffer 中心：右侧 320px 是面板条），按
		// backbuffer 尺寸 / 读数 client 尺寸换算（两者应相等，缩放时也不抓偏）。
		int bbClientWidth = 0;
		int bbClientHeight = 0;
		Rect4 bbViewport;
		if (!ParseSize(readings.Get("client"), bbClientWidth, bbClientHeight) ||
			!ParseRect(readings.Get("viewport"), bbViewport) || bbClientWidth <= 0 || bbClientHeight <= 0)
		{
			fail("E_EDITOR_BACKBUFFER", "读数行缺少合法 client/viewport");
		}
		const double bbScaleX = static_cast<double>(backbufferDesc.width) / bbClientWidth;
		const double bbScaleY = static_cast<double>(backbufferDesc.height) / bbClientHeight;
		const uint32_t sampleX = static_cast<uint32_t>((bbViewport.x + bbViewport.w / 2) * bbScaleX);
		const uint32_t sampleY = static_cast<uint32_t>((bbViewport.y + bbViewport.h / 2) * bbScaleY);
		if (sampleX >= backbufferDesc.width || sampleY >= backbufferDesc.height)
		{
			fail("E_EDITOR_BACKBUFFER", "视口中心换算后超出 backbuffer 范围");
		}
		std::vector<uint8_t> backbufferPixels(static_cast<size_t>(backbufferBytes), 0);
		auto backbufferToken = ctx.pGPUBackend->Readback(backbufferObjects.windowImage, backbufferPixels);
		if (!backbufferToken)
		{
			fail("E_EDITOR_BACKBUFFER",
			     "Readback(窗口 backbuffer) 被拒绝（surface 无 TRANSFER_SRC / 格式不支持 / 已有未完成请求）");
		}
		// 跑两帧：编辑器在 Tick 内 SubmitGraph（含 Present）；捕获在 Present 之前完成。
		pumpFrames(2);
		backbufferToken->Wait();
		if (!backbufferToken->IsReady())
		{
			fail("E_EDITOR_BACKBUFFER", "窗口 backbuffer 捕获未完成（token 未 ready）");
		}
		const size_t centerIndex =
			(static_cast<size_t>(sampleY) * backbufferDesc.width + sampleX) * 4u;
		const unsigned char bytes4[4] = { backbufferPixels[centerIndex], backbufferPixels[centerIndex + 1],
			                             backbufferPixels[centerIndex + 2], backbufferPixels[centerIndex + 3] };
		const bool bgra = backbufferDesc.format == ETextureFormat::E_B8G8R8A8_UNORM;
		const unsigned char red = bgra ? bytes4[2] : bytes4[0];
		const unsigned char green = bytes4[1];
		const unsigned char blue = bgra ? bytes4[0] : bytes4[2];
		const bool backbufferRed = red == 255 && green == 0 && blue == 0 && bytes4[3] == 255;
		CA_LOG("[编辑器 像素] 展示证明（窗口 backbuffer 回读）：{}x{} format={} 视口中心=({},{}) 中心字节="
		       "({},{},{},{}) 色序={} 红={}",
		       backbufferDesc.width, backbufferDesc.height, static_cast<int>(backbufferDesc.format), sampleX, sampleY,
		       bytes4[0], bytes4[1], bytes4[2], bytes4[3], bgra ? "BGRA" : "RGBA", backbufferRed ? 1 : 0);
		if (!backbufferRed)
		{
			fail("E_EDITOR_BACKBUFFER_PIXEL", "窗口交换链 backbuffer 中心不是精确三角形红");
		}
		CA_LOG("[编辑器 测试] ⑩ 展示证明通过：窗口真实交换链 backbuffer 中心精确红（Present 前捕获，"
		       "不依赖屏幕合成）");
	}

	// 可选诊断（默认关）：屏幕捕获（WGC 窗口/显示器、BitBlt、DXGI）。本机虚拟显示适配器抓不到
	// Vulkan 呈现内容（实测原生 C++ 正控也是白），所以默认不跑、不作断言；`CASCADED_EDITOR_DISPLAY_PROOF=1`
	// 时才执行并只打印读数。
	if (displayProofMode)
	{
		// 可见窗口模式：先保持渲染 120 帧（≈2 秒）让 DWM 真正合成上屏，再做多通道捕获。
		for (int frame = 0; frame < 120; ++frame)
		{
			pScriptHost->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
		void* proofHandle = targetWindow->GetNativeWindowHandle();
		HWND proofWindow = proofHandle == nullptr ? nullptr : *static_cast<HWND*>(proofHandle);
		CA_LOG("[编辑器 测试] ⑩ 可见窗口模式：IsWindowVisible={} IsIconic={} 前台={} dpi={}",
		       proofWindow != nullptr && ::IsWindowVisible(proofWindow) ? 1 : 0,
		       proofWindow != nullptr && ::IsIconic(proofWindow) ? 1 : 0,
		       proofWindow != nullptr && ::GetForegroundWindow() == proofWindow ? 1 : 0,
		       proofWindow != nullptr ? WindowDpi(proofWindow) : 0u);
		if (!ReadEditorReadings(readingsPath, &readings))
		{
			fail("E_EDITOR_READINGS", "屏幕捕获前没有读数行");
		}
		if (!ParseRect(readings.Get("viewport"), viewportRect))
		{
			fail("E_EDITOR_SCREEN_PIXEL", "读数行没有合法 viewport");
		}
		// 用**最新读数**的客户区尺寸（② set-size 之后已是 900x560，不是启动时的 1280x720）。
		int currentClientWidth = 0;
		int currentClientHeight = 0;
		if (!ParseSize(readings.Get("client"), currentClientWidth, currentClientHeight) ||
			currentClientWidth <= 0 || currentClientHeight <= 0)
		{
			fail("E_EDITOR_SCREEN_PIXEL", "读数行没有合法 client 尺寸");
		}
		if (!DesktopSessionAvailable())
		{
			CA_LOG("[编辑器 测试] ⑩ 展示证明：跳过 —— 当前没有可用的 desktop 会话（OpenInputDesktop 失败）；"
			       "渲染证明（离屏捕获中心红）已通过，不静默当通过");
		}
		else
		{
			void* handlePointer = targetWindow->GetNativeWindowHandle();
			HWND hwnd = handlePointer == nullptr ? nullptr : *static_cast<HWND*>(handlePointer);
			if (hwnd == nullptr)
			{
				fail("E_EDITOR_SCREEN_PIXEL", "目标窗口 HWND 为空");
			}
			// 诊断读数：客户区 / 窗口矩形 / 客户区屏幕原点 / DPI / 最小化 / 前台。DPI 缩放时"读数里的
			// 客户区尺寸"（GLFW 窗口尺寸）与 GetClientRect 可能不同，换算比例原样打出来。
			RECT clientRect{};
			::GetClientRect(hwnd, &clientRect);
			RECT windowRect{};
			::GetWindowRect(hwnd, &windowRect);
			POINT clientOrigin{ 0, 0 };
			::ClientToScreen(hwnd, &clientOrigin);
			const int actualClientWidth = clientRect.right - clientRect.left;
			const int actualClientHeight = clientRect.bottom - clientRect.top;
			CA_LOG("[编辑器 测试] ⑩ 诊断：GetClientRect={}x{} windowRect=({},{},{},{}) clientOrigin=({},{}) "
			       "dpi={} iconic={} foreground={} 读数client={}x{} viewport=({},{},{},{})",
			       actualClientWidth, actualClientHeight, windowRect.left, windowRect.top, windowRect.right,
			       windowRect.bottom, clientOrigin.x, clientOrigin.y, WindowDpi(hwnd), ::IsIconic(hwnd) ? 1 : 0,
			       ::GetForegroundWindow() == hwnd ? 1 : 0, currentClientWidth, currentClientHeight, viewportRect.x,
			       viewportRect.y, viewportRect.w, viewportRect.h);
			if (::IsIconic(hwnd))
			{
				::ShowWindow(hwnd, SW_RESTORE);
				::Sleep(300);
			}
			::ShowWindow(hwnd, SW_SHOW);
			::SetForegroundWindow(hwnd);
			::BringWindowToTop(hwnd);
			::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			OtherWindowHider otherWindows;
			otherWindows.HideAllExcept(hwnd);
			const double scaleX = (currentClientWidth > 0 && actualClientWidth > 0)
				? static_cast<double>(actualClientWidth) / currentClientWidth
				: 1.0;
			const double scaleY = (currentClientHeight > 0 && actualClientHeight > 0)
				? static_cast<double>(actualClientHeight) / currentClientHeight
				: 1.0;
			bool captured = false;
			unsigned char pixel[4] = { 0, 0, 0, 0 };
			std::string captureMethod = "none";
			std::string captureError;

			// 通道 0（主）：Windows.Graphics.Capture —— 按窗口抓内容，不受 flip 呈现 / 桌面变化影响。
			{
				const int wgcClientX = viewportRect.x + viewportRect.w / 2;
				const int wgcClientY = viewportRect.y + viewportRect.h / 2;
				unsigned char wgcPixel[4] = { 0, 0, 0, 0 };
				std::string wgcError;
				if (CaptureWindowPixelWgc(hwnd, wgcClientX, wgcClientY, currentClientWidth, currentClientHeight,
				                          wgcPixel, wgcError))
				{
					CA_LOG("[编辑器 测试] ⑩ WGC 窗口捕获：client=({},{}) 像素=({},{},{},{})", wgcClientX, wgcClientY,
					       wgcPixel[0], wgcPixel[1], wgcPixel[2], wgcPixel[3]);
					captured = wgcPixel[0] == 255 && wgcPixel[1] == 0 && wgcPixel[2] == 0 && wgcPixel[3] == 255;
					if (captured)
					{
						captureMethod = "windows-graphics-capture";
						CA_LOG("[编辑器 测试] ⑩ 展示证明（Windows.Graphics.Capture）：视口区域中心 = "
						       "({},{},{},{})",
						       wgcPixel[0], wgcPixel[1], wgcPixel[2], wgcPixel[3]);
					}
				}
				else
				{
					CA_LOG("[编辑器 测试] ⑩ WGC 窗口捕获失败：{}", wgcError.c_str());
				}
			}
			// 通道 0b：WGC **显示器**捕获 —— DWM 合成后的真实屏幕内容（independent-flip 也包含），
			// 窗口捕获若因 flip 拿不到内容时走这条；采样点用屏幕坐标，内部换算到显示器本地坐标。
			if (!captured)
			{
				HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
				POINT wgcOrigin{ 0, 0 };
				if (monitor != nullptr && ::ClientToScreen(hwnd, &wgcOrigin))
				{
					const int monitorX =
						wgcOrigin.x + static_cast<int>((viewportRect.x + viewportRect.w / 2) * scaleX);
					const int monitorY =
						wgcOrigin.y + static_cast<int>((viewportRect.y + viewportRect.h / 2) * scaleY);
					unsigned char monitorPixel[4] = { 0, 0, 0, 0 };
					std::string monitorError;
					if (CaptureMonitorPixelWgc(monitor, monitorX, monitorY, monitorPixel, monitorError))
					{
						CA_LOG("[编辑器 测试] ⑩ WGC 显示器捕获：screen=({},{}) 像素=({},{},{},{})", monitorX,
						       monitorY, monitorPixel[0], monitorPixel[1], monitorPixel[2], monitorPixel[3]);
						captured = monitorPixel[0] == 255 && monitorPixel[1] == 0 && monitorPixel[2] == 0 && monitorPixel[3] == 255;
						if (captured)
						{
							captureMethod = "wgc-monitor";
							CA_LOG("[编辑器 测试] ⑩ 展示证明（WGC 显示器捕获）：视口区域中心 = ({},{},{},{})",
							       monitorPixel[0], monitorPixel[1], monitorPixel[2], monitorPixel[3]);
						}
					}
					else
					{
						CA_LOG("[编辑器 测试] ⑩ WGC 显示器捕获失败：{}", monitorError.c_str());
					}
				}
			}
			// 通道 1（诊断/备选）：屏幕 DC BitBlt —— flip 呈现下可能只抓到窗口背景（白）。
			for (int attempt = 0; attempt < 15 && !captured; ++attempt)
			{
				::Sleep(150);
				ctx.pWindowSystem->UpdateSystem();
				POINT origin{ 0, 0 };
				if (!::ClientToScreen(hwnd, &origin))
				{
					captureError = "ClientToScreen 失败";
					break;
				}
				// 每次尝试都打一组采样点（客户区坐标 + 换算后的屏幕坐标 + 抓到的像素）：一次就能看出是
				// "抓错区域"（采到面板白/背景绿）还是"渲染没上屏"（全白/全黑）。
				struct SamplePoint
				{
					char const* name;
					int clientX;
					int clientY;
				};
				const SamplePoint samples[] = {
					{ "viewport_center", viewportRect.x + viewportRect.w / 2, viewportRect.y + viewportRect.h / 2 },
					{ "viewport_quarter", viewportRect.x + viewportRect.w / 4, viewportRect.y + viewportRect.h / 2 },
					{ "viewport_3quarter", viewportRect.x + viewportRect.w * 3 / 4,
					  viewportRect.y + viewportRect.h / 2 },
					{ "client_center", currentClientWidth / 2, currentClientHeight / 2 },
				};
				for (SamplePoint const& sample : samples)
				{
					const int sampleX = origin.x + static_cast<int>(sample.clientX * scaleX);
					const int sampleY = origin.y + static_cast<int>(sample.clientY * scaleY);
					unsigned char samplePixel[4] = { 0, 0, 0, 0 };
					std::string sampleError;
					if (CaptureScreenPixel(sampleX, sampleY, samplePixel, sampleError))
					{
						CA_LOG("[编辑器 测试] ⑩ 采样 {}：client=({},{}) screen=({},{}) 像素=({},{},{},{})",
						       sample.name, sample.clientX, sample.clientY, sampleX, sampleY, samplePixel[0],
						       samplePixel[1], samplePixel[2], samplePixel[3]);
					}
					else
					{
						CA_LOG("[编辑器 测试] ⑩ 采样 {}：屏幕捕获失败：{}", sample.name, sampleError.c_str());
					}
				}
				const int centerX = origin.x + static_cast<int>((viewportRect.x + viewportRect.w / 2) * scaleX);
				const int centerY = origin.y + static_cast<int>((viewportRect.y + viewportRect.h / 2) * scaleY);
				if (CaptureScreenPixel(centerX, centerY, pixel, captureError))
				{
					captured = pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
					if (captured)
					{
						captureMethod = "screen-dc-bitblt";
						CA_LOG("[编辑器 测试] ⑩ 展示证明：视口区域中心屏幕像素 = ({},{},{},{})（第 {} 次尝试，"
						       "screen=({},{}) scale={:.3f}x{:.3f}）",
						       pixel[0], pixel[1], pixel[2], pixel[3], attempt + 1, centerX, centerY, scaleX,
						       scaleY);
						break;
					}
				}
			}
			// BitBlt 抓不到时换 DXGI Desktop Duplication（DWM 真实合成输出，含 independent-flip 交换链）。
			// 判据仍是"视口中心是三角形红"；两条通道任一通过即成立，日志记录实际用哪条。
			//
			// 桌面复制只在**桌面内容变化**时递帧：静态桌面（甚至抖动窗口）下 AcquireNextFrame 会一直
			// WAIT_TIMEOUT。这里在抓帧期间维持一个 64×64 的 GDI 刺激窗口（右下角，交替刷红/蓝），
			// 保证同一个 duplication 会话内持续有新帧可抓；采样点坐标不变（目标窗口不动）。
			unsigned char dxgiPixel[4] = { 0, 0, 0, 0 };
			std::string dxgiError;
			if (!captured)
			{
				RECT workArea{};
				::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
				HWND stimulusWindow = CreateCaptureStimulusWindow(64);
				if (stimulusWindow != nullptr)
				{
					::SetWindowPos(stimulusWindow, HWND_TOPMOST, workArea.right - 80, workArea.bottom - 80, 64, 64,
					               SWP_NOACTIVATE | SWP_SHOWWINDOW);
				}
				else
				{
					CA_LOG("[编辑器 测试] ⑩ 捕获刺激窗口创建失败（GetLastError={}）", ::GetLastError());
				}
				POINT origin{ 0, 0 };
				if (::ClientToScreen(hwnd, &origin))
				{
					const int dxgiX = origin.x + static_cast<int>((viewportRect.x + viewportRect.w / 2) * scaleX);
					const int dxgiY = origin.y + static_cast<int>((viewportRect.y + viewportRect.h / 2) * scaleY);
					if (CaptureDesktopPixelDxgi(dxgiX, dxgiY, stimulusWindow, dxgiPixel, dxgiError, 15, 250))
					{
						CA_LOG("[编辑器 测试] ⑩ DXGI 桌面复制：screen=({},{}) 像素=({},{},{},{})", dxgiX, dxgiY,
						       dxgiPixel[0], dxgiPixel[1], dxgiPixel[2], dxgiPixel[3]);
						captured = dxgiPixel[0] == 255 && dxgiPixel[1] == 0 && dxgiPixel[2] == 0 && dxgiPixel[3] == 255;
						if (captured)
						{
							captureMethod = "dxgi-desktop-duplication";
							CA_LOG("[编辑器 测试] ⑩ 展示证明（DXGI 桌面复制）：视口区域中心 = ({},{},{},{}) —— "
							       "屏幕 DC 的 BitBlt 抓不到 flip 呈现内容，改用桌面复制（判据不变）",
							       dxgiPixel[0], dxgiPixel[1], dxgiPixel[2], dxgiPixel[3]);
						}
					}
					else
					{
						CA_LOG("[编辑器 测试] ⑩ DXGI 桌面复制失败：{}", dxgiError.c_str());
					}
				}
				if (stimulusWindow != nullptr)
				{
					::DestroyWindow(stimulusWindow);
				}
				ctx.pWindowSystem->UpdateSystem();
			}
			// 断言时刻的整窗客户区 PNG（诊断证据；失败时人工看这张图）。
			{
				int pngWidth = 0;
				int pngHeight = 0;
				std::string pngError;
				const std::string pngPath =
					std::string("test_output/editor_screen_capture_") + ctx.backendName + "_at_assert.png";
				if (SaveClientAreaPng(hwnd, pngPath, pngWidth, pngHeight, pngError))
				{
					CA_LOG("[编辑器 测试] ⑩ 诊断 PNG（断言时刻）：{} 客户区={}x{}", pngPath.c_str(), pngWidth,
					       pngHeight);
				}
				else
				{
					CA_LOG("[编辑器 测试] ⑩ 诊断 PNG 写入失败：{}", pngError.c_str());
				}
			}
			if (captured)
			{
				CA_LOG("[编辑器 测试] ⑩ 展示证明通过：捕获通道={}", captureMethod.c_str());
			}
			::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			otherWindows.RestoreAll();
			if (!captured)
			{
				char detail[512] = {};
				std::snprintf(detail, sizeof(detail),
				              "屏幕捕获的视口中心像素不是三角形红：BitBlt=(%u,%u,%u,%u) DXGI=(%u,%u,%u,%u) "
				              "dxgiError=%s（desktop 会话存在；诊断 PNG 见 test_output/editor_screen_capture_%s_at_assert.png）",
				              pixel[0], pixel[1], pixel[2], pixel[3], dxgiPixel[0], dxgiPixel[1], dxgiPixel[2],
				              dxgiPixel[3], dxgiError.c_str(), ctx.backendName.c_str());
				// 诊断通道：只打印，不判红（展示证明已由 backbuffer 回读承担；本机虚拟显示抓不到 flip 内容）。
				CA_LOG("[编辑器 测试] ⑩ 屏幕捕获诊断（不作断言）：{}", detail);
			}
		}
	}

	// ④ 外部关窗：C++ 侧 CloseWindow → object-invalidated 事件 → 旧编号命令 E_OBJECT_INVALIDATED、零原生调用。
	{
		targetWindow->CloseWindow();
		bool invalidated = false;
		const auto startedAt = std::chrono::steady_clock::now();
		while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count() <
			10000.0)
		{
			pScriptHost->Tick(0.016);
			ctx.pWindowSystem->UpdateSystem();
			std::string message;
			std::string receiveError;
			if (client.ReceiveText(message, 100, receiveError))
			{
				if (JsonIsEvent(message, "object-invalidated"))
				{
					invalidated = true;
					break;
				}
				events.push_back(message);
				continue;
			}
			if (receiveError.find("超时") == std::string::npos)
			{
				fail("E_EDITOR_INVALIDATED", "等待失效事件时连接被关闭：" + receiveError);
			}
		}
		if (!invalidated)
		{
			fail("E_EDITOR_INVALIDATED", "C++ 侧 CloseWindow 之后 10 秒内没有收到 object-invalidated");
		}
		sendCommand("{\"type\":\"command\",\"requestId\":109,\"command\":\"get-object\",\"objectId\":1}");
		std::string result;
		waitResult(109, result);
		if (!JsonHas(result, "\"ok\":false") || JsonErrorCode(result) != "E_OBJECT_INVALIDATED")
		{
			fail("E_EDITOR_INVALIDATED", "失效编号的命令不是 E_OBJECT_INVALIDATED：" + result);
		}
		pumpFrames(1);
		ReadEditorReadings(readingsPath, &readings);
		if (readings.I64("cmdRequest") != 109 || readings.I64("cmdCalls") != 0)
		{
			fail("E_EDITOR_INVALIDATED", "失效编号命令的原生调用读数不是 0：" + readings.Get("cmdCalls"));
		}
		CA_LOG("[编辑器 测试] ④ 外部关窗：object-invalidated 已广播；旧编号 → E_OBJECT_INVALIDATED、cmdCalls=0，"
		       "进程未崩");
	}

	// 收尾：先主动停服务（M11，隔离后续用例），再清理客户端与测试自己的窗口；
	// 宿主 LastError 必须仍是基线（编辑器没有把错误漏到宿主通道）。
	::SetEnvironmentVariableA("CASCADED_EDITOR_STOP", "1");
	pumpFrames(1);
	::SetEnvironmentVariableA("CASCADED_EDITOR_STOP", nullptr);
	if (!LogContains(readingsPath, "已关闭（测试请求停止"))
	{
		fail("E_EDITOR_STOP", "CASCADED_EDITOR_STOP=1 之后没有服务关闭日志");
	}
	CA_LOG("[编辑器 测试] M11 服务主动停止：监听已停、客户端已断、控制器已关闭（后续用例不受影响）");
	client.Close();
	windowKeepAlive->CloseWindow();
	windowKeepAlive.reset();
	ctx.pWindowSystem->UpdateSystem();
	if (pScriptHost->LastError() != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "用例结束之后 LastError 变了：%s", pScriptHost->LastError());
		fail("E_EDITOR_HOST_ERROR", detail);
	}
	CA_LOG("TestEditorWindowPanel 通过：命令往返/撤销/协议错误/对象失效/线程边界/分栏同步/两层像素证据全部满足");
}

// ---- TestCSharpScene（csharp-scene-foundation 3.2，第 19 用例）----
// C# 断言只能在真跑 C# 的台子上执行：`Test/CoreTests` 是纯 C++ 可执行文件（0 个 .cs、无 hostfxr），
// 所以序列化往返 / 失败通道 / 生命周期钩子 / 场景→图翻译全部由托管侧 `SceneProbe` 真跑，
// 打成机器可读读数行（`[托管 读数] probe=scene-idle / scene-format / scene-state`）。
// 本函数判决这些读数，并自己读回离屏捕获的像素（C# 绑定面没有 Readback，像素断言只能在 C++ 侧）。
//
// 夹具期望值在这里**另写一份**（窗口 320x240、3 个实体里 2 个带 MeshRenderer、按同一份着色器分组后
// 2 个 pass（窗口 + 同尺寸离屏）、一个 pass 里 6 个顶点、1 次呈现、背景纯蓝 / 中心纯红、
// 第一个翻译顶点 (-0.25,-0.5,0)、dt 0.125/0.25 秒）—— 与 C# 侧不共享常量表，一份改错不会两边一起绿。
//
// 红态怎么构造（每条判据都能失败）：
//   · 用反射默认序列化生成镜像 → public 字段被 STJ 漏掉 → `rtChecked`/`rtMismatch` 读数不对；
//   · Load 静默补默认值 → `trunc`/`version`/`missingId` 读数变成 `none`；
//   · Host.Tick 不传 dt → `lastDtMicros` 不为 125000 / 250000、`onUpdate` 不涨；
//   · 无活动场景时还调钩子或建图 → `scene-idle` 行里 onStart/onUpdate/renderFrames 不为 0；
//   · 坏着色器路径被静默跳过 → Tick 回值照常递增、LastError 里没有那个路径；
//   · Rotation 真被施加 → `v0xMicros/v0yMicros/v0zMicros` 与 (-250000,-500000,0) 不符。
void TestCSharpScene(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};
	auto requireInt = [&fail](char const* code, char const* field, int64_t actual, int64_t expected)
	{
		if (actual != expected)
		{
			char detail[384] = {};
			std::snprintf(detail, sizeof(detail), "读数 %s = %lld，期望 %lld", field,
			              static_cast<long long>(actual), static_cast<long long>(expected));
			fail(code, detail);
		}
	};
	auto requireText = [&fail](char const* code, char const* field, std::string const& actual,
	                           char const* expected)
	{
		if (actual != expected)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "读数 %s = \"%s\"，期望 \"%s\"", field, actual.c_str(),
			              expected);
			fail(code, detail);
		}
	};

	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		fail("E_SCENE_HOST", "IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		fail("E_SCENE_HOST", castl::string("托管侧不可用：") + pScriptHost->LastError());
	}

	char const* const kLog = "test_output/TestCSharpScene.log";
	// C# 侧 SceneProbe 里另写一份同样的字符串（两边不共享）。
	char const* const kBadShaderPath = "Shaders/Test/csharp_scene_missing_shader";
	constexpr uint32_t kWidth = 320u;
	constexpr uint32_t kHeight = 240u;
	// 2 个带 MeshRenderer 的实体用同一份着色器 → 按着色器分组后只有 1 组 → 每组两个 pass
	// （窗口 + 同尺寸离屏），共 2 个 pass；顶点 = 2 个三角形 × 3 = 6。
	constexpr int64_t kPassCount = 2;
	constexpr int64_t kVertexCount = 6;

	// 宿主绑定的窗口提供**场景窗口的尺寸**：C# 侧按这个尺寸自己 NewWindow（表面带真实句柄）并建同尺寸
	// 离屏捕获目标 —— 所以这里登记一个 320x240 的窗口，像素断言按 320x240 判。
	auto newWindow = ctx.pWindowSystem->NewWindow(static_cast<int>(kWidth), static_cast<int>(kHeight), "C# Scene");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_SCENE_HOST", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	struct SceneEnvGuard
	{
		~SceneEnvGuard() { ::SetEnvironmentVariableA("CASCADED_CSHARP_SCENE_PROBE", nullptr); }
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_SCENE_PROBE", "1");

	const std::string baselineError = pScriptHost->LastError();

	auto readProbe = [&](char const* probe, HostProbeReadings* out, char const* what)
	{
		if (!ReadHostProbeReadings(kLog, probe, out))
		{
			fail("E_SCENE_READINGS",
			     castl::string("托管读数行 probe=") + probe + " 没有出现在 " + kLog + "（" + what + "）");
		}
	};

	// 读一个离屏目标的背景像素（左上角 1,1）与中心像素，并把整张图写成 PNG 供人工核对。
	auto readPixels = [&](HostProbeReadings const& readings, char const* tag,
	                      std::array<uint8_t, 4>* background, std::array<uint8_t, 4>* center)
	{
		ProbeNativeObjects objects = ResolveProbeObjects(readings);
		if (objects.graph == nullptr)
		{
			fail("E_SCENE_GRAPH", "读数行里的 graphSelf 解析不出一张原生 GPUGraph（图没保留或被提前释放）");
		}
		if (!objects.captureImage.IsValid() || !objects.windowImage.IsValid())
		{
			fail("E_SCENE_TARGET", "图附件的资源引用里解析不出离屏纹理 / 窗口后台缓冲");
		}
		// 原生图结构：同一份着色器的两个三角形并在一个 pass 里 → 每目标一个 pass，共 2 个 pass、呈现 1 项。
		const uint32_t passes = static_cast<uint32_t>(objects.graph->GetRenderPasses().size());
		if (passes != static_cast<uint32_t>(kPassCount))
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "原生图 pass 数 %u，期望 %lld", passes,
			              static_cast<long long>(kPassCount));
			fail("E_SCENE_RENDER", detail);
		}
		if (objects.graph->GetFinalizePass().m_PresentBackBuffers.size() != 1u)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "原生图呈现项 %zu 项，期望 1 项（窗口后台缓冲）",
			              objects.graph->GetFinalizePass().m_PresentBackBuffers.size());
			fail("E_SCENE_RENDER", detail);
		}
		// 每个 pass 的绘制：一条 POSITION + COLOR（步长 24）顶点流、一个绘制调用、顶点数 = 2 个三角形 × 3。
		for (auto const& pass : objects.graph->GetRenderPasses())
		{
			for (auto const& batch : pass.GetDrawCallBatches())
			{
				if (batch.m_VertexInputDescs.size() != 1u)
				{
					fail("E_SCENE_STREAM", "绘制批次里的顶点流不止一个");
				}
				auto const& streamDescriptor = batch.m_VertexInputDescs.begin()->second.Get();
				const auto& attributes = streamDescriptor.attributes;
				if (attributes.size() != 2u || attributes[0].semanticName != CANAME("POSITION") ||
					attributes[0].offset != 0u || attributes[1].semanticName != CANAME("COLOR") ||
					attributes[1].offset != 12u || streamDescriptor.stride != 24u)
				{
					char detail[256] = {};
					std::snprintf(detail, sizeof(detail),
					              "顶点流不是 POSITION(0)+COLOR(12)/步长 24：属性 %zu、步长 %u", attributes.size(),
					              streamDescriptor.stride);
					fail("E_SCENE_STREAM", detail);
				}
				if (batch.m_DrawCalls.size() != 1u)
				{
					char detail[256] = {};
					std::snprintf(detail, sizeof(detail), "绘制调用 %zu 个，期望 1 个", batch.m_DrawCalls.size());
					fail("E_SCENE_DRAW", detail);
				}
				const uint32_t drawn = batch.m_DrawCalls[0].GetDrawInfo().vertexCount;
				if (drawn != static_cast<uint32_t>(kVertexCount))
				{
					char detail[256] = {};
					std::snprintf(detail, sizeof(detail), "绘制顶点数 %u，期望 %lld（两个三角形串在同一个 pass 里）",
					              drawn, static_cast<long long>(kVertexCount));
					fail("E_SCENE_DRAW", detail);
				}
			}
		}
		const auto& captureDesc = objects.captureImage.GetExternalManagedTexture()->GetDescriptor();
		if (captureDesc.width != kWidth || captureDesc.height != kHeight)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "离屏目标尺寸 %ux%u，期望 %ux%u", captureDesc.width,
			              captureDesc.height, kWidth, kHeight);
			fail("E_SCENE_TARGET", detail);
		}
		ctx.pGPUBackend->WaitIdle();
		std::vector<uint8_t> pixels(static_cast<size_t>(
			GetImageByteSize(captureDesc.width, captureDesc.height, ETextureFormat::E_R8G8B8A8_UNORM)));
		auto token = ctx.pGPUBackend->Readback(objects.captureImage, pixels);
		if (!token)
		{
			fail("E_SCENE_READBACK", "离屏目标的 Readback 返回空 token");
		}
		token->Wait();
		auto pixelAt = [&pixels, &captureDesc](uint32_t x, uint32_t y)
		{
			const size_t index = (static_cast<size_t>(y) * captureDesc.width + x) * 4u;
			return std::array<uint8_t, 4>{ pixels[index], pixels[index + 1], pixels[index + 2],
				                           pixels[index + 3] };
		};
		*background = pixelAt(1u, 1u);
		*center = pixelAt(captureDesc.width / 2u, captureDesc.height / 2u);
		const castl::string pngPath = castl::string("test_output/TestCSharpScene_") + ctx.backendName + "_" +
			tag + ".png";
		stbi_write_png(pngPath.c_str(), static_cast<int>(captureDesc.width),
		               static_cast<int>(captureDesc.height), 4, pixels.data(),
		               static_cast<int>(captureDesc.width * 4u));
		CA_LOG("TestCSharpScene 像素：tag={} size={}x{} bg=({},{},{},{}) center=({},{},{},{}) png={}", tag,
		       captureDesc.width, captureDesc.height, (*background)[0], (*background)[1], (*background)[2],
		       (*background)[3], (*center)[0], (*center)[1], (*center)[2], (*center)[3], pngPath.c_str());
	};

	// ================= tick 1：零行为读数 + 序列化断言 + 注册新场景 =================
	const int32_t tick1 = pScriptHost->Tick(0.125);
	if (tick1 <= 0)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "Tick(0.125) 回值 %d（探针没有执行）", tick1);
		fail("E_SCENE_TICK", detail);
	}
	if (pScriptHost->LastError() != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "tick1 之后 LastError 非空：%s", pScriptHost->LastError());
		fail("E_SCENE_HOST_ERROR", detail);
	}

	HostProbeReadings idle;
	readProbe("scene-idle", &idle, "tick1 注册之前的零行为读数");
	requireInt("E_SCENE_IDLE", "idle.tick", idle.I64("tick"), 1);
	requireInt("E_SCENE_IDLE", "idle.active", idle.I64("active"), 0);
	requireInt("E_SCENE_IDLE", "idle.onStart", idle.I64("onStart"), 0);
	requireInt("E_SCENE_IDLE", "idle.onUpdate", idle.I64("onUpdate"), 0);
	requireInt("E_SCENE_IDLE", "idle.renderFrames", idle.I64("renderFrames"), 0);

	HostProbeReadings format;
	readProbe("scene-format", &format, "序列化往返 / 失败通道读数");
	// 2 个实体 × 26 个字段（id / name / 三个 float3 = 9 / meshRenderer 非空 / shaderPath / color 3 /
	// 顶点 9 + 顶点长度 1）+ 场景级 2 个 = 54：C# 侧漏比一个字段，这个数就会变小。
	requireInt("E_SCENE_ROUNDTRIP", "rtChecked", format.I64("rtChecked"), 54);
	requireInt("E_SCENE_ROUNDTRIP", "rtMismatch", format.I64("rtMismatch"), 0);
	if (format.I64("file1Bytes") <= 100)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "场景文件只有 %lld 字节（空对象？）",
		              static_cast<long long>(format.I64("file1Bytes")));
		fail("E_SCENE_ROUNDTRIP", detail);
	}
	if (format.I64("diffBytes") <= 0)
	{
		fail("E_SCENE_ROUNDTRIP", "改一个 Position 分量再保存，两份文件字节完全相同（字段没真写进文件）");
	}
	requireInt("E_SCENE_ROUNDTRIP", "reloadDeltaMicros", format.I64("reloadDeltaMicros"), 500000);
	requireText("E_SCENE_TRUNC", "trunc", format.Get("trunc"), "SceneFormatException");
	requireText("E_SCENE_VERSION", "version", format.Get("version"), "SceneFormatException");
	requireText("E_SCENE_MISSING_ID", "missingId", format.Get("missingId"), "SceneFormatException");
	requireText("E_SCENE_BAD_TYPE", "badType", format.Get("badType"), "SceneFormatException");
	requireText("E_SCENE_MISSING_VERSION", "missingVersion", format.Get("missingVersion"),
	            "SceneFormatException");
	requireText("E_SCENE_SAVE_IO", "saveIo", format.Get("saveIo"), "IOException");
	requireText("E_SCENE_BAD_RES_LOAD", "badResLoad", format.Get("badResLoad"), "ok");
	requireInt("E_SCENE_BAD_RES_LOAD", "badResEntities", format.I64("badResEntities"), 1);
	requireInt("E_SCENE_KEYS", "keysTotal", format.I64("keysTotal"), 13);
	requireInt("E_SCENE_KEYS", "keysUnknown", format.I64("keysUnknown"), 0);
	// 错误消息必须点名具体问题（不是一句「出错了」）。
	if (!LogContains(kLog, "解析失败"))
	{
		fail("E_SCENE_TRUNC", "截断文件的错误消息没有点名「解析失败」");
	}
	if (!LogContains(kLog, "formatVersion 不支持"))
	{
		fail("E_SCENE_VERSION", "版本 999 的错误消息没有点名 formatVersion 不支持");
	}
	if (!LogContains(kLog, "entities[0].id"))
	{
		fail("E_SCENE_MISSING_ID", "缺 id 的错误消息没有点名 entities[0].id");
	}
	if (!LogContains(kLog, "保存场景失败"))
	{
		fail("E_SCENE_SAVE_IO", "Save 的 IO 失败消息没有点名保存失败与路径");
	}

	HostProbeReadings state1;
	readProbe("scene-state", &state1, "tick1 完成后的场景状态");
	requireInt("E_SCENE_LIFECYCLE", "state1.active", state1.I64("active"), 1);
	requireInt("E_SCENE_LIFECYCLE", "state1.onStart", state1.I64("onStart"), 1);
	requireInt("E_SCENE_LIFECYCLE", "state1.repeatDelta", state1.I64("repeatDelta"), 0);
	requireText("E_SCENE_LIFECYCLE", "state1.startScene", state1.Get("startScene"), "场景甲");
	requireInt("E_SCENE_DT", "state1.lastDtMicros", state1.I64("lastDtMicros"), 125000);
	requireInt("E_SCENE_DT", "state1.onUpdate", state1.I64("onUpdate"), 1);
	requireInt("E_SCENE_RENDER", "state1.renderFrames", state1.I64("renderFrames"), 1);
	requireInt("E_SCENE_RENDER", "state1.entities", state1.I64("entities"), 3);
	requireInt("E_SCENE_RENDER", "state1.rendered", state1.I64("rendered"), 2);
	requireInt("E_SCENE_RENDER", "state1.shaderGroups", state1.I64("shaderGroups"), 1);
	requireInt("E_SCENE_RENDER", "state1.vertices", state1.I64("vertices"), kVertexCount);
	requireInt("E_SCENE_RENDER", "state1.pass", state1.I64("pass"), kPassCount);
	requireInt("E_SCENE_RENDER", "state1.present", state1.I64("present"), 1);
	requireText("E_SCENE_RENDER", "state1.size", state1.Get("size"), "320x240");
	// Transform 真施加到顶点上：Position (0.25,0,0) + Scale 2 → 第一顶点 (-0.25*2+0.25, -0.25*2, 0)。
	// Rotation 非零（Z 轴 π/2）却没有参与 → 读数必须仍是 (-0.25,-0.5,0)，即「旋转暂不施加」可核。
	requireInt("E_SCENE_TRANSFORM", "state1.v0xMicros", state1.I64("v0xMicros"), -250000);
	requireInt("E_SCENE_TRANSFORM", "state1.v0yMicros", state1.I64("v0yMicros"), -500000);
	requireInt("E_SCENE_TRANSFORM", "state1.v0zMicros", state1.I64("v0zMicros"), 0);
	const std::string freshFirstId = state1.Get("firstEntityId");
	if (freshFirstId.size() != 36)
	{
		fail("E_SCENE_IDENTITY", castl::string("实体 id 不是 36 字符的 uuid 字符串：") + freshFirstId);
	}

	// 像素证据 1：新场景（未经过磁盘）驱动渲染，中心纯红、背景纯蓝。
	{
		std::array<uint8_t, 4> background{};
		std::array<uint8_t, 4> center{};
		readPixels(state1, "fresh", &background, &center);
		const std::array<uint8_t, 4> expectedBackground{ 0, 0, 255, 255 };
		const std::array<uint8_t, 4> expectedCenter{ 255, 0, 0, 255 };
		if (background != expectedBackground || center != expectedCenter)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "像素不符：背景 (%u,%u,%u,%u)、中心 (%u,%u,%u,%u)",
			              background[0], background[1], background[2], background[3], center[0], center[1],
			              center[2], center[3]);
			fail("E_SCENE_PIXEL", detail);
		}
		CA_LOG("TestCSharpScene：新场景渲染中心纯红、背景纯蓝（pass={} present={}）", state1.I64("pass"),
		       state1.I64("present"));
	}

	// ================= tick 2：注销 → 从磁盘 Load → 注册（加载回来的场景同样渲染）=================
	const int32_t tick2 = pScriptHost->Tick(0.25);
	if (tick2 != tick1 + 1)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "Tick(0.25) 回值 %d，期望 %d（上一次 +1）", tick2, tick1 + 1);
		fail("E_SCENE_TICK", detail);
	}
	if (pScriptHost->LastError() != baselineError)
	{
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail), "tick2 之后 LastError 非空：%s", pScriptHost->LastError());
		fail("E_SCENE_HOST_ERROR", detail);
	}
	HostProbeReadings state2;
	readProbe("scene-state", &state2, "tick2 完成后的场景状态（加载回来的场景）");
	requireInt("E_SCENE_RELOAD", "state2.active", state2.I64("active"), 1);
	requireInt("E_SCENE_RELOAD", "state2.onStart", state2.I64("onStart"), 2);
	requireInt("E_SCENE_LIFECYCLE", "state2.onUpdate", state2.I64("onUpdate"), 2);
	requireInt("E_SCENE_DT", "state2.lastDtMicros", state2.I64("lastDtMicros"), 250000);
	requireInt("E_SCENE_RELOAD", "state2.renderFrames", state2.I64("renderFrames"), 2);
	requireInt("E_SCENE_RELOAD", "state2.pass", state2.I64("pass"), kPassCount);
	requireInt("E_SCENE_RELOAD", "state2.rendered", state2.I64("rendered"), 2);
	requireInt("E_SCENE_RELOAD", "state2.vertices", state2.I64("vertices"), kVertexCount);
	if (state2.Get("firstEntityId") != freshFirstId)
	{
		fail("E_SCENE_IDENTITY",
		     castl::string("加载回来的实体 id 与保存前不一致：") + state2.Get("firstEntityId") + " vs " +
		         freshFirstId);
	}
	// 图是否「重建」不由 token 判（对象句柄可能被复用）：结构（2 pass / 1 呈现）由 readPixels 从
	// 原生图上核，内容（顶点与像素）由下面的读数与读回核 —— 这三样任一错都会红。
	requireInt("E_SCENE_RELOAD", "state2.graph", state2.I64("graph") != 0 ? 1 : 0, 1);
	requireInt("E_SCENE_RELOAD", "state2.capture", state2.I64("capture") != 0 ? 1 : 0, 1);
	{
		std::array<uint8_t, 4> background{};
		std::array<uint8_t, 4> center{};
		readPixels(state2, "loaded", &background, &center);
		const std::array<uint8_t, 4> expectedBackground{ 0, 0, 255, 255 };
		const std::array<uint8_t, 4> expectedCenter{ 255, 0, 0, 255 };
		if (background != expectedBackground || center != expectedCenter)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "加载回来的场景像素不符：背景 (%u,%u,%u,%u)、中心 (%u,%u,%u,%u)",
			              background[0], background[1], background[2], background[3], center[0], center[1],
			              center[2], center[3]);
			fail("E_SCENE_PIXEL", detail);
		}
	}
	CA_LOG("TestCSharpScene：保存→清空内存→加载→注册→渲染，中心像素仍是纯红、实体 id 逐字相同");

	// ================= tick 3：坏资源名 → 渲染期明确报错（点名该着色器路径）=================
	// 判据不拿 Tick 回值去比「上一次 +1」（错误码是固定整数，可能撞上巧合同值）：
	// 这里判三件事 —— 报错文本来自 Tick 的异常路径、点名是场景渲染失败、点名了那个着色器路径；
	// 「没有假成功」再由下面 state4 的 renderFrames 不涨兜住（那一帧确实没有提交）。
	const int32_t tick3 = pScriptHost->Tick(0.5);
	const std::string badResourceError = pScriptHost->LastError();
	if (badResourceError.find("Tick 抛异常") == std::string::npos)
	{
		fail("E_SCENE_BAD_RES",
		     castl::string("坏着色器路径没有让 Tick 走异常路径（LastError = ") + badResourceError + "）");
	}
	if (badResourceError.find("场景渲染") == std::string::npos)
	{
		fail("E_SCENE_BAD_RES", castl::string("坏着色器路径的报错没有点名是场景渲染失败：") + badResourceError);
	}
	if (badResourceError.find(kBadShaderPath) == std::string::npos)
	{
		fail("E_SCENE_BAD_RES", castl::string("坏着色器路径的报错没有点名该资源：") + badResourceError);
	}
	CA_LOG("TestCSharpScene：坏资源名在渲染期被明确拒绝（Tick 回值 {}，错误文本点名 {}）", tick3,
	       kBadShaderPath);

	// ================= tick 4：注销 → Tick 回到零行为 =================
	const int32_t tick4 = pScriptHost->Tick(0.5);
	if (tick4 != tick2 + 2)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "注销后的 Tick 回值 %d，期望 %d（恢复正常递增）", tick4,
		              tick2 + 2);
		fail("E_SCENE_UNREGISTER", detail);
	}
	HostProbeReadings state4;
	readProbe("scene-state", &state4, "tick4（注销之后）的场景状态");
	requireInt("E_SCENE_UNREGISTER", "state4.active", state4.I64("active"), 0);
	requireInt("E_SCENE_UNREGISTER", "state4.onStart", state4.I64("onStart"), 3);
	// tick3 的坏资源帧仍然调过一次 OnUpdate（design D5 的顺序：先钩子、后翻译），只是那一帧没提交成功。
	requireInt("E_SCENE_UNREGISTER", "state4.onUpdate", state4.I64("onUpdate"), 3);
	requireInt("E_SCENE_UNREGISTER", "state4.renderFrames", state4.I64("renderFrames"),
	           state2.I64("renderFrames"));
	requireInt("E_SCENE_UNREGISTER", "state4.pass", state4.I64("pass"), 0);
	requireInt("E_SCENE_UNREGISTER", "state4.present", state4.I64("present"), 0);
	CA_LOG("TestCSharpScene 通过：{} 后端序列化往返 / 失败通道 / 生命周期 / 场景渲染像素 / "
	       "坏资源名报错 / 注销零行为 全部满足",
	       ctx.backendName);
}


// ---- TestCSharpResourceFactory（csharp-reverse-interface-binding 8.1，第 20 用例）----
// 反向接口（C# 实现 C++）的消费方验收：C# 工厂/资源经生成的反向代理被 C++ 资源系统调到。
// 判据全部钉死可观测读数：写死期望表（类型名 / 字节 / 失败行为）、句柄报 1001、保活计数精确值、
// HasResource 读数、错误槽上报（不是"文件字节不变"这种空真判据）。
namespace
{
	// 记录型批：把写进来的字节留下来，并数 SubmitAndWait 次数（失败时"没写、没提交"要能断言）。
	class ReverseRecordingBatch final : public ca_io::WBatch
	{
	public:
		bool Write(uint64_t size, void const* source) override
		{
			++Writes;
			if (size > 0u && source != nullptr)
			{
				Bytes.assign(static_cast<unsigned char const*>(source),
				             static_cast<unsigned char const*>(source) + size);
			}
			return true;
		}
		void Seek(uint64_t) override {}
		void SubmitAndWait() override { ++Submits; }
		uint32_t SubmitCount() const override { return Submits; }
		castl::string const& SourcePath() const override { return Path; }

		std::vector<unsigned char> Bytes;
		int Writes = 0;
		uint32_t Submits = 0;
		castl::string Path = "reverse-probe";
	};

	// 读型批：反向 Deserialize 被排除，任何 Read/Submit 都算"进了实现"。
	class ReverseRecordingReader final : public ca_io::IOBatch
	{
	public:
		bool Read(uint64_t, void*) override { ++Reads; return false; }
		void Seek(uint64_t) override {}
		void SubmitAndWait() override { ++Submits; }
		uint32_t SubmitCount() const override { return Submits; }
		castl::string const& SourcePath() const override { return Path; }

		int Reads = 0;
		uint32_t Submits = 0;
		castl::string Path = "reverse-probe-read";
	};

	// C++ 原生工厂对照（钉死期望表的一半）：资源字节 {0x11,0x22,0x33}，类型名 "CppProbe"。
	class NativeProbeResource final : public resource_management::IResource
	{
	public:
		void Serialize(ca_io::WBatch* writer) override
		{
			static unsigned char const kBytes[] = { 0x11, 0x22, 0x33 };
			writer->Write(sizeof(kBytes), kBytes);
			writer->SubmitAndWait();
		}
		void Deserialize(ca_io::IOBatch*) override {}
	};

	class NativeProbeFactory final : public resource_management::IResourceFactory
	{
	public:
		castl::string GetResourceType() const override { return "CppProbe"; }
		resource_management::IResource* Create() override { return new NativeProbeResource(); }
	};

	class ThrowingNativeFactory final : public resource_management::IResourceFactory
	{
	public:
		castl::string GetResourceType() const override { return "CppThrowProbe"; }
		resource_management::IResource* Create() override
		{
			throw resource_management::resource_creation_error("C++ 原生工厂 Create 故意抛出");
		}
	};

	// 读数文件最后一条 `key=value`（阶段机每阶段追加；同名键以最后一次为准）。
	std::string ReadProbeValue(std::filesystem::path const& path, char const* key)
	{
		std::ifstream in(path);
		std::string line;
		std::string value;
		const std::string prefix = std::string(key) + "=";
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (line.rfind(prefix, 0) == 0)
			{
				value = line.substr(prefix.size());
			}
		}
		return value;
	}

	// 取一张反向函数表：先用 size=0 探长度，再按长度拷出来（表版本用登记表头里的常量）。
	// 返回的指针指向 `storage`，只在本用例内有效。
	template <typename Table>
	Table* FetchReverseTable(cs_script::IScriptHost* host, uint32_t interfaceId,
	                         std::vector<unsigned char>& storage)
	{
		const uint32_t size = host->GetInterfaceApiForTest(interfaceId, nullptr,
		                                                   kBindingInterfaceTableVersion, 0u, nullptr, nullptr);
		if (size == 0u)
		{
			return nullptr;
		}
		storage.assign(size, 0u);
		const uint32_t written = host->GetInterfaceApiForTest(interfaceId, nullptr,
		                                                      kBindingInterfaceTableVersion, size,
		                                                      storage.data(), nullptr);
		if (written != size)
		{
			return nullptr;
		}
		return reinterpret_cast<Table*>(storage.data());
	}
}

void TestCSharpResourceFactory(TestContext& ctx)
{
	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		throw std::runtime_error("IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		throw std::runtime_error(std::string("托管侧不可用：") + pScriptHost->LastError());
	}
	if (ctx.pResourceManagingSystem == nullptr)
	{
		throw std::runtime_error("取不到 ResourceManagingSystem（资源模块没加载成功）");
	}

	auto fail = [](char const* code, std::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s", detail.c_str());
		CA_LOG("[TestCSharpResourceFactory 失败] {}", detail);
		throw std::runtime_error(code);
	};

	// ---- 探针门控 + 读数文件（守卫负责清理）----
	std::filesystem::path const readings =
		std::filesystem::temp_directory_path() / "cascaded_reverse_resource_probe.txt";
	std::error_code removeError;
	std::filesystem::remove(readings, removeError);
	struct ReverseProbeEnvGuard
	{
		~ReverseProbeEnvGuard()
		{
			::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_PROBE", nullptr);
			::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_PHASE", nullptr);
			::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_READINGS", nullptr);
		}
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_PROBE", "1");
	::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_READINGS", readings.string().c_str());
	::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_PHASE", "");

	// 全量跑时前面的用例可能留下宿主错误文本；只把本用例新增的错误算失败。
	const std::string baselineError = pScriptHost->LastError();
	auto tick = [&](char const* phase) {
		::SetEnvironmentVariableA("CASCADED_CSHARP_RESOURCE_PHASE", phase);
		pScriptHost->Tick(0.016);
		const std::string hostError = pScriptHost->LastError();
		if (hostError != baselineError)
		{
			fail("E_REVERSE_HOST", std::string("阶段 ") + phase + " 之后宿主 LastError 非空：" + hostError +
				"（读数：" + ReadProbeValue(readings, "probe_error") + "）");
		}
	};
	auto read = [&](char const* key) { return ReadProbeValue(readings, key); };
	auto readU64 = [&](char const* key) -> uint64_t { return std::strtoull(read(key).c_str(), nullptr, 10); };
	auto readInt = [&](char const* key) -> int64_t { return std::strtoll(read(key).c_str(), nullptr, 10); };

	// ---- 反向函数表：**必须用 CSharpHost 那一份**（表里的函数指针）----
	// 句柄表是生成在头里的 inline + 函数级 static ⇒ EXE 与 DLL 各一份。tester 自己 include 的产物
	// 解析到的是 tester.exe 的表，用它会查不到 CSharpHost.dll 创建的句柄（实测：handle=1 但
	// TakeOwnership 返回空）。所以 resolve / release / keepalive 全部走 `GetInterfaceApiForTest`
	// 拿到的表里的函数指针 —— 那才是创建句柄的那个模块。
	std::vector<unsigned char> factoryTableStorage;
	std::vector<unsigned char> resourceTableStorage;
	auto* factoryTable = FetchReverseTable<resource_management__IResourceFactoryApi_v2>(
		pScriptHost, kBindingInterfaceIdresource_management__IResourceFactory, factoryTableStorage);
	auto* resourceTable = FetchReverseTable<resource_management__IResourceApi_v2>(
		pScriptHost, kBindingInterfaceIdresource_management__IResource, resourceTableStorage);
	if (factoryTable == nullptr || resourceTable == nullptr)
	{
		fail("E_REVERSE_TABLE", std::string("取不到反向函数表（") +
			(factoryTable == nullptr ? "IResourceFactory " : "") +
			(resourceTable == nullptr ? "IResource" : "") + "）；登记表 / kBindingPolicies 缺行？");
	}
	if (factoryTable->abi_version != 2u || resourceTable->abi_version != 2u)
	{
		fail("E_REVERSE_TABLE_ABI", "反向函数表的 abi_version 不是 2");
	}

	auto requireKeepAlive = [&](char const* what, bool factory, uint64_t handle,
	                            uint64_t expected) {
		const uint64_t actual = factory ? factoryTable->ReverseKeepAliveCount(handle)
		                                : resourceTable->ReverseKeepAliveCount(handle);
		if (actual != expected)
		{
			fail("E_REVERSE_KEEPALIVE",
			     std::string(what) + "：保活计数是 " + std::to_string(actual) + "，期望 " +
			         std::to_string(expected));
		}
	};

	// ---- ① C# 工厂经 CreateImpl 注册（句柄注册 + 借用指针 + 释放回调）----
	tick("create-factory");
	const std::string factoryType = read("factory_type");
	if (factoryType != "CSharpProbe")
	{
		fail("E_REVERSE_FACTORY_TYPE", "C# 工厂的类型名是 \"" + factoryType + "\"，写死期望 \"CSharpProbe\"");
	}
	const uint64_t factoryHandle = readU64("factory_handle");
	if (factoryHandle == 0u)
	{
		fail("E_REVERSE_FACTORY_HANDLE", "C# CreateImpl 没有返回强句柄（读数 factory_handle=0）");
	}
	if (readU64("factory_keepalive") != 1u)
	{
		fail("E_REVERSE_FACTORY_KEEPALIVE", "工厂注册后保活计数应为 1，实际读数 " + read("factory_keepalive"));
	}
	// 用**创建模块的表**解析句柄：拿到的借用指针就是 CSharpHost 里那个代理。
	resource_management::IResourceFactory* factoryPtr = static_cast<resource_management::IResourceFactory*>(
		factoryTable->ResolveImpl(factoryHandle));
	if (factoryPtr == nullptr)
	{
		fail("E_REVERSE_FACTORY_RESOLVE", "ResolveImpl(句柄) 返回空（句柄查不到代理）");
	}
	ctx.pResourceManagingSystem->RegisterFactory("CSharpProbe", factoryHandle, factoryPtr,
	                                             factoryTable->ReleaseImpl);
	if (!ctx.pResourceManagingSystem->HasFactory("CSharpProbe"))
	{
		fail("E_REVERSE_FACTORY_REGISTER", "RegisterFactory(句柄) 之后 HasFactory 仍是 false");
	}
	resource_management::IResourceFactory* csharpFactory =
		ctx.pResourceManagingSystem->GetFactory("CSharpProbe");
	if (csharpFactory == nullptr || csharpFactory != factoryPtr)
	{
		fail("E_REVERSE_FACTORY_GET", "GetFactory(\"CSharpProbe\") 没有返回同一份工厂（借用指针不符）");
	}
	// 单一所有者不变：强引用仍在创建模块的句柄表里（注册表只持借用指针 + 释放能力），
	// 所以句柄必须**仍可解析到同一个代理**（不是"取走强引用后降级成借用"那条同模块路径）。
	if (factoryTable->ResolveImpl(factoryHandle) != static_cast<void*>(factoryPtr))
	{
		fail("E_REVERSE_FACTORY_HANDLE_KIND", "注册之后同一个句柄解析不到同一个工厂代理");
	}

	// ---- ② 按类型名运行时构造 C# 资源（Create → shared_ptr → RegisterResource，不 Deserialize）----
	const std::string csharpResourcePath = "ReverseProbe/CSharp.res";
	auto csharpResource = ctx.pResourceManagingSystem->CreateResource("CSharpProbe", csharpResourcePath);
	if (!csharpResource)
	{
		fail("E_REVERSE_CREATE", "CreateResource 返回空");
	}
	if (!ctx.pResourceManagingSystem->HasResource(csharpResourcePath))
	{
		fail("E_REVERSE_REGISTER", "CreateResource 之后 HasResource 仍是 false");
	}
	if (ctx.pResourceManagingSystem->GetResource(csharpResourcePath).get() != csharpResource.get())
	{
		fail("E_REVERSE_SAME_INSTANCE", "按路径取回的不是同一个实例");
	}
	tick("after-create");
	const uint64_t resourceHandle = readU64("resource_handle");
	if (resourceHandle == 0u)
	{
		fail("E_REVERSE_RESOURCE_HANDLE", "C# 资源的借用句柄是 0（CreateProxy 没发句柄）");
	}
	requireKeepAlive("资源注册后", false, resourceHandle, 1u);

	// ---- ③ 写路径往返：钉死 {0xDE,0xAD,0xBE,0xEF}，长度 4，copy 返 4 ----
	{
		ReverseRecordingBatch batch;
		csharpResource->Serialize(&batch);
		std::vector<unsigned char> const expected = { 0xDE, 0xAD, 0xBE, 0xEF };
		if (batch.Bytes != expected)
		{
			fail("E_REVERSE_SERIALIZE_BYTES", "写进批的字节不是写死期望 {DE,AD,BE,EF}");
		}
		if (batch.Bytes.size() != 4u)
		{
			fail("E_REVERSE_SERIALIZE_LEN", "写进批的字节数是 " + std::to_string(batch.Bytes.size()) +
				"，期望 4");
		}
		if (batch.Submits != 1u)
		{
			fail("E_REVERSE_SERIALIZE_SUBMIT", "写完之后 SubmitAndWait 次数是 " +
				std::to_string(batch.Submits) + "，期望 1");
		}
	}
	// 反向读被排除：代理抛 1013，且一个字节都不读。
	{
		ReverseRecordingReader reader;
		bool threw = false;
		try
		{
			csharpResource->Deserialize(&reader);
		}
		catch (cascaded_reverse::ReverseBindingError const& error)
		{
			threw = error.Code() == 1013;
			if (!threw)
			{
				// 诊断：跨模块抛过来的对象若布局不一致，`Code()` 会读到错误偏移；`what()` 走虚函数不受
				// 布局影响，所以把两者一起打出来，判"是不是同一个异常"。
				fail("E_REVERSE_DESERIALIZE_CODE", std::string("Deserialize 抛的错误码是 ") +
					std::to_string(error.Code()) + "，what=\"" + error.what() +
					"\"，sizeof=" + std::to_string(sizeof(cascaded_reverse::ReverseBindingError)) +
					"，期望 1013");
			}
		}
		catch (std::exception const& other)
		{
			fail("E_REVERSE_DESERIALIZE_TYPE",
			     std::string("Deserialize 抛的不是 ReverseBindingError：") + other.what());
		}
		if (!threw)
		{
			fail("E_REVERSE_DESERIALIZE", "反向 Deserialize 没有抛 ReverseBindingError");
		}
		if (reader.Reads != 0 || reader.Submits != 0)
		{
			fail("E_REVERSE_DESERIALIZE_IO", "反向 Deserialize 动了批（Reads/Submits 非 0）");
		}
	}

	// ---- ④ Serialize 失败：C# 抛异常 → len 哨兵 → 代理中止（不写不提交）→ 下一次调用上报 1012 ----
	tick("make-serialize-throw");
	const int64_t serializeCallsBeforeFailure = readInt("serialize_calls");
	{
		ReverseRecordingBatch batch;
		csharpResource->Serialize(&batch);   // 第一次：C# 抛异常，代理只落槽、中止
		if (!batch.Bytes.empty() || batch.Submits != 0u)
		{
			fail("E_REVERSE_FAIL_WROTE", "Serialize 失败却写了批（字节数 " +
				std::to_string(batch.Bytes.size()) + "、提交 " + std::to_string(batch.Submits) + "）");
		}
		bool threw = false;
		ReverseRecordingBatch secondCall;
		try
		{
			csharpResource->Serialize(&secondCall);
		}
		catch (cascaded_reverse::ReverseBindingError const& error)
		{
			const std::string text = error.what();
			threw = error.Code() == 1012 && text.find("反向接口") != std::string::npos &&
				text.find("反向资源 Serialize 故意抛出的异常") != std::string::npos;
			if (!threw)
			{
				fail("E_REVERSE_FAIL_REPORT", "下一次调用没有按契约上报（码 " +
					std::to_string(error.Code()) + "，文本 " + text + "）");
			}
		}
		if (!threw)
		{
			fail("E_REVERSE_FAIL_SILENT", "Serialize 失败之后的下一次调用没有抛异常（静默继续）");
		}
	}
	tick("after-throw");
	if (readInt("serialize_calls") != serializeCallsBeforeFailure + 1)
	{
		fail("E_REVERSE_FAIL_CALLS", "C# 覆写调用数不符：失败前 " +
			std::to_string(serializeCallsBeforeFailure) + "，现在是 " + read("serialize_calls") +
			"（期望只多 1 次 —— 中止后的那次调用不许再进 C#）");
	}

	// ---- ⑤ 非注册线程：1005 落槽 + 中止 + C# 覆写 0 次；下次注册线程调用上报 1005 ----
	{
		ReverseRecordingBatch crossThreadBatch;
		std::thread crossThread([&]() { csharpResource->Serialize(&crossThreadBatch); });
		crossThread.join();
		if (!crossThreadBatch.Bytes.empty() || crossThreadBatch.Submits != 0u)
		{
			fail("E_REVERSE_THREAD_WROTE", "非注册线程调用却写了批");
		}
		bool threw = false;
		ReverseRecordingBatch secondCall;
		try
		{
			csharpResource->Serialize(&secondCall);
		}
		catch (cascaded_reverse::ReverseBindingError const& error)
		{
			threw = error.Code() == 1005;
			if (!threw)
			{
				fail("E_REVERSE_THREAD_CODE", "非注册线程之后上报的错误码是 " +
					std::to_string(error.Code()) + "，期望 1005");
			}
		}
		if (!threw)
		{
			fail("E_REVERSE_THREAD_SILENT", "非注册线程调用被静默吞掉（下一次调用没有抛 1005）");
		}
	}
	tick("report-counters-thread");
	if (readInt("serialize_calls") != serializeCallsBeforeFailure + 1)
	{
		fail("E_REVERSE_THREAD_CALLS", "非注册线程竟然进了 C# 覆写（serialize_calls 涨了）");
	}

	// ---- ⑥ Create 失败：抛异常、不注册空、第二次重新调 Create（计数 2）----
	tick("create-failing-factories");
	const uint64_t throwingHandle = readU64("throwing_factory_handle");
	const uint64_t nullHandle = readU64("null_factory_handle");
	if (throwingHandle == 0u || nullHandle == 0u)
	{
		fail("E_REVERSE_FAIL_FACTORY_HANDLE", "失败场景的工厂句柄是 0");
	}
	auto resolveFactory = [&](uint64_t handle) {
		return static_cast<resource_management::IResourceFactory*>(factoryTable->ResolveImpl(handle));
	};
	if (resolveFactory(throwingHandle) == nullptr || resolveFactory(nullHandle) == nullptr)
	{
		fail("E_REVERSE_FAIL_FACTORY_RESOLVE", "失败场景的工厂句柄解析不到代理");
	}
	ctx.pResourceManagingSystem->RegisterFactory("CSharpThrowProbe", throwingHandle,
	                                             resolveFactory(throwingHandle),
	                                             factoryTable->ReleaseImpl);
	ctx.pResourceManagingSystem->RegisterFactory("CSharpNullProbe", nullHandle,
	                                             resolveFactory(nullHandle),
	                                             factoryTable->ReleaseImpl);
	for (int attempt = 1; attempt <= 2; ++attempt)
	{
		const std::string path = "ReverseProbe/Throwing_" + std::to_string(attempt) + ".res";
		bool threw = false;
		try
		{
			ctx.pResourceManagingSystem->CreateResource("CSharpThrowProbe", path);
		}
		catch (resource_management::resource_creation_error const& error)
		{
			threw = std::string(error.what()).find("反向资源 Serialize") == std::string::npos &&
				std::string(error.what()).find("反向工厂 Create 故意抛出的异常") != std::string::npos;
		}
		if (!threw)
		{
			fail("E_REVERSE_CREATE_FAIL_THROW",
			     "第 " + std::to_string(attempt) + " 次 Create 失败没有抛含原始文本的异常");
		}
		if (ctx.pResourceManagingSystem->HasResource(path))
		{
			fail("E_REVERSE_CREATE_FAIL_REGISTER", "Create 失败却把空资源注册进了资源表（路径 " + path + "）");
		}
	}
	{
		const std::string path = "ReverseProbe/Null.res";
		bool threw = false;
		try
		{
			ctx.pResourceManagingSystem->CreateResource("CSharpNullProbe", path);
		}
		catch (resource_management::resource_creation_error const&)
		{
			threw = true;
		}
		if (!threw || ctx.pResourceManagingSystem->HasResource(path))
		{
			fail("E_REVERSE_CREATE_NULL", "Create 返回 null 的场景没有抛异常，或注册了空资源");
		}
	}
	tick("report-counters-create");
	if (readInt("throwing_create_count") != 2)
	{
		fail("E_REVERSE_CREATE_COUNT", "Create 抛异常的工厂被调了 " + read("throwing_create_count") +
			" 次，期望 2（第二次 Load 必须重新调 Create）");
	}
	if (readInt("null_create_count") != 1)
	{
		fail("E_REVERSE_CREATE_NULL_COUNT", "Create 返回 null 的工厂调用次数是 " +
			read("null_create_count") + "，期望 1");
	}
	// 未注册类型：明确报错，不静默返回空。
	{
		bool threw = false;
		try
		{
			ctx.pResourceManagingSystem->CreateResource("NoSuchFactoryType", "ReverseProbe/None.res");
		}
		catch (resource_management::resource_creation_error const& error)
		{
			threw = std::string(error.what()).find("未注册") != std::string::npos;
		}
		if (!threw)
		{
			fail("E_REVERSE_CREATE_UNREGISTERED", "未注册的类型没有明确报错");
		}
	}

	// ---- ⑦ C++ 原生工厂对照（钉死期望表逐项核）----
	{
		auto nativeFactory = castl::shared_ptr<resource_management::IResourceFactory>(
			new NativeProbeFactory(), [](resource_management::IResourceFactory* released) { delete released; });
		ctx.pResourceManagingSystem->RegisterFactory("CppProbe", nativeFactory);
		auto nativeResource = ctx.pResourceManagingSystem->CreateResource("CppProbe", "ReverseProbe/Cpp.res");
		if (!nativeResource)
		{
			fail("E_REVERSE_CPP_CREATE", "C++ 原生工厂 Create 返回空");
		}
		ReverseRecordingBatch batch;
		nativeResource->Serialize(&batch);
		std::vector<unsigned char> const expected = { 0x11, 0x22, 0x33 };
		if (batch.Bytes != expected)
		{
			fail("E_REVERSE_CPP_BYTES", "C++ 原生资源的字节与写死期望 {11,22,33} 不符");
		}
		if (nativeFactory->GetResourceType() != "CppProbe" ||
			csharpFactory->GetResourceType() != "CSharpProbe")
		{
			fail("E_REVERSE_COMPARE_TYPE", "C++/C# 工厂的 GetResourceType 与写死期望不符");
		}
		// 失败行为两边都抛异常（C++ 原生直接抛；C# 经桥转码）。
		auto throwingNative = castl::shared_ptr<resource_management::IResourceFactory>(
			new ThrowingNativeFactory(), [](resource_management::IResourceFactory* released) { delete released; });
		ctx.pResourceManagingSystem->RegisterFactory("CppThrowProbe", throwingNative);
		bool cppThrew = false;
		try
		{
			ctx.pResourceManagingSystem->CreateResource("CppThrowProbe", "ReverseProbe/CppThrow.res");
		}
		catch (resource_management::resource_creation_error const&)
		{
			cppThrew = true;
		}
		if (!cppThrew)
		{
			fail("E_REVERSE_CPP_THROW", "C++ 原生工厂 Create 抛异常没有被转成 resource_creation_error");
		}
	}

	// ---- ⑧ 寿命两档：C# Dispose 只掉借用一份（句柄 1001 + 按路径同一实例 + 计数不归零）----
	tick("dispose-resource");
	if (readInt("dispose_code") != 0)
	{
		fail("E_REVERSE_DISPOSE_CODE", "C# 第一次 Dispose 返回 " + read("dispose_code") + "，期望 0");
	}
	if (readInt("dispose_again_code") != 1001)
	{
		fail("E_REVERSE_DISPOSE_AGAIN", "C# 第二次 Dispose 返回 " + read("dispose_again_code") +
			"，期望 1001（double-free 守卫）");
	}
	if (readU64("resource_keepalive_after_dispose") != 1u)
	{
		fail("E_REVERSE_DISPOSE_KEEPALIVE", "Dispose 之后保活计数是 " +
			read("resource_keepalive_after_dispose") + "，期望仍是 1（注册表还持有）");
	}
	requireKeepAlive("C# Dispose 之后（注册表仍持）", false, resourceHandle, 1u);
	if (ctx.pResourceManagingSystem->GetResource(csharpResourcePath).get() != csharpResource.get())
	{
		fail("E_REVERSE_DISPOSE_INSTANCE", "C# Dispose 之后按路径取回的不是同一个实例");
	}
	// 注册表释放 → 代理析构 → releaseUser → 托管 GCHandle.Free → 计数归零。
	csharpResource.reset();
	if (!ctx.pResourceManagingSystem->ReleaseResource(csharpResourcePath))
	{
		fail("E_REVERSE_RELEASE_RESOURCE", "ReleaseResource 返回 false");
	}
	tick("report-resource-keepalive");
	requireKeepAlive("注册表释放之后", false, resourceHandle, 0u);

	// ---- ⑨ 工厂替换（supersede）：旧 C# 工厂在替换时释放 → 保活归零 ----
	// 注册表持的是借用指针，所以"替换"就是注册表调 release(旧句柄) → CSharpHost 里代理析构 →
	// releaseUser → GCHandle.Free。tester 自己不再持有任何拷贝（GetFactory 返回借用指针）。
	{
		auto replacement = castl::shared_ptr<resource_management::IResourceFactory>(
			new NativeProbeFactory(), [](resource_management::IResourceFactory* released) { delete released; });
		ctx.pResourceManagingSystem->RegisterFactory("CSharpProbe", replacement);
		requireKeepAlive("工厂被替换之后（旧 C# 工厂）", true, factoryHandle, 0u);
	}
	tick("dispose-factory");
	// 工厂已经被替换（注册表放掉了唯一强引用 → 代理析构 → 句柄已按代际失效）：两次 Dispose 都必须是
	// 1001 —— 这正是"句柄失效可观测"的另一面；0 只可能出现在"注册表还持有时主动 Dispose"那条路，
	// 而那条路之后每帧都可能再调工厂，Free 掉托管对象会变成悬空，本用例不走。
	if (readInt("factory_dispose_code") != 1001 || readInt("factory_dispose_again_code") != 1001)
	{
		fail("E_REVERSE_FACTORY_DISPOSE", "工厂被替换之后 Dispose 两次的返回码是 " +
			read("factory_dispose_code") + " / " + read("factory_dispose_again_code") +
			"，期望 1001 / 1001（句柄已失效且幂等）");
	}

	// ---- ⑩ 计数清零与收尾：注册表里的东西都放掉，下一用例从干净状态开始 ----
	{
		bool erased = ctx.pResourceManagingSystem->UnregisterFactory("CSharpProbe");
		erased = ctx.pResourceManagingSystem->UnregisterFactory("CppProbe") || erased;
		erased = ctx.pResourceManagingSystem->UnregisterFactory("CSharpThrowProbe") || erased;
		erased = ctx.pResourceManagingSystem->UnregisterFactory("CSharpNullProbe") || erased;
		erased = ctx.pResourceManagingSystem->UnregisterFactory("CppThrowProbe") || erased;
		if (!erased)
		{
			fail("E_REVERSE_CLEANUP", "一个工厂都没注销成功（注册表状态不对劲）");
		}
	}
	ctx.pResourceManagingSystem->ReleaseResource("ReverseProbe/Cpp.res");
	CA_LOG("TestCSharpResourceFactory 通过：C# 工厂注册（句柄）→ Create 造 C# 资源 → 写路径往返 "
	       "{DE,AD,BE,EF}（长度 4）→ Deserialize 拒（1013）→ Serialize 失败上报（1012）→ 非注册线程拒"
	       "（1005）→ Create 失败不注册空（HasResource=false、计数 2）→ C++ 原生工厂对照 → 寿命两档"
	       "（Dispose 1001 + 同一实例 + 计数 1→0）");
}

// ---- TestCSharpWindowCallbacks（本 change 任务 4.1 / 4.2 / 4.3 / 5.2）----
// 10 类窗口/输入回调走一整圈：C++ 侧用 `fake_window::System` 确定性触发，托管侧注册真实
// 逐类对拍。生命周期也在这里验：替换后只有新委托执行、置空后旧委托不再执行、重绑会注销旧表面对象、
// `Shutdown` 之前必须先执行托管注销（关闭后旧委托不再执行）。
//
// 红态怎么构造：让 `CallbackSlot.Release` 不释放 `GCHandle`（保活项不归零）、去掉替换/置空后的注销、
// 或让 `CSharpHost::Shutdown` 不调托管 `Shutdown` —— 本测试与脚本都会变红。
void TestCSharpWindowCallbacks(TestContext& ctx)
{
	auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
	if (pScriptHost == nullptr)
	{
		throw std::runtime_error("IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}
	if (!pScriptHost->IsAvailable())
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "托管侧不可用：%s", pScriptHost->LastError());
		throw std::runtime_error("E_CALLBACK_HOST");
	}

	struct CallbackEnvGuard
	{
		~CallbackEnvGuard()
		{
			::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PROBE", nullptr);
			::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", nullptr);
		}
	} envGuard;
	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PROBE", "1");
	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "");

	// 全量跑时 `TestCSharpWindow` 会故意触发一次"未知接口编号 9"，引擎侧 `LastError` 会一直留着那条文本
	// （CSharpHost 只在托管侧写错误时才覆盖它）。这里先记下基线：只有本用例产生的错误才算失败。
	const std::string baselineError = pScriptHost->LastError();
	auto checkHostError = [&pScriptHost, &baselineError](char const* stage) {
		const std::string current = pScriptHost->LastError();
		if (current == baselineError)
		{
			return;
		}
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s 之后 LastError 非空：%s", stage, current.c_str());
		throw std::runtime_error("E_CALLBACK_ERROR");
	};
	auto logTrigger = [](char const* name, cawindow::IWindow* window, std::string const& args) {
		CA_LOG("[C++ 回调] {} window=0x{:x} args=({})", name, reinterpret_cast<uintptr_t>(window), args);
	};

	fake_window::System systemA;
	auto windowA = systemA.NewWindow(640, 480, "Callback Probe Window", true, true, true, false).lock();
	if (!windowA)
	{
		throw std::runtime_error("E_CALLBACK_WINDOW_A");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowA.get());
	// 默认实例 = 该接口**最先登记**的那一条：全量跑时 TestSimpleTriangle 已经登记过真实窗口系统，
	// 所以这里必须用同一个实例名去覆盖那一条，否则托管侧仍然绑到真实实现（假实现上的触发一次都不到）。
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, &systemA);

	// 失败时也要在**假实现还活着**的时候先关闭运行时：否则进程收尾时托管侧会去调一个已经析构的
	// 栈上夹具（`Host.Shutdown` → 注销回调 → 虚调用），把"测试失败"变成崩溃。
	try
	{

	// 固定非零显示器读数的 C++ 侧：托管侧在 Tick 里读同一份夹具数据，脚本逐字段对拍
	{
		const cawindow::MonitorInfo info = systemA.GetMonitor(0);
		CA_LOG("[C++ GetMonitor] count={}", systemA.GetMonitorCount());
		CA_LOG("[C++ GetMonitor] probe=1 id=0 rect=({},{},{},{}) work=({},{},{},{}) dpi={}", info.m_MonitorRect.x,
		       info.m_MonitorRect.y, info.m_MonitorRect.width, info.m_MonitorRect.height, info.m_WorkAreaRect.x,
		       info.m_WorkAreaRect.y, info.m_WorkAreaRect.width, info.m_WorkAreaRect.height, info.m_DPIScale);
	}

	// Tick #1：绑定 + 注册 10 类回调（托管侧断言保活项 = 10）
	pScriptHost->Tick(0.016);
	checkHostError("注册 10 类回调");

	// 10 类事件各触发一次；参数与 `UserScript.CallbackProbe` 的常量逐项一致
	int triggered = 0;
	logTrigger("SetWindowFocusCallback", windowA.get(), "1");
	triggered += systemA.TriggerWindowFocus(windowA.get(), true);
	logTrigger("SetCursorEnterCallback", windowA.get(), "1");
	triggered += systemA.TriggerCursorEnter(windowA.get(), true);
	logTrigger("SetCursorPosCallback", windowA.get(), "12.5,34.5");
	triggered += systemA.TriggerCursorPos(windowA.get(), 12.5f, 34.5f);
	logTrigger("SetMouseButtonCallback", windowA.get(), "1,0,3");
	triggered += systemA.TriggerMouseButton(windowA.get(), 1, 0, 3);
	logTrigger("SetScrollCallback", windowA.get(), "1.5,-2.5");
	triggered += systemA.TriggerScroll(windowA.get(), 1.5f, -2.5f);
	logTrigger("SetWindowCloseCallback", windowA.get(), "");
	triggered += systemA.TriggerWindowClose(windowA.get());
	logTrigger("SetWindowPosCallback", windowA.get(), "100.5,200.5");
	triggered += systemA.TriggerWindowPos(windowA.get(), 100.5f, 200.5f);
	logTrigger("SetWindowSizeCallback", windowA.get(), "800.5,600.5");
	triggered += systemA.TriggerWindowSize(windowA.get(), 800.5f, 600.5f);
	logTrigger("SetKeyCallback", windowA.get(), "65,30,1,2");
	triggered += systemA.TriggerKey(windowA.get(), 65, 30, 1, 2);
	logTrigger("SetCharCallback", windowA.get(), "65");
	triggered += systemA.TriggerChar(windowA.get(), 65u);
	if (triggered != 10)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "10 类回调只触发了 %d 类", triggered);
		throw std::runtime_error("E_CALLBACK_TRIGGER");
	}

	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "verify-all");
	pScriptHost->Tick(0.016);
	checkHostError("校验 10 类回调并替换/置空");

	// 替换后的焦点回调执行；置空后的光标进入回调不再执行（不记 C++ 触发行，免得被当成第二组）
	int const replacedCalled = systemA.TriggerWindowFocus(windowA.get(), true);
	int const clearedCalled = systemA.TriggerCursorEnter(windowA.get(), true);
	if (replacedCalled != 1 || clearedCalled != 0)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail),
		              "替换后焦点回调触发结果 = %d（期望 1）、置空后光标进入触发结果 = %d（期望 0）", replacedCalled,
		              clearedCalled);
		throw std::runtime_error("E_CALLBACK_REPLACE");
	}

	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "verify-replace");
	pScriptHost->Tick(0.016);
	checkHostError("校验替换与置空");

	// 任务 4.3：用户委托抛异常不许穿出原生边界；下一次 UpdateSystem 必须原样上报，随后正常回调仍执行
	systemA.TriggerScroll(windowA.get(), 3.5f, 4.5f);
	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "verify-exception");
	pScriptHost->Tick(0.016);
	checkHostError("回调异常上报");
	systemA.TriggerScroll(windowA.get(), 5.5f, 6.5f);   // 异常之后重新注册的正常回调

	// 任务 4.3：跨线程回调不许直接进入托管委托（桥必须记录 1005 并停止该次调用）
	std::thread crossThread([&systemA, &windowA]() { systemA.TriggerWindowFocus(windowA.get(), true); });
	crossThread.join();
	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "verify-thread");
	pScriptHost->Tick(0.016);
	checkHostError("跨线程回调拒绝");

	// 重绑：同一对 (编号, 实例名) 换成第二份实现，托管 EnsureBound 会 Dispose 旧表面对象
	fake_window::System systemB;
	auto windowB = systemB.NewWindow(640, 480, "Callback Probe Window", true, true, true, false).lock();
	if (!windowB)
	{
		throw std::runtime_error("E_CALLBACK_WINDOW_B");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowB.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, &systemB);
	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "verify-rebind");
	pScriptHost->Tick(0.016);
	checkHostError("校验重绑");

	// 重绑已让托管侧释放旧表面对象并注册到新实现；现在触发两侧事件，再确认计数与保活项
	int const oldSystemCalled = systemA.TriggerWindowFocus(windowA.get(), true);
	int const newSystemCalled = systemB.TriggerWindowFocus(windowB.get(), true);
	if (oldSystemCalled != 0 || newSystemCalled != 1)
	{
		std::snprintf(g_errorDetail, sizeof(g_errorDetail),
		              "重绑后旧实现触发结果 = %d（期望 0）、新实现 = %d（期望 1）", oldSystemCalled, newSystemCalled);
		throw std::runtime_error("E_CALLBACK_REBIND");
	}
	::SetEnvironmentVariableA("CASCADED_CSHARP_CALLBACK_PHASE", "verify-rebind-done");
	pScriptHost->Tick(0.016);
	checkHostError("校验重绑后的回调执行");

	// 关闭：托管 `Shutdown` 必须先释放表面对象并注销回调，之后旧委托不许再执行
	pScriptHost->Shutdown();
	int const afterShutdown = systemB.TriggerWindowFocus(windowB.get(), true);
	if (afterShutdown != 0)
	{
		throw std::runtime_error("E_CALLBACK_SHUTDOWN");
	}
	if (pScriptHost->IsAvailable())
	{
		throw std::runtime_error("E_CALLBACK_STILL_AVAILABLE");
	}
	CA_LOG("[C++ 回调] Shutdown 之后旧委托不再执行（第二份实现的回调已被注销）");
	}
	catch (...)
	{
		pScriptHost->Shutdown();
		throw;
	}
}

// ---- TestGPUTiming（vulkan-gpu-queries / 能力 `gpu-graph-timing`）----
// 一张图：一个渲染 pass + 一个显式 `SetAsync(true)` 的计算 pass。调 `ExecuteGraphTimed` → `Wait()` →
// `Passes()`，断言两条结果、`passType`/`passIndex` 对得上、`queue` 是实际队列、`endNs >= beginNs`、
// 名称非空。
// 红态构造（后续任务）：2.2 / 3.2 把已交付结果改成指向被后续执行覆盖的缓冲；4.1 删掉某个后端
// compute pass 的查询、或把 direct 回退伪报成 compute 队列。
void TestGPUTiming(TestContext& ctx)
{
	// 失败详情必须**直接**写 stderr：异常消息跨 mimalloc / ucrtbase 两个堆的边界可能变成 0xDD
	// （见 `g_errorDetail` 那段注释），而任务 1.2 的验收要求 stderr 里有"当前后端不支持计时"原文。
	auto const fail = [](char const* code, castl::string const& detail) {
		std::cerr << "TestGPUTiming 失败：" << detail << std::endl;
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s", detail.c_str());
		throw std::runtime_error(code);
	};

	GPUTimingCapabilities const directCaps = ctx.pGPUBackend->GetTimingCapabilities(EGPUQueueType::eDirect);
	if (!directCaps.passTimestamps)
	{
		fail("E_TIMING_UNSUPPORTED", castl::string("当前后端不支持计时：direct 队列 passTimestamps = false"));
		return;
	}
	// 计算队列存不存在是后端的事：Vulkan 没有独立计算队列时，异步计算 pass 合法地回退到 direct
	// （design D4）。计算 pass 的期望队列由这个能力位决定，测试不假设跨后端映射相同。
	bool const computeQueueAvailable =
		ctx.pGPUBackend->GetTimingCapabilities(EGPUQueueType::eCompute).passTimestamps;

	auto newWindow = ctx.pWindowSystem->NewWindow(1024, 512, "GPUTiming");
	auto windowHandle = ctx.pGPUBackend->GetWindowHandle(newWindow.lock());
	ImageHandle windowBackBuffer(windowHandle);

	struct TimingVertex
	{
		std::array<float, 3> pos;
		std::array<float, 3> color;
	};
	std::vector<TimingVertex> const vertices = {
		{{-0.25f, -0.25f, 0.25f }, {1.0f, 0.0f, 0.0f}},
		{{0.25f, -0.25f, 0.25f }, {0.0f, 1.0f, 0.0f}},
		{{0.0f, 0.5f, 0.5f }, {0.0f, 0.0f, 1.0f}},
	};
	cacore::HashObj<VertexInputsDescriptor> const vertexDescs = VertexInputsDescriptor::Create(sizeof(TimingVertex),
		{
			VertexAttribute::Create(offsetof(TimingVertex, pos), VertexInputFormat::eR32G32B32_SFloat, CANAME("POSITION")),
			VertexAttribute::Create(offsetof(TimingVertex, color), VertexInputFormat::eR32G32B32_SFloat, CANAME("COLOR")),
		}, false);

	BufferHandle vbuffer(CANAME("GPUTimingVertexBuffer"));
	BufferHandle computeBuffer(CANAME("GPUTimingComputeBuffer"));
	auto computeParams = ctx.pGPUBackend->CreateShaderStruct(CANAME("TestComputeBufferParams"));
	computeParams->SetBuffer(CANAME("RWVertexBuffer"), computeBuffer);

	// 渲染 pass 显式命名：名称规则要求取 `RenderPass::GetName()`（design D3）。
	cacore::NameHash const mainRenderPassName = CANAME("GPUTimingMainRenderPass");
	auto renderContent = [&](cacore::NameHash const& passName) -> RenderPass {
		return RenderPass::New(windowBackBuffer
			, AttachmentConfig::Clear(GraphicsClearValue::ClearColor(0.0f, 0.0f, 0.0f, 1.0f)))
			.Name(passName)
			.SetShaderInfo({ CAPATH("Shaders/Test/TestSimpleTriangle") })
			.Batch
			(
				DrawCallBatch::New()
				.VertexStream(CANAME("TestVerticesInput"), vertexDescs)
				.DrawCall(
					DrawCall::New()
					.SetVertexBuffer(CANAME("TestVerticesInput"), vbuffer)
					.Draw(vertices.size())
				)
			);
	};

	auto buildGraph = [&](bool asyncCompute) -> castl::shared_ptr<GPUGraph> {
		auto graph = castl::make_shared<GPUGraph>();
		graph->Present(windowBackBuffer)
			.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(vertices.size(), sizeof(vertices[0])))
			.ScheduleData(vbuffer, vertices.data(), vertices.size() * sizeof(vertices[0]))
			.AllocBuffer(computeBuffer, GPUBufferDescriptor::Create(4, sizeof(float) * 3))
			.Comp(ComputeBatch::New(asyncCompute)
				.Dispatch(ComputeDispatch::Create({ CAPATH("Shaders/Test/TestComputeVertexBuffer") }, 1, 1, 1)
					.SetParam(CANAME("computeParams"), computeParams))
			)
			.Rast(renderContent(mainRenderPassName));
		return graph;
	};

	auto runTimed = [&](bool asyncCompute) {
		auto scheduler = ctx.pThreadManager->NewScheduler();
		return ctx.pGPUBackend->ExecuteGraphTimed(scheduler.get(), buildGraph(asyncCompute));
	};
	auto const findPass = [](std::span<GPUPassTiming const> all, EGPUTimedPassType type) -> GPUPassTiming const* {
		for (auto const& pass : all)
		{
			if (pass.passType == type)
				return &pass;
		}
		return nullptr;
	};

	// ---- 一、异步计算图 ----
	auto timingAsync = runTimed(true);
	if (!timingAsync)
	{
		fail("E_TIMING_NULL", castl::string("ExecuteGraphTimed 返回空结果（异步计算图）"));
		return;
	}
	timingAsync->Wait();
	std::span<GPUPassTiming const> const passes = timingAsync->Passes();
	if (passes.size() != 2)
	{
		fail("E_TIMING_COUNT", castl::string("异步计算图应返回两条结果，实际 ") + std::to_string(passes.size()));
	}
	GPUPassTiming const* pRender = findPass(passes, EGPUTimedPassType::eRender);
	GPUPassTiming const* pCompute = findPass(passes, EGPUTimedPassType::eCompute);
	if (pRender == nullptr || pCompute == nullptr)
	{
		fail("E_TIMING_TYPES", castl::string("两条结果的 passType 不是一个 eRender + 一个 eCompute"));
		return;
	}
	if (pRender->passIndex != 0 || pCompute->passIndex != 0)
	{
		fail("E_TIMING_INDEX", castl::string("passIndex 应分别是该类型内的原始序号 0 / 0，实际 ") +
			std::to_string(pRender->passIndex) + " / " + std::to_string(pCompute->passIndex));
	}
	if (pRender->queue != EGPUQueueType::eDirect)
	{
		fail("E_TIMING_RENDER_QUEUE", castl::string("渲染 pass 的队列应为 eDirect（实际 ") +
			std::to_string(static_cast<uint32_t>(pRender->queue)) + "）");
	}
	EGPUQueueType const expectedComputeQueue = computeQueueAvailable ? EGPUQueueType::eCompute : EGPUQueueType::eDirect;
	if (pCompute->queue != expectedComputeQueue)
	{
		fail("E_TIMING_COMPUTE_QUEUE", castl::string("异步计算 pass 的队列与后端实际队列不符：期望 ") +
			(computeQueueAvailable ? "eCompute" : "eDirect（后端没有独立计算队列）") + "，实际 " +
			std::to_string(static_cast<uint32_t>(pCompute->queue)));
	}
	if (pRender->name.empty() || pCompute->name.empty())
	{
		fail("E_TIMING_NAME", castl::string("结果名称不得为空：渲染 = \"") + pRender->name + "\"，计算 = \"" +
			pCompute->name + "\"");
	}
	// 名称规则（任务 2.1/3.1）：渲染 pass 取 RenderPass::GetName()，计算 pass 用 compute[<原始序号>]。
	if (pRender->name != "GPUTimingMainRenderPass")
	{
		fail("E_TIMING_RENDER_NAME", castl::string("渲染 pass 的名称应取 RenderPass::GetName() = "
			"\"GPUTimingMainRenderPass\"，实际 \"" + pRender->name + "\""));
	}
	if (pCompute->name != "compute[0]")
	{
		fail("E_TIMING_COMPUTE_NAME", castl::string("计算 pass 的名称应为 \"compute[0]\"，实际 \"") +
			pCompute->name + "\"");
	}
	if (pRender->endNs < pRender->beginNs || pCompute->endNs < pCompute->beginNs)
	{
		fail("E_TIMING_ORDER", castl::string("endNs 必须不小于 beginNs：渲染 ") + std::to_string(pRender->beginNs) +
			" → " + std::to_string(pRender->endNs) + "，计算 " + std::to_string(pCompute->beginNs) + " → " +
			std::to_string(pCompute->endNs));
	}
	CA_LOG("TestGPUTiming 异步计算图：SubmissionID = {}；渲染 pass（index {}，队列 eDirect）{} → {} ns，名称 \"{}\"；"
	       "计算 pass（index {}，队列 {}）{} → {} ns，名称 \"{}\"",
	       timingAsync->SubmissionID(), pRender->passIndex, pRender->beginNs, pRender->endNs, pRender->name,
	       pCompute->passIndex, computeQueueAvailable ? "eCompute" : "eDirect（后端回退）",
	       pCompute->beginNs, pCompute->endNs, pCompute->name);

	// 双后端一致性（任务 4.1）：把 pass 键规范化成可逐字比较的一行。两个后端跑同一个用例，
	// 这一行必须逐字相同；断言同时把集合钉死为 {(eRender,0),(eCompute,0)}。
	castl::string const asyncGraphKeyLine = castl::string("eRender:") + std::to_string(pRender->passIndex)
		+ ",eCompute:" + std::to_string(pCompute->passIndex);
	if (asyncGraphKeyLine != "eRender:0,eCompute:0")
	{
		fail("E_TIMING_KEYS", castl::string("同一张图的 pass 键集合应为 {(eRender,0),(eCompute,0)}，实际 {") +
			asyncGraphKeyLine + "}");
	}
	CA_LOG("TestGPUTiming pass 键：{}（两个后端同一张图必须逐字相同）", asyncGraphKeyLine);

	// ---- 二、已交付结果在后续执行后必须逐字段不变（任务 2.2 / 3.2）----
	// 做法（design D5）：先保存第一次结果的逐字段副本，再执行一张 pass 结构不同的计时图，
	// 然后**重新读**第一次结果的 Passes() 与副本逐项比较。这样比"第二次之后再读"更能确定性
	// 识别"结果指向了被后续执行覆盖的复用缓冲"。
	struct SavedPassTiming
	{
		EGPUTimedPassType passType = EGPUTimedPassType::eRender;
		uint32_t passIndex = 0;
		EGPUQueueType queue = EGPUQueueType::eNone;
		castl::string name;
		uint64_t beginNs = 0;
		uint64_t endNs = 0;
	};
	uint64_t const savedSubmissionID = timingAsync->SubmissionID();
	castl::vector<SavedPassTiming> savedPasses;
	savedPasses.reserve(passes.size());
	for (auto const& pass : passes)
	{
		SavedPassTiming saved{};
		saved.passType = pass.passType;
		saved.passIndex = pass.passIndex;
		saved.queue = pass.queue;
		saved.name = pass.name;
		saved.beginNs = pass.beginNs;
		saved.endNs = pass.endNs;
		savedPasses.push_back(castl::move(saved));
	}

	// 第二张图：pass 结构不同 —— 两个渲染 pass + 一个不请求异步的计算 pass（共 3 条结果）。
	auto buildDifferentGraph = [&]() -> castl::shared_ptr<GPUGraph> {
		auto graph = castl::make_shared<GPUGraph>();
		graph->Present(windowBackBuffer)
			.AllocBuffer(vbuffer, GPUBufferDescriptor::Create(vertices.size(), sizeof(vertices[0])))
			.ScheduleData(vbuffer, vertices.data(), vertices.size() * sizeof(vertices[0]))
			.AllocBuffer(computeBuffer, GPUBufferDescriptor::Create(4, sizeof(float) * 3))
			.Comp(ComputeBatch::New(false)
				.Dispatch(ComputeDispatch::Create({ CAPATH("Shaders/Test/TestComputeVertexBuffer") }, 1, 1, 1)
					.SetParam(CANAME("computeParams"), computeParams))
			)
			.Rast(renderContent(CANAME("GPUTimingSecondRenderPassA")))
			.Rast(renderContent(CANAME("GPUTimingSecondRenderPassB")));
		return graph;
	};

	std::unique_ptr<IGPUGraphTiming> timingDifferent;
	{
		auto scheduler = ctx.pThreadManager->NewScheduler();
		timingDifferent = ctx.pGPUBackend->ExecuteGraphTimed(scheduler.get(), buildDifferentGraph());
	}
	if (!timingDifferent)
	{
		fail("E_TIMING_NULL", castl::string("ExecuteGraphTimed 返回空结果（结构不同的第二张图）"));
		return;
	}
	timingDifferent->Wait();
	if (timingDifferent->Passes().size() != 3)
	{
		fail("E_TIMING_SECOND_COUNT", castl::string("第二张图应返回三条结果（两个渲染 + 一个计算），实际 ") +
			std::to_string(timingDifferent->Passes().size()));
	}
	if (timingDifferent->SubmissionID() <= savedSubmissionID)
	{
		fail("E_TIMING_SUBMISSION_ID", castl::string("SubmissionID 必须单调递增：第一次 ") +
			std::to_string(savedSubmissionID) + "，第二次 " + std::to_string(timingDifferent->SubmissionID()));
	}

	// 重新读第一次结果（对象还活着），与保存副本逐字段比较。
	std::span<GPUPassTiming const> const firstPassesAgain = timingAsync->Passes();
	if (timingAsync->SubmissionID() != savedSubmissionID)
	{
		fail("E_TIMING_STABLE_ID", castl::string("第二次执行后第一次结果的 SubmissionID 被改写：保存 ")
			+ std::to_string(savedSubmissionID) + "，现在 " + std::to_string(timingAsync->SubmissionID()));
	}
	if (firstPassesAgain.size() != savedPasses.size())
	{
		fail("E_TIMING_STABLE_COUNT", castl::string("第二次执行后第一次结果的条数变了：保存 ") +
			std::to_string(savedPasses.size()) + "，现在 " + std::to_string(firstPassesAgain.size()));
		return;
	}
	for (size_t i = 0; i < savedPasses.size(); ++i)
	{
		GPUPassTiming const& now = firstPassesAgain[i];
		SavedPassTiming const& saved = savedPasses[i];
		if (now.passType != saved.passType || now.passIndex != saved.passIndex
			|| now.queue != saved.queue || now.name != saved.name
			|| now.beginNs != saved.beginNs || now.endNs != saved.endNs)
		{
			fail("E_TIMING_STABLE_FIELD", castl::string("第二次执行后第一次结果的第 ") + std::to_string(i) +
				" 条被改写：保存（type " + std::to_string(static_cast<uint32_t>(saved.passType)) +
				"，index " + std::to_string(saved.passIndex) + "，queue " +
				std::to_string(static_cast<uint32_t>(saved.queue)) + "，name \"" + saved.name + "\"，" +
				std::to_string(saved.beginNs) + " → " + std::to_string(saved.endNs) + "），现在（type " +
				std::to_string(static_cast<uint32_t>(now.passType)) + "，index " + std::to_string(now.passIndex) +
				"，queue " + std::to_string(static_cast<uint32_t>(now.queue)) + "，name \"" + now.name + "\"，" +
				std::to_string(now.beginNs) + " → " + std::to_string(now.endNs) + "）");
		}
	}
	CA_LOG("TestGPUTiming 结果稳定性：第一次 SubmissionID = {}（{} 条），第二次 SubmissionID = {}（{} 条），"
	       "第一次结果逐字段与副本一致",
	       savedSubmissionID, savedPasses.size(), timingDifferent->SubmissionID(),
	       timingDifferent->Passes().size());

	// ---- 三、计算 pass 改为 SetAsync(false)：两边该 pass 的 queue 都必须是 direct（任务 4.1）----
	auto timingSyncCompute = runTimed(false);
	if (!timingSyncCompute)
	{
		fail("E_TIMING_NULL", castl::string("ExecuteGraphTimed 返回空结果（非异步计算图）"));
		return;
	}
	timingSyncCompute->Wait();
	std::span<GPUPassTiming const> const syncPasses = timingSyncCompute->Passes();
	if (syncPasses.size() != 2)
	{
		fail("E_TIMING_SYNC_COUNT", castl::string("非异步计算图应返回两条结果，实际 ") +
			std::to_string(syncPasses.size()));
	}
	GPUPassTiming const* pSyncRender = findPass(syncPasses, EGPUTimedPassType::eRender);
	GPUPassTiming const* pSyncCompute = findPass(syncPasses, EGPUTimedPassType::eCompute);
	if (pSyncRender == nullptr || pSyncCompute == nullptr)
	{
		fail("E_TIMING_SYNC_TYPES", castl::string("非异步计算图的两条结果不是一个 eRender + 一个 eCompute"));
		return;
	}
	if (pSyncCompute->queue != EGPUQueueType::eDirect)
	{
		fail("E_TIMING_SYNC_COMPUTE_QUEUE", castl::string("SetAsync(false) 的计算 pass 实际记录在 direct 队列上，"
			"结果必须如实报 eDirect，不得按 pass 类型伪报 —— 实际 ") +
			std::to_string(static_cast<uint32_t>(pSyncCompute->queue)));
	}
	if (pSyncCompute->name != "compute[0]")
	{
		fail("E_TIMING_SYNC_COMPUTE_NAME", castl::string("非异步计算 pass 的名称应为 \"compute[0]\"，实际 \"") +
			pSyncCompute->name + "\"");
	}
	if (pSyncCompute->endNs < pSyncCompute->beginNs)
	{
		fail("E_TIMING_SYNC_ORDER", castl::string("endNs 必须不小于 beginNs：计算 ") +
			std::to_string(pSyncCompute->beginNs) + " → " + std::to_string(pSyncCompute->endNs));
	}
	castl::string const syncGraphKeyLine = castl::string("eRender:") + std::to_string(pSyncRender->passIndex)
		+ ",eCompute:" + std::to_string(pSyncCompute->passIndex);
	if (syncGraphKeyLine != "eRender:0,eCompute:0")
	{
		fail("E_TIMING_SYNC_KEYS", castl::string("非异步计算图的 pass 键集合应为 {(eRender,0),(eCompute,0)}，实际 {") +
			syncGraphKeyLine + "}");
	}
	CA_LOG("TestGPUTiming 非异步计算图：SubmissionID = {}；pass 键 {}；计算 pass（index {}，队列 eDirect）{} → {} ns，"
	       "名称 \"{}\"",
	       timingSyncCompute->SubmissionID(), syncGraphKeyLine, pSyncCompute->passIndex,
	       pSyncCompute->beginNs, pSyncCompute->endNs, pSyncCompute->name);
}

int main(int argc, char* argv[])
{
	// Task 1.7 + 3.4: Must be first line BEFORE any I/O
	std::ios::sync_with_stdio(false);


	mi_version();

	// Task 1.7: Enable crash handler early
	MiniDump::EnableAutoDump(true);

	// CRT assert/error dialogs: keep them VISIBLE but auto-dismiss. In a Debug
	// build, assert() (incl. VMA_ASSERT) would otherwise pop a modal
	// "Abort/Retry/Ignore" box that blocks forever until a human clicks it.
	// AutoDismissAssertHook shows the message (MessageBoxTimeout, 4s) so a problem
	// is still visible, then closes itself — no click needed. The CRT still aborts
	// afterward and MiniDump::EnableAutoDump above captures the crash. _CRT_WARN
	// is left at its default (no dialog).
	_CrtSetReportHook2(0, AutoDismissAssertHook);

	// ---- CLI Argument Parsing ----
	castl::string backendName = "vulkan";
	castl::string testName;
	bool listOnly = false;
	bool showHelp = false;
	int headlessFrames = 0;
	int headlessTimeout = 60;
	int captureFrame = -1;
	// 任务 3.1：0 = 不进入保持循环（没有默认时长，必须是显式传参）
	int showWindowSeconds = 0;
	castl::string reportPath;

	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "--help") == 0)
		{
			showHelp = true;
		}
		else if (strcmp(argv[i], "--list") == 0)
		{
			listOnly = true;
		}
		else if (strcmp(argv[i], "--backend") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --backend requires a value (vulkan or d3d12)" << std::endl;
				return 1;
			}
			backendName = argv[++i];
		}
		else if (strcmp(argv[i], "--test") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --test requires a test name" << std::endl;
				return 1;
			}
			testName = argv[++i];
		}
		else if (strcmp(argv[i], "--headless") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --headless requires a frame count" << std::endl;
				return 1;
			}
			++i;
			char* end = nullptr;
			long val = strtol(argv[i], &end, 10);
			if (*end != '\0' || val < 0)
			{
				std::cerr << "Error: --headless value must be a non-negative integer, got: " << argv[i] << std::endl;
				return 1;
			}
			headlessFrames = static_cast<int>(val);
		}
		else if (strcmp(argv[i], "--headless-timeout") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --headless-timeout requires a value in seconds" << std::endl;
				return 1;
			}
			++i;
			char* end = nullptr;
			long val = strtol(argv[i], &end, 10);
			if (*end != '\0' || val <= 0)
			{
				std::cerr << "Error: --headless-timeout must be a positive integer, got: " << argv[i] << std::endl;
				return 1;
			}
			headlessTimeout = static_cast<int>(val);
		}
		else if (strcmp(argv[i], "--report") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --report requires a file path" << std::endl;
				return 1;
			}
			reportPath = argv[++i];
		}
		else if (strcmp(argv[i], "--show-window-seconds") == 0)
		{
			// 任务 3.1：保持可见的时长。**不许有默认值** —— 不传就不进入保持循环。
			// 只收正整数：0、负数、非数字、溢出都当非法（`strtol` 对 9999999999 会饱和成
			// 2147483647 并置 errno=ERANGE，只看 `> 0` 会把它当合法值收下）。
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --show-window-seconds requires a positive integer (seconds)" << std::endl;
				return 1;
			}
			const char* text = argv[++i];
			errno = 0;
			char* end = nullptr;
			long val = strtol(text, &end, 10);
			if (end == text || *end != '\0' || errno == ERANGE || val <= 0 || val > INT_MAX)
			{
				std::cerr << "Error: --show-window-seconds value must be a positive integer, got: " << text
				          << std::endl;
				return 1;
			}
			showWindowSeconds = static_cast<int>(val);
		}
		else if (strcmp(argv[i], "--capture") == 0)
		{
			if (i + 1 >= argc)
			{
				std::cerr << "Error: --capture requires a frame index" << std::endl;
				return 1;
			}
			++i;
			char* end = nullptr;
			long val = strtol(argv[i], &end, 10);
			if (*end != '\0' || val < 0)
			{
				std::cerr << "Error: --capture value must be a non-negative integer, got: " << argv[i] << std::endl;
				return 1;
			}
			captureFrame = static_cast<int>(val);
		}
		else
		{
			std::cerr << "Error: Unknown argument: " << argv[i] << std::endl;
			std::cerr << "Use --help to see available options." << std::endl;
			return 1;
		}
	}

	// --help
	if (showHelp)
	{
		std::cout << "GPUBackendTester - GPU Backend Automated Testing Tool\n\n";
		std::cout << "Usage: GPUBackendTester.exe [options]\n\n";
		std::cout << "Options:\n";
		std::cout << "  --backend <vulkan|d3d12>   Select GPU backend (default: vulkan)\n";
		std::cout << "  --test <name>              Run a specific test by name\n";
		std::cout << "  --list                     List all available tests\n";
		std::cout << "  --headless <N>             Run N frames then exit (headless mode)\n";
		std::cout << "  --headless-timeout <N>     Headless timeout in seconds (default: 60)\n";
		std::cout << "  --report <path>            Output test results as JSON (headless default: test_output/result.json)\n";
		std::cout << "  --capture <N>              Read back headless frame N to test_output/<test>.png\n";
		std::cout << "                             (headless only; interactive mode writes a PNG only on TestReadback)\n";
		std::cout << "  --help                     Show this help message\n";
		return 0;
	}

	// --list
	if (listOnly)
	{
		std::cout << "Available tests:\n";
		std::cout << "  TestSimpleTriangle\n";
		std::cout << "  TestTriangleWithConstantColor\n";
		std::cout << "  TestTriangleWithStructuredBufferColor\n";
		std::cout << "  TestTriangleWithImageBuffer\n";
		std::cout << "  TestDoublePass\n";
		std::cout << "  TestComputeBuffer\n";
		std::cout << "  TestReadback\n";
		std::cout << "  TestIMGUI\n";
		std::cout << "  TestScriptHost\n";
		std::cout << "  TestCSharpWindow\n";
		std::cout << "  TestCSharpTriangle\n";
		// csharp-scene-foundation 3.2：第 19 项（场景模型 / 序列化 / 生命周期 / 场景→图）。
		// 也要用 C#，所以全量执行位置必须排在 `TestCSharpWindowCallbacks` 之前（见 run-all 那段）。
		std::cout << "  TestCSharpScene\n";
		std::cout << "  TestCSharpWindowCallbacks\n";
		std::cout << "  TestGPUTiming\n";
		// csharp-render-shader-constants 任务 1.2：第 14 项
		std::cout << "  TestCSharpConstantColor\n";
		// csharp-render-structured-buffer 任务 1.2：第 15 项（`--list` 顺序 ≠ 全量执行顺序：
		// 全量里它必须排在 `TestCSharpWindowCallbacks` 之前，见 run-all 那段的注释）
		std::cout << "  TestCSharpStructuredBuffer\n";
		// csharp-render-texture-sampling 任务 1.2：第 16 项（同样：`--list` 里排最后，
		// 全量执行位置必须排在 `TestCSharpWindowCallbacks` 之前）
		std::cout << "  TestCSharpTextureSampling\n";
		// editor-window-panel 3.1：第 18 项（单窗口分栏面板 + scene view）。0.2 阶段先由它承载 WebView2
		// 贴附探针；全量执行位置见 run-all 那段（要用 C#，必须排在 `TestCSharpWindowCallbacks` 之前）。
		std::cout << "  TestEditorWindowPanel\n";
		// io-error-channel 任务 4.2 / 5.1：第 17 项（批式读写的失败通道与资源加载失败透出；
		// 不开窗口、不用 C#，所以全量里放哪都不影响别的用例，见 run-all 那段）
		std::cout << "  TestIOErrorChannel\n";
		// csharp-reverse-interface-binding 8.1：第 20 项（C# 工厂/资源经反向代理被 C++ 调到；
		// 要用 C#，全量执行位置必须排在 `TestCSharpWindowCallbacks` 之前）。
		std::cout << "  TestCSharpResourceFactory\n";
		return 0;
	}

	// 任务 2.2（design D4）：非 headless 带 --capture —— 写图仅在 headless 生效，但交互模式
	// TestReadback 的单帧路径确实会写 PNG（依赖 test_output 目录已存在），所以只警告、不拒绝。
	if (headlessFrames <= 0 && captureFrame >= 0)
	{
		std::cerr << "[capture] 警告：--capture 的写图仅在 headless 生效；交互模式只有 TestReadback 的单帧路径会写 PNG"
		             "（依赖 test_output 目录已存在），其余测试只建回读 RT 不写图。" << std::endl;
	}

	// Task 2.4: Headless default JSON path
	if (headlessFrames > 0 && reportPath.empty())
	{
		reportPath = "test_output/result.json";
	}

	// Task 3.1: Ensure test_output directory exists in headless mode
	if (headlessFrames > 0)
	{
		createDirectoryIfNeeded("test_output");
		// 任务 2.1（D3）：跑前清既有 `*.log` 并打印计数；交互运行不进这个分支，故不清理。
		const int cleanedLogs = CleanupStaleLogs("test_output");
		std::cout << "test_output 跑前清理：" << cleanedLogs << " 个日志" << std::endl;
	}

	// ---- Initialize TestContext ----
	TestContext ctx;
	ctx.headlessFrames = headlessFrames;
	ctx.captureFrame = captureFrame;
	ctx.backendName = backendName;

	// Decision 4: Derive project root from exe path via sentinel file traversal
	// This replaces the fragile CWD-dependent "../../../../" which breaks at wrong CWD depths.
	std::filesystem::path rootPath;
	{
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(NULL, exePath, MAX_PATH);
		std::filesystem::path p(exePath);
		p = p.parent_path();
		bool found = false;
		while (!p.empty() && p != p.root_path())
		{
			if (std::filesystem::exists(p / "CAResources") && std::filesystem::exists(p / "CLAUDE.md"))
			{
				rootPath = p;
				found = true;
				break;
			}
			p = p.parent_path();
		}
		if (!found)
		{
			std::cerr << "WARNING: Project root not found via exe path traversal, falling back to CWD-relative ../../../../" << std::endl;
			std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format };
			rootPath = std::filesystem::absolute(rootPathFS);
		}
	}

	ctx.resourcePath = (rootPath / "CAResources").string();
	ctx.assetPath = (rootPath / "CAAssets").string();
	ctx.editorConfigPath = (rootPath / "EditorConfigs").string();

	// ---- Headless: Enable Vulkan Validation Layer ----
	if (headlessFrames > 0)
	{
		// _putenv
	}

	// ---- Load Modules ----
	ctx.pModuleManager = &ctx.moduleManager;

	CA_ADD_MODULE(ctx.pModuleManager, TimerSystem_Impl);
	CA_ADD_MODULE(ctx.pModuleManager, ThreadManager);

	if (strcmp(backendName.c_str(), "d3d12") == 0)
	{
		CA_ADD_MODULE(ctx.pModuleManager, D3D12RenderBackend);
	}
	else if (strcmp(backendName.c_str(), "vulkan") == 0)
	{
		CA_ADD_MODULE(ctx.pModuleManager, VulkanRenderBackend);
	}
	else
	{
		std::cerr << "Error: Unknown backend: " << backendName.c_str() << ". Use 'vulkan' or 'd3d12'." << std::endl;
		return 1;
	}

	CA_ADD_MODULE(ctx.pModuleManager, ShaderCompilerSlang);
	CA_ADD_MODULE(ctx.pModuleManager, WindowSystem);
	CA_ADD_MODULE(ctx.pModuleManager, IOManager_FS);
	CA_ADD_MODULE(ctx.pModuleManager, CAGeneralReourceSystem);
	CA_ADD_MODULE(ctx.pModuleManager, IMGUIContext);
	CA_ADD_MODULE(ctx.pModuleManager, CSharpHost);

	ctx.pModuleManager->LinkModules();

	// C# 宿主模块：先打一行"能不能取到 IScriptHost"（csharp-hostfxr-bootstrap 任务 1.2 的验收读数）。
	// 跨边界调用的真正断言在 TestScriptHost 里（docs/testing.md 的第 ② 层），这里不再自己调 Tick ——
	// 那会让托管侧的 Tick 计数提前走一格，测试里的"第一次 Tick 应为 1"就对不上了。
	if (auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>())
	{
		CA_LOG("IScriptHost 已取到：IsAvailable = {}", pScriptHost->IsAvailable() ? "true" : "false");
		if (!pScriptHost->IsAvailable())
		{
			CA_LOG_ERR("托管侧不可用：{}", pScriptHost->LastError());
		}
	}
	else
	{
		CA_LOG("IScriptHost 取不到（CSharpHost 模块没加载成功）");
	}

	// Task 4.4: Get SetValidationLogFile function pointer from Vulkan DLL
	using SetValidationLogFileFn = void(*)(FILE*);
	SetValidationLogFileFn SetValidationLogFilePtr = nullptr;
	if (strcmp(backendName.c_str(), "vulkan") == 0)
	{
		HMODULE hVulkanDll = GetModuleHandleA("VulkanRenderBackend.dll");
		if (hVulkanDll)
		{
			SetValidationLogFilePtr = (SetValidationLogFileFn)GetProcAddress(hVulkanDll, "SetValidationLogFile");
		}
	}

	// ---- Initialize Systems ----
	SetGlobalTimerSystem(ctx.pModuleManager->GetInstance<catimer::TimerSystem>());

	ctx.pWindowSystem = ctx.pModuleManager->GetInstance<IWindowSystem>();
	ctx.pThreadManager = ctx.pModuleManager->GetInstance<CThreadManager>();
	ctx.pIMGUIContext = ctx.pModuleManager->GetInstance<imgui_display::IMGUIContext>();

	ctx.pResourceManagingSystem = ctx.pModuleManager->GetInstance<ResourceManagingSystem>();
	ctx.pResourceManagingSystem->SetResourceRootPath(ctx.assetPath);

	ctx.pImportingSystem = ctx.pModuleManager->GetInstance<ResourceImportingSystem>();

	ctx.pGPUBackend = ctx.pModuleManager->GetInstance<CRenderBackend>();

	// Null checks
	if (!ctx.pGPUBackend)
	{
		std::cerr << "Error: Failed to initialize GPU backend (CRenderBackend is null)" << std::endl;
		return 1;
	}
	if (!ctx.pWindowSystem)
	{
		std::cerr << "Error: Failed to initialize Window System (IWindowSystem is null)" << std::endl;
		return 1;
	}

	ctx.pImportingSystem->ScanSourceDirectory(ctx.resourcePath);

	// ---- JSON report setup ----
	std::ofstream jsonFile;
	bool jsonFirst = true;
	if (!reportPath.empty())
	{
		jsonFile.open(reportPath.c_str(), std::ios::out | std::ios::trunc);
	}

	// ---- Test runner with diagnostics wrapper ----
	// vulkan-gpu-queries 任务 1.2：进程退出码必须反映测试结果（此前 `main` 无条件返回 EXIT_SUCCESS，
	// 而任务 1.2/2.1/3.1/4.1/5.1 都以 tester 的退出码 0/1 作为验收读数）。判定仍以 `result.json`
	// 的 `status` 为准；退出码只是让"脚本跑一条命令"能直接看到红绿。
	bool anyTestFailed = false;
	auto runTestWithDiagnostics = [&](const char* name, auto testFunc)
	{
		FILE* validationLogFile = nullptr;
		ctx.captureWrites = 0; // 任务 2.3：本测试写出的 capture PNG 数，按测试重置

		if (headlessFrames > 0)
		{
			// Task 3.2: Redirect stdout to log file (headless only, stderr preserved)
			castl::string logPath = castl::string("test_output/") + name + ".log";
			freopen(logPath.c_str(), "w", stdout);

			// Task 4.4: Set validation log file for Vulkan backend
			if (SetValidationLogFilePtr)
			{
				castl::string validationPath = castl::string("test_output/") + name + "_validation.log";
				validationLogFile = fopen(validationPath.c_str(), "w");
				SetValidationLogFilePtr(validationLogFile);
			}
		}

		// Task 2.2: Time the test
		auto startTime = std::chrono::steady_clock::now();
		int exitCode = 0;
		const char* status = "pass";
		castl::string errorMsg;

		try
		{
			std::cout << "Running: " << name << std::endl;
			testFunc(ctx);
		}
		catch (const std::exception& e)
		{
			status = "fail";
			exitCode = 1;
			errorMsg = e.what();
			std::cerr << "Test " << name << " threw exception: " << e.what() << std::endl;
		}
		catch (...)
		{
			status = "fail";
			exitCode = 1;
			errorMsg = "unknown exception";
			std::cerr << "Test " << name << " threw unknown exception" << std::endl;
		}
		auto endTime = std::chrono::steady_clock::now();
		auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

		// Task 3.3: Restore stdout
		if (headlessFrames > 0)
		{
			fflush(stdout);
			freopen("CON", "w", stdout);
		}

		// 任务 2.3（design D4）：headless 运行里请求了 capture 却没写出 PNG（帧号越界或回读无效）——
		// 在测试的帧循环结束之后点名测试与帧号；交互运行不发这条（误用由参数校验那一条覆盖）。
		// 测试本身已失败时不再补警告：失败原因已经由它自己的异常 / 错误码报出来了。
		if (headlessFrames > 0 && captureFrame >= 0 && exitCode == 0 && ctx.captureWrites == 0)
		{
			std::cerr << "[capture] 警告：测试 " << name << " 请求 capture 帧 " << captureFrame
			          << "，但本次运行未写出 PNG（帧号越界或回读无效）" << std::endl;
		}

		// Task 4.4: Clear and close validation log
		if (validationLogFile)
		{
			fflush(validationLogFile);
			SetValidationLogFilePtr(nullptr);
			fclose(validationLogFile);
		}

		// Task 2.3: Write JSON result
		// 失败详情优先取静态缓冲（见 `g_errorDetail` 那段注释：长字符串走堆会在 catch 前被释放）
		appendJsonResult(jsonFile, jsonFirst, name, status, durationMs, exitCode,
			g_errorDetail[0] != '\0' ? g_errorDetail : (errorMsg.empty() ? nullptr : errorMsg.c_str()));
		g_errorDetail[0] = '\0';

		if (exitCode != 0)
		{
			anyTestFailed = true;
		}

		return (exitCode == 0);
	};

	// ---- Run Tests ----
	if (!testName.empty())
	{
		// Single test dispatch
		if (strcmp(testName.c_str(), "TestSimpleTriangle") == 0)
			runTestWithDiagnostics("TestSimpleTriangle", TestSimpleTriangle);
		else if (strcmp(testName.c_str(), "TestTriangleWithConstantColor") == 0)
			runTestWithDiagnostics("TestTriangleWithConstantColor", TestTriangleWithConstantColor);
		else if (strcmp(testName.c_str(), "TestTriangleWithStructuredBufferColor") == 0)
			runTestWithDiagnostics("TestTriangleWithStructuredBufferColor", TestTriangleWithStructuredBufferColor);
		else if (strcmp(testName.c_str(), "TestTriangleWithImageBuffer") == 0)
			runTestWithDiagnostics("TestTriangleWithImageBuffer", TestTriangleWithImageBuffer);
		else if (strcmp(testName.c_str(), "TestDoublePass") == 0)
			runTestWithDiagnostics("TestDoublePass", TestDoublePass);
		else if (strcmp(testName.c_str(), "TestComputeBuffer") == 0)
			runTestWithDiagnostics("TestComputeBuffer", TestComputeBuffer);
		else if (strcmp(testName.c_str(), "TestReadback") == 0)
			runTestWithDiagnostics("TestReadback", TestReadback);
		else if (strcmp(testName.c_str(), "TestIMGUI") == 0)
			runTestWithDiagnostics("TestIMGUI", TestIMGUI);
		else if (strcmp(testName.c_str(), "TestScriptHost") == 0)
	{
		runTestWithDiagnostics("TestScriptHost", TestScriptHost);
		runTestWithDiagnostics("TestCSharpWindow", TestCSharpWindow);
	}
	else if (strcmp(testName.c_str(), "TestCSharpWindow") == 0)
	{
		runTestWithDiagnostics("TestCSharpWindow", TestCSharpWindow);
	}
		else if (strcmp(testName.c_str(), "TestGPUTiming") == 0)
	{
		runTestWithDiagnostics("TestGPUTiming", TestGPUTiming);
	}
		else if (strcmp(testName.c_str(), "TestCSharpWindowCallbacks") == 0)
	{
		runTestWithDiagnostics("TestCSharpWindowCallbacks", TestCSharpWindowCallbacks);
	}
		else if (strcmp(testName.c_str(), "TestCSharpTriangle") == 0)
	{
		runTestWithDiagnostics("TestCSharpTriangle", TestCSharpTriangle);
	}
		else if (strcmp(testName.c_str(), "TestCSharpConstantColor") == 0)
	{
		runTestWithDiagnostics("TestCSharpConstantColor", TestCSharpConstantColor);
	}
		else if (strcmp(testName.c_str(), "TestCSharpStructuredBuffer") == 0)
	{
		runTestWithDiagnostics("TestCSharpStructuredBuffer", TestCSharpStructuredBuffer);
	}
		else if (strcmp(testName.c_str(), "TestCSharpTextureSampling") == 0)
	{
		runTestWithDiagnostics("TestCSharpTextureSampling", TestCSharpTextureSampling);
	}
		else if (strcmp(testName.c_str(), "TestEditorWindowPanel") == 0)
	{
		runTestWithDiagnostics("TestEditorWindowPanel", TestEditorWindowPanel);
	}
		else if (strcmp(testName.c_str(), "TestCSharpScene") == 0)
	{
		runTestWithDiagnostics("TestCSharpScene", TestCSharpScene);
	}
		else if (strcmp(testName.c_str(), "TestCSharpResourceFactory") == 0)
	{
		runTestWithDiagnostics("TestCSharpResourceFactory", TestCSharpResourceFactory);
	}
		else if (strcmp(testName.c_str(), "TestIOErrorChannel") == 0)
	{
		runTestWithDiagnostics("TestIOErrorChannel", TestIOErrorChannel);
	}
		else
		{
			std::cerr << "Error: Unknown test: " << testName.c_str() << std::endl;
			std::cerr << "Use --list to see available tests." << std::endl;
			return 1;
		}
	}
	else
	{
		// Run all tests in order
		runTestWithDiagnostics("TestSimpleTriangle", TestSimpleTriangle);
		runTestWithDiagnostics("TestTriangleWithConstantColor", TestTriangleWithConstantColor);
		runTestWithDiagnostics("TestTriangleWithStructuredBufferColor", TestTriangleWithStructuredBufferColor);
		runTestWithDiagnostics("TestTriangleWithImageBuffer", TestTriangleWithImageBuffer);
		runTestWithDiagnostics("TestDoublePass", TestDoublePass);
		runTestWithDiagnostics("TestComputeBuffer", TestComputeBuffer);
		runTestWithDiagnostics("TestReadback", TestReadback);
		runTestWithDiagnostics("TestIMGUI", TestIMGUI);
		runTestWithDiagnostics("TestScriptHost", TestScriptHost);
		runTestWithDiagnostics("TestCSharpWindow", TestCSharpWindow);
		runTestWithDiagnostics("TestCSharpTriangle", TestCSharpTriangle);
		runTestWithDiagnostics("TestGPUTiming", TestGPUTiming);
		// 常量色参数测试也要用 C#：必须排在 `TestCSharpWindowCallbacks`（它会显式关掉托管运行时）之前。
		runTestWithDiagnostics("TestCSharpConstantColor", TestCSharpConstantColor);
		// 结构化缓冲探针同样要用 C#：`--list` 里它紧跟常量色，但**全量执行位置必须在这里** ——
		// `TestCSharpWindowCallbacks` 会显式关闭托管运行时，把它排在那个用例之后就再也拿不到 C#。
		runTestWithDiagnostics("TestCSharpStructuredBuffer", TestCSharpStructuredBuffer);
		// 纹理采样探针同样要用 C#：**全量执行位置必须在这里**、排在 `TestCSharpWindowCallbacks` 之前
		//（那个用例会显式关闭托管运行时，排在它之后就再也拿不到 C#）。
		runTestWithDiagnostics("TestCSharpTextureSampling", TestCSharpTextureSampling);
		// editor-window-panel 3.1：第 18 用例（WebView2 面板 + scene view）。同样要用 C#，必须排在
		// `TestCSharpWindowCallbacks`（它会显式关掉托管运行时）之前。
		runTestWithDiagnostics("TestEditorWindowPanel", TestEditorWindowPanel);
		// io-error-channel 任务 4.2：批式读写的失败通道与资源加载失败透出。它不开窗口、不用 C#，
		// 放在 `TestCSharpWindowCallbacks` 之前（那条会关掉托管运行时；这条与 C# 无关，位置只为好读）。
		runTestWithDiagnostics("TestIOErrorChannel", TestIOErrorChannel);
		// csharp-scene-foundation 3.2：第 19 用例。同样要用 C#，必须排在
		// `TestCSharpWindowCallbacks`（它会显式关掉托管运行时）之前。
		runTestWithDiagnostics("TestCSharpScene", TestCSharpScene);
		// csharp-reverse-interface-binding 8.1：第 20 用例。同样要用 C#，必须排在
		// `TestCSharpWindowCallbacks`（它会显式关掉托管运行时）之前。
		runTestWithDiagnostics("TestCSharpResourceFactory", TestCSharpResourceFactory);
		// 关闭运行时的用例放在最后：它会显式调用 `IScriptHost::Shutdown`，之后的用例不可能再用 C#。
		runTestWithDiagnostics("TestCSharpWindowCallbacks", TestCSharpWindowCallbacks);
	}

	// 任务 3.2：`--show-window-seconds N` —— 所有选中的测试跑完之后，再保持 N 秒：
	// 每轮 `Tick()` + `UpdateSystem()`，期间不关脚本建的窗口（脚本本来就没关，这里也不去动它）。
	// 判据落在日志里，由三条计数决定，不看墙钟自报：
	//   ① `Tick` 调用次数 > 0；② 它等于循环次数（循环真的每轮都泵帧）；③ 保持结束时计数不回落。
	if (showWindowSeconds > 0)
	{
		auto* pKeepScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>();
		int32_t const countBeforeKeep = ctx.pWindowSystem ? ctx.pWindowSystem->GetWindowCount() : -1;
		int32_t loopCount = 0;
		int32_t tickCalls = 0;
		auto const startedAt = std::chrono::steady_clock::now();
		double elapsedSeconds = 0.0;
		while (true)
		{
			elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
			if (elapsedSeconds >= static_cast<double>(showWindowSeconds))
			{
				break;
			}
			++loopCount;
			if (pKeepScriptHost != nullptr)
			{
				pKeepScriptHost->Tick(0.016);
				++tickCalls;
			}
			if (ctx.pWindowSystem != nullptr)
			{
				ctx.pWindowSystem->UpdateSystem();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		int32_t const countAfterKeep = ctx.pWindowSystem ? ctx.pWindowSystem->GetWindowCount() : -1;
		bool const keepOk = tickCalls > 0 && tickCalls == loopCount && countAfterKeep == countBeforeKeep;
		// 这一行必须在**测试自己的日志文件已关闭、stdout/stderr 都被改道之后**还能读到，所以单独落一个文件。
		std::string const keepLogPath = "test_output/keep_window.log";
		if (FILE* keepLog = std::fopen(keepLogPath.c_str(), "w"))
		{
			std::fprintf(keepLog,
			             "保持窗口判据：%s（循环 %d 次 / Tick 调用 %d 次 / 实际 %.2f 秒、要求 %d 秒 / 计数 %d → %d）\n",
			             keepOk ? "通过" : "失败", loopCount, tickCalls, elapsedSeconds, showWindowSeconds,
			             countBeforeKeep, countAfterKeep);
			std::fclose(keepLog);
		}
	}

	// Task 2.5: Close JSON array
	closeJsonArray(jsonFile, jsonFirst);

	// 托管运行时的关闭由 CSharpHost 实例的析构负责（在模块被卸载之前），这里只打一行读数
	if (auto* pScriptHost = ctx.pModuleManager->GetInstance<cs_script::IScriptHost>())
	{
		CA_LOG("引擎：托管侧在退出前的状态 IsAvailable = {}（关闭由模块析构负责）",
			pScriptHost->IsAvailable() ? "true" : "false");
	}

	// vulkan-gpu-queries 任务 1.2：有测试失败时进程退出码为 1（见 `anyTestFailed` 那段注释）。
	return anyTestFailed ? EXIT_FAILURE : EXIT_SUCCESS;
}

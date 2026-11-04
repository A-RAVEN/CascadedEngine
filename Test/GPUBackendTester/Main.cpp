#include<mimalloc.h>
#include <ScriptHost/IScriptHost.h>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstring>
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
// 脚本渲染适配面的原生测试钩子（csharp-render-backend-triangle 任务 4.1）：本文件要读脚本创建的
// 离屏 `ImageHandle`、适配器的等待次数与真实呈现清单。托管侧只能看到公开接口。
#include "../../Interface/RenderInterface/private/ScriptRenderBridge.h"

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


// ---- Helpers (added for diagnostics) ----

static void createDirectoryIfNeeded(const char* path)
{
	CreateDirectoryA(path, NULL);
}

// 失败详情放在**静态缓冲**里（任务 4.4）：这个可执行文件里长 std::string 的堆缓冲在
// 异常抛出与捕获之间会被释放掉（实测 e.what() 拿到 32 个 0xDD = MSVC debug 堆的"已释放"填充，
// 而短于 SSO 阈值的消息完全正常），根因是 mimalloc 与 ucrtbase 两个堆同时存在：
//   mimalloc-redirect: error: mimalloc-redirect.dll seems to be initialized after ucrtbase.dll
// 所以跨 catch 边界只传一句短码，详细文字走这块静态内存，appendJsonResult 直接按 char* 写出。
static char g_errorDetail[512] = {};
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
	// 造一次 `interfaceId = 9` 的取表调用：返回值必须是 0，且 `LastError()` 里必须说明原因。
	// 9 用的是"当前没有接线的编号"，不是某个将来要用的具体接口。
	{
		uint32_t const rejected = pScriptHost->GetInterfaceApiForTest(9u, nullptr, 1u, 0u, nullptr, nullptr);
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

// ---- TestCSharpTriangle（csharp-render-backend-triangle 任务 1.2 / 3.2 / 4.1）----
// C# 用自己的顶点字节、着色器路径、窗口与离屏目标建图并提交；原生侧只读那份离屏目标做真实像素
// 判据，同时核对窗口描述快照、真实呈现清单、适配器 WaitIdle 次数与待完成集合。
//
// Debug 自检模式（进程环境变量 `CASCADED_CSHARP_RENDER_SELFTEST`，由外层命令设置、C# 侧读同一份值）：
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

	// 编号 3 = 脚本渲染后端：宿主在模块 link 阶段必须已经登记真实适配器。
	void* renderInstance = pScriptHost->GetInterfaceInstance(3, nullptr);
	auto* renderBackend = dynamic_cast<graphics_backend::CScriptRenderBackend*>(
		static_cast<graphics_backend::IScriptRenderBackend*>(renderInstance));
	if (renderBackend == nullptr)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "编号 3 没有登记脚本渲染适配器（实例 = %p）", renderInstance);
		fail("E_RENDER_BACKEND_MISSING", detail);
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

	// 任务 3.2（csharp-render-shader-constants）：本用例是**第一个**渲染探针用例，之前跑的
	// `TestScriptHost` / `TestCSharpWindow` 都没有设置任何渲染开关 —— 适配器的离屏目标、参数结构
	// 与后端执行计数必须全为 0，否则说明未启用探针的测试也创建了 GPU 工作。
	{
		const uint32_t captureCount = renderBackend->GetCreateCaptureTargetCountForTest();
		const uint32_t structCount = renderBackend->GetCreateShaderStructCountForTest();
		const uint32_t executeCount = renderBackend->GetExecuteGraphCountForTest();
		// csharp-render-structured-buffer 任务 3.1：非探针阶段建缓冲也必须为 0（新增的计数槽）。
		const uint32_t addBufferCount = renderBackend->GetAddBufferCountForTest();
		CA_LOG("非探针阶段读数（TestScriptHost / TestCSharpWindow 之后）：captureTarget={} "
		       "createShaderStruct={} addBuffer={} executeGraph={}",
		       captureCount, structCount, addBufferCount, executeCount);
		if (captureCount != 0u || structCount != 0u || executeCount != 0u || addBufferCount != 0u)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail),
			              "非探针测试创建了 GPU 工作：captureTarget=%u createShaderStruct=%u addBuffer=%u "
			              "executeGraph=%u",
			              captureCount, structCount, addBufferCount, executeCount);
			fail("E_RENDER_NONPROBE_WORK", detail);
		}
	}

	// 窗口接口实例：托管探针用 `IWindowSystem.NewWindow` 建自己的窗口并从它取窗口目标。
	auto newWindow = ctx.pWindowSystem->NewWindow(320, 240, "C# Triangle");
	auto windowKeepAlive = newWindow.lock();
	if (windowKeepAlive == nullptr)
	{
		fail("E_TRIANGLE_HOST", "IWindowSystem::NewWindow 返回了空弱引用");
	}
	pScriptHost->SetInterfaceInstance(1, kWindowInstanceName, windowKeepAlive.get());
	pScriptHost->SetInterfaceInstance(2, kRealWindowSystemInstanceName, ctx.pWindowSystem);

	// 基线只看本次 Tick 产生的错误：全量跑时前面的用例可能留下一条（例如故意触发的未知编号）。
	const std::string baselineError = pScriptHost->LastError();
	const uint32_t waitBefore = renderBackend->GetWaitIdleCountForTest();
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

	auto captureTarget = renderBackend->GetLastCaptureTargetForTest();
	auto windowTarget = renderBackend->GetLastWindowTargetForTest();
	if (captureTarget == nullptr || windowTarget == nullptr)
	{
		fail("E_RENDER_PROBE_MISSING",
		     "渲染探针没有创建离屏 / 窗口目标（探针开关或三个函数表绑定失败）");
	}
	const uint32_t waitAfter = renderBackend->GetWaitIdleCountForTest();
	const uint32_t pendingAfter = renderBackend->GetPendingGraphCountForTest();

	// 窗口描述快照：离屏目标必须与窗口同尺寸、RGBA8；窗口目标是后台缓冲、离屏不是。
	const auto& windowDesc = windowTarget->GetDescriptorForTest();
	const auto& captureDesc = captureTarget->GetDescriptorForTest();
	if (windowDesc.width != captureDesc.width || windowDesc.height != captureDesc.height ||
		captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM || !windowTarget->IsBackbuffer() ||
		captureTarget->IsBackbuffer())
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_TRIANGLE_TARGET", detail);
	}

	// 图结构（任务 3.2 / spec 负向场景）：`skip-draw` 在**调用层**省略窗口与离屏目标的两次
	// `AddTrianglePass`，图 pass 数必须从正常模式的 2 变成 0；原生测试以 `E_TRIANGLE_DRAW` 判红。
	// 不得用零面积三角形代替省略调用（design 六）。
	auto lastGraph = renderBackend->GetLastGraphForTest().lock();
	if (lastGraph == nullptr)
	{
		fail("E_TRIANGLE_GRAPH", "最近一次提交的图对象已经不存在（脚本没有保留图表面对象）");
	}
	constexpr uint32_t kExpectedPassCount = 2u;
	if (skipDraw)
	{
		ctx.pGPUBackend->WaitIdle(); // 空图不会提交，仍清一次后端，避免残留
		if (lastGraph->GetPassCount() != 0u)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail),
			              "skip-draw 没有省略 AddTrianglePass：图 pass 数 %u（期望 0）",
			              lastGraph->GetPassCount());
			fail("E_TRIANGLE_DRAW", detail);
		}
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-draw：图 pass 数 0（正常模式 %u），按预期失败", kExpectedPassCount);
		fail("E_TRIANGLE_DRAW", detail);
	}
	if (lastGraph->GetPassCount() != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              lastGraph->GetPassCount(), kExpectedPassCount);
		fail("E_TRIANGLE_DRAW", detail);
	}

	// skip-wait：负向模式必须让适配器计数不涨；待完成集合非空也一并记下。
	if (skipWait)
	{
		ctx.pGPUBackend->WaitIdle(); // 清理仍在执行的 GPU 工作，再报负向失败
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-wait：适配器 WaitIdle 计数 %u → %u、待完成 %u（期望 0 次、非空待完成）", waitBefore,
		              waitAfter, pendingAfter);
		fail("E_TRIANGLE_WAIT", detail);
	}
	if (waitAfter != waitBefore + 1 || pendingAfter != 0)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：适配器 WaitIdle 计数 %u → %u、待完成 %u",
		              waitBefore, waitAfter, pendingAfter);
		fail("E_TRIANGLE_WAIT", detail);
	}

	const auto& presents = lastGraph->GetGraphForTest()->GetFinalizePass().m_PresentBackBuffers;
	const bool presentMatches = presents.size() == 1 && presents[0] == windowTarget->GetImageForTest();
	if (skipPresent)
	{
		ctx.pGPUBackend->WaitIdle();
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-present：真实呈现清单 %zu 项（负向模式期望 0；%s）", presents.size(),
		              presents.size() == 0 ? "呈现确实被省略" : "呈现没有被省略");
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
	auto token = ctx.pGPUBackend->Readback(captureTarget->GetImageForTest(), pixels);
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
	CA_LOG("TestCSharpTriangle：backend={} mode={} size={}x{} wait={}→{} pending={} presents={} "
	       "bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height, waitBefore, waitAfter,
	       pendingAfter, presents.size(), background[0], background[1], background[2], background[3],
	       center[0], center[1], center[2], center[3], pngPath.c_str());

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

// ---- TestCSharpConstantColor（csharp-render-shader-constants 任务 1.2 / 4.1）----
// C# 用单 `POSITION` 顶点流 + `ConstantColor` 参数结构（3 个 `ColorSubStruct`）建图，把参数结构绑定到
// `constantColorBlock`，窗口与离屏各一个同逻辑 pass，提交并等待；原生侧读那份离屏目标做精确像素判据
// （背景 (0,0,255,255)、中心 (255,0,0,255)），同时核对 pass 数与每个 pass 的真实参数表条目。
//
// Debug 负向（进程环境变量 `CASCADED_CSHARP_CONSTCOLOR_SELFTEST`，C# 侧读同一份值）：
//   `bad-struct-colors` 三个数组槽改成红/绿/蓝 → 中心不再是纯红，以 `E_CONSTCOLOR_PIXEL` 判红；
//   `skip-param` 完全不调 `SetPassParam` → `SubmitGraph` 在进入后端执行前拒绝并点名 `constantColorBlock`，
//                原生以 `E_CONSTCOLOR_PARAM` 判红，**不读像素**（不对未定义内容编造期望）。
// `Tools/check_csharp_constant_color.py` 逐后端对账正常（pass）与两个负向（fail + 对应错误码）。
void TestCSharpConstantColor(TestContext& ctx)
{
	auto fail = [](char const* code, castl::string const& detail) {
		std::snprintf(g_errorDetail, sizeof(g_errorDetail), "%s: %s", code, detail.c_str());
		throw std::runtime_error(code);
	};

	// C++ 侧的独立夹具值（design 第五节）：尺寸、pass 数、两个像素与块名都另写一份，
	// 不与 C# 的 `CSharpConstantColorProbe` 共享常量表 —— 一边改错不会两边一起绿。
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

	// 编号 3 = 脚本渲染后端：宿主在模块 link 阶段登记真实适配器。
	void* renderInstance = pScriptHost->GetInterfaceInstance(3, nullptr);
	auto* renderBackend = dynamic_cast<graphics_backend::CScriptRenderBackend*>(
		static_cast<graphics_backend::IScriptRenderBackend*>(renderInstance));
	if (renderBackend == nullptr)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "编号 3 没有登记脚本渲染适配器（实例 = %p）", renderInstance);
		fail("E_CONSTCOLOR_BACKEND_MISSING", detail);
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
	const uint32_t waitBefore = renderBackend->GetWaitIdleCountForTest();
	// 任务 3.2 的读数基线：三个参数结构计数、`SetPassParam` 计数、离屏目标计数、后端执行次数。
	const uint32_t createStructBefore = renderBackend->GetCreateShaderStructCountForTest();
	const uint32_t setFloat3Before = renderBackend->GetSetFloat3CountForTest();
	const uint32_t setStructBefore = renderBackend->GetSetStructCountForTest();
	const uint32_t setPassParamBefore = renderBackend->GetSetPassParamCountForTest();
	const uint32_t captureBefore = renderBackend->GetCreateCaptureTargetCountForTest();
	const uint32_t executeBefore = renderBackend->GetExecuteGraphCountForTest();
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

	auto captureTarget = renderBackend->GetLastCaptureTargetForTest();
	auto windowTarget = renderBackend->GetLastWindowTargetForTest();
	if (captureTarget == nullptr || windowTarget == nullptr)
	{
		fail("E_CONSTCOLOR_PROBE_MISSING",
		     "渲染探针没有创建离屏 / 窗口目标（探针开关或四个函数表绑定失败）");
	}
	const uint32_t waitAfter = renderBackend->GetWaitIdleCountForTest();
	const uint32_t pendingAfter = renderBackend->GetPendingGraphCountForTest();

	// 窗口描述快照：离屏目标与窗口同尺寸、RGBA8；窗口目标是后台缓冲、离屏不是。
	const auto& windowDesc = windowTarget->GetDescriptorForTest();
	const auto& captureDesc = captureTarget->GetDescriptorForTest();
	if (windowDesc.width != kWindowWidth || windowDesc.height != kWindowHeight ||
		captureDesc.width != kWindowWidth || captureDesc.height != kWindowHeight ||
		captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM || !windowTarget->IsBackbuffer() ||
		captureTarget->IsBackbuffer())
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_CONSTCOLOR_TARGET", detail);
	}

	// 图结构：两个 pass（窗口 + 离屏），每个 pass 的参数表都必须正好 1 条 `constantColorBlock`
	// 且条目非空；`skip-param` 形态两张表都必须为空。
	auto lastGraph = renderBackend->GetLastGraphForTest().lock();
	if (lastGraph == nullptr)
	{
		fail("E_CONSTCOLOR_GRAPH", "最近一次提交的图对象已经不存在（脚本没有保留图表面对象）");
	}
	if (lastGraph->GetPassCount() != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              lastGraph->GetPassCount(), kExpectedPassCount);
		fail("E_CONSTCOLOR_GRAPH", detail);
	}
	size_t totalBoundParams = 0;
	uint32_t passesWithBlock = 0;
	for (auto const& pass : lastGraph->GetGraphForTest()->GetRenderPasses())
	{
		auto const& bound = pass.GetShaderStructs();
		totalBoundParams += bound.size();
		auto entry = bound.find(CANAME("constantColorBlock"));
		if (entry != bound.end() && entry->second != nullptr)
		{
			++passesWithBlock;
		}
	}

	// 任务 3.2：探针调用计数必须与 C# 的实际调用逐项对上（读数全部来自适配器的原生钩子）。
	{
		const uint32_t createStructDelta = renderBackend->GetCreateShaderStructCountForTest() - createStructBefore;
		const uint32_t setFloat3Delta = renderBackend->GetSetFloat3CountForTest() - setFloat3Before;
		const uint32_t setStructDelta = renderBackend->GetSetStructCountForTest() - setStructBefore;
		const uint32_t setPassParamDelta = renderBackend->GetSetPassParamCountForTest() - setPassParamBefore;
		const uint32_t captureDelta = renderBackend->GetCreateCaptureTargetCountForTest() - captureBefore;
		const uint32_t executeDelta = renderBackend->GetExecuteGraphCountForTest() - executeBefore;
		const uint32_t expectedPassParam = skipParam ? 0u : 2u;
		const uint32_t expectedExecute = skipParam ? 0u : 1u;
		CA_LOG("TestCSharpConstantColor 调用计数：createStruct={} setFloat3={} setStruct={} "
		       "setPassParam={} captureTarget={} executeGraph={}",
		       createStructDelta, setFloat3Delta, setStructDelta, setPassParamDelta, captureDelta,
		       executeDelta);
		if (createStructDelta != 4u || setFloat3Delta != 3u || setStructDelta != 3u ||
			setPassParamDelta != expectedPassParam || captureDelta != 1u ||
			executeDelta != expectedExecute)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "调用计数不符：createStruct=%u/4 setFloat3=%u/3 setStruct=%u/3 "
			              "setPassParam=%u/%u captureTarget=%u/1 executeGraph=%u/%u",
			              createStructDelta, setFloat3Delta, setStructDelta, setPassParamDelta,
			              expectedPassParam, captureDelta, executeDelta, expectedExecute);
			fail("E_CONSTCOLOR_COUNTS", detail);
		}
	}

	if (skipParam)
	{
		// 提交前拒绝：参数表为空、适配器错误文本点名块名、进入后端的执行次数为 0。
		const std::string adapterError = renderBackend->GetLastError().c_str();
		const bool namedBlock = adapterError.find(kBlockName) != std::string::npos;
		const uint32_t executedTotal = renderBackend->GetExecuteGraphCountForTest();
		if (totalBoundParams != 0)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "skip-param：参数表仍有 %zu 条绑定", totalBoundParams);
			fail("E_CONSTCOLOR_PARAM", detail);
		}
		if (!namedBlock)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "skip-param：适配器错误文本没有点名 %s，实际 = \"%s\"",
			              kBlockName, adapterError.c_str());
			fail("E_CONSTCOLOR_PARAM", detail);
		}
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail),
		              "skip-param：参数表 0 条、适配器点名 %s、后端执行次数 %u、等待计数 %u→%u，按预期失败",
		              kBlockName, executedTotal, waitBefore, waitAfter);
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
	if (waitAfter != waitBefore + 1 || pendingAfter != 0)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：适配器 WaitIdle 计数 %u → %u、待完成 %u",
		              waitBefore, waitAfter, pendingAfter);
		fail("E_CONSTCOLOR_WAIT", detail);
	}
	// 顶点流：单 `POSITION` 布局（任务 3.2 的 `SetPositionStream` 断言）。两个 pass 的顶点流
	// 描述都必须只有一条 POSITION 属性、偏移 0、步长 12。
	{
		uint32_t checkedBatches = 0;
		for (auto const& pass : lastGraph->GetGraphForTest()->GetRenderPasses())
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
	const auto& presents = lastGraph->GetGraphForTest()->GetFinalizePass().m_PresentBackBuffers;
	if (presents.size() != 1u || presents[0] != windowTarget->GetImageForTest())
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
	auto token = ctx.pGPUBackend->Readback(captureTarget->GetImageForTest(), pixels);
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
	CA_LOG("TestCSharpConstantColor：backend={} mode={} size={}x{} wait={}→{} pending={} presents={} "
	       "passes={} boundParams={} bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height, waitBefore, waitAfter,
	       pendingAfter, presents.size(), lastGraph->GetPassCount(), totalBoundParams, background[0],
	       background[1], background[2], background[3], center[0], center[1], center[2], center[3],
	       pngPath.c_str());

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

	// 真实后端负例（任务 4.1）：未知类型名创建与未登记着色器路径绑定都必须在适配层被拒绝，
	// 进程不崩（D3D12 侧对空元数据的构造函数路径因此永远走不到）。
	{
		const uint32_t createBeforeNegative = renderBackend->GetCreateShaderStructCountForTest();
		auto unknownStruct = renderBackend->CreateShaderStruct(CANAME("NoSuchStructForConstantColor"));
		const std::string unknownError = renderBackend->GetLastError().c_str();
		if (unknownStruct != nullptr ||
			unknownError.find("NoSuchStructForConstantColor") == std::string::npos ||
			renderBackend->GetCreateShaderStructCountForTest() != createBeforeNegative)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "未知类型名没有被挡下：句柄=%p 文本=\"%s\" 后端创建次数 %u→%u",
			              static_cast<void*>(unknownStruct.get()), unknownError.c_str(),
			              createBeforeNegative, renderBackend->GetCreateShaderStructCountForTest());
			fail("E_CONSTCOLOR_NEGATIVE_TYPE", detail);
		}
		CA_LOG("真实后端负例 1/2：未知类型名 → 空句柄 + 文本点名，后端创建次数 0");

		auto negativeGraph = renderBackend->CreateGraph();
		bool graphBuilt = negativeGraph != nullptr &&
			negativeGraph->AddBuffer(CANAME("NegativeVertices"),
				GPUBufferDescriptor::Create(3u, 12u), std::vector<uint8_t>(36u, 0u)) == 0 &&
			negativeGraph->SetPositionStream(CANAME("NegativeStream"), 12u, 0u) == 0 &&
			negativeGraph->AddTrianglePass(captureTarget, CAPATH("Shaders/Test/NoSuchShader"),
				CANAME("NegativeStream"), CANAME("NegativeVertices"), 3u, 0.0f, 0.0f, 1.0f, 1.0f) == 0;
		if (!graphBuilt)
		{
			fail("E_CONSTCOLOR_NEGATIVE_PATH", "负例图没有建出来（AddBuffer / SetPositionStream / AddTrianglePass 被拒）");
		}
		auto negativeParams = renderBackend->CreateShaderStruct(CANAME("ConstantColor"));
		if (negativeParams == nullptr)
		{
			fail("E_CONSTCOLOR_NEGATIVE_PATH",
			     castl::string("负例的参数结构没有创建出来：") + renderBackend->GetLastError());
		}
		const int32_t bindStatus =
			negativeGraph->SetPassParam(0u, CANAME("constantColorBlock"), negativeParams);
		const std::string bindError = negativeGraph->GetLastError().c_str();
		if (bindStatus == 0 || bindError.find("NoSuchShader") == std::string::npos)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "未登记着色器路径没有被拒绝：status=%d 文本=\"%s\"",
			              bindStatus, bindError.c_str());
			fail("E_CONSTCOLOR_NEGATIVE_PATH", detail);
		}
		CA_LOG("真实后端负例 2/2：未登记着色器路径 → status={} 文本点名路径", bindStatus);
	}
}

// ---- TestCSharpStructuredBuffer（csharp-render-structured-buffer 任务 1.2 / 4.1）----
// C# 用单 `POSITION` 顶点流 + `StructuredColor` 参数结构（一个 `StructuredBuffer<float3>` 字段，经
// `SetBuffer` 按名绑定图内登记的缓冲）建图：窗口与离屏各一个同逻辑 pass、两次
// `SetPassParam("structuredColorBlock", …)`，提交并等待。原生侧用既有 `Readback` 读那份离屏目标做
// 精确像素判据（背景 (0,255,0,255)、中心 (255,0,255,255)），并核对 pass 数、每个 pass 的参数表条目、
// 呈现清单与等待计数。
//
// Debug 负向（进程环境变量 `CASCADED_CSHARP_SB_SELFTEST`，C# 侧读同一份值）：
//   `bad-buffer-color` 缓冲元素写成 (0,0,1) → 中心 MUST 精确等于 (0,0,255,255)，以 `E_SB_PIXEL` 判红；
//   `unbound-field`    完全不调 `SetBuffer` → `SubmitGraph` 在进入后端执行前拒绝并点名
//                      `colorStructuredBuffer`，以 `E_SB_BIND` 判红，**不读像素**；
//   `bad-buffer-name`  绑未登记的 `sbColorsMissing` → 同样在提交前拒绝并点名缓冲名，以 `E_SB_BIND` 判红。
// `Tools/check_csharp_structured_buffer.py` 逐后端对账正常（pass）与三个负向（fail + 对应错误码）。
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

	// 编号 3 = 脚本渲染后端：宿主在模块 link 阶段登记真实适配器。
	void* renderInstance = pScriptHost->GetInterfaceInstance(3, nullptr);
	auto* renderBackend = dynamic_cast<graphics_backend::CScriptRenderBackend*>(
		static_cast<graphics_backend::IScriptRenderBackend*>(renderInstance));
	if (renderBackend == nullptr)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "编号 3 没有登记脚本渲染适配器（实例 = %p）", renderInstance);
		fail("E_SB_BACKEND_MISSING", detail);
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
		~StructuredBufferEnvGuard()
		{
			::SetEnvironmentVariableA("CASCADED_CSHARP_SB_PROBE", nullptr);
		}
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
	const uint32_t waitBefore = renderBackend->GetWaitIdleCountForTest();
	const uint32_t createStructBefore = renderBackend->GetCreateShaderStructCountForTest();
	const uint32_t setFloat3Before = renderBackend->GetSetFloat3CountForTest();
	const uint32_t setStructBefore = renderBackend->GetSetStructCountForTest();
	const uint32_t setPassParamBefore = renderBackend->GetSetPassParamCountForTest();
	const uint32_t captureBefore = renderBackend->GetCreateCaptureTargetCountForTest();
	const uint32_t executeBefore = renderBackend->GetExecuteGraphCountForTest();
	// 任务 3.1：`SetBuffer` 与 `AddBuffer` 的增量也要从适配器读数核对（探针里各 1 / 2）。
	const uint32_t setBufferCounterBefore = renderBackend->GetSetBufferCountForTest();
	const uint32_t addBufferBefore = renderBackend->GetAddBufferCountForTest();
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

	auto captureTarget = renderBackend->GetLastCaptureTargetForTest();
	auto windowTarget = renderBackend->GetLastWindowTargetForTest();
	if (captureTarget == nullptr || windowTarget == nullptr)
	{
		fail("E_SB_PROBE_MISSING",
		     "渲染探针没有创建离屏 / 窗口目标（探针开关或四个函数表绑定失败）");
	}
	const uint32_t waitAfter = renderBackend->GetWaitIdleCountForTest();
	const uint32_t pendingAfter = renderBackend->GetPendingGraphCountForTest();

	// 窗口描述快照：离屏目标与窗口同尺寸、RGBA8；窗口目标是后台缓冲、离屏不是。
	const auto& windowDesc = windowTarget->GetDescriptorForTest();
	const auto& captureDesc = captureTarget->GetDescriptorForTest();
	if (windowDesc.width != kWindowWidth || windowDesc.height != kWindowHeight ||
		captureDesc.width != kWindowWidth || captureDesc.height != kWindowHeight ||
		captureDesc.format != ETextureFormat::E_R8G8B8A8_UNORM || !windowTarget->IsBackbuffer() ||
		captureTarget->IsBackbuffer())
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "目标快照不符：窗口 %ux%u / 离屏 %ux%u（格式 %d）",
		              windowDesc.width, windowDesc.height, captureDesc.width, captureDesc.height,
		              static_cast<int>(captureDesc.format));
		fail("E_SB_TARGET", detail);
	}

	// 图结构与参数表：两个 pass（窗口 + 离屏），每个 pass 的参数表正好 1 条 `structuredColorBlock`
	// 且条目非空。缓冲名解析到本图登记的 `sbColors` 由 2.2 的 `GetBoundBufferNameForTest` 在 4.1 核对。
	auto lastGraph = renderBackend->GetLastGraphForTest().lock();
	if (lastGraph == nullptr)
	{
		fail("E_SB_GRAPH", "最近一次提交的图对象已经不存在（脚本没有保留图表面对象）");
	}
	if (lastGraph->GetPassCount() != kExpectedPassCount)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "图 pass 数 %u，期望 %u（窗口 + 离屏两次同逻辑绘制）",
		              lastGraph->GetPassCount(), kExpectedPassCount);
		fail("E_SB_GRAPH", detail);
	}
	size_t totalBoundParams = 0;
	uint32_t passesWithBlock = 0;
	for (auto const& pass : lastGraph->GetGraphForTest()->GetRenderPasses())
	{
		auto const& bound = pass.GetShaderStructs();
		totalBoundParams += bound.size();
		auto entry = bound.find(CANAME("structuredColorBlock"));
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

	// 任务 4.1：`StructuredColor` 的缓冲元数据读数。查询方法在 `CRenderBackend` 上（适配器没有），
	// 所以经后端对象读；这里只复核 design 已定死的读数（只读访问、描述符数组长度 1），不临时改规则。
	{
		ShaderCompilerSlang::ShaderStructData const* structData =
			ctx.pGPUBackend->FindShaderStructData(CANAME("StructuredColor"));
		if (structData == nullptr)
		{
			fail("E_SB_STRUCT", "后端查不到 StructuredColor 的编译元数据");
		}
		ShaderCompilerSlang::BufferData const* bufferEntry = nullptr;
		for (auto const& candidate : structData->m_Buffers)
		{
			if (candidate.m_Name == CANAME("colorStructuredBuffer"))
			{
				bufferEntry = &candidate;
				break;
			}
		}
		if (bufferEntry == nullptr)
		{
			fail("E_SB_STRUCT", "StructuredColor 的元数据缓冲表里没有 colorStructuredBuffer");
		}
		CA_LOG("TestCSharpStructuredBuffer 元数据读数：type=StructuredColor field={} rw={} elements={}",
		       bufferEntry->m_Name.c_str(), static_cast<int>(bufferEntry->m_RWType),
		       bufferEntry->m_ElementCount);
		if (bufferEntry->m_RWType != ShaderCompilerSlang::EShaderResourceAccess::eReadOnly ||
			bufferEntry->m_ElementCount != 1u)
		{
			fail("E_SB_STRUCT", "StructuredColor 的 colorStructuredBuffer 不是「只读 + 描述符数组长度 1」");
		}
	}

	// 任务 4.1：两个 pass 的缓冲字段解析读**图侧登记表**（`GetBoundBufferNameForTest`），不读后端私有表。
	{
		auto* concreteGraph = dynamic_cast<graphics_backend::CScriptRenderGraph*>(lastGraph.get());
		if (concreteGraph == nullptr)
		{
			fail("E_SB_GRAPH", "最近提交的图不是本适配面的 CScriptRenderGraph");
		}
		const cacore::NameHash expectedName = unboundField
			? cacore::NameHash()
			: (badBufferName ? CANAME("sbColorsMissing") : CANAME("sbColors"));
		for (uint32_t passIndex = 0; passIndex < kExpectedPassCount; ++passIndex)
		{
			const cacore::NameHash resolved = concreteGraph->GetBoundBufferNameForTest(passIndex,
				CANAME("structuredColorBlock"), CANAME("colorStructuredBuffer"));
			if (!(resolved == expectedName))
			{
				char detail[512] = {};
				std::snprintf(detail, sizeof(detail),
				              "pass %u 的缓冲字段解析不是预期名字：期望 %s，实际 %s", passIndex,
				              expectedName.Valid() ? expectedName.c_str() : "(空)",
				              resolved.Valid() ? resolved.c_str() : "(空)");
				fail("E_SB_BIND", detail);
			}
			CA_LOG("TestCSharpStructuredBuffer pass={} 缓冲字段解析={}", passIndex,
			       resolved.Valid() ? resolved.c_str() : "(空)");
		}
	}

	// 两种提交前拒绝：参数表 2 条（两次 SetPassParam 都成功）、适配器错误文本点名缺失项、
	// 后端执行次数 0、不读像素 —— 以 `E_SB_BIND` 判红。任务 3.1 的 `SetBuffer` 增量也要在这里
	// 各对各的：`unbound-field` 是 0，`bad-buffer-name` 是 1（确实绑了一个名字，只是名字不存在）。
	if (unboundField || badBufferName)
	{
		const std::string adapterError = renderBackend->GetLastError().c_str();
		char const* const requiredName = unboundField ? kFieldName : kMissingBufferName;
		const bool named = adapterError.find(requiredName) != std::string::npos;
		const uint32_t executeDelta = renderBackend->GetExecuteGraphCountForTest() - executeBefore;
		const uint32_t setBufferDelta = renderBackend->GetSetBufferCountForTest() - setBufferCounterBefore;
		const uint32_t addBufferDelta = renderBackend->GetAddBufferCountForTest() - addBufferBefore;
		const uint32_t expectedSetBuffer = unboundField ? 0u : 1u;
		if (!named)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail), "%s：适配器错误文本没有点名 %s，实际 = \"%s\"",
			              mode.c_str(), requiredName, adapterError.c_str());
			fail("E_SB_BIND", detail);
		}
		if (executeDelta != 0u)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：后端执行次数增量 %u（期望 0）", mode.c_str(),
			              executeDelta);
			fail("E_SB_BIND", detail);
		}
		CA_LOG("TestCSharpStructuredBuffer 负向计数：setBuffer={}（期望 {}） addBuffer={}（期望 2）",
		       setBufferDelta, expectedSetBuffer, addBufferDelta);
		if (setBufferDelta != expectedSetBuffer || addBufferDelta != 2u)
		{
			char detail[256] = {};
			std::snprintf(detail, sizeof(detail), "%s：计数不符 setBuffer=%u/%u addBuffer=%u/2", mode.c_str(),
			              setBufferDelta, expectedSetBuffer, addBufferDelta);
			fail("E_SB_COUNTS", detail);
		}
		char detail[512] = {};
		std::snprintf(detail, sizeof(detail),
		              "%s：参数表 %zu 条、适配器点名 %s、后端执行次数 0、等待计数 %u→%u，按预期失败",
		              mode.c_str(), totalBoundParams, requiredName, waitBefore, waitAfter);
		fail("E_SB_BIND", detail);
	}

	// 正常 / bad-buffer-color：调用计数逐项对上（读数全部来自适配器的原生钩子）。
	{
		const uint32_t createStructDelta = renderBackend->GetCreateShaderStructCountForTest() - createStructBefore;
		const uint32_t setFloat3Delta = renderBackend->GetSetFloat3CountForTest() - setFloat3Before;
		const uint32_t setStructDelta = renderBackend->GetSetStructCountForTest() - setStructBefore;
		const uint32_t setPassParamDelta = renderBackend->GetSetPassParamCountForTest() - setPassParamBefore;
		const uint32_t captureDelta = renderBackend->GetCreateCaptureTargetCountForTest() - captureBefore;
		const uint32_t executeDelta = renderBackend->GetExecuteGraphCountForTest() - executeBefore;
		const uint32_t setBufferDelta = renderBackend->GetSetBufferCountForTest() - setBufferCounterBefore;
		const uint32_t addBufferDelta = renderBackend->GetAddBufferCountForTest() - addBufferBefore;
		CA_LOG("TestCSharpStructuredBuffer 调用计数：createStruct={} setBuffer={} setFloat3={} setStruct={} "
		       "setPassParam={} addBuffer={} captureTarget={} executeGraph={}",
		       createStructDelta, setBufferDelta, setFloat3Delta, setStructDelta, setPassParamDelta,
		       addBufferDelta, captureDelta, executeDelta);
		if (createStructDelta != 1u || setBufferDelta != 1u || setFloat3Delta != 0u ||
			setStructDelta != 0u || setPassParamDelta != 2u || addBufferDelta != 2u ||
			captureDelta != 1u || executeDelta != 1u)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "调用计数不符：createStruct=%u/1 setBuffer=%u/1 setFloat3=%u/0 setStruct=%u/0 "
			              "setPassParam=%u/2 addBuffer=%u/2 captureTarget=%u/1 executeGraph=%u/1",
			              createStructDelta, setBufferDelta, setFloat3Delta, setStructDelta, setPassParamDelta,
			              addBufferDelta, captureDelta, executeDelta);
			fail("E_SB_COUNTS", detail);
		}
	}

	if (waitAfter != waitBefore + 1 || pendingAfter != 0)
	{
		char detail[256] = {};
		std::snprintf(detail, sizeof(detail), "等待判据不符：适配器 WaitIdle 计数 %u → %u、待完成 %u",
		              waitBefore, waitAfter, pendingAfter);
		fail("E_SB_WAIT", detail);
	}
	const auto& presents = lastGraph->GetGraphForTest()->GetFinalizePass().m_PresentBackBuffers;
	if (presents.size() != 1u || presents[0] != windowTarget->GetImageForTest())
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
	auto token = ctx.pGPUBackend->Readback(captureTarget->GetImageForTest(), pixels);
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
	CA_LOG("TestCSharpStructuredBuffer：backend={} mode={} size={}x{} wait={}→{} pending={} presents={} "
	       "passes={} boundParams={} bg=({},{},{},{}) center=({},{},{},{}) png={}",
	       ctx.backendName, mode.empty() ? "normal" : mode.c_str(), width, height, waitBefore, waitAfter,
	       pendingAfter, presents.size(), lastGraph->GetPassCount(), totalBoundParams, background[0],
	       background[1], background[2], background[3], center[0], center[1], center[2], center[3],
	       pngPath.c_str());

	auto differs = [](std::array<uint8_t, 4> const& left, std::array<uint8_t, 4> const& right) {
		return left[0] != right[0] || left[1] != right[1] || left[2] != right[2] || left[3] != right[3];
	};
	if (badBufferColor)
	{
		// 缓冲元素 (0,0,1)：中心 MUST 精确等于 (0,0,255,255)。判据不能写成"中心非 magenta"或
		// "中心不等于背景"—— 空帧 / 黑帧都能通过那种弱判据。
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

	// 任务 4.1：真实后端负例 —— 经真实适配器对缓冲表外字段名执行 `SetBuffer`，必须在适配层被拒绝、
	// 进程不崩（D3D12 侧未命中字段是静默跳过，不可作为脚本面）。
	{
		auto negativeParams = renderBackend->CreateShaderStruct(CANAME("StructuredColor"));
		if (negativeParams == nullptr)
		{
			fail("E_SB_NEGATIVE",
			     castl::string("真实后端负例的参数结构没有创建：") + renderBackend->GetLastError());
		}
		const uint32_t setBufferBeforeNegative = renderBackend->GetSetBufferCountForTest();
		const int32_t negativeStatus =
			negativeParams->SetBuffer(CANAME("noSuchBufferField"), CANAME("sbColors"));
		const std::string negativeError = negativeParams->GetLastError().c_str();
		if (negativeStatus == 0 || negativeError.find("noSuchBufferField") == std::string::npos ||
			renderBackend->GetSetBufferCountForTest() != setBufferBeforeNegative)
		{
			char detail[512] = {};
			std::snprintf(detail, sizeof(detail),
			              "真实后端负例没有被挡下：status=%d 文本=\"%s\" 计数 %u→%u", negativeStatus,
			              negativeError.c_str(), setBufferBeforeNegative,
			              renderBackend->GetSetBufferCountForTest());
			fail("E_SB_NEGATIVE", detail);
		}
		CA_LOG("真实后端负例：缓冲表外字段名 SetBuffer → status={} 文本={}", negativeStatus,
		       negativeError.c_str());
	}
}

// ---- TestCSharpWindowCallbacks（本 change 任务 4.1 / 4.2 / 4.3 / 5.2）----
// 10 类窗口/输入回调走一整圈：C++ 侧用 `fake_window::System` 确定性触发，托管侧注册真实
// `Action<...>` 并把窗口身份与全部标量参数写进日志，`Tools/check_window_system_bindings.py --callbacks`
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
		std::cout << "  TestCSharpWindowCallbacks\n";
		std::cout << "  TestGPUTiming\n";
		// csharp-render-shader-constants 任务 1.2：第 14 项
		std::cout << "  TestCSharpConstantColor\n";
		// csharp-render-structured-buffer 任务 1.2：第 15 项（`--list` 顺序 ≠ 全量执行顺序：
		// 全量里它必须排在 `TestCSharpWindowCallbacks` 之前，见 run-all 那段的注释）
		std::cout << "  TestCSharpStructuredBuffer\n";
		return 0;
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

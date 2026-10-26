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

	return EXIT_SUCCESS;
}

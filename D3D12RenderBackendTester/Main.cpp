#include <cstdlib>
#define NOMINMAX
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
#include <thread>
#include <chrono>
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
using namespace thread_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace cawindow;
using namespace catimer;
using namespace ca_io;


int main(int argc, char* argv[])
{
	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format };
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	castl::string resourceString = castl::to_ca(rootPath.string()) + "CAResources";
	castl::string assetString = castl::to_ca(rootPath.string()) + "CAAssets";
	castl::string editorResourceString = castl::to_ca(rootPath.string()) + "EditorConfigs";

	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("D3D12RenderBackend");
	TModuleLoader<IWindowSystem> windowSystemLoader("WindowSystem");

	auto windowSystem = windowSystemLoader.New();

	InitTimerSystem();
	GetGlobalTimerSystem()->SetThreadName("Main");

	TIMER_NEWFRAME();

	auto pThreadManager = threadManagerLoader.New();
	unsigned int n = std::thread::hardware_concurrency();
	n = (n == 0) ? 5 : (castl::min)(n, 16u);
	pThreadManager->InitializeThreadCount(GetGlobalTimerSystem(), n, 1);
	pThreadManager->SetDedicateThreadMapping(0, { "MainThread" });

	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize(GetGlobalTimerSystem(), "Test Vulkan Backend", "CASCADED Engine");


	auto newWindow = windowSystem->NewWindow(1024, 512, "Window System Window");
	auto windowHandle = pBackend->GetWindowHandle(newWindow.lock());

	pThreadManager.reset();
	pBackend->Release();
	pBackend.reset();

	//gCPUProfiler.Shutdown();
	return EXIT_SUCCESS;
}

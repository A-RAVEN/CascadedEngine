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
#include <magic_enum/magic_enum.hpp>
#include <CAResource/ResourceSystemFactory.h>

using namespace thread_management;
using namespace resource_management;
using namespace library_loader;
using namespace graphics_backend;
using namespace cawindow;
using namespace catimer;
using namespace ca_io;


int main(int argc, char* argv[])
{
	TModuleLoader<ShaderCompilerSlang::IShaderCompilerManager> shaderManager("ShaderCompilerSlang");
	castl::shared_ptr < ShaderCompilerSlang::IShaderCompilerManager> shaderCompilerManager = shaderManager.New();
	shaderCompilerManager->InitializePoolSize(1);



	std::filesystem::path rootPathFS{ "../../../../" , std::filesystem::path::format::native_format };
	std::filesystem::path rootPath = std::filesystem::absolute(rootPathFS);
	castl::string resourceString = castl::to_ca(rootPath.string()) + "CAResources";
	castl::string assetString = castl::to_ca(rootPath.string()) + "CAAssets";
	castl::string editorResourceString = castl::to_ca(rootPath.string()) + "EditorConfigs";

	{
		castl::string shaderPath = resourceString + "/Shaders";
		castl::string testPath = shaderPath + "/TestBindingShader.slang";
		auto pCompiler = shaderCompilerManager->AquireShaderCompilerShared();
		pCompiler->BeginCompileTask();
		pCompiler->AddInlcudePath(shaderPath.c_str());
		pCompiler->AddSourceFile(testPath.c_str());
		pCompiler->EnableDebugInfo();
		pCompiler->SetTarget(ShaderCompilerSlang::EShaderTargetType::eSpirV);
		pCompiler->Compile();
		if (pCompiler->HasError())
		{
			CA_LOG_ERR("Shader compile failed");
		}
		else
		{
			castl::vector<ShaderCompilerSlang::ShaderCompileTargetResult> result = pCompiler->GetResults();
			for (auto& shaderCompileTargetResult : result)
			{
				std::cout << "\nTargetType: " <<  magic_enum::enum_name(shaderCompileTargetResult.targetType) << std::endl;
				auto& reflectionData = shaderCompileTargetResult.m_ReflectionData;
				auto& bindingData = reflectionData.m_BindingData;
				for (auto& binding : bindingData)
				{

					std::cout << "-Binding Space: " << binding.m_BindingSpace << std::endl;
					int uniformID = 0;
					for (auto& uniformBuffer : binding.m_UniformBuffers)
					{
						std::cout << "--Uniform" << uniformID << ": BindingID" << uniformBuffer.m_BindingIndex << std::endl;
						for (auto& group : uniformBuffer.m_Groups)
						{
							std::cout << "---Group: " << group.m_Name << std::endl;
							std::cout << "----Offset/Size/Stride:" << group.m_MemoryOffset << "/" << group.m_MemorySize << "/" << group.m_Stride << std::endl;
							if (group.isArray())
							{
								std::cout << "----IsArray(ElementCount):" << group.m_ElementCount << std::endl;
							}
							for (auto& element : group.m_Elements)
							{
								std::cout << "-----Name: " << element.m_Name.Get() << std::endl;
								std::cout << "-----Offset/Size/Stride:" << element.m_MemoryOffset << "/" << element.m_ElementMemorySize << "/" << element.m_Stride << std::endl;
								if (element.isArray())
								{
									std::cout << "-----IsArray(ElementCount):" << element.m_ElementCount << std::endl;
								}
							}
						}
					}
					for (auto& texture : binding.m_Textures)
					{
						std::cout << "--Texture " << texture.m_Name << "; BindingID: " << texture.m_BindingIndex << std::endl;
					}
					for (auto& sampler : binding.m_Samplers)
					{
						std::cout << "--Sampler " << sampler.m_Name << "; BindingID: " << sampler.m_BindingIndex << std::endl;
					}
					for (auto& buffer : binding.m_Buffers)
					{
						std::cout << "--Buffer " << buffer.m_Name << "; BindingID: " << buffer.m_BindingIndex << std::endl;
					}
				}
			}
		}
	}

	TModuleLoader<CThreadManager> threadManagerLoader("ThreadManager");
	TModuleLoader<CRenderBackend> renderBackendLoader("D3D12RenderBackend");
	TModuleLoader<IWindowSystem> windowSystemLoader("WindowSystem");
	TModuleLoader<IOManager> ioManagerLoader("IOManager_FS");
	TModuleLoader<ResourceFactory> resourceSystemLoader("CAGeneralReourceSystem");

	//Timer System
	InitTimerSystem();

	//Window System
	auto windowSystem = windowSystemLoader.New();

	//Initialize Thread Manager
	auto pThreadManager = threadManagerLoader.New();
	unsigned int n = std::thread::hardware_concurrency();
	n = (n == 0) ? 5 : (castl::min)(n, 16u);
	pThreadManager->InitializeThreadCount(GetGlobalTimerSystem(), n);

	//Initialize IO Manager
	auto g_IOManager = ioManagerLoader.New();
	g_IOManager->Initialize(pThreadManager.get());

	//Resource System
	auto resourceSystemFactory = resourceSystemLoader.New();
	auto pResourceManagingSystem = resourceSystemFactory->NewManagingSystemShared();

	pResourceManagingSystem->Initialize(g_IOManager);
	pResourceManagingSystem->SetResourceRootPath(assetString);

	auto importingSystem = resourceSystemFactory->NewImportingSystemShared();
	importingSystem->SetResourceManager(pResourceManagingSystem.get());

	auto pBackend = renderBackendLoader.New();
	pBackend->Initialize(GetGlobalTimerSystem()
		, g_IOManager.get(), pResourceManagingSystem.get(), importingSystem.get()
		, "Test Vulkan Backend", "CASCADED Engine");

	pBackend->RunTestCode();

	importingSystem->ScanSourceDirectory(resourceString);

	GPUTextureDescriptor textureDesc = GPUTextureDescriptor::Create(1024, 512, ETextureFormat::E_B8G8R8A8_UNORM, ETextureAccessType::eSampled | ETextureAccessType::eTransferDst);
	castl::shared_ptr<GPUTexture> texture = pBackend->CreateGPUTexture(textureDesc);

	auto newWindow = windowSystem->NewWindow(1024, 512, "Window System Window");
	auto windowHandle = pBackend->GetWindowHandle(newWindow.lock());

	pThreadManager.reset();
	pBackend->Release();
	pBackend.reset();

	//gCPUProfiler.Shutdown();
	return EXIT_SUCCESS;
}

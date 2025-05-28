#include "D3D12Includes.h"
#include "D3D12Debug.h"
#include "RenderBackend_D3D12.h"
#include <CATimer/Timer.h>
#include <LibraryExportCommon.h>
#include <ResourceManagment/D3DImageObject.h>
#include <ResourceManagment/D3DBufferObject.h>
#include <Utils/InterfaceTranslation.h>
#include <ShaderLibrary/D3D12ShaderStruct.h>

namespace graphics_backend
{

    void GetHardwareAdapter(
        IDXGIFactory1* pFactory,
        IDXGIAdapter1** ppAdapter,
        bool requestHighPerformanceAdapter = true)
    {
        *ppAdapter = nullptr;

        ComPtr<IDXGIAdapter1> adapter;

        ComPtr<IDXGIFactory6> factory6;
        if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
        {
            for (
                UINT adapterIndex = 0;
                SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                    adapterIndex,
                    requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                    IID_PPV_ARGS(&adapter)));
                    ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    // Don't select the Basic Render Driver adapter.
                    // If you want a software adapter, pass in "/warp" on the command line.
                    continue;
                }

                // Check to see whether the adapter supports Direct3D 12, but don't create the
                // actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                {
                    break;
                }
            }
        }

        if (adapter.Get() == nullptr)
        {
            for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    // Don't select the Basic Render Driver adapter.
                    // If you want a software adapter, pass in "/warp" on the command line.
                    continue;
                }

                // Check to see whether the adapter supports Direct3D 12, but don't create the
                // actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                {
                    break;
                }
            }
        }

        if (adapter.Get() == nullptr)
        {
            //Fall back to software adapter
            for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
            {
                DXGI_ADAPTER_DESC1 desc;
                adapter->GetDesc1(&desc);
                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                    {
                        break;
                    }
                }
            }
        }

        *ppAdapter = adapter.Detach();
    }

    RenderBackend_D3D12::RenderBackend_D3D12() : 
        m_MemoryManager(this)
        , m_SamplerManager(this)
    {
    }

    void RenderBackend_D3D12::Initialize(catimer::TimerSystem* timer
        , ca_io::IOManager* ioManager
        , resource_management::ResourceManagingSystem* resourceManager
        , resource_management::ResourceImportingSystem* resourceImporter
        , castl::string const& appName
        , castl::string const& engineName)
	{
        catimer::SetGlobalTimerSystem(timer);
        p_IOManager = ioManager;
        p_ResourceManager = resourceManager;
		p_ResourceImporter = resourceImporter;
        p_ResourceImporter->AddImporter(&m_ShaderResourceImporter);

        UINT dxgiFactoryFlags = 0;

#if D3D12_RENDER_BACKEND_DEBUG
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();

                // Enable additional debug layers.
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif

        //ComPtr<IDXGIFactory4> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_Factory)));

        GetHardwareAdapter(m_Factory.Get(), &m_Adapter);

        ThrowIfFailed(D3D12CreateDevice(
            m_Adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_Device)
        ));

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

        m_MemoryManager.Init();
	}

    void RenderBackend_D3D12::Release()
    {
        m_MemoryManager.Release();

    }

    castl::shared_ptr<WindowHandle> RenderBackend_D3D12::GetWindowHandle(castl::shared_ptr<cawindow::IWindow> window)
    {
        auto found = m_WindowContexts.find(window);
        if (found != m_WindowContexts.end())
        {
            return found->second;
        }
        castl::shared_ptr<WindowContext> newHandle = castl::make_shared<WindowContext>(this, window);
        m_WindowContexts.insert(castl::make_pair(window, newHandle));
        return newHandle;
    }

    bool RenderBackend_D3D12::AnyWindowRunning()
    {
        return !m_WindowContexts.empty();
    }

    castl::shared_ptr<GPUBuffer> RenderBackend_D3D12::CreateGPUBuffer(GPUBufferDescriptor const& descriptor)
    {
        castl::shared_ptr<D3DBufferObject> result = castl::make_shared<D3DBufferObject>(this);
        D3D12_RESOURCE_DESC resourceDesc = GetResourceDescFromGPUBufferDescriptor(descriptor);
        GPUResource resource = m_MemoryManager.AllocGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
        result->SetGPUResource(castl::move(resource));
        return result;
    }



    castl::shared_ptr<GPUTexture> RenderBackend_D3D12::CreateGPUTexture(GPUTextureDescriptor const& inDescriptor)
    {
        castl::shared_ptr<D3DImageObject> result = castl::make_shared<D3DImageObject>(this);
		D3D12_RESOURCE_DESC resourceDesc = GetResourceDescFromTextureDescriptor(inDescriptor);
        GPUResource resource = m_MemoryManager.AllocGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
        result->SetGPUResource(castl::move(resource));
        return result;
    }

    castl::shared_ptr<ShaderStruct> RenderBackend_D3D12::CreateShaderStruct(cacore::NameHash const& structType)
    {
        auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
        auto found = shaderLibrary->m_ShaderStructs.find(structType);
        ShaderCompilerSlang::ShaderStructData const* pData = nullptr;
        if (found != shaderLibrary->m_ShaderStructs.end())
        {
            pData = &found->second;
        }
        return NewSubObject_Shared<D3D2ShaderStruct>(pData);
    }

    void RenderBackend_D3D12::RunTestCode()
    {
		AliasedMemoryAllocator allocator(this, m_MemoryManager.GetAllocator());
		GPUTextureDescriptor desc = GPUTextureDescriptor::Create(512, 512, ETextureFormat::E_B8G8R8A8_UNORM, ETextureAccessType::eRT);
		auto resourceDesc = GetResourceDescFromTextureDescriptor(desc);
        auto resource = allocator.AllocateGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
        auto resource2 = allocator.AllocateGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
        auto resource3 = allocator.AllocateGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
        resource.FreeVirtualMemmories();
        resource2.FreeVirtualMemmories();
        auto resource4 = allocator.AllocateGPUResource(resourceDesc, D3D12_HEAP_TYPE_DEFAULT);
        allocator.LogAllocatorStates();
        allocator.CommitAllocations();
		allocator.Release();
    }

    ShaderFileInfo const* RenderBackend_D3D12::GetShaderFileInfo(ShaderInfo const& shaderInfo)
    {
        auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
        auto fileInfo = shaderLibrary->GetShaderFileInfo(shaderInfo.path);
        return fileInfo;
    }

    ShaderSetData RenderBackend_D3D12::GetShaderCodes(ShaderInfo const& shaderInfo)
    {
        ShaderSetData result;
        auto shaderLibrary = p_ResourceManager->GetOrLoadResource<ShaderLibrary>("D3D12ShaderLibrary.shLib");
        auto fileInfo = shaderLibrary->GetShaderFileInfo(shaderInfo.path);
        result.reflectionData = &fileInfo->reflectionData;
        for (auto& fileInfo : fileInfo->entryPointToShaderProgram)
        {
            auto code = shaderLibrary->GetShaderCode(fileInfo.second);

            switch (code->shaderType)
            {
            case ECompileShaderType::eVert:
                result.vertexShader = code->data;
                break;
            case ECompileShaderType::eFrag:
                result.fragmentShader = code->data;
                break;
            case ECompileShaderType::eComp:
                result.computeShader = code->data;
                break;
            default:
                break;
            }
            result;
        }
        return result;
    }


    CA_LIBRARY_INSTANCE_LOADING_FUNCTIONS(CRenderBackend, RenderBackend_D3D12)
}
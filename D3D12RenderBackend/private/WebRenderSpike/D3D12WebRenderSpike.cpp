// D3D12 共享纹理助手实现（editor-web-render-spike 2.1）。
//
// 机制（design D2，纠正过的正确机制）：
//   - 共享纹理 = `D3D12_HEAP_FLAG_SHARED` 的 committed resource + `ID3D12Device::CreateSharedHandle`
//     （`D3D11_RESOURCE_MISC_SHARED` 是 D3D11 的标志，D3D12 的 RESOURCE_DESC 根本没有 MiscFlags）。
//     文档：https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps
//           https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createsharedhandle
//   - 资源标志带 `ALLOW_RENDER_TARGET`（要画）与 `ALLOW_SIMULTANEOUS_ACCESS`（允许被多个消费者/队列
//     同时访问；与 RENDER_TARGET、非 MSAA 兼容）。文档：
//     https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags
//   - 第二消费者用 `ID3D12Device::OpenSharedHandle` 打开**同一个句柄**，得到独立资源对象后再读回像素
//     ——"句柄可核"不是只看非空，而是"第二个消费者能开、读回来的像素与第一份一致"。
//   - 渲染复用后端设备（`RenderBackend_D3D12::GetDevice()`），但提交走本类自己的 direct 队列 +
//     单次命令分配器 + fence 同步：引擎的图队列有自己的屏障/帧上下文模型，spike 不该插进去。
//     本类所有提交都在引擎主线程同步完成（提交后立刻等 fence），没有在飞行中的工作。
#include "WebRenderSpike/D3D12WebRenderSpike.h"
#include "RenderBackend_D3D12.h"

#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>

namespace graphics_backend
{
	namespace
	{
		// 特征图案的固定版式（与 EditorWeb/src/spikeChannel.ts 的采样点表、C++ 测试的采样点表逐点一致）。
		// 全部写死：纹理尺寸 256×256（D2），三角/标记/帧序号位的位置是判据的一部分，不许"按尺寸缩放"。
		constexpr uint32_t kTextureSize = 256;
		constexpr float kFrameBitOriginX = 32.0f;
		constexpr float kFrameBitOriginY = 4.0f;
		constexpr float kFrameBitSize = 8.0f;

		// 简易全屏 quad/triangle 着色器：顶点位置由根常量给出（像素坐标），像素着色器输出常量颜色。
		// 三角用"退化 quad"画（TL 与 TR 重合于顶点），因此一套 PSO 够画背景、三角、标记、帧序号位。
		constexpr char const* kShaderSource = R"(
cbuffer DrawConstants : register(b0)
{
    float4 g_Top;     // 左上角 xy、右上角 xy（像素坐标）
    float4 g_Bottom;  // 右下角 xy、左下角 xy（像素坐标）
    float4 g_Color;
    float2 g_TargetSize;
    float2 g_Pad;
};

struct VSOut
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    // 一个 quad 的 6 个顶点：TL, TR, BR, TL, BR, BL
    float2 corners[6] = { g_Top.xy, g_Top.zw, g_Bottom.xy, g_Top.xy, g_Bottom.xy, g_Bottom.zw };
    float2 p = corners[vertexId];
    VSOut output;
    // 像素坐标 → 裁剪空间（左上原点）。
    output.pos = float4((p.x / g_TargetSize.x) * 2.0 - 1.0,
                        1.0 - (p.y / g_TargetSize.y) * 2.0,
                        0.0, 1.0);
    output.color = g_Color;
    return output;
}

float4 PSMain(VSOut input) : SV_Target
{
    return input.color;
}
)";

		std::string HexHresult(HRESULT result)
		{
			char text[16] = {};
			std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned int>(result));
			return text;
		}
	}

	D3D12WebRenderSpikeTexture::D3D12WebRenderSpikeTexture(RenderBackend_D3D12* backend)
		: m_Backend(backend)
	{
	}

	D3D12WebRenderSpikeTexture::~D3D12WebRenderSpikeTexture()
	{
		Release();
	}

	bool D3D12WebRenderSpikeTexture::Fail(std::string message)
	{
		m_LastError = std::move(message);
		return false;
	}

	bool D3D12WebRenderSpikeTexture::EnsurePipeline(ID3D12Device* device)
	{
		if (m_RootSignature != nullptr && m_PipelineState != nullptr)
		{
			return true;
		}
		ComPtr<ID3DBlob> vertexBlob;
		ComPtr<ID3DBlob> pixelBlob;
		ComPtr<ID3DBlob> errorBlob;
		UINT const flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
		HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "WebRenderSpike.hlsl", nullptr, nullptr,
		                        "VSMain", "vs_5_0", flags, 0, &vertexBlob, &errorBlob);
		if (FAILED(hr))
		{
			std::string reason = errorBlob != nullptr
				? std::string(static_cast<char const*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize())
				: std::string("（无编译错误文本）");
			return Fail("顶点着色器编译失败（" + HexHresult(hr) + "）：" + reason);
		}
		errorBlob.Reset();
		hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), "WebRenderSpike.hlsl", nullptr, nullptr, "PSMain",
		                "ps_5_0", flags, 0, &pixelBlob, &errorBlob);
		if (FAILED(hr))
		{
			std::string reason = errorBlob != nullptr
				? std::string(static_cast<char const*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize())
				: std::string("（无编译错误文本）");
			return Fail("像素着色器编译失败（" + HexHresult(hr) + "）：" + reason);
		}

		// 根签名：12+4 = 16 个 32 位根常量（g_Top 4 + g_Bottom 4 + g_Color 4 + g_TargetSize 2 + g_Pad 2）。
		D3D12_ROOT_PARAMETER parameter{};
		parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameter.Constants.ShaderRegister = 0;
		parameter.Constants.RegisterSpace = 0;
		parameter.Constants.Num32BitValues = 16;
		parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		D3D12_ROOT_SIGNATURE_DESC rootDesc{};
		rootDesc.NumParameters = 1;
		rootDesc.pParameters = &parameter;
		rootDesc.NumStaticSamplers = 0;
		rootDesc.pStaticSamplers = nullptr;
		rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		ComPtr<ID3DBlob> signatureBlob;
		errorBlob.Reset();
		hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
		if (FAILED(hr))
		{
			std::string reason = errorBlob != nullptr
				? std::string(static_cast<char const*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize())
				: std::string("（无序列化错误文本）");
			return Fail("根签名序列化失败（" + HexHresult(hr) + "）：" + reason);
		}
		hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		                                IID_PPV_ARGS(&m_RootSignature));
		if (FAILED(hr))
		{
			return Fail("CreateRootSignature 失败（" + HexHresult(hr) + "）");
		}

		// 图形 PSO：无输入布局（顶点由 SV_VertexID 生成）、无深度、无混合、双面不剔除。
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.pRootSignature = m_RootSignature.Get();
		psoDesc.VS = { vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize() };
		psoDesc.PS = { pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize() };
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;
		hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState));
		if (FAILED(hr))
		{
			return Fail("CreateGraphicsPipelineState 失败（" + HexHresult(hr) + "）");
		}
		return true;
	}

	bool D3D12WebRenderSpikeTexture::Create(uint32_t width, uint32_t height)
	{
		m_LastError.clear();
		if (m_Backend == nullptr)
		{
			return Fail("后端为空（D3D12 共享纹理助手拿不到设备）");
		}
		ID3D12Device* device = m_Backend->GetDevice().Get();
		if (device == nullptr)
		{
			return Fail("后端设备为空（后端未初始化）");
		}
		if (width == 0 || height == 0)
		{
			return Fail("纹理尺寸非法：" + std::to_string(width) + "x" + std::to_string(height));
		}
		if (m_Resource != nullptr && m_Width == width && m_Height == height)
		{
			return true;
		}
		ReleaseResources();

		// 共享堆纹理：committed resource + D3D12_HEAP_FLAG_SHARED + 渲染目标/同时访问标志。
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heapProperties.CreationNodeMask = 1;
		heapProperties.VisibleNodeMask = 1;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

		HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_SHARED, &resourceDesc,
		                                             D3D12_RESOURCE_STATE_COMMON, nullptr,
		                                             IID_PPV_ARGS(&m_Resource));
		if (FAILED(hr))
		{
			return Fail("CreateCommittedResource（HEAP_FLAG_SHARED）失败（" + HexHresult(hr) + "）");
		}
		// 共享句柄：CreateSharedHandle 成功且句柄非空才是"可核的产出"。
		hr = device->CreateSharedHandle(m_Resource.Get(), nullptr, GENERIC_ALL, nullptr, &m_SharedHandle);
		if (FAILED(hr) || m_SharedHandle == nullptr)
		{
			m_SharedHandle = nullptr;
			return Fail("CreateSharedHandle 失败（" + HexHresult(hr) + "）");
		}

		// RTV（本类自己的描述符堆，1 个描述符）。
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = 1;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap));
		if (FAILED(hr))
		{
			return Fail("CreateDescriptorHeap（RTV）失败（" + HexHresult(hr) + "）");
		}
		m_RtvHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
		device->CreateRenderTargetView(m_Resource.Get(), nullptr, m_RtvHandle);

		// 本类自己的 direct 队列 + 一次性命令分配器/列表 + fence（不碰后端的图队列）。
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_Queue));
		if (FAILED(hr))
		{
			return Fail("CreateCommandQueue（spike）失败（" + HexHresult(hr) + "）");
		}
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_Allocator));
		if (FAILED(hr))
		{
			return Fail("CreateCommandAllocator 失败（" + HexHresult(hr) + "）");
		}
		hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Allocator.Get(), nullptr,
		                               IID_PPV_ARGS(&m_CommandList));
		if (FAILED(hr))
		{
			return Fail("CreateCommandList 失败（" + HexHresult(hr) + "）");
		}
		hr = m_CommandList->Close();
		if (FAILED(hr))
		{
			return Fail("首次 Close 命令列表失败（" + HexHresult(hr) + "）");
		}
		hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));
		if (FAILED(hr))
		{
			return Fail("CreateFence 失败（" + HexHresult(hr) + "）");
		}
		m_FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (m_FenceEvent == nullptr)
		{
			return Fail("CreateEventW 失败（等待 GPU 完成用）");
		}
		m_FenceValue = 0;

		if (!EnsurePipeline(device))
		{
			return false;
		}
		m_Width = width;
		m_Height = height;
		return true;
	}

	bool D3D12WebRenderSpikeTexture::Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
	                                            D3D12_RESOURCE_STATES after)
	{
		if (resource == nullptr)
		{
			return Fail("资源为空（屏障）");
		}
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
		m_CommandList->ResourceBarrier(1, &barrier);
		return true;
	}

	bool D3D12WebRenderSpikeTexture::SubmitAndWait()
	{
		HRESULT hr = m_CommandList->Close();
		if (FAILED(hr))
		{
			return Fail("Close 命令列表失败（" + HexHresult(hr) + "）");
		}
		ID3D12CommandList* lists[] = { m_CommandList.Get() };
		m_Queue->ExecuteCommandLists(1, lists);
		++m_FenceValue;
		if (FAILED(m_Queue->Signal(m_Fence.Get(), m_FenceValue)))
		{
			return Fail("队列 Signal 失败");
		}
		if (m_Fence->GetCompletedValue() < m_FenceValue)
		{
			if (FAILED(m_Fence->SetEventOnCompletion(m_FenceValue, m_FenceEvent)))
			{
				return Fail("SetEventOnCompletion 失败");
			}
			if (WaitForSingleObject(m_FenceEvent, 10000) != WAIT_OBJECT_0)
			{
				return Fail("等待 GPU 完成超时（10 秒）");
			}
		}
		return true;
	}

	bool D3D12WebRenderSpikeTexture::RenderFrame(uint32_t frameIndex)
	{
		m_LastError.clear();
		if (m_Resource == nullptr)
		{
			return Fail("共享纹理尚未创建（RenderFrame 之前必须先 Create）");
		}
		HRESULT hr = m_Allocator->Reset();
		if (FAILED(hr))
		{
			return Fail("命令分配器 Reset 失败（" + HexHresult(hr) + "）");
		}
		hr = m_CommandList->Reset(m_Allocator.Get(), m_PipelineState.Get());
		if (FAILED(hr))
		{
			return Fail("命令列表 Reset 失败（" + HexHresult(hr) + "）");
		}

		if (!Transition(m_Resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET))
		{
			return false;
		}
		m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());
		m_CommandList->SetPipelineState(m_PipelineState.Get());
		m_CommandList->OMSetRenderTargets(1, &m_RtvHandle, FALSE, nullptr);
		D3D12_VIEWPORT viewport{};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(m_Width);
		viewport.Height = static_cast<float>(m_Height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_CommandList->RSSetViewports(1, &viewport);
		D3D12_RECT scissor{ 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };
		m_CommandList->RSSetScissorRects(1, &scissor);
		// PSO 的 PrimitiveTopologyType 只定"图元类别"；实际拓扑必须在命令列表上设（默认是 undefined）。
		m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto drawCorners = [this](float tlX, float tlY, float trX, float trY, float brX, float brY, float blX,
		                          float blY, float r, float g, float b, float a) {
			float constants[16] = {
				tlX, tlY, trX, trY,          // g_Top
				brX, brY, blX, blY,          // g_Bottom
				r, g, b, a,                  // g_Color
				static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 0.0f, // g_TargetSize + pad
			};
			m_CommandList->SetGraphicsRoot32BitConstants(0, 16, constants, 0);
			m_CommandList->DrawInstanced(6, 1, 0, 0);
		};
		auto drawRect = [&drawCorners](float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
			drawCorners(x0, y0, x1, y0, x1, y1, x0, y1, r, g, b, a);
		};

		// ① 背景绿 (0,255,0,255)。
		drawRect(0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f, 0.0f, 1.0f);
		// ② 中心红三角 (255,0,0,255)：顶点 (128,64)，底边 y=192、x 从 64 到 192。
		//    TL 与 TR 重合于顶点 = 退化 quad，第二段三角形就是目标三角（一套 PSO 画所有图元）。
		drawCorners(128.0f, 64.0f, 128.0f, 64.0f, 192.0f, 192.0f, 64.0f, 192.0f, 1.0f, 0.0f, 0.0f, 1.0f);
		// ③ 四角标记：8×8 红方块（内缩 4 像素）。
		drawRect(4.0f, 4.0f, 12.0f, 12.0f, 1.0f, 0.0f, 0.0f, 1.0f);
		drawRect(244.0f, 4.0f, 252.0f, 12.0f, 1.0f, 0.0f, 0.0f, 1.0f);
		drawRect(4.0f, 244.0f, 12.0f, 252.0f, 1.0f, 0.0f, 0.0f, 1.0f);
		drawRect(244.0f, 244.0f, 252.0f, 252.0f, 1.0f, 0.0f, 0.0f, 1.0f);
		// ④ 帧序号：顶边 16 位二进制，每比特 8×8（白=1、黑=0），从 x=32 起排。帧序号进像素后
		//    "同一帧"的对账才有物理依据（哈希覆盖它，帧号一变哈希就变）。
		for (uint32_t bit = 0; bit < 16; ++bit)
		{
			bool const on = ((frameIndex >> (15u - bit)) & 1u) != 0u;
			float const x0 = kFrameBitOriginX + static_cast<float>(bit) * kFrameBitSize;
			float const y0 = kFrameBitOriginY;
			float const value = on ? 1.0f : 0.0f;
			drawRect(x0, y0, x0 + kFrameBitSize, y0 + kFrameBitSize, value, value, value, 1.0f);
		}

		// 回到 COMMON：共享资源在两次使用之间保持"无布局"状态，第二消费者（OpenSharedHandle）的
		// 屏障前后态也能从 COMMON 出发，不会与第一份资源的布局状态打架。
		if (!Transition(m_Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON))
		{
			return false;
		}
		return SubmitAndWait();
	}

	bool D3D12WebRenderSpikeTexture::CopyToReadbackAndWait(ID3D12Resource* source, uint8_t* dst, uint64_t capacity,
	                                                       char const* what)
	{
		if (dst == nullptr)
		{
			return Fail(std::string(what) + "：目标缓冲区为空");
		}
		uint64_t const required = static_cast<uint64_t>(m_Width) * static_cast<uint64_t>(m_Height) * 4ull;
		if (capacity < required)
		{
			return Fail(std::string(what) + "：目标缓冲区太小（" + std::to_string(capacity) + " < " +
			            std::to_string(required) + " 字节）");
		}
		ID3D12Device* device = m_Backend->GetDevice().Get();
		D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
		UINT rowCount = 0;
		UINT64 rowSize = 0;
		UINT64 totalBytes = 0;
		device->GetCopyableFootprints(&sourceDesc, 0, 1, 0, &footprint, &rowCount, &rowSize, &totalBytes);
		if (totalBytes == 0)
		{
			return Fail(std::string(what) + "：GetCopyableFootprints 返回 0 字节");
		}

		D3D12_HEAP_PROPERTIES readbackHeap{};
		readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
		readbackHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		readbackHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		readbackHeap.CreationNodeMask = 1;
		readbackHeap.VisibleNodeMask = 1;
		D3D12_RESOURCE_DESC bufferDesc{};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Alignment = 0;
		bufferDesc.Width = totalBytes;
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.SampleDesc.Quality = 0;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		ComPtr<ID3D12Resource> readbackBuffer;
		HRESULT hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		                                             IID_PPV_ARGS(&readbackBuffer));
		if (FAILED(hr))
		{
			return Fail(std::string(what) + "：创建 READBACK 缓冲失败（" + HexHresult(hr) + "）");
		}

		hr = m_Allocator->Reset();
		if (FAILED(hr))
		{
			return Fail(std::string(what) + "：命令分配器 Reset 失败（" + HexHresult(hr) + "）");
		}
		hr = m_CommandList->Reset(m_Allocator.Get(), nullptr);
		if (FAILED(hr))
		{
			return Fail(std::string(what) + "：命令列表 Reset 失败（" + HexHresult(hr) + "）");
		}
		if (!Transition(source, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE))
		{
			return false;
		}
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = readbackBuffer.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = footprint;
		destination.PlacedFootprint.Offset = 0;
		D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
		sourceLocation.pResource = source;
		sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		sourceLocation.SubresourceIndex = 0;
		m_CommandList->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
		if (!Transition(source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON))
		{
			return false;
		}
		if (!SubmitAndWait())
		{
			return false;
		}

		// 紧凑拷贝：丢掉 footprint 的行间距填充。
		void* mapped = nullptr;
		hr = readbackBuffer->Map(0, nullptr, &mapped);
		if (FAILED(hr) || mapped == nullptr)
		{
			return Fail(std::string(what) + "：Map READBACK 缓冲失败（" + HexHresult(hr) + "）");
		}
		auto const* sourceBytes = static_cast<uint8_t const*>(mapped);
		uint64_t const sourceStride = footprint.Footprint.RowPitch;
		uint64_t const destinationStride = static_cast<uint64_t>(m_Width) * 4ull;
		for (uint32_t row = 0; row < m_Height; ++row)
		{
			std::memcpy(dst + row * destinationStride, sourceBytes + row * sourceStride,
			            static_cast<size_t>(destinationStride));
		}
		readbackBuffer->Unmap(0, nullptr);
		return true;
	}

	bool D3D12WebRenderSpikeTexture::Readback(uint8_t* dst, uint64_t capacity)
	{
		m_LastError.clear();
		if (m_Resource == nullptr)
		{
			return Fail("共享纹理尚未创建（Readback 之前必须先 Create）");
		}
		return CopyToReadbackAndWait(m_Resource.Get(), dst, capacity, "本设备读回");
	}

	bool D3D12WebRenderSpikeTexture::ReadbackViaOpenedHandle(uint8_t* dst, uint64_t capacity)
	{
		m_LastError.clear();
		if (m_SharedHandle == nullptr)
		{
			return Fail("共享句柄为空（第二消费者无从打开）");
		}
		ID3D12Device* device = m_Backend->GetDevice().Get();
		m_OpenedResource.Reset();
		HRESULT hr = device->OpenSharedHandle(m_SharedHandle, IID_PPV_ARGS(&m_OpenedResource));
		if (FAILED(hr) || m_OpenedResource == nullptr)
		{
			return Fail("OpenSharedHandle 打开共享句柄失败（" + HexHresult(hr) + "）");
		}
		D3D12_RESOURCE_DESC openedDesc = m_OpenedResource->GetDesc();
		if (openedDesc.Width != m_Width || openedDesc.Height != m_Height ||
		    openedDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
		{
			return Fail("打开的资源描述与创建时不一致（宽 " + std::to_string(openedDesc.Width) + "、高 " +
			            std::to_string(openedDesc.Height) + "、格式 " + std::to_string(static_cast<int>(openedDesc.Format)) +
			            "）");
		}
		return CopyToReadbackAndWait(m_OpenedResource.Get(), dst, capacity, "第二消费者读回");
	}

	void D3D12WebRenderSpikeTexture::ReleaseResources()
	{
		m_OpenedResource.Reset();
		m_PipelineState.Reset();
		m_RootSignature.Reset();
		m_CommandList.Reset();
		m_Allocator.Reset();
		m_Fence.Reset();
		m_Queue.Reset();
		if (m_FenceEvent != nullptr)
		{
			CloseHandle(m_FenceEvent);
			m_FenceEvent = nullptr;
		}
		m_RtvHeap.Reset();
		m_RtvHandle = {};
		m_Resource.Reset();
		if (m_SharedHandle != nullptr)
		{
			CloseHandle(m_SharedHandle);
			m_SharedHandle = nullptr;
		}
		m_Width = 0;
		m_Height = 0;
		m_FenceValue = 0;
	}

	void D3D12WebRenderSpikeTexture::Release()
	{
		ReleaseResources();
	}

	D3D12WebRenderSpikeProvider::D3D12WebRenderSpikeProvider(RenderBackend_D3D12* backend)
		: m_Backend(backend)
	{
	}

	D3D12WebRenderSpikeProvider::~D3D12WebRenderSpikeProvider()
	{
		// 先销毁纹理助手（它持有后端设备/队列的 COM 引用），后端随后才释放设备/队列。
		m_Texture.reset();
		m_Backend = nullptr;
	}

	IWebRenderSpikeSharedTexture* D3D12WebRenderSpikeProvider::GetWebRenderSpikeTexture()
	{
		// 懒创建：只有真的跑渲染原型用例才会建共享纹理（默认路径零额外资源）。
		if (m_Backend == nullptr || !m_Backend->DeviceInited())
		{
			return nullptr;
		}
		if (m_Texture == nullptr)
		{
			m_Texture = std::make_unique<D3D12WebRenderSpikeTexture>(m_Backend);
		}
		return m_Texture.get();
	}
}

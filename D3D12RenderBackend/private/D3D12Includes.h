#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif
#include <windows.h>

//#include <d3d12.h>
//#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include <HashKeys.h>

#define D3D12MA_USING_DIRECTX_HEADERS 1
#include <D3D12MemAlloc.h>
#include <DirectXTex.h>

using Microsoft::WRL::ComPtr;

#define D3D12_RENDER_BACKEND_DEBUG 1
//#ifdef NDEBUG
//#define D3D12_RENDER_BACKEND_DEBUG 1
//#endif
//
//#ifndef D3D12_RENDER_BACKEND_DEBUG
//#define D3D12_RENDER_BACKEND_DEBUG 0
//#endif

inline bool operator==(D3D12_INPUT_ELEMENT_DESC const& lhs, D3D12_INPUT_ELEMENT_DESC const& rhs)
{
	return (std::strcmp(lhs.SemanticName, rhs.SemanticName) == 0)
		&& lhs.SemanticIndex == rhs.SemanticIndex
		&& lhs.Format == rhs.Format
		&& lhs.InputSlot == rhs.InputSlot
		&& lhs.AlignedByteOffset == rhs.AlignedByteOffset
		&& lhs.InputSlotClass == rhs.InputSlotClass
		&& lhs.InstanceDataStepRate == rhs.InstanceDataStepRate;
}

// 新运行时（生成代码）要的包装规则：`ComPtr<ID3DBlob>` 写的是它包着的那段字节，不是指针地址。
namespace cacore::codegen
{
	template<>
	struct wrapper_traits<ComPtr<ID3DBlob>>
	{
		static constexpr bool is_wrapper = true;
		using inner_type = castl::vector<uint8_t>;

		static inner_type get_data(ComPtr<ID3DBlob> const& obj)
		{
			// 空指针 = 没有内容，写成一段空载荷；**不许**在这里解引用 —— 实测（把 D3D12 `ShaderCode` 的
			// `data` 置空以后写一遍）解引用会让进程访问违例（退出码 0xC0000005）。读回来是一个 0 字节的
			// blob，且"写→读→再写"字节不变。
			if (obj == nullptr)
				return inner_type{};
			castl::vector<uint8_t> result(obj->GetBufferSize());
			memcpy(result.data(), obj->GetBufferPointer(), obj->GetBufferSize());
			return result;
		}

		static void set_data(ComPtr<ID3DBlob>& obj, inner_type const& data)
		{
			D3DCreateBlob(data.size(), &obj);
			memcpy(obj->GetBufferPointer(), data.data(), obj->GetBufferSize());
		}
	};

	// 哈希按内容算（与上面的写法同源）：有 blob 就按它的字节算，空指针算一个固定值。
	template<>
	struct custom_hash_trait<ComPtr<ID3DBlob>>
	{
		static void hash(ComPtr<ID3DBlob> const& obj, auto& hasher)
		{
			if (obj == nullptr)
			{
				const uint64_t zero = 0;
				hasher.hash_raw(&zero, sizeof(zero));
				return;
			}
			hasher.hash_raw(obj->GetBufferPointer(), obj->GetBufferSize());
		}
	};
}

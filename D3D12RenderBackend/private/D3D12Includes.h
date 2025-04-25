#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers.
#endif
#define NOMINMAX
#include <windows.h>

//#include <d3d12.h>
//#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include <Reflection.h>

#define D3D12MA_USING_DIRECTX_HEADERS 1
#include "D3D12MemAlloc.h"

using Microsoft::WRL::ComPtr;

#define D3D12_RENDER_BACKEND_DEBUG 1
#ifdef NDEBUG
#define D3D12_RENDER_BACKEND_DEBUG 1
#endif

#ifndef D3D12_RENDER_BACKEND_DEBUG
#define D3D12_RENDER_BACKEND_DEBUG 0
#endif


namespace careflection
{
	template<>
	struct managed_wrapper_traits<ComPtr<ID3DBlob>>
	{
		constexpr static bool is_managed_wrapper = true;
		using inner_type = castl::vector<uint8_t>;
		static inner_type get_data(ComPtr<ID3DBlob> const& obj) {
			castl::vector<uint8_t> result(obj->GetBufferSize());
			memcpy(result.data(), obj->GetBufferPointer(), obj->GetBufferSize());
			return result;
		}
		static void set_data(ComPtr<ID3DBlob>& obj, castl::vector<uint8_t> const& data) {
			D3DCreateBlob(data.size(), &obj);
			memcpy(obj->GetBufferPointer(), data.data(), obj->GetBufferSize());
		}
	};
}

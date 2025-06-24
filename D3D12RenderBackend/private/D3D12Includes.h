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
#include <Reflection.h>
#include <Hasher.h>

#define D3D12MA_USING_DIRECTX_HEADERS 1
#include <D3D12MemAlloc.h>
#include <DirectXTex.h>

using Microsoft::WRL::ComPtr;

#define D3D12_RENDER_BACKEND_DEBUG 1
#ifdef NDEBUG
#define D3D12_RENDER_BACKEND_DEBUG 1
#endif

#ifndef D3D12_RENDER_BACKEND_DEBUG
#define D3D12_RENDER_BACKEND_DEBUG 0
#endif

namespace cacore
{
	template<>
	struct custom_hash_trait<ComPtr<ID3DBlob>>
	{
		constexpr static void hash(ComPtr<ID3DBlob> const& obj, auto& hasher)
		{
			if (obj == nullptr)
			{
				hasher.hash_one<uint64_t>(0);
				return;
			}
			hasher.hash_raw(obj->GetBufferPointer(), obj->GetBufferSize());
		}
	};
}

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

	template<typename T>
	struct managed_pointer_traits<ComPtr<T>>
	{
		constexpr static bool is_managed_pointer = true;
		using pointer_type = ComPtr<T>;
		using pointee_type = T;
		constexpr static T const* get_pointer(pointer_type const& ptr) { return ptr.Get(); }
		constexpr static T* get_pointer(pointer_type& ptr) { return ptr.Get(); }
		constexpr static void set_pointer_null(pointer_type& ptr) { ptr = nullptr; }
	};
}

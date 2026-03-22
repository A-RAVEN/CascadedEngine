#pragma once
#include <D3D12Includes.h>

namespace graphics_backend
{
	class RenderBackend_D3D12;
	class D3D12ShaderObjectDic;

	class D3D12SubobjectBase
	{
	public:
		D3D12SubobjectBase(RenderBackend_D3D12* app);
		D3D12SubobjectBase(D3D12SubobjectBase&& other);
		D3D12SubobjectBase(D3D12SubobjectBase const& other) = delete;
		D3D12SubobjectBase(D3D12SubobjectBase& other) = delete;

		D3D12SubobjectBase& operator=(D3D12SubobjectBase&& other) = default;

		virtual void Release() {};
		RenderBackend_D3D12* GetApp() const {
			return pApp;
		}

		ComPtr<IDXGIFactory4> GetFactory() const;
		ComPtr<ID3D12Device> GetDevice() const;
		ComPtr<IDXGIAdapter1> GetAdapter() const;
		
		template<typename TInterface>
		ComPtr<TInterface> GetFactory() const
		{
			ComPtr<TInterface> result;
			GetFactory().As<TInterface>(&result);
			return result;
		}

		template<typename TInterface>
		ComPtr<TInterface> GetDevice() const
		{
			ComPtr<TInterface> result;
			GetDevice().As<TInterface>(&result);
			return result;
		}

		template<typename TInterface>
		ComPtr<TInterface> GetAdapter() const
		{
			ComPtr<TInterface> result;
			GetAdapter().As<TInterface>(&result);
			return result;
		}

	private:
		RenderBackend_D3D12* pApp;
	};
}
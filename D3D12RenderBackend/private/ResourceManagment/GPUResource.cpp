#pragma once
#include <CASTL/CATypeTraits.h>
#include "GPUResource.h"

namespace graphics_backend
{
	GPUResource::GPUResource(RenderBackend_D3D12* app) : 
		D3D12SubobjectBase(app)
		, m_Allocation(nullptr)
	{

	}
	GPUResource::GPUResource(D3D12SubobjectBase* parent) : 
		D3D12SubobjectBase(parent->GetApp())
		, m_Allocation(nullptr)
	{

	}
	GPUResource::GPUResource(GPUResource&& other) noexcept : m_Allocation(other.m_Allocation), D3D12SubobjectBase(castl::move(other))
	{
	}
	//GPUResource& GPUResource::operator=(GPUResource&& other) noexcept
	//{
	//	m_Allocation = other.m_Allocation;
	//}
	void GPUResource::SetAllocation(D3D12MA::Allocation* allocation)
	{
		m_Allocation = allocation;
	}
	void GPUResource::Release()
	{
		m_Allocation->Release();
	}
	D3D12MA::Allocation* GPUResource::GetAllocation() const
	{
		return m_Allocation;
	}
	ID3D12Resource* GPUResource::GetResource() const
	{
		return m_Allocation->GetResource();
	}
}
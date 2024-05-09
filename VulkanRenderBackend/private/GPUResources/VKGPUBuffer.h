#pragma once
#include <Platform.h>
#include <VulkanIncludes.h>
#include <GPUBuffer.h>
#include "GPUResource.h"

namespace graphics_backend
{
	class VKGPUBuffer : public GPUBuffer, public GPUResource
	{
	public:
		constexpr vk::Buffer GetBuffer() const
		{
			return m_Buffer;
		}
		virtual GPUBufferDescriptor const& GetDescriptor() const override { return m_Descriptor; }
		virtual void SetName(castl::string const& name) override { m_Name = name; }
		virtual castl::string const& GetName() const override { return m_Name; }
		bool Initialized() const
		{
			return m_Buffer != vk::Buffer{ nullptr };
		}
		void SetBuffer(vk::Buffer buffer)
		{
			m_Buffer = buffer;
		}
		void SetDescriptor(GPUBufferDescriptor const& descriptor)
		{
			m_Descriptor = descriptor;
		}
	private:
		vk::Buffer m_Buffer = {nullptr};
		GPUBufferDescriptor m_Descriptor = {};
		castl::string m_Name = { "" };
	};
}
#pragma once
#include <Platform.h>
#include <VulkanIncludes.h>
#include <GPUBuffer.h>
#include "GPUResource.h"
#include "GPUResourceInternal.h"

namespace graphics_backend
{
	class VKGPUBuffer : public GPUBuffer, public GPUResource, public VKAppSubObjectBaseNoCopy
	{
	public:
		VKGPUBuffer(CVulkanApplication& app) : VKAppSubObjectBaseNoCopy(app) {}

		constexpr VKBufferObject const& GetBuffer() const
		{
			return m_Buffer;
		}
		constexpr VKBufferObject& GetBuffer()
		{
			return m_Buffer;
		}
		virtual GPUBufferDescriptor const& GetDescriptor() const override { return m_Descriptor; }
		virtual void SetName(castl::string const& name) override { m_Name = name; }
		virtual castl::string const& GetName() const override { return m_Name; }
		bool Initialized() const
		{
			return m_Buffer.IsValid();
		}
		void SetBuffer(VKBufferObject const& buffer)
		{
			m_Buffer = buffer;
		}
		void SetDescriptor(GPUBufferDescriptor const& descriptor)
		{
			m_Descriptor = descriptor;
		}
	private:
		VKBufferObject m_Buffer = VKBufferObject::Default();
		GPUBufferDescriptor m_Descriptor = {};
		castl::string m_Name = { "" };
	};
}
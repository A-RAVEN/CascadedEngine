#include <GPUGraph/VulkanPassRWState.h>
#include <RenderBackend_Vulkan.h>

namespace graphics_backend
{
	void VulkanPassRWState::Init()
	{
		m_ImageStates.clear();
		m_BufferStates.clear();
		m_TrackedImages.clear();
		m_TrackedBuffers.clear();
		m_QueueTypes = EGPUQueueTypeFlags::None;
	}

	void VulkanPassRWState::Release()
	{
		m_ImageStates.clear();
		m_BufferStates.clear();
		m_TrackedImages.clear();
		m_TrackedBuffers.clear();
	}

	void VulkanPassRWState::SetImageState(ImageHandle const& image, EResourceState readState, EResourceState writeState)
	{
		auto& state = m_ImageStates[image];
		state.readState = readState;
		state.writeState = writeState;
		state.isRead = (readState != EResourceState::eUndefined);
		state.isWritten = (writeState != EResourceState::eUndefined);

		// Track for iteration
		if (castl::find(m_TrackedImages.begin(), m_TrackedImages.end(), image) == m_TrackedImages.end())
		{
			m_TrackedImages.push_back(image);
		}
	}

	void VulkanPassRWState::SetBufferState(BufferHandle const& buffer, EResourceState readState, EResourceState writeState)
	{
		auto& state = m_BufferStates[buffer];
		state.readState = readState;
		state.writeState = writeState;
		state.isRead = (readState != EResourceState::eUndefined);
		state.isWritten = (writeState != EResourceState::eUndefined);

		// Track for iteration
		if (castl::find(m_TrackedBuffers.begin(), m_TrackedBuffers.end(), buffer) == m_TrackedBuffers.end())
		{
			m_TrackedBuffers.push_back(buffer);
		}
	}

	PassResourceState const* VulkanPassRWState::GetImageState(ImageHandle const& image) const
	{
		auto it = m_ImageStates.find(image);
		if (it != m_ImageStates.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	PassResourceState const* VulkanPassRWState::GetBufferState(BufferHandle const& buffer) const
	{
		auto it = m_BufferStates.find(buffer);
		if (it != m_BufferStates.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	void VulkanPassRWState::Merge(VulkanPassRWState const& other)
	{
		for (auto const& [image, state] : other.m_ImageStates)
		{
			SetImageState(image, state.readState, state.writeState);
		}

		for (auto const& [buffer, state] : other.m_BufferStates)
		{
			SetBufferState(buffer, state.readState, state.writeState);
		}

		m_QueueTypes |= other.m_QueueTypes;
	}
}

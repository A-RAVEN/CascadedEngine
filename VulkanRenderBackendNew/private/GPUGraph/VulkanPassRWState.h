#pragma once
#include <Utils/VulkanSubobjectBase.h>
#include <CASTL/CAUnorderedMap.h>
#include <CASTL/CAVector.h>
#include <ShaderResourceHandle.h>
#include <Common.h>

namespace graphics_backend
{
	// Resource state tracking
	enum class EResourceState
	{
		eUndefined,
		eVertexBuffer,
		eIndexBuffer,
		eConstantBuffer,
		eShaderResource,
		eUnorderedAccess,
		eRenderTarget,
		eDepthWrite,
		eDepthRead,
		ePresent,
		eCopySrc,
		eCopyDst
	};

	// Per-pass resource read/write state
	struct PassResourceState
	{
		EResourceState readState = EResourceState::eUndefined;
		EResourceState writeState = EResourceState::eUndefined;
		bool isRead = false;
		bool isWritten = false;
	};

	// Track resource states per pass
	class VulkanPassRWState : public VulkanSubobjectBase
	{
	public:
		VulkanPassRWState() = default;
		~VulkanPassRWState() = default;

		void Init();
		virtual void Release() override;

		// Set resource state for a pass
		void SetImageState(ImageHandle const& image, EResourceState readState, EResourceState writeState);
		void SetBufferState(BufferHandle const& buffer, EResourceState readState, EResourceState writeState);

		// Get resource state
		PassResourceState const* GetImageState(ImageHandle const& image) const;
		PassResourceState const* GetBufferState(BufferHandle const& buffer) const;

		// Get all tracked resources
		castl::vector<ImageHandle> const& GetTrackedImages() const { return m_TrackedImages; }
		castl::vector<BufferHandle> const& GetTrackedBuffers() const { return m_TrackedBuffers; }

		// Queue type flags
		void SetQueueTypes(EGPUQueueTypeFlags flags) { m_QueueTypes = flags; }
		EGPUQueueTypeFlags GetQueueTypes() const { return m_QueueTypes; }

		// Merge states from another pass
		void Merge(VulkanPassRWState const& other);

	private:
		castl::unordered_map<ImageHandle, PassResourceState> m_ImageStates;
		castl::unordered_map<BufferHandle, PassResourceState> m_BufferStates;
		castl::vector<ImageHandle> m_TrackedImages;
		castl::vector<BufferHandle> m_TrackedBuffers;
		EGPUQueueTypeFlags m_QueueTypes = EGPUQueueType::eNone;
	};
}

#include "pch.h"
#include "VulkanApplication.h"
#include "ShaderConstantSet_Impl.h"

namespace graphics_backend
{
	using namespace thread_management;
	//align, size
	castl::pair<size_t, size_t> GetVectorAlignment(uint32_t vector_size)
	{
		switch (vector_size)
		{
		case 1:
			return castl::pair<size_t, size_t>{ 1, 1 };
		case 2:
			return castl::pair<size_t, size_t>{ 2, 2 };
		case 3:
			return castl::pair<size_t, size_t>{ 4, 3 };
		case 4:
			return castl::pair<size_t, size_t>{ 4, 4 };
		default:
			CA_LOG_ERR(castl::string("UnSupported Vector Size: %d", vector_size));
		}
		return { 0, 0 };
	}

	//Column Major
	castl::pair<size_t, size_t> GetMatrixAlignment(uint32_t column, uint32_t row)
	{
		return { 4, 4 * row };
	}

	ShaderConstantSet_Impl::ShaderConstantSet_Impl(CVulkanApplication& owner) :
		BaseTickingUpdateResource(owner)
	{
	}

	void ShaderConstantSet_Impl::SetValue(castl::string const& name, void* pValue)
	{
		auto& positions = p_Metadata->GetArithmeticValuePositions();
		auto found = positions.find(name);
		if (found == positions.end())
		{
			CA_LOG_ERR("shader variable " + name + " not found in constant buffer");
			return;
		}
		size_t currentSizeInbytes = m_UploadData.size();
		size_t requiredSizeInBytes = sizeof(uint32_t) * (found->second.first + found->second.second);
		if (requiredSizeInBytes > currentSizeInbytes)
		{
			m_UploadData.resize(requiredSizeInBytes);
		}
		memcpy(&m_UploadData[sizeof(uint32_t) * found->second.first]
			, pValue
			, sizeof(uint32_t) * found->second.second);
		MarkDirtyThisFrame();
	}

	void ShaderConstantSet_Impl::Initialize(ShaderConstantSetMetadata const* inMetaData)
	{
		p_Metadata = inMetaData;
	}

	castl::string const& ShaderConstantSet_Impl::GetName() const
	{
		return p_Metadata->GetBuilder()->GetName();
	}

	void ShaderConstantSet_Impl::TickUpload()
	{
		CVulkanMemoryManager& memoryManager = GetMemoryManager();
		auto threadContext = GetVulkanApplication().AquireThreadContextPtr();
		auto currentFrame = GetFrameCountContext().GetCurrentFrameID();

		size_t bufferSize = p_Metadata->GetTotalSize() * sizeof(uint32_t);
		if (!m_BufferObject.IsRAIIAquired())
		{
			m_BufferObject = memoryManager.AllocateBuffer(
				EMemoryType::GPU
				, EMemoryLifetime::Persistent
				, bufferSize
				, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst);
		}
		auto tempBuffer = memoryManager.AllocateFrameBoundTransferStagingBuffer(bufferSize);
		memcpy(tempBuffer->GetMappedPointer(), m_UploadData.data(), bufferSize);

		auto cmdBuffer = threadContext->GetCurrentFramePool().AllocateMiscCommandBuffer("Upload Shader Constant Buffer");
		cmdBuffer.copyBuffer(tempBuffer->GetBuffer(), m_BufferObject->GetBuffer(), vk::BufferCopy(0, 0, bufferSize));
		cmdBuffer.end();
		castl::atomic_thread_fence(castl::memory_order_release);
		MarkUploadingDoneThisFrame();
	}

	void ShaderConstantSet_Impl::Release()
	{
		m_BufferObject.RAIIRelease();
	}

	size_t AccumulateOnOffset(size_t& inoutOffset, castl::pair<size_t, size_t> const& inAlign_Size)
	{
		size_t next_offset = (inoutOffset + inAlign_Size.first - 1) / inAlign_Size.first * inAlign_Size.first;
		inoutOffset = next_offset + inAlign_Size.second;
		return next_offset;
	}

	void ShaderConstantSetMetadata::Initialize(ShaderConstantsBuilder const& builder)
	{
		p_Builder = &builder;
		auto& descriptors = builder.GetNumericDescriptors();
		m_ArithmeticValuePositions.clear();
		m_TotalSize = 0;
		for (auto& descPairs : descriptors)
		{
			auto& desc = descPairs.second;
			auto& descName = descPairs.first;
			uint32_t count = desc.count;
			CA_ASSERT(count > 0
				, "descriptor must have at least one element");
			CA_ASSERT(m_ArithmeticValuePositions.find(descName) == m_ArithmeticValuePositions.end()
				, "descriptor must have at least one element");
			castl::pair<size_t, size_t>align_size;
			if (desc.y == 1)
			{
				uint32_t vectorSize = desc.x * desc.y;
				if (count == 1)
				{
					align_size = GetVectorAlignment(vectorSize);
				}
				else
				{
					align_size = castl::make_pair(4, 4 * count);
				}
			}
			else
			{
				//Matrix always align at 4
				uint32_t vectorSize = desc.x;
				align_size = castl::make_pair(4, 4 * count * desc.y);
			}

			CA_ASSERT(align_size.first > 0 && align_size.second > 0
				, "invalid align size");
			size_t descOffset = AccumulateOnOffset(m_TotalSize, align_size);
			m_ArithmeticValuePositions.emplace(descName, castl::make_pair(descOffset, align_size.second));
		}
	}

	castl::unordered_map<castl::string, castl::pair<size_t, size_t>> const& ShaderConstantSetMetadata::GetArithmeticValuePositions() const
	{
		return m_ArithmeticValuePositions;
	}

	size_t ShaderConstantSetMetadata::GetTotalSize() const
	{
		return m_TotalSize;
	}

	ShaderConstantSetAllocator::ShaderConstantSetAllocator(CVulkanApplication& owner) :
		VKAppSubObjectBaseNoCopy(owner)
		, m_ShaderConstantSetPool(owner)
	{
	}

	void ShaderConstantSetAllocator::Create(ShaderConstantsBuilder const& builder)
	{
		m_ShaderConstantSetPool.ReleaseAll();
		m_Metadata.Initialize(builder);
	}

	castl::shared_ptr<ShaderConstantSet> ShaderConstantSetAllocator::AllocateSet()
	{
		return m_ShaderConstantSetPool.AllocShared(&m_Metadata);
	}

	void ShaderConstantSetAllocator::Release()
	{
		m_ShaderConstantSetPool.ReleaseAll();
	}
	void ShaderConstantSetAllocator::TickUploadResources(CTaskGraph* pTaskGraph)
	{
		m_ShaderConstantSetPool.TickUpload(pTaskGraph);
	}
}
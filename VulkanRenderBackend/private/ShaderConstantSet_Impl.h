#pragma once
#include <Platform.h>
#include <ShaderBindingSet.h>
#include <ShaderBindingBuilder.h>
#include <ThreadManager.h>
#include <CASTL/CAUnorderedMap.h>
#include "TickUploadingResource.h"
//#include "CVulkanBufferObject.h"

namespace graphics_backend
{
	class ShaderConstantSetMetadata;
	class ShaderConstantSetAllocator;
	class ShaderConstantSet_Impl : public BaseTickingUpdateResource, public ShaderConstantSet
	{
	public:
		ShaderConstantSet_Impl(CVulkanApplication& owner);
		// 通过 ShaderBindingSet 继承
		virtual void SetValue(castl::string const& name, void* pValue) override;
		void Initialize(ShaderConstantSetMetadata const* inMetaData);
		virtual castl::string const& GetName() const override;
		//VulkanBufferHandle const& GetBufferObject() const { return m_BufferObject; }
		virtual void TickUpload() override;
		void Release();
	private:
		ShaderConstantSetMetadata const* p_Metadata;
		castl::vector<uint8_t> m_UploadData;
		//VulkanBufferHandle m_BufferObject;
	};

	class ShaderConstantSetMetadata
	{
	public:
		void Initialize(ShaderConstantsBuilder const& builder);
		castl::unordered_map<castl::string, castl::pair<size_t, size_t>> const& GetArithmeticValuePositions() const;
		size_t GetTotalSize() const;
		ShaderConstantsBuilder const* GetBuilder() const { return p_Builder; }
	private:
		ShaderConstantsBuilder const* p_Builder;
		size_t m_TotalSize = 0;
		castl::unordered_map<castl::string, castl::pair<size_t, size_t>> m_ArithmeticValuePositions;
	};

	class ShaderConstantSetAllocator : public VKAppSubObjectBaseNoCopy
	{
	public:
		ShaderConstantSetAllocator(CVulkanApplication& owner);
		void Create(ShaderConstantsBuilder const& builder);
		castl::shared_ptr<ShaderConstantSet> AllocateSet();
		void Release();
		void TickUploadResources(thread_management::CTaskGraph* pTaskGraph);
		ShaderConstantSetMetadata const& GetMetadata() const { return m_Metadata; }
	private:
		ShaderConstantSetMetadata m_Metadata;
		TTickingUpdateResourcePool<ShaderConstantSet_Impl> m_ShaderConstantSetPool;
	};
}
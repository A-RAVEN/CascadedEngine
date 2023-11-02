#pragma once
#include <RenderInterface/header/ShaderBindingSet.h>
#include <RenderInterface/header/ShaderBindingBuilder.h>
#include <ThreadManager/header/ThreadManager.h>
#include "framework.h"
#include "TickUploadingResource.h"
#include "CVulkanBufferObject.h"

namespace graphics_backend
{
	class ShaderConstantSetMetadata;
	class ShaderConstantSetAllocator;
	class ShaderConstantSet_Impl : public BaseTickingUpdateResource, public ShaderConstantSet
	{
	public:
		ShaderConstantSet_Impl(CVulkanApplication& owner);
		// 通过 ShaderBindingSet 继承
		virtual void SetValue(std::string const& name, void* pValue) override;
		void Initialize(ShaderConstantSetMetadata const* inMetaData);
		virtual std::string const& GetName() const override;
		VulkanBufferHandle const& GetBufferObject() const { return m_BufferObject; }
		virtual void TickUpload() override;
		virtual void Release() override;
	private:
		ShaderConstantSetMetadata const* p_Metadata;
		std::vector<uint8_t> m_UploadData;
		VulkanBufferHandle m_BufferObject;
	};

	class ShaderConstantSetMetadata
	{
	public:
		void Initialize(ShaderConstantsBuilder const& builder);
		std::unordered_map<std::string, std::pair<size_t, size_t>> const& GetArithmeticValuePositions() const;
		size_t GetTotalSize() const;
		ShaderConstantsBuilder const* GetBuilder() const { return p_Builder; }
	private:
		ShaderConstantsBuilder const* p_Builder;
		size_t m_TotalSize = 0;
		std::unordered_map<std::string, std::pair<size_t, size_t>> m_ArithmeticValuePositions;
	};

	class ShaderConstantSetAllocator : public BaseApplicationSubobject
	{
	public:
		ShaderConstantSetAllocator(CVulkanApplication& owner);
		void Create(ShaderConstantsBuilder const& builder);
		std::shared_ptr<ShaderConstantSet> AllocateSet();
		virtual void Release() override;
		void TickUploadResources(thread_management::CTaskGraph* pTaskGraph);
	private:
		ShaderConstantSetMetadata m_Metadata;
		TTickingUpdateResourcePool<ShaderConstantSet_Impl> m_ShaderConstantSetPool;
	};
}
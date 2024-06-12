#pragma once
#include <CPipelineStateObject.h>
#include <VulkanApplicationSubobjectBase.h>
#include <VulkanIncludes.h>
#include <CShaderModuleObject.h>
#include <Compiler.h>
#include <Hasher.h>

namespace graphics_backend
{
	struct ComputePipelineDescriptor
	{
	public:
		castl::shared_ptr<CShaderModuleObject> computeShader = nullptr;
		castl::vector<vk::DescriptorSetLayout> descriptorSetLayouts{};
		auto operator<=>(const ComputePipelineDescriptor&) const = default;
	};

	class ComputePipelineObject final : public VKAppSubObjectBaseNoCopy
	{
	public:
		ComputePipelineObject(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner) {};
		void Create(ComputePipelineDescriptor const& computeshaderModule);
		vk::Pipeline const& GetPipeline() const { return m_Pipeline; }
		vk::PipelineLayout const& GetPipelineLayout() const { return m_PipelineLayout; }
	protected:
		vk::Pipeline m_Pipeline = nullptr;
		vk::PipelineLayout m_PipelineLayout = nullptr;
	};

	using ComputePipelineObjectDic = HashPool<ComputePipelineDescriptor, ComputePipelineObject>;
}
#pragma once
#include <CVertexInputDescriptor.h>
#include <CPipelineStateObject.h>
#include "VulkanApplicationSubobjectBase.h"
#include "VulkanIncludes.h"
#include "CShaderModuleObject.h"
#include "RenderPassObject.h"
#include <Compiler.h>
#include <Hasher.h>

namespace graphics_backend
{
	struct ShaderStateDescriptor
	{
	public:
		castl::shared_ptr<CShaderModuleObject> vertexShader = nullptr;
		castl::shared_ptr <CShaderModuleObject> fragmentShader = nullptr;
		auto operator<=>(const ShaderStateDescriptor&) const = default;
	};

	struct CPipelineObjectDescriptor
	{
		CPipelineStateObject pso{};
		CVertexInputDescriptor vertexInputs{};
		ShaderStateDescriptor shaderState{};
		//TODO Wrap ME
		castl::vector<vk::DescriptorSetLayout> descriptorSetLayouts{};
		castl::shared_ptr<RenderPassObject> renderPassObject = nullptr;
		uint32_t subpassIndex = 0;
		auto operator<=>(const CPipelineObjectDescriptor&) const = default;
	};

	class CPipelineObject final : public VKAppSubObjectBaseNoCopy
	{
	public:
		CPipelineObject(CVulkanApplication& owner) : VKAppSubObjectBaseNoCopy(owner) {};
		void Create(CPipelineObjectDescriptor const& pipelineObjectDescriptor);
		vk::Pipeline const& GetPipeline() const { return m_GraphicsPipeline; }
		vk::PipelineLayout const& GetPipelineLayout() const { return m_PipelineLayout; }
	protected:
		vk::Pipeline m_GraphicsPipeline = nullptr;
		vk::PipelineLayout m_PipelineLayout = nullptr;
	};

	using PipelineObjectDic = HashPool<CPipelineObjectDescriptor, CPipelineObject>;
}

CA_REFLECTION(graphics_backend::ShaderStateDescriptor
	, vertexShader
	, fragmentShader);

CA_REFLECTION(graphics_backend::CPipelineObjectDescriptor
	, pso
	, vertexInputs
	, shaderState
	, descriptorSetLayouts
	, renderPassObject
	, subpassIndex);

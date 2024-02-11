#pragma once
#include <CVertexInputDescriptor.h>
#include <CPipelineStateObject.h>
#include "VulkanApplicationSubobjectBase.h"
#include "VulkanIncludes.h"
#include "CShaderModuleObject.h"
#include "RenderPassObject.h"

template<>
struct hash_utils::is_contiguously_hashable<vk::DescriptorSetLayout> : public castl::true_type {};

namespace graphics_backend
{
	struct ShaderStateDescriptor
	{
	public:
		castl::shared_ptr<CShaderModuleObject> vertexShader = nullptr;
		castl::shared_ptr <CShaderModuleObject> fragmentShader = nullptr;

		bool operator==(ShaderStateDescriptor const& rhs) const
		{
			return vertexShader == rhs.vertexShader
				&& fragmentShader == rhs.fragmentShader;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, ShaderStateDescriptor const& shaderstate_desc) noexcept
		{
			hash_append(h, reinterpret_cast<size_t>(shaderstate_desc.vertexShader.get()));
			hash_append(h, reinterpret_cast<size_t>(shaderstate_desc.fragmentShader.get()));
		}
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

		bool operator==(CPipelineObjectDescriptor const& rhs) const
		{
			return pso == rhs.pso
				&& vertexInputs == rhs.vertexInputs
				&& shaderState == rhs.shaderState
				&& renderPassObject == rhs.renderPassObject
				&& subpassIndex == rhs.subpassIndex;
		}

		template <class HashAlgorithm>
		friend void hash_append(HashAlgorithm& h, CPipelineObjectDescriptor const& pipeline_desc) noexcept
		{
			hash_append(h, pipeline_desc.pso);
			hash_append(h, pipeline_desc.vertexInputs);
			hash_append(h, pipeline_desc.shaderState);
			hash_append(h, pipeline_desc.descriptorSetLayouts);
			hash_append(h, pipeline_desc.renderPassObject.get());
			hash_append(h, pipeline_desc.subpassIndex);
		}
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

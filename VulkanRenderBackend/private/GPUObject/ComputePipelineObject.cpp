#include "ComputePipelineObject.h"

namespace graphics_backend
{
	void ComputePipelineObject::Create(ComputePipelineDescriptor const& computeshaderModule)
	{
		vk::PipelineLayoutCreateInfo layoutCreateInfo{ {}, computeshaderModule.descriptorSetLayouts, {} };
		m_PipelineLayout = GetDevice().createPipelineLayout(layoutCreateInfo);

		vk::PipelineShaderStageCreateInfo computeShaderStageInfo{};
		computeShaderStageInfo.flags = vk::PipelineShaderStageCreateFlags{};
		computeShaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
		computeShaderStageInfo.module = computeshaderModule.computeShader->GetShaderModule();
		computeShaderStageInfo.pName = computeshaderModule.computeShader->GetEntryPointName().c_str();

		vk::ComputePipelineCreateInfo pipelineCreateInfo{};
		pipelineCreateInfo.stage = computeShaderStageInfo;
		pipelineCreateInfo.layout = m_PipelineLayout;
		pipelineCreateInfo.flags = vk::PipelineCreateFlags{};

		m_Pipeline = GetDevice().createComputePipeline(nullptr, pipelineCreateInfo).value;
	}
	void ComputePipelineObject::Release()
	{
		GetDevice().destroyPipelineLayout(m_PipelineLayout);
		GetDevice().destroyPipeline(m_Pipeline);
	}
}
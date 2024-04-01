#include <ShaderDescriptorSetAllocator.h>
#include "ShaderArgList.h"
#include <Compiler.h>
#include <CVulkanBufferObject.h>

namespace graphics_backend
{
	class ShaderBindingInstance
	{
	public:
		void InitShaderBindings(CVulkanApplication& application, ShaderCompilerSlang::ShaderReflectionData const& reflectionData);
		void WriteShaderData(ShaderArgList const& shaderArgList);
		castl::vector<ShaderDescriptorSetHandle> m_DescriptorSets;
		castl::vector<VulkanBufferHandle> m_UniformBuffers;
		ShaderCompilerSlang::ShaderReflectionData const* p_ReflectionData;
	};
}
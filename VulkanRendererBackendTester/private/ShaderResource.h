#pragma once
#include <GeneralResources/header/IResource.h>
#include <GeneralResources/header/ResourceImporter.h>
#include "TestShaderProvider.h"

namespace resource_management
{
	class ShaderResrouce : public IResource
	{
	public:

	private:
		TestShaderProvider m_VertexShaderProvider;
		TestShaderProvider m_FragmentShaderProvider;
	};

	class ShaderResourceLoader : public ResourceImporter<ShaderResrouce>
	{

	};
}
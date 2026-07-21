#pragma once
#include <CAResource/ResourceImporter.h>
#include <CASTL/CAString.h>
#include <CASTL/CAVector.h>
#include <CACore/CAHash.h>
#include <Compiler.h>
#include <ShaderLibrary/ShaderLibrary.h>

namespace resource_management
{
	class ResourceManagingSystem;
}

namespace graphics_backend
{

	// Task 3.1: Hierarchy element for IterateHierarchyElements helper
	struct VulkanHierarchyElement
	{
		ShaderCompilerSlang::ShaderBindingHierarchy const* pHierarchy;
		uint32_t hierarchyID;
		uint32_t offset;
	};

	// Task 3.1: IterateHierarchyElements helper function (references D3D12 implementation)
	void IterateHierarchyElements(
		ShaderCompilerSlang::ShaderReflectionData const* reflectionData,
		castl::function<void(VulkanHierarchyElement const&)> hierarchyElementCallback);

	// Task 3.2: Construct VulkanShaderResourceBindingInfo from reflection data
	VulkanShaderResourceBindingInfo ConstructShaderDescriptorInfo(
		const char* pathName,
		ShaderCompilerSlang::ShaderReflectionData const& shaderReflectionData);

	// Task 4.1: Changed base class from VulkanSubobjectBase to ResourceImporterFree
	class ShaderImporter_Vulkan : public ::resource_management::ResourceImporterFree
	{
	public:
		ShaderImporter_Vulkan() = default;
		~ShaderImporter_Vulkan() = default;

		// Task 4.2: GetTags method returning "Vulkan;Slang"
		virtual castl::string GetTags() const override { return "Vulkan;Slang"; }

		// Task 4.3: ImportResource method (main entry point following D3D12 pattern)
		virtual void ImportResource(
			::resource_management::ResourceManagingSystem* resourceManager,
			cafs::path const& sourcePath,
			cafs::path const& destPath) override;

		// Set compiler manager (following D3D12ShaderResourceImporter pattern)
		void SetCompiler(ShaderCompilerSlang::IShaderCompilerManager* compiler);

		// Set fallback source directory (Decision 2: bypasses CWD-dependent ScanSourceDirectory path)
		void SetSourceDirectory(cafs::path const& path) { m_SourceDirectory = path; }

		// Debug/test method
		void Test();

	private:
		// IShaderCompilerManager instance
		ShaderCompilerSlang::IShaderCompilerManager* m_ShaderCompilerManager = nullptr;

		// Fallback source directory for .slang scanning (Decision 2+3)
		// When ScanSourceDirectory passes a non-existent path (e.g., due to CWD-depth bug),
		// ImportResource falls back to this directory if configured.
		// Empty = no fallback; importer depends entirely on ScanSourceDirectory's sourcePath.
		cafs::path m_SourceDirectory;
	};
}

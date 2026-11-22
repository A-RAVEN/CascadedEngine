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

	// 目录级导入器（v3 拉取式，resource-importer-refactor）：扫 .slang → 编译 → 交出一条产出
	// （`VulkanShaderLibrary.shLib`）。**只产对象、不注册**：注册 / 替换 / 落盘在
	// `ResourceImportingSystem::ScanSourceDirectory` 里做。
	class ShaderImporter_Vulkan : public ::resource_management::ResourceImporter
	{
	public:
		ShaderImporter_Vulkan() = default;
		~ShaderImporter_Vulkan() = default;

		virtual castl::string GetTags() const override { return "Vulkan;Slang"; }

		// 空 = 跟随 ScanSourceDirectory 传入的根（回退目录另存 m_SourceDirectory，见下）。
		virtual castl::string GetSourceDirectory() const override { return castl::string(); }
		virtual castl::string GetDestDirectory() const override { return castl::string(); }

		// 扫源 → 编译 → 单条产出；没有可编的源时返回空迭代器（HasNext 恒假）。
		virtual ::resource_management::ResourceImporterProductions* Produce() override;

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
		// 管线传入的源根（`GetScanSourceDirectory()`）不存在时，Produce 回退到这个目录（如果配了）。
		// 空 = 没有回退；导入器完全依赖 ScanSourceDirectory 传入的根。
		cafs::path m_SourceDirectory;
	};
}

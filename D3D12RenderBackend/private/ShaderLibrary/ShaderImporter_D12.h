#pragma once
#include <CAResource/ResourceImporter.h>
#include <Compiler.h>
#include <library_loader.h>
//#include <Resources/ShaderResource.h>

namespace graphics_backend
{
	using namespace library_loader;
	using namespace resource_management;

	// 目录级导入器（v3 拉取式，resource-importer-refactor）：扫 .slang → 编译 → 一条产出
	// （`D3D12ShaderLibrary.shLib`）。只产对象、不注册：注册 / 替换 / 落盘在管线里做。
	class D3D12ShaderResourceImporter : public ResourceImporter
	{
	public:
		D3D12ShaderResourceImporter()// : m_ShaderCompilerLoader("ShaderCompilerSlang")
		{
			//m_ShaderCompilerManager = m_ShaderCompilerLoader.New();
			//m_ShaderCompilerManager->InitializePoolSize(1);
		}
		void SetCompiler(ShaderCompilerSlang::IShaderCompilerManager* compiler)
		{
			m_ShaderCompilerManager = compiler;
		}
		virtual castl::string GetTags() const override { return "D3D12;Slang"; }
		// 空 = 跟随 ScanSourceDirectory 传入的根（D3D12 没有额外回退目录）。
		virtual castl::string GetSourceDirectory() const override { return castl::string(); }
		virtual castl::string GetDestDirectory() const override { return castl::string(); }
		// 扫源 → 编译 → 单条产出；没有可编的源时返回空迭代器（HasNext 恒假）。
		virtual resource_management::ResourceImporterProductions* Produce() override;
		void Test();
	private:
		ShaderCompilerSlang::IShaderCompilerManager* m_ShaderCompilerManager = nullptr;
	};
}

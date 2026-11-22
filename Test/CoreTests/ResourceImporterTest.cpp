// resource-importer-refactor 4.1：导入器退役 / 解耦的断言（C++ 源码范围 + 编译期签名）。
//
// 三组判据，每组都能单独变红：
//   ① 退役：**C++ 源码范围**（GeneralResources/header + private、两个后端的 ShaderLibrary 目录）
//      里 MUST 没有任何旧 1:1 导入器抽象、它的模板、后缀扫描成员与旧入口名；模板判据**折行感知**
//      （`template` 与 `class <名字>` 之间只隔空白也算命中），不用 `ResourceImporter<` 这种 0 命中
//      的写法。范围 MUST NOT 用"全仓"：本文件自己与 change 工件里出现这些名字是合法的。
//   ② 解耦：两个着色器导入器的源码里 MUST 没有五个写注册表入口（导入器只产对象、不注册）。
//
//      （五个入口：RegisterResource / ReleaseResource / CreateResource / GetOrLoadResource /
//       GetOrNewResource。管线那边当然有它们 —— 这里查的是**导入器**。）
//   ③ 形状：新接口的公开签名按写死期望逐条对（编译期取成员函数指针），拉取式迭代器的最小行为
//      （先问 HasNext 再取一条、取完即空）与扫描上下文（管线写的根能读回来）各跑一遍。
#include <CAResource/ResourceImporter.h>
#include <CAResource/ResourceImportingSystem.h>
#include <CAResource/SingleResourceProductions.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
	// 源码范围（相对仓库根）：与 design D1 的口径逐字相同。
	constexpr char const* kSourceRoots[] = {
		"GeneralResources/header",
		"GeneralResources/private",
		"VulkanRenderBackendNew/private/ShaderLibrary",
		"D3D12RenderBackend/private/ShaderLibrary",
	};

	// 退役符号（逐条 = 一处旧形态）；`template` 另按折行感知判。
	constexpr char const* kRetiredTokens[] = {
		"ResourceImporterBase",
		"ResourceImporterFree",
		"GetSourceFilePostfix",
		"GetDestFilePostfix",
		"m_PostfixToImporterIndex",
		"m_Importers",
		"m_ReservedSpace",
		"m_ImportingResources",
		"m_GeneralImporters",
		"ImportResource(",
	};

	// 导入器源码里 MUST 0 的五个写注册表入口。
	constexpr char const* kRegistryEntryTokens[] = {
		"RegisterResource",
		"ReleaseResource",
		"CreateResource",
		"GetOrLoadResource",
		"GetOrNewResource",
	};

	constexpr char const* kShaderImporterSources[] = {
		"VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.h",
		"VulkanRenderBackendNew/private/ShaderLibrary/ShaderImporter_Vulkan.cpp",
		"D3D12RenderBackend/private/ShaderLibrary/ShaderImporter_D12.h",
		"D3D12RenderBackend/private/ShaderLibrary/ShaderImporter_D12.cpp",
	};

	std::filesystem::path SourcePath(std::string const& relative)
	{
		return std::filesystem::path(CASCADED_SOURCE_DIR) / relative;
	}

	bool ReadSourceText(std::filesystem::path const& path, std::string* text)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			return false;
		}
		text->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		return true;
	}

	// 折行感知的规范形：换行 / 制表 / 连续空白压成一个空格。
	std::string CollapseWhitespace(std::string const& text)
	{
		std::string collapsed;
		collapsed.reserve(text.size());
		bool lastWasSpace = false;
		for (char character : text)
		{
			const bool space = character == ' ' || character == '\t' || character == '\n' || character == '\r';
			if (space)
			{
				if (!lastWasSpace && !collapsed.empty())
				{
					collapsed.push_back(' ');
				}
				lastWasSpace = true;
			}
			else
			{
				collapsed.push_back(character);
				lastWasSpace = false;
			}
		}
		return collapsed;
	}

	// `template` 与 `class <名字>` 之间只隔空白也算命中（折行感知）。
	// 只看同一条声明：往回找最近的 `;` / `{` / `}` 当边界，边界到 `class` 之间出现 template 即命中。
	bool HasTemplateClassDeclaration(std::string const& collapsed, char const* className)
	{
		const std::string needle = std::string("class ") + className;
		std::size_t at = collapsed.find(needle);
		while (at != std::string::npos)
		{
			const std::size_t after = at + needle.size();
			const bool boundary = after >= collapsed.size() || collapsed[after] == ' ' ||
			                      collapsed[after] == ':' || collapsed[after] == '{' ||
			                      collapsed[after] == ';';
			if (boundary)
			{
				const std::size_t windowStart = at > 240u ? at - 240u : 0u;
				const std::string window = collapsed.substr(windowStart, at - windowStart);
				const std::size_t separator = window.find_last_of(";{}");
				const std::string head = separator == std::string::npos ? window : window.substr(separator + 1u);
				if (head.find("template") != std::string::npos)
				{
					return true;
				}
			}
			at = collapsed.find(needle, at + 1u);
		}
		return false;
	}

	// ③ 的最小行为素材：一个最简资源（只为了证明"导出的一条产出"真的能被装配 / 取走）。
	class FakeProducedResource final : public resource_management::IResource
	{
	public:
		void Serialize(ca_io::WBatch* writer) override
		{
			writer->SubmitAndWait();
		}
		void Deserialize(ca_io::IOBatch* reader) override
		{
			reader->SubmitAndWait();
		}
	};

	std::size_t g_failures = 0;

	void Fail(char const* code, std::string const& detail)
	{
		++g_failures;
		std::printf("  [导入器断言失败] %s：%s\n", code, detail.c_str());
	}

	void CheckContainsNone(char const* code, std::string const& text, char const* token,
	                       std::string const& where)
	{
		if (text.find(token) != std::string::npos)
		{
			Fail(code, std::string("退役符号 \"") + token + "\" 仍在 " + where);
		}
	}

	void CheckRegistryEntries(char const* code, std::string const& text, char const* token,
	                          std::string const& where)
	{
		if (text.find(token) != std::string::npos)
		{
			Fail(code, std::string("导入器源码出现了写注册表入口 \"") + token + "\"（导入器只产对象、不注册）：" +
				where);
		}
	}
}

bool RunResourceImporterTest()
{
	g_failures = 0;

	// ---- ① 退役：C++ 源码范围零命中（含折行感知的模板判据）----
	// 逐个文件读，不做全仓扫描；文件清单按目录递归枚举，新加进这些目录的文件自动纳入。
	std::size_t scannedFiles = 0;
	for (char const* root : kSourceRoots)
	{
		const std::filesystem::path rootPath = SourcePath(root);
		std::error_code error;
		if (!std::filesystem::is_directory(rootPath, error))
		{
			Fail("E_IMPORTER_SCOPE_MISSING", std::string("源码范围目录不存在：") + root);
			continue;
		}
		for (std::filesystem::recursive_directory_iterator it(rootPath, error), end; it != end; it.increment(error))
		{
			if (error || !it->is_regular_file())
			{
				continue;
			}
			const std::string extension = it->path().extension().string();
			if (extension != ".h" && extension != ".hpp" && extension != ".cpp" && extension != ".inl")
			{
				continue;
			}
			std::string text;
			if (!ReadSourceText(it->path(), &text))
			{
				Fail("E_IMPORTER_SCOPE_READ", "源码读不出来：" + it->path().string());
				continue;
			}
			++scannedFiles;
			const std::string where = it->path().string();
			for (char const* token : kRetiredTokens)
			{
				CheckContainsNone("E_IMPORTER_RETIRED", text, token, where);
			}
			const std::string collapsed = CollapseWhitespace(text);
			if (HasTemplateClassDeclaration(collapsed, "ResourceImporter"))
			{
				Fail("E_IMPORTER_TEMPLATE", "模板形态的 class ResourceImporter 仍在：" + where);
			}
		}
	}
	if (scannedFiles == 0)
	{
		// 判据自身失效要能被看见（0 个文件被扫 = 恒真）。
		Fail("E_IMPORTER_SCOPE_EMPTY", "源码范围里一个 C++ 文件都没扫到 —— 判据自身失效");
	}

	// ---- ② 解耦：两个着色器导入器不许碰注册表 ----
	for (char const* relative : kShaderImporterSources)
	{
		std::string text;
		if (!ReadSourceText(SourcePath(relative), &text))
		{
			Fail("E_IMPORTER_IMPORTER_MISSING", std::string("导入器源码不存在：") + relative);
			continue;
		}
		for (char const* token : kRegistryEntryTokens)
		{
			CheckRegistryEntries("E_IMPORTER_REGISTERS", text, token, relative);
		}
	}

	// ---- ③ 形状（编译期）：公开签名逐条对，写死期望 ----
	static_assert(std::is_abstract_v<resource_management::ResourceImporter>,
	              "ResourceImporter 必须是反向接口（抽象类）");
	static_assert(std::is_abstract_v<resource_management::ResourceImporterProductions>,
	              "ResourceImporterProductions 必须是反向接口（抽象类）");
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporter::GetTags),
	              castl::string (resource_management::ResourceImporter::*)() const>);
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporter::GetSourceDirectory),
	              castl::string (resource_management::ResourceImporter::*)() const>);
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporter::GetDestDirectory),
	              castl::string (resource_management::ResourceImporter::*)() const>);
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporter::Produce),
	              resource_management::ResourceImporterProductions* (
	                  resource_management::ResourceImporter::*)()>);
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporterProductions::HasNext),
	              bool (resource_management::ResourceImporterProductions::*)()>);
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporterProductions::NextPath),
	              castl::string (resource_management::ResourceImporterProductions::*)()>);
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImporterProductions::NextResource),
	              resource_management::IResource* (
	                  resource_management::ResourceImporterProductions::*)()>);
	// 1.2 的验收：AddImporter **只**收 ResourceImporter*。同名重载一旦多出来，`&Class::AddImporter`
	// 当场变歧义 —— 这条断言本身就是"只有一个重载"的编译期证明。
	static_assert(std::is_same_v<decltype(&resource_management::ResourceImportingSystem::AddImporter),
	              void (resource_management::ResourceImportingSystem::*)(resource_management::ResourceImporter*)>,
	              "AddImporter 必须只有一个重载且只收 ResourceImporter*");

	// ---- ③ 行为：单条迭代器"先问再取、取完即空"；扫描上下文的根能写能读 ----
	{
		resource_management::SingleResourceProductions productions("Probe/Single.res", new FakeProducedResource());
		if (!productions.HasNext())
		{
			Fail("E_IMPORTER_ITER_FIRST", "单条迭代器第一次 HasNext 应当为真");
		}
		if (productions.NextPath() != "Probe/Single.res")
		{
			Fail("E_IMPORTER_ITER_PATH", "迭代器交出的路径与构造时给的不一致");
		}
		resource_management::IResource* first = productions.NextResource();
		if (first == nullptr)
		{
			Fail("E_IMPORTER_ITER_RESOURCE", "迭代器第一次 NextResource 返回空");
		}
		if (productions.HasNext())
		{
			Fail("E_IMPORTER_ITER_SECOND", "单条迭代器取完之后 HasNext 仍为真（会交出同一条两次）");
		}
		delete first;
	}
	{
		// 接口能被 C++ 实现（原生导入器的形态），且管线写的根真的落在对象自己的存储里。
		class FakeImporter final : public resource_management::ResourceImporter
		{
		public:
			castl::string GetTags() const override { return "Fake"; }
			castl::string GetSourceDirectory() const override { return castl::string(); }
			castl::string GetDestDirectory() const override { return castl::string(); }
			resource_management::ResourceImporterProductions* Produce() override
			{
				return new resource_management::SingleResourceProductions("Probe/FromFake.res",
					new FakeProducedResource());
			}
		};
		FakeImporter importer;
		importer.SetScanDirectories("scan-source", "scan-dest");
		if (importer.GetScanSourceDirectory() != "scan-source" ||
			importer.GetScanDestDirectory() != "scan-dest")
		{
			Fail("E_IMPORTER_SCAN_CONTEXT", "扫描上下文写进去的根读不回来");
		}
		resource_management::ResourceImporterProductions* productions = importer.Produce();
		if (productions == nullptr || !productions->HasNext())
		{
			Fail("E_IMPORTER_FAKE_PRODUCE", "C++ 实现的导入器没有交出产出");
		}
		if (productions != nullptr)
		{
			resource_management::IResource* produced = productions->NextResource();
			delete produced;
			delete productions;
		}
	}

	if (g_failures != 0)
	{
		std::printf("Resource importer 断言：%zu 处失败（扫描 %zu 个 C++ 源码文件）\n", g_failures, scannedFiles);
		return false;
	}
	std::printf("Resource importer 断言：退役 / 解耦 / 形状 全部成立（扫描 %zu 个 C++ 源码文件）\n",
	            scannedFiles);
	return true;
}

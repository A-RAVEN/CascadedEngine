// 渲染基础形状的原生行为判据 + 生成期拒绝探针（csharp-render-binding-foundations 任务 1.3）。
//
// 判据分两半：
//   ① 原生行为：用固定非零字段逐项核对实现收到的记录、枚举、掩码、名称、规范化路径、
//      哈希对象所含值、字节内容与其他混合参数；空数组合法、空指针配非零长度在 thunk 入口
//      被拒绝且实现调用次数为 0；强返回句柄由发行者入口解析、配对释放后旧句柄失效。
//   ② 生成期拒绝：`BindableShapes.RenderReject*.h` 每个负向夹具单独起一次 CodeGen.exe，
//      断言非零退出并点名对应类型 / 成员 / 依赖。
#include "BindableShapes.RenderFoundation.h"

#include "ca_bind_fixture__IBindFoundation.Bindings.cpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>
#include <windows.h>

// `RenderDescC` 是生成的 C 镜像：字段的真实宽度在这里钉死。把窄枚举统一拉宽成 int32_t、
// 把 uint16_t 掩码写成 int、或把 bool 写成别的类型，都会让这些 static_assert 当场红。
static_assert(std::is_same_v<decltype(RenderDescC::visible), bool>,
	"公开记录里的 bool 字段在 C 镜像里必须是一个字节的 bool");
static_assert(std::is_same_v<decltype(RenderDescC::mode), uint16_t>,
	"记录字段里的普通枚举必须按真实底层类型 uint16_t 镜像");
static_assert(std::is_same_v<decltype(RenderDescC::flags), uint16_t>,
	"记录字段里的 EnumFlags 必须按 MaskType 镜像");
static_assert(std::is_same_v<decltype(RenderDescC::weight), uint16_t>,
	"uint16_t 必须按真实宽度镜像");
static_assert(sizeof(RenderDescC) == 16, "RenderDescC 的大小必须与 C++ 记录一致");
static_assert(offsetof(RenderDescC, visible) == 0, "RenderDescC.visible 偏移");
static_assert(offsetof(RenderDescC, id) == 4, "RenderDescC.id 偏移");
static_assert(offsetof(RenderDescC, mode) == 8, "RenderDescC.mode 偏移");
static_assert(offsetof(RenderDescC, flags) == 10, "RenderDescC.flags 偏移");
static_assert(offsetof(RenderDescC, weight) == 12, "RenderDescC.weight 偏移");
static_assert(sizeof(RenderNestedC) == 20, "RenderNestedC 的大小必须与 C++ 记录一致");
static_assert(std::is_same_v<decltype(RenderHashableC::first), uint32_t>, "RenderHashableC.first 宽度");

// C ABI 的定宽类型也必须逐字对上：方法级标志位是 uint16_t，不是 int32_t。
using GetFlagsFunction = uint16_t (*)(void*, uint16_t);
static_assert(std::is_same_v<decltype(&ca_bind_fixture__IBindFoundation__GetFlags), GetFlagsFunction>,
	"方法级 EnumFlags 必须按 MaskType（uint16_t）投影");
using MixBytesFunction = int64_t (*)(void*, uint8_t const*, uint64_t, int32_t, RenderDescC const*,
	char const*, uint64_t, int32_t, uint16_t, void*);
static_assert(std::is_same_v<decltype(&ca_bind_fixture__IBindFoundation__MixBytes), MixBytesFunction>,
	"字节入参必须是 uint8_t const* + uint64_t，且与所有其他参数同序组合");

namespace
{
	bool Expect(bool condition, char const* what)
	{
		if (!condition)
		{
			std::printf("  [渲染基础] 失败：%s\n", what);
		}
		return condition;
	}

	class FakeFoundation final : public ca_bind_fixture::IBindFoundation
	{
	public:
		ca_bind_fixture::RenderDesc GetDesc(ca_bind_fixture::RenderDesc const& desc) override
		{
			lastDesc = desc;
			ca_bind_fixture::RenderDesc result = desc;
			result.id += 1u;
			result.weight = static_cast<uint16_t>(desc.weight + 1u);
			result.visible = !desc.visible;
			result.mode = ca_bind_fixture::ERenderMode::Fill;
			result.flags = ca_bind_fixture::ERenderFlags(ca_bind_fixture::ERenderFlag::Depth) |
				ca_bind_fixture::ERenderFlags(ca_bind_fixture::ERenderFlag::Cull);
			return result;
		}

		ca_bind_fixture::RenderNested GetNested(ca_bind_fixture::RenderNested const& nested) override
		{
			lastNested = nested;
			ca_bind_fixture::RenderNested result = nested;
			result.scale = nested.scale + 0.5f;
			result.desc.id += 100u;
			return result;
		}

		bool GetBool(bool value) override
		{
			lastBool = value;
			return !value;
		}

		ca_bind_fixture::ERenderMode GetMode(ca_bind_fixture::ERenderMode mode) override
		{
			lastMode = mode;
			return ca_bind_fixture::ERenderMode::Wire;
		}

		uint16_t GetFlags(ca_bind_fixture::ERenderFlags flags) override
		{
			lastFlags = flags.m_mask;
			return static_cast<uint16_t>(flags.m_mask | 0x8u);
		}

		void SetName(cacore::NameHash const& name) override
		{
			lastName = name.string();
		}

		void SetPath(cacore::PathHash const& path) override
		{
			lastPath = path.string();
		}

		void ConsumeHash(cacore::HashObj<ca_bind_fixture::RenderHashable> const& value) override
		{
			lastHashValue = value.Get();
			lastHash = value.GetHash();
			lastHashValid = value.Valid();
		}

		int64_t MixBytes(castl::vector<uint8_t> const& data, int32_t count,
			ca_bind_fixture::RenderDesc const& desc, castl::string const& label,
			ca_bind_fixture::ERenderMode mode, ca_bind_fixture::ERenderFlags flags, void* token) override
		{
			++mixCalls;
			lastBytes.assign(data.begin(), data.end());
			lastCount = count;
			lastMixDesc = desc;
			lastLabel = label;
			lastMixMode = mode;
			lastMixFlags = flags.m_mask;
			lastToken = token;
			int64_t sum = count;
			for (uint8_t byte : data)
			{
				sum += static_cast<int64_t>(byte);
			}
			return sum;
		}

		castl::shared_ptr<ca_bind_fixture::RenderDesc> AcquireDesc(castl::vector<uint8_t> const& data,
			ca_bind_fixture::RenderDesc const& desc, castl::string const& label,
			ca_bind_fixture::ERenderMode mode, ca_bind_fixture::ERenderFlags flags) override
		{
			++acquireCalls;
			lastAcquireBytes.assign(data.begin(), data.end());
			lastAcquireDesc = desc;
			lastAcquireLabel = label;
			lastAcquireMode = mode;
			lastAcquireFlags = flags.m_mask;
			created = castl::shared_ptr<ca_bind_fixture::RenderDesc>(new ca_bind_fixture::RenderDesc(desc));
			created->id += 1000u;
			return created;
		}

		void ConsumeDesc(castl::shared_ptr<ca_bind_fixture::RenderDesc> desc) override
		{
			++consumeDescCalls;
			lastConsumed = desc;
		}

		ca_bind_fixture::RenderDesc lastDesc{};
		ca_bind_fixture::RenderNested lastNested{};
		bool lastBool = false;
		ca_bind_fixture::ERenderMode lastMode = ca_bind_fixture::ERenderMode::None;
		uint16_t lastFlags = 0;
		castl::string lastName;
		castl::string lastPath;
		ca_bind_fixture::RenderHashable lastHashValue{};
		uint64_t lastHash = 0;
		bool lastHashValid = false;
		std::vector<uint8_t> lastBytes;
		int32_t lastCount = 0;
		ca_bind_fixture::RenderDesc lastMixDesc{};
		castl::string lastLabel;
		ca_bind_fixture::ERenderMode lastMixMode = ca_bind_fixture::ERenderMode::None;
		uint16_t lastMixFlags = 0;
		void* lastToken = nullptr;
		int mixCalls = 0;
		std::vector<uint8_t> lastAcquireBytes;
		ca_bind_fixture::RenderDesc lastAcquireDesc{};
		castl::string lastAcquireLabel;
		ca_bind_fixture::ERenderMode lastAcquireMode = ca_bind_fixture::ERenderMode::None;
		uint16_t lastAcquireFlags = 0;
		int acquireCalls = 0;
		int consumeDescCalls = 0;
		castl::shared_ptr<ca_bind_fixture::RenderDesc> created;
		castl::shared_ptr<ca_bind_fixture::RenderDesc> lastConsumed;
	};

	RenderDescC MakeDescC()
	{
		RenderDescC desc{};
		desc.visible = true;
		desc.id = 0x1234u;
		desc.mode = static_cast<uint16_t>(ca_bind_fixture::ERenderMode::Wire);
		desc.flags = static_cast<uint16_t>(ca_bind_fixture::ERenderFlag::Depth) |
			static_cast<uint16_t>(ca_bind_fixture::ERenderFlag::Blend);
		desc.weight = 0x55u;
		return desc;
	}

	bool CheckRecordValues(RenderDescC const& mirror, ca_bind_fixture::RenderDesc const& expected)
	{
		return mirror.visible == expected.visible && mirror.id == expected.id &&
			mirror.mode == static_cast<uint16_t>(expected.mode) && mirror.flags == expected.flags.m_mask &&
			mirror.weight == expected.weight;
	}

	// 起一次 CodeGen.exe，断言非零退出 + 诊断点名两个片段（类型/成员 + 拒绝原因）。
	bool RunRejectFixture(char const* fixture, char const* expectedType, char const* expectedReason)
	{
		char exePath[MAX_PATH] = {};
		if (::GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0)
		{
			std::printf("  [拒绝夹具] 取不到 CoreTests 自己的路径\n");
			return false;
		}
		const std::filesystem::path binDir = std::filesystem::path(exePath).parent_path();
		const std::filesystem::path tool = binDir / "CodeGen.exe";
		const std::filesystem::path sourceDir = CASCADED_SOURCE_DIR;
		const std::filesystem::path header = sourceDir / "Tools/CodeGen" / fixture;
		const std::filesystem::path audit = sourceDir / "Tools/CodeGen/check_bindable_audit.py";
		if (!std::filesystem::is_regular_file(tool) || !std::filesystem::is_regular_file(header))
		{
			std::printf("  [拒绝夹具] 缺工具或夹具：%s / %s\n", tool.string().c_str(), header.string().c_str());
			return false;
		}

		std::error_code ec;
		const std::filesystem::path outDir =
			std::filesystem::temp_directory_path() / (std::string("cascaded_reject_") + fixture);
		std::filesystem::remove_all(outDir, ec);
		std::filesystem::create_directories(outDir, ec);
		const std::filesystem::path logPath = outDir / "codegen.log";
		const std::filesystem::path scriptPath = outDir / "run_codegen.bat";
		{
			std::ofstream script(scriptPath, std::ios::binary);
			script << "@echo off\r\n";
			script << "\"" << tool.string() << "\" \"" << header.string()
			       << "\" --emit-binding --out-dir \"" << outDir.string() << "\" --audit-script \""
			       << audit.string() << "\" \"-I" << (sourceDir / "CACore/header").string() << "\" \"-I"
			       << (sourceDir / "Tools/CodeGen").string() << "\" \"-I"
			       << (sourceDir / "Tools/CodeGen/clang_shim").string() << "\" -xc++ > \"" << logPath.string()
			       << "\" 2>&1\r\n";
			script << "exit /b %ERRORLEVEL%\r\n";
		}
		const int exitCode = std::system(scriptPath.string().c_str());
		std::ifstream log(logPath, std::ios::binary);
		const std::string output((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
		log.close();
		std::filesystem::remove_all(outDir, ec);

		if (exitCode == 0)
		{
			std::printf("  [拒绝夹具] %s：生成器退出码 0（应当非零），输出：\n%s\n", fixture, output.c_str());
			return false;
		}
		if (output.find(expectedType) == std::string::npos ||
			output.find(expectedReason) == std::string::npos)
		{
			std::printf("  [拒绝夹具] %s：诊断没有同时点名 `%s` 与 `%s`，输出：\n%s\n", fixture,
				expectedType, expectedReason, output.c_str());
			return false;
		}
		return true;
	}

	bool RunRenderRejectProbes()
	{
		struct RejectCase
		{
			char const* fixture;
			char const* type;
			char const* reason;
		};
		const RejectCase cases[] = {
			{ "BindableShapes.RenderRejectVirtualMethod.h", "RejectVirtualRecord", "虚函数" },
			{ "BindableShapes.RenderRejectPrivateField.h", "RejectPrivateRecord", "非公开" },
			{ "BindableShapes.RenderRejectNoDefaultCtor.h", "RejectNoDefaultRecord", "默认构造" },
			{ "BindableShapes.RenderRejectNoHashSource.h", "RejectNoHashRecord", "哈希来源" },
			{ "BindableShapes.RenderRejectWrapperReturn.h", "NameHash", "返回" },
			{ "BindableShapes.RenderRejectWrapperNested.h", "RejectWrapperNestedRecord.name", "嵌入记录" },
			{ "BindableShapes.RenderRejectWeakParam.h", "value", "weak_ptr" },
			{ "BindableShapes.RenderRejectUnregisteredInterface.h", "IRejectUnregisteredTarget", "已知接口清单" },
		};
		bool ok = true;
		for (RejectCase const& item : cases)
		{
			ok = Expect(RunRejectFixture(item.fixture, item.type, item.reason),
				(item.fixture + std::string("：拒绝原因或点名不符")).c_str()) && ok;
		}
		return ok;
	}
}

bool RunBindingsRenderFoundationTest()
{
	FakeFoundation fake;
	bool ok = true;

	// ---- ① 平坦记录：入参逐字段到达实现，返回值逐字段搬回 ----
	const RenderDescC descC = MakeDescC();
	const RenderDescC echoed = ca_bind_fixture__IBindFoundation__GetDesc(&fake, &descC);
	ok = Expect(fake.lastDesc.visible && fake.lastDesc.id == 0x1234u &&
		fake.lastDesc.mode == ca_bind_fixture::ERenderMode::Wire &&
		fake.lastDesc.flags.m_mask == 3u && fake.lastDesc.weight == 0x55u,
		"平坦记录的入参没有逐字段到达原生实现") && ok;
	ok = Expect(echoed.visible == 0 && echoed.id == 0x1235u &&
		echoed.mode == static_cast<uint16_t>(ca_bind_fixture::ERenderMode::Fill) &&
		echoed.flags == 5u && echoed.weight == 0x56u,
		"平坦记录的返回值没有逐字段搬回 C 镜像") && ok;

	// ---- ② 嵌套记录：两层字段都按真实偏移搬运 ----
	RenderNestedC nestedC{};
	nestedC.desc = descC;
	nestedC.scale = 2.5f;
	const RenderNestedC nestedEcho = ca_bind_fixture__IBindFoundation__GetNested(&fake, &nestedC);
	ok = Expect(fake.lastNested.desc.id == 0x1234u && fake.lastNested.desc.weight == 0x55u &&
		fake.lastNested.scale == 2.5f,
		"嵌套记录的入参没有逐层到达原生实现") && ok;
	ok = Expect(nestedEcho.desc.id == 0x1234u + 100u && nestedEcho.scale == 3.0f,
		"嵌套记录的返回值没有逐层搬回") && ok;

	// ---- ③ bool / 普通枚举 / 方法级标志位 ----
	ok = Expect(ca_bind_fixture__IBindFoundation__GetBool(&fake, 1) == 0 && fake.lastBool,
		"bool 入参没有还原成 true") && ok;
	ok = Expect(ca_bind_fixture__IBindFoundation__GetMode(&fake, static_cast<int32_t>(ca_bind_fixture::ERenderMode::Wire)) ==
		static_cast<int32_t>(ca_bind_fixture::ERenderMode::Wire) && fake.lastMode == ca_bind_fixture::ERenderMode::Wire,
		"普通枚举没有按 int32_t 双向投影") && ok;
	ok = Expect(ca_bind_fixture__IBindFoundation__GetFlags(&fake, 5u) == 13u && fake.lastFlags == 5u,
		"方法级 EnumFlags 没有按 MaskType 重建/投影") && ok;

	// ---- ④ NameHash / PathHash：原生构造函数语义 ----
	const char* const nameText = "渲染-基础-Ω";
	ca_bind_fixture__IBindFoundation__SetName(&fake, nameText, static_cast<uint64_t>(std::strlen(nameText)));
	ok = Expect(fake.lastName == castl::string(nameText), "NameHash 没有还原成完整 UTF-8 文本") && ok;
	const char* const pathText = "Render\\Textures//..\\Textures//Albedo.png";
	ca_bind_fixture__IBindFoundation__SetPath(&fake, pathText, static_cast<uint64_t>(std::strlen(pathText)));
	const castl::string expectedPath = cacore::PathHash(castl::string(pathText)).string();
	ok = Expect(fake.lastPath == expectedPath, "PathHash 没有按现有 C++ 构造函数规范化") && ok;
	ok = Expect(fake.lastPath != castl::string(pathText), "PathHash 输入本身没有被规范化，判据构造有问题") && ok;

	// ---- ⑤ HashObj<T>：只传所含值，哈希状态与直接构造一致 ----
	RenderHashableC hashC{};
	hashC.first = 0x11223344u;
	hashC.second = 0xAABBCCDDu;
	ca_bind_fixture__IBindFoundation__ConsumeHash(&fake, &hashC);
	ca_bind_fixture::RenderHashable expectedHashValue{};
	expectedHashValue.first = hashC.first;
	expectedHashValue.second = hashC.second;
	const uint64_t expectedHash = cacore::HashObj<ca_bind_fixture::RenderHashable>(expectedHashValue).GetHash();
	ok = Expect(fake.lastHashValue.first == hashC.first && fake.lastHashValue.second == hashC.second,
		"HashObj 所含值没有逐字段到达原生实现") && ok;
	ok = Expect(fake.lastHashValid && fake.lastHash == expectedHash,
		"重建的 HashObj 哈希状态与直接构造不一致") && ok;

	// ---- ⑥ 字节 + 标量/记录/文本/枚举/标志位/句柄混合参数 ----
	const uint8_t bytes[] = { 0x00u, 0x7fu, 0x80u, 0xffu };
	const int32_t mixCount = -7;
	const int64_t mixed = ca_bind_fixture__IBindFoundation__MixBytes(&fake, bytes, sizeof(bytes), mixCount, &descC, "混合-参数",
		static_cast<uint64_t>(std::strlen("混合-参数")), static_cast<int32_t>(ca_bind_fixture::ERenderMode::Fill),
		6u, reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234u)));
	ok = Expect(mixed == -7 + 0 + 127 + 128 + 255, "混合参数的返回值不对") && ok;
	ok = Expect(fake.lastBytes.size() == 4 && fake.lastBytes[0] == 0x00u && fake.lastBytes[3] == 0xffu &&
		fake.lastCount == mixCount && fake.lastMixDesc.id == 0x1234u &&
		fake.lastLabel == castl::string("混合-参数") && fake.lastMixMode == ca_bind_fixture::ERenderMode::Fill &&
		fake.lastMixFlags == 6u && fake.lastToken == reinterpret_cast<void*>(static_cast<uintptr_t>(0x1234u)),
		"混合参数里有实参没有到达原生实现") && ok;

	// ---- ⑦ 空数组合法；空指针配非零长度在 thunk 入口拒绝且不进入实现 ----
	const int mixCallsBefore = fake.mixCalls;
	const int64_t emptyResult = ca_bind_fixture__IBindFoundation__MixBytes(&fake, nullptr, 0u, 0, &descC, nullptr, 0u,
		static_cast<int32_t>(ca_bind_fixture::ERenderMode::None), 0u, nullptr);
	ok = Expect(fake.mixCalls == mixCallsBefore + 1 && fake.lastBytes.empty() && emptyResult == 0,
		"空数组没有被当作合法的零长度输入") && ok;
	const int mixCallsBeforeBad = fake.mixCalls;
	const int64_t bad = ca_bind_fixture__IBindFoundation_thunk_MixBytes(&fake, nullptr, 3u, 0, &descC, nullptr, 0u,
		static_cast<int32_t>(ca_bind_fixture::ERenderMode::None), 0u, nullptr);
	ok = Expect(bad == 0 && fake.mixCalls == mixCallsBeforeBad &&
		ca_bind_fixture__IBindFoundation__LastErrorCode(&fake) == ca_bind_fixture__IBindFoundation_ERROR_NULL_POINTER,
		"空指针配非零字节长度没有在 thunk 入口被拒绝") && ok;

	// ---- ⑧ 强返回 + 配对释放：发行者入口解析到同一对象，释放后旧句柄失效 ----
	const uint64_t handle = ca_bind_fixture__IBindFoundation__AcquireDesc(&fake, bytes, sizeof(bytes), &descC, "acquire",
		static_cast<uint64_t>(std::strlen("acquire")), 0u, 3u);
	ok = Expect(handle != 0ull && fake.acquireCalls == 1 && fake.lastAcquireDesc.id == 0x1234u &&
		fake.lastAcquireBytes.size() == 4 && fake.lastAcquireLabel == castl::string("acquire") &&
		fake.lastAcquireMode == ca_bind_fixture::ERenderMode::None && fake.lastAcquireFlags == 3u,
		"智能指针返回的方法漏掉了实参转换") && ok;
	void* resolved = ca_bind_fixture__IBindFoundation__ResolveHandle(&fake, handle);
	ok = Expect(resolved != nullptr && static_cast<ca_bind_fixture::RenderDesc*>(resolved)->id == 0x1234u + 1000u,
		"强返回句柄没有解析到实现创建的对象") && ok;
	ca_bind_fixture__IBindFoundation__ReleaseAcquireDesc(&fake, handle);
	ok = Expect(ca_bind_fixture__IBindFoundation__ResolveHandle(&fake, handle) == nullptr &&
		ca_bind_fixture__IBindFoundation__LastErrorCode(&fake) == ca_bind_fixture__IBindFoundation_ERROR_STALE_HANDLE,
		"配对释放后旧句柄仍然可以解析") && ok;

	// ---- ⑨ shared_ptr 入参：反查重建到同一对象 ----
	const uint64_t consumedHandle = ca_bind_fixture__IBindFoundation__AcquireDesc(&fake, bytes, sizeof(bytes), &descC, "consume",
		static_cast<uint64_t>(std::strlen("consume")), 0u, 0u);
	void* consumedObject = ca_bind_fixture__IBindFoundation__ResolveHandle(&fake, consumedHandle);
	ca_bind_fixture__IBindFoundation__ConsumeDesc(&fake, consumedHandle);
	ok = Expect(fake.consumeDescCalls == 1 && fake.lastConsumed.get() == consumedObject,
		"shared_ptr 入参没有反查重建到同一对象") && ok;
	ca_bind_fixture__IBindFoundation__ReleaseAcquireDesc(&fake, consumedHandle);

	// ---- ⑩ 生成期拒绝：每个负向夹具单独起一次 CodeGen.exe ----
	ok = RunRenderRejectProbes() && ok;
	return ok;
}

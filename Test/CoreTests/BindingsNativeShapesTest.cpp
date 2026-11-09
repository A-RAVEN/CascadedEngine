// 原生形态夹具的原生行为判据（codegen-native-binding-prereqs 任务 2.2 / 2.3）。
//
// 判据分三半：
//   ① 编译期形状：自返回链式方法的 C ABI 槽必须返回 void；非纯虚 / 非虚方法必须进表；
//   ② 原生行为：非纯虚 virtual、非虚公开方法、链式调用两连的真实计数与顺序；
//      静态工厂返回纯标量记录、默认构造类 Create 的句柄登记与配对释放；
//   ③ 生成期拒绝探针（起 CodeGen.exe）：模板成员函数、返回含容器记录的静态工厂都必须非零点名。
// 生成物由本翻译单元直接 include（与既有绑定用例同一口径），判据跑的是产品生成的 C ABI。
#include "BindableShapes.NativeShapes.h"

#include "IBindNativeShape.Bindings.cpp"
#include "BindNativeObject.Bindings.cpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <windows.h>

namespace
{
	int32_t g_injectedSentinel = 0x1234;
}
// 注入钩子（任务 3.3）：固定名，返回引擎默认提供的指针。
extern "C" void* CAInject_IBindNativeShape_SetInjectedPointer_externalState()
{
	return &g_injectedSentinel;
}

namespace
{
	bool Expect(bool condition, char const* what)
	{
		if (!condition)
		{
			std::printf("  [原生形态] 失败：%s\n", what);
		}
		return condition;
	}

	class FakeNativeShape final : public ca_bind_fixture::IBindNativeShape
	{
	public:
		uint32_t GetCounter() const override { return counter; }

		void SetCounter(uint32_t value) override
		{
			counter = value;
			++setCounterCalls;
		}

		ca_bind_fixture::IBindNativeShape& ChainAdd(uint32_t value) override
		{
			counter += value;
			chainOrder.push_back(value);
			return *this;
		}

		void ConsumeRef(ca_bind_fixture::NativeResourceRef const& ref, uint32_t channel) override
		{
			++consumeRefCalls;
			lastChannel = channel;
			lastRefKind = ref.GetReferenceKind();
			lastRefObject = ref.GetReferencedObject().get();
			lastRefName = ref.GetReferencedName().string();
		}

		void ConsumePass(ca_bind_fixture::PassLike const& pass) override
		{
			++consumePassCalls;
			lastPassAttachments.clear();
			for (ca_bind_fixture::PassAttachment const& attachment : pass.m_Attachments)
			{
				lastPassAttachments.emplace_back(attachment.index, attachment.weight);
			}
			lastPassModes.clear();
			for (ca_bind_fixture::EPassMode mode : pass.m_Modes)
			{
				lastPassModes.push_back(static_cast<uint16_t>(mode));
			}
			lastPassName = pass.m_Name.string();
			lastPassPrivateCount = pass.GetPrivateCount();
		}

		void SetInjectedPointer(uint32_t token, int32_t* externalState) override
		{
			++setInjectedCalls;
			lastInjectedToken = token;
			lastInjectedPointer = externalState;
		}

		// 内存窗参数投影（csharp-native-direct-binding 任务 1.1 / design D2）：逐字节留下收到的内容，
		// 供判据比对"托管给的字节 == 原生实现看到的字节"。
		void UploadWindow(void const* data, uint64_t size, uint64_t offset, bool copy) override
		{
			++uploadWindowCalls;
			uploadWindowSize = size;
			uploadWindowBytes.clear();
			if (data != nullptr && size > 0u)
			{
				const uint8_t* begin = static_cast<uint8_t const*>(data);
				uploadWindowBytes.assign(begin, begin + size);
			}
			lastUploadOffset = offset;
			lastUploadCopy = copy;
		}

		// 记录两段式（任务 1.2 / design D8）：只认 "passx"，其余按键未命中返回空指针。
		ca_bind_fixture::PassLike const* FindPass(cacore::NameHash const& passName) override
		{
			++findPassCalls;
			if (passName.string() != "passx")
			{
				return nullptr;
			}
			return &findPassValue;
		}
		ca_bind_fixture::PassLike findPassValue;
		uint32_t findPassCalls = 0u;

		// 返回位置标量容器两段式（任务 1.6 / design D13）：可切换"有元素 / 空容器"两种返回。
		castl::vector<uint32_t> const& GetPassIndices() const override
		{
			return passIndicesEmpty ? emptyPassIndices : passIndices;
		}
		castl::vector<uint32_t> passIndices{7u, 11u, 42u};
		castl::vector<uint32_t> emptyPassIndices;
		bool passIndicesEmpty = false;

		// 图形状序列化（任务 1.5 / design D11）：逐字段留下收到的内容。
		void ConsumeGraphShape(ca_bind_fixture::GraphLikeRecord const& shape) override
		{
			++graphShapeCalls;
			graphRefKinds.clear();
			for (ca_bind_fixture::NativeResourceRef const& ref : shape.m_Refs)
			{
				graphRefKinds.push_back(static_cast<uint32_t>(ref.GetReferenceKind()));
			}
			graphCount = 0u;
			for (auto const& pair : shape.m_Counts)
			{
				graphCount = pair.second;
			}
			graphAttachmentIndex = 0u;
			graphAttachmentWeight = 0.0f;
			for (auto const& pair : shape.m_Attachments)
			{
				graphAttachmentIndex = pair.second.index;
				graphAttachmentWeight = pair.second.weight;
			}
			graphRefName.clear();
			for (auto const& pair : shape.m_RefsByName)
			{
				graphRefName = pair.second.GetReferencedName().string();
			}
			graphObject = shape.m_Object.get();
			graphMappedObject = nullptr;
			for (auto const& pair : shape.m_Objects)
			{
				graphMappedObject = pair.second.get();
			}
		}
		uint32_t graphShapeCalls = 0u;
		std::vector<uint32_t> graphRefKinds;
		uint32_t graphCount = 0u;
		uint32_t graphAttachmentIndex = 0u;
		float graphAttachmentWeight = 0.0f;
		std::string graphRefName;
		void const* graphObject = nullptr;
		void const* graphMappedObject = nullptr;

		uint32_t counter = 0u;
		uint32_t setCounterCalls = 0u;
		std::vector<uint32_t> chainOrder;
		uint32_t consumeRefCalls = 0u;
		uint32_t lastChannel = 0u;
		ca_bind_fixture::NativeResourceRef::ENativeRefKind lastRefKind =
			ca_bind_fixture::NativeResourceRef::ENativeRefKind::None;
		void const* lastRefObject = nullptr;
		std::string lastRefName;
		uint32_t consumePassCalls = 0u;
		std::vector<std::pair<uint32_t, float>> lastPassAttachments;
		std::vector<uint16_t> lastPassModes;
		std::string lastPassName;
		uint32_t lastPassPrivateCount = 0u;
		uint32_t setInjectedCalls = 0u;
		uint32_t lastInjectedToken = 0u;
		int32_t* lastInjectedPointer = nullptr;
		uint32_t uploadWindowCalls = 0u;
		uint64_t uploadWindowSize = 0u;
		std::vector<uint8_t> uploadWindowBytes;
		uint64_t lastUploadOffset = 0u;
		bool lastUploadCopy = true;
	};

	// 起一次 CodeGen.exe，断言非零退出 + 诊断点名两个片段（类型/成员 + 拒绝原因）。
	bool RunRejectProbe(char const* fixture, char const* expectedType, char const* expectedReason)
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
			std::filesystem::temp_directory_path() / (std::string("cascaded_native_reject_") + fixture);
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
}

// 自返回（任务 2.2）：C ABI 槽返回 void，原生返回的 *this 不写回。
using ChainAddFunction = void (*)(void*, uint32_t);
static_assert(std::is_same_v<decltype(&IBindNativeShape_ChainAdd), ChainAddFunction>,
	"返回自身引用的链式方法必须投影成 void 返回的 C ABI 槽");

using GetVirtualValueFunction = uint32_t (*)(void*);
static_assert(std::is_same_v<decltype(&IBindNativeShape_GetVirtualValue), GetVirtualValueFunction>,
	"非纯虚 virtual 方法必须进表");

using GetDoubledFunction = uint32_t (*)(void*);
static_assert(std::is_same_v<decltype(&IBindNativeShape_GetDoubled), GetDoubledFunction>,
	"非虚公开方法必须进表");

// 静态工厂（任务 2.3）：返回纯标量记录的 C 镜像（镜像类型在全局作用域，不在夹具命名空间）。
using MakeTallyFunction = NativeCountC (*)(void*, uint32_t);
static_assert(std::is_same_v<decltype(&IBindNativeShape_MakeTally), MakeTallyFunction>,
	"返回纯标量记录的静态工厂必须投影成 `<记录>C` 返回的 C ABI 槽");

// 重载序数槽（任务 2.4）：第 1 槽保持原名、第 2 槽 `_2`，签名各自对上。
using OverloadedOneFunction = uint32_t (*)(void*, uint32_t);
static_assert(std::is_same_v<decltype(&IBindNativeShape_Overloaded), OverloadedOneFunction>,
	"重载第 1 槽的 C ABI 签名不对");
using OverloadedTwoFunction = uint32_t (*)(void*, uint32_t, uint32_t);
static_assert(std::is_same_v<decltype(&IBindNativeShape_Overloaded_2), OverloadedTwoFunction>,
	"重载第 2 槽必须叫 _2 且签名各自独立");

// 内存窗参数投影（任务 1.1 / design D2）：`const void*` + 相邻长度投影成
// `uint8_t const*` + `uint64_t` 两段 ABI；其余标量（offset / copy）原样跟在后面。
using UploadWindowFunction = void (*)(void*, uint8_t const*, uint64_t, uint64_t, int32_t);
static_assert(std::is_same_v<decltype(&IBindNativeShape_UploadWindow), UploadWindowFunction>,
	"内存窗投影的 C ABI 签名不是 (uint8_t const*, uint64_t 长度, uint64_t offset, int32_t copy)");

// 模板显式实例化（任务 1.3 / design D9）：两个声明形态各有一个具体槽，槽名带类型记号。
using TemplatedValueUintFunction = uint32_t (*)(void*, uint32_t);
static_assert(std::is_same_v<decltype(&IBindNativeShape_TemplatedValue_uint32_t), TemplatedValueUintFunction>,
	"模板实例化 uint32_t 形态的 C ABI 槽签名不对");
using TemplatedValueFloatFunction = uint32_t (*)(void*, float);
static_assert(std::is_same_v<decltype(&IBindNativeShape_TemplatedValue_float), TemplatedValueFloatFunction>,
	"模板实例化 float 形态的 C ABI 槽签名不对");

bool RunBindingsNativeShapesTest()
{
	FakeNativeShape fake;
	bool ok = true;

	// ---- 非纯虚 virtual：接口里的实现体被绑进表，调用到达原生实现 ----
	ok = Expect(IBindNativeShape_GetVirtualValue(&fake) == 7u,
		"非纯虚 virtual 方法没有读到接口实现体（应为 7）") && ok;

	// ---- 非虚公开方法：调纯虚钩子（GetCounter 走虚分派）----
	IBindNativeShape_SetCounter(&fake, 9u);
	ok = Expect(fake.counter == 9u && fake.setCounterCalls == 1u,
		"SetCounter 没有到达原生实现") && ok;
	ok = Expect(IBindNativeShape_GetDoubled(&fake) == 18u,
		"非虚公开方法 GetDoubled 没有读到 GetCounter 的两倍") && ok;

	// ---- 自返回链式：C ABI 返回 void，原生计数 +2 且顺序一致 ----
	IBindNativeShape_ChainAdd(&fake, 3u);
	IBindNativeShape_ChainAdd(&fake, 4u);
	ok = Expect(fake.counter == 16u,
		"链式两次调用后原生计数应为 9 + 3 + 4 = 16") && ok;
	ok = Expect(fake.chainOrder.size() == 2u && fake.chainOrder[0] == 3u && fake.chainOrder[1] == 4u,
		"链式两次调用的顺序与实参没有按声明顺序到达原生实现") && ok;

	// ---- 静态工厂（任务 2.3）：返回纯标量记录，字段逐项到达 ----
	const NativeCountC count = IBindNativeShape_MakeTally(&fake, 41u);
	ok = Expect(count.calls == 41u && count.lastValue == 42u,
		"静态工厂返回的纯标量记录没有逐字段搬回") && ok;

	// ---- 重载序数槽（任务 2.4）：第 1 槽保持现名、第 2 槽 `_2`；两实现计数互不冒充 ----
	const uint32_t oneBefore = ca_bind_fixture::IBindNativeShape::s_OverloadOneCalls;
	const uint32_t twoBefore = ca_bind_fixture::IBindNativeShape::s_OverloadTwoCalls;
	ok = Expect(IBindNativeShape_Overloaded(&fake, 10u) == 11u,
		"重载第 1 槽没有命中 Overloaded(uint32_t) 的实现") && ok;
	ok = Expect(IBindNativeShape_Overloaded_2(&fake, 10u, 5u) == 15u,
		"重载第 2 槽没有命中 Overloaded(uint32_t, uint32_t) 的实现") && ok;
	ok = Expect(ca_bind_fixture::IBindNativeShape::s_OverloadOneCalls == oneBefore + 1u &&
	            ca_bind_fixture::IBindNativeShape::s_OverloadTwoCalls == twoBefore + 1u,
		"两个重载实现的原生计数没有各 +1（互相冒充）") && ok;

	// ---- 构造 Create（任务 2.3）：句柄登记、同一实例、配对释放、释放后解析失败 ----
	const uint64_t handle = BindNativeObject_Create(nullptr);
	ok = Expect(handle != 0u, "BindNativeObject_Create 没有返回句柄") && ok;
	void* object = BindNativeObject_ResolveHandle(nullptr, handle);
	ok = Expect(object != nullptr, "Create 登记的句柄解析不到对象") && ok;
	BindNativeObject_SetValue(object, 21u);
	ok = Expect(BindNativeObject_GetValue(object) == 21u && BindNativeObject_DoubleValue(object) == 42u,
		"Create 出来的对象上的方法没读到真实值") && ok;
	BindNativeObject_ReleaseHandle(nullptr, handle);
	ok = Expect(BindNativeObject_ResolveHandle(nullptr, handle) == nullptr,
		"配对释放之后旧句柄仍能解析出对象") && ok;
	ok = Expect(BindNativeObject_LastErrorCode(nullptr) == 1001,
		"配对释放之后旧句柄的错误码不是 1001") && ok;

	// ---- 资源引用值类（任务 3.1）：两条构造通道、同一实例、坏引用不进实现 ----
	{
		auto shared = std::make_shared<FakeNativeShape>();
		const uint64_t strong = IBindNativeShape_registerStrongHandle(
			shared.get(), std::static_pointer_cast<void>(shared),
			"ca_bind_fixture::IBindNativeShape");
		ok = Expect(strong != 0u, "资源引用：注册对象强句柄失败") && ok;

		// 通道一：按对象句柄构造（kind=Object）。
		IBindNativeShape_ConsumeRef(shared.get(), 1u, strong, nullptr, 0u, 7u);
		ok = Expect(shared->consumeRefCalls == 1u && shared->lastChannel == 7u,
			"对象通道没有进入原生实现") && ok;
		ok = Expect(shared->lastRefKind == ca_bind_fixture::NativeResourceRef::ENativeRefKind::Object,
			"对象通道的 kind 不是 Object") && ok;
		ok = Expect(shared->lastRefObject == shared.get(),
			"对象通道解析到的不是同一个实例") && ok;
		ok = Expect(shared->lastRefName.empty(), "对象通道不该带名称") && ok;

		// 通道二：按名称构造（kind=Named）：原生侧走 NameHash 原生构造通道。
		const char* nameText = "alpha";
		IBindNativeShape_ConsumeRef(shared.get(), 2u, 0u,
			reinterpret_cast<uint8_t const*>(nameText), 5u, 8u);
		ok = Expect(shared->consumeRefCalls == 2u && shared->lastChannel == 8u,
			"名称通道没有进入原生实现") && ok;
		ok = Expect(shared->lastRefKind == ca_bind_fixture::NativeResourceRef::ENativeRefKind::Named,
			"名称通道的 kind 不是 Named") && ok;
		ca_bind_fixture::NativeResourceRef direct(cacore::NameHash("alpha"));
		ok = Expect(shared->lastRefName == direct.GetReferencedName().string(),
			"按名构造与原生直构的 NameHash 不一致") && ok;

		// 坏句柄：错误码 + 实现计数不变（不进实现）。
		IBindNativeShape_ConsumeRef(shared.get(), 1u, 0u, nullptr, 0u, 9u);
		ok = Expect(shared->consumeRefCalls == 2u, "坏句柄进入了原生实现") && ok;
		ok = Expect(IBindNativeShape_LastErrorCode(shared.get()) == 1001,
			"坏句柄的错误码不是 1001") && ok;
		// 空名：错误码 + 实现计数不变。
		IBindNativeShape_ConsumeRef(shared.get(), 2u, 0u, nullptr, 0u, 10u);
		ok = Expect(shared->consumeRefCalls == 2u, "空名进入了原生实现") && ok;
		ok = Expect(IBindNativeShape_LastErrorCode(shared.get()) == 1002,
			"空名的错误码不是 1002") && ok;
	}

	// ---- 序列化块记录（任务 3.2）：往返字段级一致 + 手摆畸形块逐项拒绝 ----
	{
		ca_bind_fixture::PassLike pass;
		pass.m_Attachments = { { 1u, 0.5f }, { 2u, 1.5f } };
		pass.m_Modes = { ca_bind_fixture::EPassMode::Fill, ca_bind_fixture::EPassMode::Wire };
		pass.m_Name = cacore::NameHash("passx");
		pass.SetPrivateCount(7u);

		using Codec = cacore::codegen::CodeGenBindingCodec<ca_bind_fixture::PassLike>;
		std::vector<uint8_t> bytes;
		Codec::Serialize(pass, bytes);
		IBindNativeShape_ConsumePass(&fake, bytes.data(), static_cast<uint64_t>(bytes.size()));
		ok = Expect(fake.consumePassCalls == 1u, "序列化块往返没有进入原生实现") && ok;
		ok = Expect(fake.lastPassAttachments.size() == 2u &&
		            fake.lastPassAttachments[0].first == 1u && fake.lastPassAttachments[0].second == 0.5f &&
		            fake.lastPassAttachments[1].first == 2u && fake.lastPassAttachments[1].second == 1.5f,
			"容器元素的字段没有逐项搬回") && ok;
		ok = Expect(fake.lastPassModes.size() == 2u && fake.lastPassModes[0] == 0u &&
		            fake.lastPassModes[1] == 1u, "枚举容器没有逐项搬回") && ok;
		ok = Expect(fake.lastPassName == "passx", "文本字段没有搬回") && ok;
		ok = Expect(fake.lastPassPrivateCount == 7u, "私有字段没有经 CA_CODEGEN_FRIEND 搬回") && ok;
		ok = Expect(Codec::kLayoutHash ==
		            Codec::Fnv(Codec::kLayoutText, static_cast<uint64_t>(::strlen(Codec::kLayoutText))),
			"块头布局哈希与布局清单不同源") && ok;

		const uint32_t callsAfterOk = fake.consumePassCalls;
		// ① 版本字节 +1
		{
			std::vector<uint8_t> bad = bytes;
			bad[0] = static_cast<uint8_t>(bad[0] + 1u);
			IBindNativeShape_ConsumePass(&fake, bad.data(), static_cast<uint64_t>(bad.size()));
			ok = Expect(fake.consumePassCalls == callsAfterOk &&
			            IBindNativeShape_LastErrorCode(&fake) == 1007,
				"版本字节 +1 没有按 1007 拒绝") && ok;
		}
		// ② 类型名哈希改错
		{
			std::vector<uint8_t> bad = bytes;
			bad[4] = static_cast<uint8_t>(bad[4] ^ 0xFFu);
			IBindNativeShape_ConsumePass(&fake, bad.data(), static_cast<uint64_t>(bad.size()));
			ok = Expect(fake.consumePassCalls == callsAfterOk &&
			            IBindNativeShape_LastErrorCode(&fake) == 1008,
				"类型名哈希改错没有按 1008 拒绝") && ok;
		}
		// ③ 字段布局哈希改错
		{
			std::vector<uint8_t> bad = bytes;
			bad[12] = static_cast<uint8_t>(bad[12] ^ 0xFFu);
			IBindNativeShape_ConsumePass(&fake, bad.data(), static_cast<uint64_t>(bad.size()));
			ok = Expect(fake.consumePassCalls == callsAfterOk &&
			            IBindNativeShape_LastErrorCode(&fake) == 1009,
				"字段布局哈希改错没有按 1009 拒绝") && ok;
		}
		// ③b "改名同序"：只改字段名、类型序列不变，前两项哈希检不出 —— 必须由字段布局哈希抓。
		{
			std::string renamed = Codec::kLayoutText;
			const std::string from = "m_Attachments";
			const std::string to = "m_AttachmentsX";
			const std::size_t at = renamed.find(from);
			if (at != std::string::npos)
			{
				renamed.replace(at, from.size(), to);
			}
			const uint64_t renamedHash =
				Codec::Fnv(renamed.data(), static_cast<uint64_t>(renamed.size()));
			std::vector<uint8_t> bad = bytes;
			::memcpy(bad.data() + 12, &renamedHash, sizeof(renamedHash));
			IBindNativeShape_ConsumePass(&fake, bad.data(), static_cast<uint64_t>(bad.size()));
			ok = Expect(renamedHash != Codec::kLayoutHash && fake.consumePassCalls == callsAfterOk &&
			            IBindNativeShape_LastErrorCode(&fake) == 1009,
				"改名同序的块没有被字段布局哈希拒绝") && ok;
		}
		// ④ 载荷截断
		{
			IBindNativeShape_ConsumePass(&fake, bytes.data(),
				static_cast<uint64_t>(bytes.size() - 1u));
			ok = Expect(fake.consumePassCalls == callsAfterOk &&
			            IBindNativeShape_LastErrorCode(&fake) == 1010,
				"载荷截断没有按 1010 拒绝") && ok;
		}
	}

	// ---- 参数注入（任务 3.3）：C ABI/托管表面没有该参数，原生实现收到钩子返回的哨兵指针 ----
	{
		IBindNativeShape_SetInjectedPointer(&fake, 77u);
		ok = Expect(fake.setInjectedCalls == 1u && fake.lastInjectedToken == 77u,
			"注入方法没有把 token 送到原生实现") && ok;
		ok = Expect(fake.lastInjectedPointer == &g_injectedSentinel,
			"注入参数不是钩子返回的哨兵指针") && ok;
		ok = Expect(fake.lastInjectedPointer != nullptr && *fake.lastInjectedPointer == 0x1234,
			"钩子提供的指针读不到哨兵值") && ok;
	}

	// ---- 内存窗参数投影（任务 1.1 / design D2）：逐字节一致 + 长度正确 + 其余参数原样 ----
	{
		std::vector<uint8_t> payload = { 0x01u, 0x02u, 0x03u, 0x05u, 0x08u, 0x0Du, 0x15u, 0x22u, 0x37u };
		IBindNativeShape_UploadWindow(&fake, payload.data(), static_cast<uint64_t>(payload.size()),
			16u, 0);
		ok = Expect(fake.uploadWindowCalls == 1u, "内存窗调用没有进入原生实现") && ok;
		ok = Expect(fake.uploadWindowSize == static_cast<uint64_t>(payload.size()),
			"原生实现收到的长度与源字节数不一致") && ok;
		ok = Expect(fake.uploadWindowBytes == payload,
			"原生实现收到的字节与源字节不是逐字节一致") && ok;
		ok = Expect(fake.lastUploadOffset == 16u && fake.lastUploadCopy == false,
			"内存窗方法里其余标量参数（offset / copy）没有原样过界") && ok;
		std::printf("  [内存窗] 长度=%llu 字节一致=%d offset=%llu copy=%d\n",
			static_cast<unsigned long long>(fake.uploadWindowSize),
			fake.uploadWindowBytes == payload ? 1 : 0,
			static_cast<unsigned long long>(fake.lastUploadOffset), fake.lastUploadCopy ? 1 : 0);
		// 空窗口：长度 0、指针非空 —— 实现照样被调用（不上报假错误）。
		IBindNativeShape_UploadWindow(&fake, payload.data(), 0u, 0u, 1);
		ok = Expect(fake.uploadWindowCalls == 2u && fake.uploadWindowSize == 0u &&
		            fake.uploadWindowBytes.empty() && fake.lastUploadCopy == true,
			"零长度内存窗没有按零长度 + copy=true 到达原生实现") && ok;
	}

	// ---- 记录两段式（任务 1.2 / design D8）：命中得序列化副本、未命中得空 ----
	{
		ca_bind_fixture::PassLike stored;
		stored.m_Attachments = { { 4u, 0.25f } };
		stored.m_Modes = { ca_bind_fixture::EPassMode::Wire };
		stored.m_Name = cacore::NameHash("passx");
		stored.SetPrivateCount(9u);
		fake.findPassValue = stored;

		using Codec = cacore::codegen::CodeGenBindingCodec<ca_bind_fixture::PassLike>;
		const char* key = "passx";
		const uint64_t length = IBindNativeShape_FindPassLen(&fake, key, 5u);
		ok = Expect(length > 0u, "记录两段式：命中时 Len 返回 0（应当返回序列化字节数）") && ok;
		std::vector<uint8_t> buffer(static_cast<std::size_t>(length));
		IBindNativeShape_CopyFindPass(&fake, key, 5u, buffer.data(),
			static_cast<uint64_t>(buffer.size()));
		ok = Expect(IBindNativeShape_LastErrorCode(&fake) == 0,
			"记录两段式：Copy 之后错误码非 0") && ok;
		ca_bind_fixture::PassLike decoded;
		const uint32_t decodeCode =
			Codec::Deserialize(decoded, buffer.data(), static_cast<uint64_t>(buffer.size()));
		ok = Expect(decodeCode == 0u, "记录两段式：搬回来的字节反序列化失败") && ok;
		ok = Expect(decoded.m_Name.string() == stored.m_Name.string(), "记录两段式：文本字段不一致") && ok;
		ok = Expect(decoded.GetPrivateCount() == 9u, "记录两段式：私有字段没有搬回") && ok;
		ok = Expect(decoded.m_Attachments.size() == 1u && decoded.m_Attachments[0].index == 4u &&
		            decoded.m_Attachments[0].weight == 0.25f,
			"记录两段式：容器元素字段没有搬回") && ok;
		ok = Expect(decoded.m_Modes.size() == 1u &&
		            decoded.m_Modes[0] == ca_bind_fixture::EPassMode::Wire,
			"记录两段式：枚举容器没有搬回") && ok;

		// 未命中：原生返回空指针 ⇒ Len=0（托管侧映射成 null），Copy 不写目标缓冲。
		const char* missing = "nope";
		const uint64_t missingLength = IBindNativeShape_FindPassLen(&fake, missing, 4u);
		ok = Expect(missingLength == 0u,
			"记录两段式：未命中时 Len 不是 0（托管得不到 null）") && ok;
		std::vector<uint8_t> untouched(8u, 0xAAu);
		IBindNativeShape_CopyFindPass(&fake, missing, 4u, untouched.data(), 8u);
		ok = Expect(untouched[0] == 0xAAu, "记录两段式：未命中时 Copy 仍然写了目标缓冲") && ok;
		ok = Expect(fake.findPassCalls == 4u, "记录两段式：四次伴生调用没有分别到达原生实现") && ok;
		std::printf("  [记录两段式] 命中字节=%llu 反序列化码=%u 未命中Len=%llu 未命中写缓冲=%d 调用=%u\n",
			static_cast<unsigned long long>(length), decodeCode,
			static_cast<unsigned long long>(missingLength),
			untouched[0] == 0xAAu ? 0 : 1, fake.findPassCalls);
	}

	// ---- 模板显式实例化（任务 1.3 / design D9）：两形态各出槽、各命中自己的实例 ----
	{
		const uint32_t uintBefore = ca_bind_fixture::IBindNativeShape::s_TemplateUintCalls;
		const uint32_t floatBefore = ca_bind_fixture::IBindNativeShape::s_TemplateFloatCalls;
		const uint32_t uintResult = IBindNativeShape_TemplatedValue_uint32_t(&fake, 41u);
		const uint32_t floatResult = IBindNativeShape_TemplatedValue_float(&fake, 1.5f);
		const uint32_t uintAfter = ca_bind_fixture::IBindNativeShape::s_TemplateUintCalls;
		const uint32_t floatAfter = ca_bind_fixture::IBindNativeShape::s_TemplateFloatCalls;
		ok = Expect(uintResult == 141u,
			"模板实例化：uint32_t 形态没有命中 value+100 的实现") && ok;
		ok = Expect(floatResult == 3u,
			"模板实例化：float 形态没有命中 value*2 的实现") && ok;
		ok = Expect(uintAfter == uintBefore + 1u && floatAfter == floatBefore + 1u,
			"模板实例化：两个实例的原生计数没有各 +1（互相冒充）") && ok;
		std::printf("  [模板实例化] uint32_t→%u float→%u 计数uint=%u 计数float=%u\n", uintResult,
			floatResult, uintAfter, floatAfter);
	}

	// ---- 图形状序列化（任务 1.5 / design D11）：值类容器 / 命名映射 / 接口对象引用 ----
	{
		auto shared = std::make_shared<FakeNativeShape>();
		const uint64_t objectHandle = IBindNativeShape_registerStrongHandle(
			shared.get(), std::static_pointer_cast<void>(shared),
			"ca_bind_fixture::IBindNativeShape");
		ok = Expect(objectHandle != 0u, "图形状：注册对象句柄失败") && ok;

		// 手摆一块 D11 线形（C# 侧写的就是这份布局；C++ 侧只定义读方向 —— 见 evidence/1.5）。
		using GraphCodec = cacore::codegen::CodeGenBindingCodec<ca_bind_fixture::GraphLikeRecord>;
		std::vector<uint8_t> bytes;
		auto writeRaw = [&bytes](void const* data, uint64_t size) {
			const uint8_t* begin = static_cast<uint8_t const*>(data);
			bytes.insert(bytes.end(), begin, begin + size);
		};
		auto writeU32 = [&writeRaw](uint32_t value) { writeRaw(&value, sizeof(value)); };
		auto writeU64 = [&writeRaw](uint64_t value) { writeRaw(&value, sizeof(value)); };
		auto writeText = [&writeRaw, &writeU64](std::string const& text) {
			writeU64(static_cast<uint64_t>(text.size()));
			writeRaw(text.data(), static_cast<uint64_t>(text.size()));
		};
		auto writeValueClass = [&writeU32, &writeU64, &writeText](uint32_t kind, uint64_t handle,
		                                                          std::string const& name) {
			writeU32(kind);
			writeU64(handle);
			writeText(name);
		};
		// 块头三项（version + 类型名哈希 + 字段布局哈希）：与生成的编解码器同源常量。
		writeU32(1u);
		writeU64(GraphCodec::kTypeNameHash);
		writeU64(GraphCodec::kLayoutHash);
		writeU64(1u);                                    // ① 值类容器：1 条按名构造
		writeValueClass(2u, 0u, "alpha");
		writeU64(1u);                                    // ② 命名映射（标量值）
		writeText("count");
		writeU32(7u);
		writeU64(1u);                                    // ② 命名映射（记录值）
		writeText("att");
		writeU32(3u);
		{
			float weight = 0.5f;
			writeRaw(&weight, sizeof(weight));
		}
		writeU64(1u);                                    // ② 命名映射（值类值）
		writeText("ref");
		writeValueClass(2u, 0u, "beta");
		writeU64(objectHandle);                          // ③ 接口对象引用（直接成员）
		writeU64(1u);                                    // ③ 命名映射（接口引用值）
		writeText("obj");
		writeU64(objectHandle);

		IBindNativeShape_ConsumeGraphShape(&fake, bytes.data(), static_cast<uint64_t>(bytes.size()));
		ok = Expect(fake.graphShapeCalls == 1u, "图形状：块没有进入原生实现") && ok;
		ok = Expect(fake.graphRefKinds.size() == 1u && fake.graphRefKinds[0] == 2u,
			"图形状：值类容器元素没有按四字段线形重建") && ok;
		ok = Expect(fake.graphCount == 7u, "图形状：命名映射的标量值没有搬回") && ok;
		ok = Expect(fake.graphAttachmentIndex == 3u && fake.graphAttachmentWeight == 0.5f,
			"图形状：命名映射的记录值没有搬回") && ok;
		ok = Expect(fake.graphRefName == "beta", "图形状：命名映射的值类值没有搬回") && ok;
		ok = Expect(fake.graphObject == shared.get(), "图形状：接口对象引用没有反查回同一个对象") && ok;
		ok = Expect(fake.graphMappedObject == shared.get(),
			"图形状：映射里的接口对象引用没有反查回同一个对象") && ok;

		// 坏句柄：直接成员的对象句柄改成不存在的值 ⇒ 1011，实现计数不增（边界拒）。
		{
			std::vector<uint8_t> bad = bytes;
			// 直接成员句柄在最后一张映射**之前**：尾部 = handle(8) + count(8) + keyLen(8) + key("obj",3) + handle(8)
			const std::size_t directOffset = bad.size() - (8u + 8u + 8u + 3u + 8u);
			const uint64_t bogus = 0xDEADBEEFull;
			::memcpy(bad.data() + directOffset, &bogus, sizeof(bogus));
			IBindNativeShape_ConsumeGraphShape(&fake, bad.data(), static_cast<uint64_t>(bad.size()));
			ok = Expect(fake.graphShapeCalls == 1u, "图形状：坏句柄进入了原生实现") && ok;
			ok = Expect(IBindNativeShape_LastErrorCode(&fake) == 1011,
				"图形状：坏句柄的错误码不是 1011") && ok;
		}
		std::printf("  [图形状] 值类元素=%u 标量值=%u 记录值=(%u,%.2f) 值类名=%s 对象一致=%d/%d\n",
			fake.graphRefKinds.empty() ? 0u : fake.graphRefKinds[0], fake.graphCount,
			fake.graphAttachmentIndex, fake.graphAttachmentWeight, fake.graphRefName.c_str(),
			fake.graphObject == shared.get() ? 1 : 0, fake.graphMappedObject == shared.get() ? 1 : 0);
	}

	// ---- 返回位置标量容器两段式（任务 1.6 / design D13）：Count + Copy 两槽 ----
	{
		const uint64_t count = IBindNativeShape_GetPassIndicesCount(&fake);
		ok = Expect(count == 3u, "标量容器两段式：Count 不等于 3") && ok;
		std::vector<uint32_t> values(static_cast<std::size_t>(count), 0u);
		IBindNativeShape_CopyGetPassIndices(&fake, reinterpret_cast<uint8_t*>(values.data()),
			static_cast<uint64_t>(values.size() * sizeof(uint32_t)));
		ok = Expect(values.size() == 3u && values[0] == 7u && values[1] == 11u && values[2] == 42u,
			"标量容器两段式：逐元素与原生不一致") && ok;

		// 空容器：Count=0；Copy 在零长度上不许解引用（哨兵一个字节都不许被改），空指针+零容量也不许崩。
		fake.passIndicesEmpty = true;
		const uint64_t emptyCount = IBindNativeShape_GetPassIndicesCount(&fake);
		ok = Expect(emptyCount == 0u, "标量容器两段式：空容器 Count 不为 0") && ok;
		uint32_t sentinel = 0xA5A5A5A5u;
		IBindNativeShape_CopyGetPassIndices(&fake, reinterpret_cast<uint8_t*>(&sentinel),
			sizeof(sentinel));
		const bool sentinelIntact = sentinel == 0xA5A5A5A5u;
		ok = Expect(sentinelIntact, "标量容器两段式：空容器 Copy 写了缓冲（不该解引用）") && ok;
		IBindNativeShape_CopyGetPassIndices(&fake, nullptr, 0u);
		fake.passIndicesEmpty = false;
		std::printf("  [标量容器] count=%llu 元素=%u/%u/%u 空count=%llu 空写保护=%d\n",
			static_cast<unsigned long long>(count), values[0], values[1], values[2],
			static_cast<unsigned long long>(emptyCount), sentinelIntact ? 1 : 0);
	}

	// ---- 生成期拒绝探针（任务 2.2 / 2.3 / 3.2 / 3.3 / 1.1 / 1.2 / 1.3 / 1.6）----
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectTemplate.h",
			"IBindRejectTemplateMethod", "模板"),
		"模板成员函数拒绝夹具：没有非零点名 ① 模板/泛型") && ok;
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectFactory.h",
			"RejectContainerRecord", "返回位置容器"),
		"静态工厂返回容器拒绝夹具：没有非零点名 返回位置容器") && ok;
	// 隐式转换重载的拒绝行为必须保持（任务 2.4：既有夹具复跑仍拒）。
	ok = Expect(RunRejectProbe("BindableShapes.RejectOverload.h",
			"IBindRowOverloadReject", "③ 依赖隐式转换的重载"),
		"隐式转换重载拒绝夹具：没有非零点名 ③") && ok;
	// 序列化块记录的超界形状（任务 3.2）：映射容器 / 两层容器 / 出参方向。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectBlockMap.h",
			"RejectMapRecord", "映射容器"),
		"序列化块映射容器拒绝夹具：没有非零点名映射容器") && ok;
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectBlockNested.h",
			"RejectNestedVectorRecord", "两层容器"),
		"序列化块两层容器拒绝夹具：没有非零点名两层容器") && ok;
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectBlockOut.h",
			"RejectOutRecord", "出参方向"),
		"序列化块出参方向拒绝夹具：没有非零点名出参方向") && ok;
	// 参数注入的三条生成期拒绝（任务 3.3）。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectInjectNonPointer.h",
			"IBindRejectInjectNonPointer", "不是裸指针"),
		"注入非指针参数拒绝夹具：没有非零点名不是裸指针") && ok;
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectInjectMissingParam.h",
			"IBindRejectInjectMissingParam", "不存在"),
		"注入参数名不存在拒绝夹具：没有非零点名参数名不存在") && ok;
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectInjectMissingHook.h",
			"IBindRejectInjectMissingHook", "钩子声明"),
		"注入缺钩子声明拒绝夹具：没有非零点名钩子声明") && ok;
	// 内存窗配对的两条生成期拒绝（任务 1.1 / design D2）。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectWindowAmbiguous.h",
			"IBindRejectWindowAmbiguous", "多个整型候选"),
		"内存窗配对不唯一拒绝夹具：没有非零点名 多个整型候选") && ok;
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectWindowMissing.h",
			"IBindRejectWindowMissing", "没有相邻整型长度参数"),
		"内存窗长度缺失拒绝夹具：没有非零点名 没有相邻整型长度参数") && ok;
	// 记录两段式的唯一例外之外（任务 1.2 / design D8）：指向资源对象的裸指针返回仍拒。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectRecordReturn.h",
			"IBindRejectRecordReturn", "不是构建期已知接口清单"),
		"所有权裸指针返回拒绝夹具：没有非零点名裸指针返回不可绑") && ok;
	// 模板实例化的类型名拼错（任务 1.3 / design D9）。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectInstantiateType.h",
			"IBindRejectInstantiateType", "NoSuchType"),
		"模板实例化拼错类型名拒绝夹具：没有非零点名 NoSuchType") && ok;
	// 图形状序列化的未支持类别（任务 1.5 / design D11）：键非 NameHash 的映射生成期点名。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectMapKey.h",
			"RejectIntKeyMapRecord", "NameHash"),
		"键非 NameHash 的映射拒绝夹具：没有非零点名 NameHash") && ok;
	// 返回位置的记录容器（任务 1.6 / design D13）：D13 只开标量/枚举元素，记录元素必须生成期点名。
	ok = Expect(RunRejectProbe("BindableShapes.NativeShapesRejectRecordContainer.h",
			"IBindRejectRecordContainer", "blittable"),
		"记录容器返回拒绝夹具：没有非零点名 blittable") && ok;

	return ok;
}

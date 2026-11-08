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

	// ---- 生成期拒绝探针（任务 2.2 / 2.3 / 3.2 / 3.3）----
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

	return ok;
}

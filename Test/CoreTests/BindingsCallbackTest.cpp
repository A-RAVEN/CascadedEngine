// `castl::function<void(...)>` 回调的原生注册 / 参数顺序 / 替换 / 注销 / 重入 / 异常
// （`complete-csharp-window-system-bindings` 任务 1.2 / 2.1）。
//
// 判据都在生成的 C ABI 上：
//   ① 安装回调后原生实现确实持有它，触发时按声明顺序收到 `void* user` 与两个标量；
//   ② 重复安装 = 替换：只有新回调执行；
//   ③ 用**旧注册号**注销不许误删后来替换的新回调（注销只清理仍匹配的那一项）；
//   ④ 空函数指针表示清空：安装返回 0，原生实现里的回调为空，触发不调用任何东西；
//   ⑤ 回调执行期间修改同一个注册必须失败并给出可诊断错误码，且正在执行的回调不被销毁；
//   ⑥ 回调里抛出的异常不许穿出原生边界，下一次回调注册调用读出错误码与原始文本。
//
// 红态怎么构造：把生成的安装 thunk 改成不调用原生实现（① 失败）、把注销改成按方法清空
// （③ 失败）、去掉活动标记（⑤ 失败）或让异常穿出 lambda（⑥ 进程终止）。

#include "BindableShapes.h"

#include "ca_bind_fixture__IBindRow13.Bindings.cpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
	class FakeRow13 final : public ca_bind_fixture::IBindRow13
	{
	public:
		void SetCallback(castl::function<void(uint32_t, float)> callback) override
		{
			m_callback = callback;
			++m_setCount;
		}

		void Trigger(uint32_t value, float weight)
		{
			if (m_callback)
			{
				m_callback(value, weight);
			}
		}

		bool HasCallback() const { return static_cast<bool>(m_callback); }

		int m_setCount = 0;

	private:
		castl::function<void(uint32_t, float)> m_callback;
	};

	struct Probe
	{
		int calls = 0;
		void* lastUser = nullptr;
		uint32_t lastValue = 0;
		float lastWeight = 0.0f;
	};

	FakeRow13* g_reentrantTarget = nullptr;
	Probe* g_reentrantProbe = nullptr;
	int g_reentrantExitCode = -1;
	uint64_t g_reentrantRegistration = 0;

	void FirstCallback(void* user, uint32_t value, float weight)
	{
		auto* probe = static_cast<Probe*>(user);
		++probe->calls;
		probe->lastUser = user;
		probe->lastValue = value;
		probe->lastWeight = weight;
	}

	void SecondCallback(void* user, uint32_t value, float weight)
	{
		auto* probe = static_cast<Probe*>(user);
		++probe->calls;
		probe->lastUser = user;
		probe->lastValue = value;
		probe->lastWeight = weight;
	}

	void ThrowingCallback(void*, uint32_t, float)
	{
		throw std::runtime_error("回调抛出的原始文本");
	}

	void ReentrantCallback(void* user, uint32_t, float)
	{
		auto* probe = static_cast<Probe*>(user);
		++probe->calls;
		// 回调执行期间替换自己：必须被活动标记挡下，安装返回 0 且错误码是可诊断的
		g_reentrantRegistration = ca_bind_fixture__IBindRow13__SetCallback(g_reentrantTarget, &SecondCallback, g_reentrantProbe);
		g_reentrantExitCode = ca_bind_fixture__IBindRow13__LastErrorCode(g_reentrantTarget);
	}
}

bool RunBindingsCallbackTest()
{
	FakeRow13 object;
	Probe first;
	Probe second;

	// ① 安装 + 参数顺序
	const uint64_t firstRegistration = ca_bind_fixture__IBindRow13__SetCallback(&object, &FirstCallback, &first);
	if (firstRegistration == 0u)
	{
		std::puts("  安装回调没有返回内部注册号");
		return false;
	}
	if (!object.HasCallback())
	{
		std::puts("  安装之后原生实现里没有回调");
		return false;
	}
	object.Trigger(7u, 1.25f);
	if (first.calls != 1 || first.lastUser != &first || first.lastValue != 7u || first.lastWeight != 1.25f)
	{
		std::printf("  回调参数不符：calls=%d user=%p value=%u weight=%g\n", first.calls, first.lastUser,
		            first.lastValue, first.lastWeight);
		return false;
	}

	// ② 替换：只有新回调执行
	const uint64_t secondRegistration = ca_bind_fixture__IBindRow13__SetCallback(&object, &SecondCallback, &second);
	if (secondRegistration == 0u || secondRegistration == firstRegistration)
	{
		std::printf("  替换回调没有拿到新的注册号（旧 %llu、新 %llu）\n",
		            static_cast<unsigned long long>(firstRegistration),
		            static_cast<unsigned long long>(secondRegistration));
		return false;
	}
	object.Trigger(9u, 2.5f);
	if (second.calls != 1 || second.lastValue != 9u || second.lastWeight != 2.5f || first.calls != 1)
	{
		std::printf("  替换后执行的不是新回调：first=%d second=%d\n", first.calls, second.calls);
		return false;
	}

	// ③ 旧注册号注销不许误删新回调
	ca_bind_fixture__IBindRow13__UnregisterCallback(&object, firstRegistration);
	if (!object.HasCallback())
	{
		std::puts("  旧注册号注销把新回调一起清掉了");
		return false;
	}
	object.Trigger(11u, 3.5f);
	if (second.calls != 2 || first.calls != 1)
	{
		std::printf("  旧注册号注销之后新回调没有继续执行：second=%d\n", second.calls);
		return false;
	}

	// ④ 空函数指针：清空且不调用
	const uint64_t cleared = ca_bind_fixture__IBindRow13__SetCallback(&object, nullptr, nullptr);
	if (cleared != 0u || object.HasCallback())
	{
		std::printf("  空函数指针没有清空原生回调（返回 %llu、HasCallback=%d）\n",
		            static_cast<unsigned long long>(cleared), object.HasCallback() ? 1 : 0);
		return false;
	}
	object.Trigger(13u, 4.5f);
	if (second.calls != 2 || first.calls != 1)
	{
		std::puts("  清空之后仍然调用了旧回调");
		return false;
	}

	// ⑤ 重入：回调执行期间替换同一个注册必须失败
	{
		Probe reentrant;
		Probe replacement;
		g_reentrantTarget = &object;
		g_reentrantProbe = &replacement;
		g_reentrantExitCode = -1;
		g_reentrantRegistration = 0u;
		const uint64_t registration = ca_bind_fixture__IBindRow13__SetCallback(&object, &ReentrantCallback, &reentrant);
		if (registration == 0u)
		{
			std::puts("  重入用例：回调没装上");
			return false;
		}
		object.Trigger(17u, 5.5f);
		if (reentrant.calls != 1)
		{
			std::puts("  重入用例：回调本身没有执行");
			return false;
		}
		if (g_reentrantRegistration != 0u || g_reentrantExitCode != ca_bind_fixture__IBindRow13_ERROR_CALLBACK_REENTRANT)
		{
			std::printf("  重入没有被挡下：注册号 %llu、错误码 %d（期望 0 / %d）\n",
			            static_cast<unsigned long long>(g_reentrantRegistration), g_reentrantExitCode,
			            ca_bind_fixture__IBindRow13_ERROR_CALLBACK_REENTRANT);
			return false;
		}
		// 正在执行的回调不许被销毁：它返回之后原生实现里仍然是它
		ca_bind_fixture__IBindRow13__UnregisterCallback(&object, registration);
		if (object.HasCallback())
		{
			std::puts("  重入用例收尾：注销没有清掉回调");
			return false;
		}
	}

	// ⑥ 回调抛异常不许穿出原生边界；下一次回调注册调用读出错误码与原始文本
	{
		const uint64_t registration = ca_bind_fixture__IBindRow13__SetCallback(&object, &ThrowingCallback, nullptr);
		if (registration == 0u)
		{
			std::puts("  异常用例：回调没装上");
			return false;
		}
		object.Trigger(19u, 6.5f);   // 不许把异常抛到这里
		ca_bind_fixture__IBindRow13__SetCallback(&object, nullptr, nullptr);
		if (ca_bind_fixture__IBindRow13__LastErrorCode(&object) != ca_bind_fixture__IBindRow13_ERROR_CALLBACK_EXCEPTION)
		{
			std::printf("  异常用例：错误码是 %d，期望 %d\n", ca_bind_fixture__IBindRow13__LastErrorCode(&object),
			            ca_bind_fixture__IBindRow13_ERROR_CALLBACK_EXCEPTION);
			return false;
		}
		char text[256] = {};
		ca_bind_fixture__IBindRow13__LastErrorText(&object, text, sizeof(text));
		if (std::strstr(text, "回调抛出的原始文本") == nullptr)
		{
			std::printf("  异常用例：错误文本里没有原始异常文本，实际：%s\n", text);
			return false;
		}
		// 异常之后进程还能继续：再装一次并触发
		Probe recovered;
		const uint64_t recoveredRegistration = ca_bind_fixture__IBindRow13__SetCallback(&object, &FirstCallback, &recovered);
		if (recoveredRegistration == 0u || ca_bind_fixture__IBindRow13__LastErrorCode(&object) != 0)
		{
			std::puts("  异常之后后续回调注册/清零失败");
			return false;
		}
		object.Trigger(23u, 7.5f);
		if (recovered.calls != 1 || recovered.lastValue != 23u)
		{
			std::puts("  异常之后后续回调没有正常执行");
			return false;
		}
		ca_bind_fixture__IBindRow13__UnregisterCallback(&object, recoveredRegistration);
	}

	return true;
}

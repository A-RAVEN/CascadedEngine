// 新运行时的用例都在各自文件里，这个文件只剩测试入口与一个分配烟测。
//
// 原先这里还有一整套**旧运行时**的夹具与用例（`TestStruct0/1/2/3`、`TestHash*`、`TestSerialize0`）：
// 它们测的是旧序列化 / 反序列化与旧哈希驱动那条已经退场的路，而且 `TestHash*` 与 `TestSerialize0`
// 在 `main` 里本来就是注释掉的（只剩 `TestAllocation` 真被调用）。新运行时的对应读数在
// `CodeGenRuntimeTest.cpp` / `PipelineStateObjectTest.cpp` / `GeneratedHashTest.cpp` 里。
// 删掉这批夹具之后，旧侧那份"把 `shared_ptr` / `unique_ptr` 成员当托管指针"的特化就没有使用者了 ——
// 它原先只为让含 `shared_ptr` 成员的夹具过旧反射的 `object incompatible for reflection` 断言；
// 实测删掉它整个工程只剩这个夹具文件编不过，所以它随夹具一起从 CASTL 头里删掉了。
#include <CASTL/CASharedPtr.h>
#include <CASTL/CAVector.h>
#include <cstdio>
#include <mimalloc.h>

// 分配烟测：只做分配 / 释放，最后打一份 mimalloc 统计。它不碰序列化，所以用本地结构体，
// 不再依赖上面那批旧夹具。
void TestAllocation()
{
	struct AllocProbe
	{
		castl::shared_ptr<float> value;
		float                    a = 0.0f;
		float                    b = 0.0f;
		float*                   raw = nullptr;
	};

	{
		AllocProbe* testStr = new AllocProbe[32];
		delete[] testStr;
	}

	{
		AllocProbe* testStr = new AllocProbe();
		delete testStr;
	}

	{
		castl::vector<AllocProbe> testVec;
		for (int i = 0; i < 3; ++i)
		{
			testVec.emplace_back();
		}
	}
	mi_stats_print(NULL);
}


// runtime TypeRegistry check (defined in EmitReflectionTest.cpp).
bool RunTypeRegistryTest();
// generated serialization roundtrip check (defined in GeneratedSerializerTest.cpp).
bool RunGeneratedSerializationTest();
// generated hash/equality check (defined in GeneratedHashTest.cpp).
bool RunGeneratedHashTest();
bool RunGeneratedContainerTest();
bool RunGeneratedHashContainerTest();
// CodeGen 自有读写运行时的判据（定义在 CodeGenRuntimeTest.cpp）。
bool RunCodeGenRuntimeTest();
// 真实类型（CPipelineStateObject 一族）的迁移判据（定义在 PipelineStateObjectTest.cpp）。
bool RunPipelineStateObjectTest();
// 私有成员 + 类内友元入口（`CA_CODEGEN_FRIEND`）的判据（定义在 FriendLikeTest.cpp）：
// 生成物按成员名读私有成员，编得过才算友元真的生效。
bool RunFriendLikeTest();
// `TResource` 的新老批字节对拍（定义在 ResourceBatchTest.cpp）。
bool RunResourceBatchTest();
// D3D12 后端 `graphics_backend::ShaderLibrary` 的新老字节对拍（定义在 ShaderLibraryBytesTest.cpp）。
bool RunShaderLibraryBytesTest();
bool RunBindingsGetApiTest();
bool RunBindingsHandleTest();
bool RunBindingsWeakHandleTest();
bool RunBindingsLayoutTest();
bool RunBindingsNestedLayoutTest();
bool RunBindingsCallbackTest();
bool RunBindingsCrossInterfaceTest();
bool RunBindingsRenderFoundationTest();
// 脚本窄渲染适配面的原生图结构 / 字节复制 / 拒绝路径 / 提交生命周期
// （csharp-render-backend-triangle 任务 1.1 / 2.1 / 2.2）。
bool RunScriptRenderBridgeTest();
bool RunWindowBindingsCompileTest();
// 原生形态夹具：非纯虚 virtual / 非虚公开方法 / 自返回链式的原生行为（本 change 任务 2.2）
bool RunBindingsNativeShapesTest();



int main(int argc, char* argv[])
{
	TestAllocation();

	// All three generated-code checks run on EVERY invocation and are reported individually: the
	// aggregate exit code is what fails the build, but the log must show which of them went red and
	// which stayed green in the SAME run (goal item 1: each assertion class needs its own red state,
	// observed while the other two are still passing). Returning early on the first failure — the old
	// behaviour — made that observation impossible.
	bool allOk = true;

	// `IWindow` 的绑定产物能被编译并链接（csharp-bindings-generator 任务 2.10）
	if (!RunWindowBindingsCompileTest())
	{
		std::puts("Window bindings compile test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Window bindings compile test PASSED");
	}

	// 布局清单：镜像结构体与 C++ 侧的真实布局一致（csharp-bindings-generator 任务 2.5）
	if (!RunBindingsLayoutTest())
	{
		std::puts("Bindings layout manifest test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings layout manifest test PASSED");
	}

	// 嵌套纯标量记录：递归镜像、按值搬运、defaulted 比较运算符与非法嵌套拒绝（本 change 任务 1.1）
	if (!RunBindingsNestedLayoutTest())
	{
		std::puts("Bindings nested layout test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings nested layout test PASSED");
	}

	// `castl::function<void(...)>` 回调的原生注册 / 参数顺序 / 替换 / 注销 / 重入 / 异常
	// （本 change 任务 1.2 / 2.1）
	if (!RunBindingsCallbackTest())
	{
		std::puts("Bindings callback test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings callback test PASSED");
	}

	// 弱句柄：登记不抬高引用计数、对象被回收后旧句柄报 1001（`csharp-window-creation` 任务 1.4）
	if (!RunBindingsWeakHandleTest())
	{
		std::puts("Bindings weak handle test FAILED");
		// 任务 5.9：这里原来写的是 `return false;` —— 在 `int main` 里等于退 0，
		// 于是"只看退出码"的判据永远绿。改成置 allOk，让退出码真的变红。
		allOk = false;
	}
	else
	{
		std::puts("Bindings weak handle test PASSED");
	}

	// 句柄表：序号 + 代际、配对释放、失效报错（csharp-bindings-generator 任务 2.4）
	if (!RunBindingsHandleTest())
	{
		std::puts("Bindings handle table test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings handle table test PASSED");
	}

	// 绑定函数表的取表校验（csharp-bindings-generator 任务 2.2）
	if (!RunBindingsGetApiTest())
	{
		std::puts("Bindings GetApi test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings GetApi test PASSED");
	}

	// 跨头文件资源 / 发行接口：发行者与目标两类入口、shared_ptr 入参重建、弱/借用返回与见证
	// （csharp-render-binding-foundations 任务 1.2）
	if (!RunBindingsCrossInterfaceTest())
	{
		std::puts("Bindings cross interface test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings cross interface test PASSED");
	}

	// 渲染基础形状：记录 / 枚举 / 标志位 / 包装值 / 字节混合参数的原生行为 + 生成期拒绝
	// （csharp-render-binding-foundations 任务 1.3）
	if (!RunBindingsRenderFoundationTest())
	{
		std::puts("Bindings render foundation test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings render foundation test PASSED");
	}

	// 脚本窄渲染适配面：真实 GPUGraph 的字节 / 属性 / 绘制 / 清除色 / 呈现清单 / 等待与待完成集合，
	// 以及错误类型句柄与已释放句柄在进入实现之前被挡下（本 change 任务 1.1 / 2.1 / 2.2）
	if (!RunScriptRenderBridgeTest())
	{
		std::puts("Script render bridge test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Script render bridge test PASSED");
	}

	// 原生形态夹具：非纯虚 virtual / 非虚公开方法 / 自返回链式（本 change 任务 2.2）
	if (!RunBindingsNativeShapesTest())
	{
		std::puts("Bindings native shapes test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("Bindings native shapes test PASSED");
	}

	if (!RunTypeRegistryTest())
	{
		std::puts("TypeRegistry test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("CodeGen TypeRegistry test PASSED");
	}

	if (!RunGeneratedSerializationTest())
	{
		std::puts("Generated serialization test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("CodeGen serialization test PASSED");
	}

	if (!RunGeneratedHashTest())
	{
		std::puts("Generated hash test FAILED");
		allOk = false;
	}
	else
	{
		std::puts("CodeGen hash test PASSED");
	}

	if (!RunGeneratedContainerTest())
	{
		std::puts("Generated container test FAILED");
		allOk = false;
	}
	if (!RunGeneratedHashContainerTest())
	{
		std::puts("Generated container hash test FAILED");
		allOk = false;
	}

	if (!RunCodeGenRuntimeTest())
	{
		std::puts("CodeGen runtime read/write test FAILED");
		allOk = false;
	}

	if (!RunPipelineStateObjectTest())
	{
		std::puts("CPipelineStateObject migration test FAILED");
		allOk = false;
	}

	if (!RunFriendLikeTest())
	{
		std::puts("CodeGen private-member friend entry test FAILED");
		allOk = false;
	}

	if (!RunResourceBatchTest())
	{
		std::puts("CodeGen TResource batch roundtrip test FAILED");
		allOk = false;
	}

	if (!RunShaderLibraryBytesTest())
	{
		std::puts("CodeGen ShaderLibrary (D3D12) bytes test FAILED");
		allOk = false;
	}

	return allOk ? 0 : 1;
}

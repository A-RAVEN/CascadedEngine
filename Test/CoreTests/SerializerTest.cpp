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
bool RunBindingsLayoutTest();
bool RunWindowBindingsCompileTest();



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

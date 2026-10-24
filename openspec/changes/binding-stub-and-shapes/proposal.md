## Why

上一轮归档的 `csharp-bindings-generator` 交付的绑定发射器**只发签名、不发实现**：`IWindow` 的函数表 22 个槽里有 10 个是桩 —— 桩的定义是"thunk 函数体里一次都没出现 `object->`"，即根本没调实现。而构建与签名对拍都看不出这件事：后者的比对对象是 `extern "C"` 签名文本，桩与真实现的签名逐字相同。于是"C# 能调 `IWindow`"这句话只对 12 个方法成立，`WindowShouldClose` / `IsKeyDown` / `GetWindowName` 这些从 C# 看过去恒为假或恒为空。

这个 change 只做一件事：**让判据能看出桩，然后把 `IWindow` 的桩清零**。它不碰接口签名、不碰句柄表、不碰托管侧 —— 那些留给后续那个"C# 能建窗口"的 change。

## What Changes

- **构建必须跑绑定判据**：`build.py` 今天一次都没跑过 `Tools/CodeGen/check_bindings.py`（`Select-String -Path "build.py" -Pattern "check_bindings"` 零命中）。改为构建内调用，并把当前构建目录传进去 —— 该脚本的产物目录硬编码成 `out/build/x64-debug/...`，而 `build.py` 默认配置的产物在 `x64-relWithDebugInfo`，不传就会判错目录。
- **新增"桩计数"判据**：不再数某句注释文本（注释只对非 `void` 返回才写，对 `void` 返回的空体恒盲），改成**结构判据** —— 从函数表成员表出发找到对应的 thunk，剥掉注释后要求出现 `object-><方法名>(` 形式的调用、且结果被 `return` 或被赋值；`decltype` / `(void)` 丢弃结果这类写法不算。另加两条硬条件：**投影返回 `void` 而实现返回非 `void`** 要判缺陷（挡住"调了但把返回值丢掉"）；**thunk 数必须等于独立清单里的方法数**（挡住整个方法被静默排除）。判定分两档：真实接口桩数必须为 0；夹具接口只打印、不判定。
- **桩清零（`IWindow` 侧）**：
  - `bool` 返回：判"能否直出"的条件写的是 `bareType(method.returnSpelling) == returnType`，而 `bool` 投影成 `int32_t`，两者永不相等 ⇒ 8 个槽全落桩。改为显式集合判定，thunk 写 `return object->M() ? 1 : 0;`。
  - 枚举返回：同一个条件也把枚举排除了；但枚举**不能**像 `bool` 那样直接 `return object->M();`（`enum class` 不能隐式转 `int32_t`），必须 `static_cast<int32_t>`。只改 `bool` 会让 `IBindRow01::GetMode` 编不过。
  - `void*` 返回：`bareType("void*")` 会把指针剥成 `"void"`，于是 `GetNativeWindowHandle` 的返回值被丢掉。改成投影 `uint64_t` 不透明令牌，C# 侧 `ulong`。**只限 `void*` 这一种拼写** —— 别的 `T*` 返回仍必须 `CA_BIND_SKIP` 写理由，不许被静默绑成裸地址令牌。
  - 字符串入参：thunk 是空体（`castl::string_view` / `castl::string` 没法从裸指针直接构造），改成造临时量再传给实现。
  - 两段式返回：`_GetXxxLen` / `_CopyXxx` 这两个伴生函数现在**只有声明、没有实现**（`IWindow` 里各只有 1 处命中），改成一个问长度、一个拷出，各自独立取一次实现的值，容量为 0 时一个字节都不写。**顺带把这类方法的直出入口与直出 thunk 一并不发**：返回值只能从伴生取，留在函数表里只能是个空体桩 → `IWindowApi_v1` 从 22 个成员变 21 个，期望文件少 2 行（`IBindRow03_GetName` / `IBindRow04_GetNameView` 的直出入口）。

## Capabilities

### New Capabilities
（无。这个 change 不引入新能力，只把既有能力的实现与判据补齐。）

### Modified Capabilities
- `csharp-bindings`: 新增"真实接口的函数表里不许有桩"、"构建必须跑绑定判据"、"标量 / 枚举 / 不透明指针的返回值必须走真实现"、"两段式伴生函数必须有实现体"四条要求。

## Impact

- 生成器：`Tools/CodeGen/CodeGenMain.cpp`（返回值投影、thunk 生成）。
- 判据：`Tools/CodeGen/check_bindings.py`（结构桩判据 + 产物目录参数）、`build.py`（调用判据并传目录）。
- 夹具：`Tools/CodeGen/BindableShapes.h` 不加形状（本轮不新增夹具行），但 `Tools/CodeGen/BindableShapes.expected.txt` 里 `bool` / 枚举 / 字符串 / 两段式那几行的签名不变、实现要变真。
- 测试：`Test/CoreTests/BindingsGetApiTest.cpp`（行为断言落点）与 `Test/CoreTests/SerializerTest.cpp`（注册调用）。
- 文档：`Tools/CodeGen/README.md` 第十四节、`docs/testing.md`、`docs/TODO.md`。
- **不改**：任何接口签名（`WindowSystem.h` 一行不动）、句柄表、`weak_ptr` 投影、托管侧、`IWindowSystem`。

## Non-goals

- 不动接口：`WindowSystem.h` 一行不改，`IWindowSystem` 不加 `CA_BIND`。
- 不动句柄：不合并 `handles` 与 `keepAlive`、不加释放入口、不实现弱句柄。
- 不动托管侧：生成的 `.cs` 仍不参与托管工程编译。
- **不实现容器形状的行为**：夹具里 4 个容器参数的 thunk（`IBindRow05::SetThings` / `IBindRow06::GetThings` / `IBindRow07::SetNested` / `IBindRow08::SetMapping`）、1 个镜像按值返回的 thunk（`IBindRow02::GetThing`）、1 个回调 thunk（`IBindRow13::SetCallback`）本轮仍是桩，桩判据对夹具产物只打印不判定；这几条记进 `docs/TODO.md` 并写明略去了什么。
- 不新增夹具行、不改投影矩阵的行文（矩阵行数与行文都不动；但两段式那两行直出入口在期望文件里要删掉，那是形状收口，不是改矩阵）。
- **不动托管侧，并接受一个连带后果**：`IWindowApi_v1` 少一个成员后，引擎回填的表大小从 184 变 176，而 `Scripting/Managed/Host.cs:135` 手抄着 `WindowApiSize = 8 + 8 * 22` —— 托管侧取表时会打印"函数表大小不符"并直接返回，那个 `GetWindowSize` 探针不再执行。本 change 不改它（改了就越界到托管侧），把这条写成已知后果、记进 `docs/TODO.md`、归后继 change 修。另外本 change 也不跑 `GPUBackendTester`，只跑 `CoreTests`。

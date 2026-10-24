## 1. 判据：先让桩能被看见

- [ ] 1.1 产物目录改成**单一来源**：删掉 `Tools/CodeGen/check_bindings.py` 里的模块级 `PRODUCT_DIR`，改为必填参数 `--product-dir`，`read_generated()` 与 `check_alignment()` 都必须用它（今天只有一处硬编码在 `:31`，两处使用在 `:98` 与 `:123` —— 只把参数喂给新加的桩判据、这两处仍读旧路径的话，"判的是当前配置的产物"就没有可失败的判据）。目录不存在时按"判据自身失效"退出 **2**；同时修掉读期望文件没剥 UTF-8 BOM 的问题（第 1 行带 BOM，`IBindRow01_LastErrorCode` 那条签名**从来没被比对过**）
  验收：传不存在的目录 MUST 退出 2 并打印"产物目录不存在，判据自身失效"；剥 BOM 后基线 MUST 从"签名 22 行"变"**23** 行"且不符数仍 0；再补一条变异：把 `expected.txt` 第 1 行那个签名改错一个字符，脚本 MUST 报不符（**今天不会报**）

- [ ] 1.2 把 `IWindow` 纳入签名对拍：`check_bindings.py` 里 `SYMBOL_RE` 硬编码了 `IBindRow\d\d_`，所以 `IWindow` 的签名一行都不在任何自动判据里。改成按期望行里的符号名直接去产物里找（不限定前缀），并把 `IWindow` 的 **21** 条投影签名写进 `Tools/CodeGen/BindableShapes.expected.txt`（含 `GetNativeWindowHandle` 的 `uint64_t`、以及**不含** `GetWindowName` 直出入口 —— 见 2.5）
  验收：期望文件里 MUST 出现 `IWindow` 前缀的行；把产物里 `IWindow_WindowShouldClose` 的返回类型临时改一个字符，脚本 MUST 报不符（今天不报）

- [ ] 1.3 在同一个脚本里加**结构桩判据**，口径必须写死到能失败：解析每份 `<接口>.Bindings.cpp` 里 `<接口>Api_v1` 的**成员表**，按成员名找到对应的 `_thunk_<方法名>` 函数体，做四件事 —— ① 先剥掉 `//` 与 `/* */` 注释；② 匹配 `object->\w+\s*\(`（不是裸子串 `object->`）；③ 排除 `decltype` / `sizeof` / `noexcept` 上下文与 `(void)` 前缀；④ 要求调用的结果出现在 `return` 里或被赋给命名变量（投影返回 `void` 的槽允许只调用）。**另加一条独立红条件**：投影后的返回类型是 `void`、而实现返回的原始拼写去掉 `const` 后不是 `void`，即算缺陷 —— 否则 `GetNativeWindowHandle` 那种"调了但把返回值丢掉"的形态永远不算桩。真实接口由脚本自己算（产物接口减去 `Tools/CodeGen/BindableShapes*.h` 里声明的接口），算出来是空集时退出 2（不许调用方传名单，名单为空会让"桩数为 0"恒真）；夹具接口只打印不判定（design D2）
  验收：跑它 MUST 打印"被判真实接口：IWindow"、`IWindow` 桩 **10** 个并以非零退出；夹具桩数 MUST 打印（今天 11 个）。**两条变异必须都变红**：① 把桩体那句注释改写成含 `object->` 的文本，判据 MUST 仍判为桩；② 把某个 `bool` 槽的 thunk 写成 `(void)object->M(); return {};`，判据 MUST 仍判为桩

- [ ] 1.4 加一条**覆盖率下限**：桩判据同时断言"某接口的 thunk 数 MUST 等于该接口独立清单里的方法数"。今天把方法整段 `CA_BIND_SKIP` 掉、或从发射循环里过滤掉，thunk 与函数表成员会**一起**消失 —— 桩数照样 0、两侧对齐照样 0 处，整个方法静默不见（实测构造：过滤掉 `GetKeyState` ⇒ 21 个 thunk、0 桩、`IWindowApi_v1_Size` 21、全绿）
  验收：构造上面那个过滤，脚本 MUST 非零退出并点名"`IWindow` 少了 `GetKeyState`"；独立清单 MUST 来自期望文件（1.2 写进去的那 21 行），不取自生成器自己

## 2. 形状：把 `IWindow` 的桩清零

- [ ] 2.1 修 `bool` 返回：把 `Tools/CodeGen/CodeGenMain.cpp` 里"能否直出"的判据从 `bareType(method.returnSpelling) == returnType` 换成显式集合（`void` / 标量 / 枚举 / 指针走直出，只有要加 `C` 后缀的镜像类型不直出），`bool` 的 thunk 生成 `return object->M() ? 1 : 0;`（design D3）
  验收：生成物里 8 个 `bool` thunk 体内 MUST 出现 `object->`；**行为断言必须双向且必须有红态**：写进 `Test/CoreTests/BindingsGetApiTest.cpp` 并在 `SerializerTest.cpp` 注册，断言"实现返回真 ⇒ C ABI 读到 1"与"返回假 ⇒ 读到 0"。注意现有 `BindingsGetApiTest.cpp:108` 那条 `GetBool(&object, true)` 对桩**也绿**（`FakeRow01::GetBool` 返回 `!value`，两个 0 相等），所以红态 MUST 用"返回真 ⇒ 读到 1"这一半来造

- [ ] 2.2 修枚举返回：枚举（`enum class`）的 thunk 生成 `return static_cast<int32_t>(object->M());`（design D3 —— 枚举不能隐式转 `int32_t`，只做 2.1 的直出会让 `IBindRow01::GetMode` 编不过）
  验收：`IBindRow01_thunk_GetMode` 体内 MUST 出现 `static_cast<int32_t>(object->GetMode(`，且 `python build.py --config Debug` MUST 通过；**另加经 C ABI 的读数断言**：在 `BindingsGetApiTest.cpp` 里调 `table.GetMode(&object, k)` 并断言读到 `(int32_t)k`（今天全仓没有任何一处经函数表调 `GetMode`，只靠"文本命中 + 编译通过"会被 `static_cast(...); return {};` 一起骗过）

- [ ] 2.3 修 `void*` 返回值，**只限 `void*` 这一种拼写**：`projectMethod` 里若 `returnSpelling` 去掉 `const` 后正好是 `void *`，投影成 `uint64_t` 不透明令牌，thunk 生成 `return reinterpret_cast<uint64_t>(object->M());`，C# 侧 `ulong`（design D4）。**别的指针返回不许走这条** —— 审计表对 `IShaderCompiler*` 这类要求走句柄表 + 配对释放，一律转成裸令牌等于把所有权不明的指针静默绑成裸地址，违反 `csharp-bindings` 现有要求「② 类必须 `CA_BIND_SKIP`」
  验收：`IWindow_GetNativeWindowHandle` 的签名 MUST 从 `void` 变 `uint64_t`；行为断言用**假 `IWindow` 子类**（照 `BindingsGetApiTest.cpp:19` 的 `FakeRow01` 写法）返回一个固定哨兵地址，断言 C ABI 读回值等于该地址 —— **必须用假对象**，`Test/CoreTests` 只链 `CACore` / `glm` / `CAGeneralReourceSystem_Interface`，`WindowSystem` 是运行时加载的 `MODULE`，这一层开不了真窗口；另加一条：给一个未排除的 `T*` 返回，生成器 MUST 非零退出

- [ ] 2.4 补字符串入参的 thunk：从 `char const* + len` 造临时对象再传给实现（design D5）
  验收：`IWindow_thunk_SetWindowName` 的函数体 MUST 出现 `object->SetWindowName(` 与一处临时量构造（今天这里只有一行形状注释）；加上 2.5 之后，"设一个名字 → 读回同一个名字"的行为断言 MUST 通过

- [ ] 2.5 补两段式返回值，并**删掉这类方法的直出入口**（design D5）：`_GetXxxLen` 与 `_CopyXxx` MUST 各自生成实现体，两个伴生各自独立取一次实现的值，`Copy` 拷 `min(cap - 1, size)` 字节并补 NUL、**`cap == 0` 时一个字节都不写**；返回字符串（`string_view` 或按值 `castl::string`）的方法 MUST **既不进函数表、也不发 `extern "C"` 直出入口、也不发直出 thunk**（三个都不发才自洽：只删表槽会让空体 thunk 留着算桩，整段 `continue` 会把刚发的伴生声明一起删掉）。`IWindowApi_v1` 从 22 个成员变 **21** 个，期望文件少 **2** 行（`03|void IBindRow03_GetName`、`04|void IBindRow04_GetNameView`）
  验收：`IWindow_GetWindowName` 直出入口在产物里 MUST 0 命中、`IWindowApi_v1` 里 `static_assert(offsetof(...))` 的条数 MUST 是 21、`IWindowApi_v1_Size` MUST 从 `8 + 8 * 22` 变 `8 + 8 * 21`；两个伴生各 MUST 至少 2 处命中（声明 + 定义）；行为断言**必须三方对齐**：`Len` 的值 MUST 等于设进去的名字长度、`Copy` 写入的字节数 MUST 也等于它（只断言"Len 非 0"会被 `Len` 算错、`Copy` 只写一半的实现骗过）；再加 `cap == 0` 一条（不崩、不写）

- [ ] 2.6 真实接口桩数为 0：跑 `python Tools/CodeGen/check_bindings.py --product-dir out/build/<配置>/codegen/bindings`
  验收：被判的真实接口集合 MUST 含 `IWindow`、其桩数 MUST 为 `0`、thunk 数 MUST 等于 21、退出码 `0`，且 `python build.py --config Debug` MUST 通过

- [ ] 2.7 加一条**变异用例**证明判据对新形状有效（必须排在 2.6 之后 —— 排在桩清零之前基线本来就是"退 1"，那条"还原后恢复退出 0"永远达不到）
  验收：把 `IWindow` 某个已修的槽改回 `return {};`，判据 MUST 退 1 并点名 `IWindow::<方法>`；还原后 MUST 回退 0。改动前 0 → 改动后 1 → 还原后 0，三个读数都要贴

- [ ] 2.8 把判据接进构建：`build.py` 在 `build_bindings()` 之后调 `check_bindings.py` 并把当前构建目录传进去，脚本非零退出即让构建失败（现在 `Select-String -Path "build.py" -Pattern "check_bindings"` 零命中）
  验收：① `build.py` MUST 打印它传给判据的 `--product-dir` 实参，该行 MUST 逐字等于当前 preset 的产物目录（不带 `--config` 跑时 MUST 出现 `x64-relWithDebugInfo`）；② `build.py` MUST **转发判据的 stdout**（现在是 `capture_output=True` + 只打自己那行摘要，判据打印的夹具桩数会被吞掉），构建成功日志里 MUST 出现一行夹具桩数（修完应是 6）；③ 造红态 MUST 临时改**发射器源码**（例如把某个 `bool` 强行落回 `return {};` 分支）—— **不能**改生成物（构建会重新生成、覆盖掉），也**不能**用 `CA_BIND_SKIP`（那样 thunk 直接消失、桩数不升）

## 3. 收尾：文档、遗留、构建验证

- [ ] 3.1 同步 `Tools/CodeGen/README.md` **第十四节**（第 1041 行起至文件末）：补上五条逐字规则句 —— `bool` 走 `? 1 : 0`、枚举走 `static_cast<int32_t>`、`void*` 投影成 `uint64_t` 不透明令牌、字符串入参从 `char const* + len` 造临时量、两段式只发两个伴生并写明 `cap == 0` 不写；再写一句"夹具里容器、镜像按值返回、回调这三类形状本轮未覆盖"。`docs/testing.md` 补桩判据与行为断言落在哪一层
  验收：在**第十四节范围内**逐条数，五条规则句 + "三类未覆盖" MUST 各至少 1 处命中（今天该范围内全是 0 处；全文数会假绿，所以 MUST 限定范围）；`docs/testing.md` 里 MUST 出现 `check_bindings.py` 与它所在层的说明，并把它那句硬编码的脚本数改对；新写的 `CodeGen.exe` 命令 MUST 用 `python Tools/CodeGen/verify_readme.py` 跑过并贴真实输出（该脚本只跑 `CodeGen.exe` 命令行，**不能**当内容判据）

- [ ] 3.2 更新 `docs/TODO.md` 四件事：
  ① 删掉本 change 修掉的四条，锚点从文件里**逐字复制**：`` `bool` 返回一律落桩 ``、`` `void*` 返回被吞 ``、`` 两段式只发声明不发实现 ``、`` 字符串入参 thunk 是空体 ``（执行前各命中 1 处、执行后各 0 处）；
  ② 未修的四条 MUST 仍各命中 1 处：`` `castl::function` 参数一律投影成固定签名 ``、`` 句柄入参的反查根本没实现 ``、`` `setError` 往 `char[256]` ``、`` 只有 1001 / 1002 两个码 ``；
  ③ **改掉这一条里会被本 change 推翻的三处陈述**：第 15 行的"22 个槽里 10 个是桩"、第 41 行改法里已兑现的那几项、第 45 行验收里的"必须报 10 处桩"；
  ④ 新增一条"夹具里容器 / 镜像按值返回 / 回调三类形状本轮未覆盖（6 个方法）"；
  ⑤ 新增一条已知后果（见 4.1）
  验收：①与 ④ 各自 MUST 从命中 1/0 变成 0/1；② 的读数逐条贴出来；③ 改完后 `10 个是桩` 与 `必须报 10 处桩` 两个串 MUST 0 命中；⑤ 的读数见 4.1

- [ ] 3.3 构建与测试验证：跑 `python build.py --config Debug`，失败则分析并修到 `BUILD SUCCESSFUL`；然后跑 `out/build/x64-debug/bin/CoreTests.exe`
  验收：构建 `BUILD SUCCESSFUL`；`CoreTests.exe` 退出码 0，且**贴出新增那几条断言各自的 `PASSED` 行**（只报"`FAILED` 行数 0"分不清"用例跑过并绿"与"用例根本没被 `SerializerTest.cpp` 的 main 调用"）；`check_bindings.py --product-dir <当前目录>` 报真实接口含 `IWindow`、桩 0、thunk 数 21、不符数 0、夹具桩数打印出 6

## 4. 已知后果（不在本 change 修）

- [ ] 4.1 `IWindowApi_v1` 从 22 个成员变 21 个，引擎回填的 `realSize` 从 184 变 176，而 `Scripting/Managed/Host.cs:135` 手抄着 `WindowApiSize = 8 + 8 * 22` —— 托管侧取表时会打印"函数表大小不符"并直接 `return`，`GetWindowSize` 探针不再执行。本 change 不动托管侧（见 proposal 的 Non-goals），这一条 MUST 记进 `docs/TODO.md` 并归后继 change
  验收：`docs/TODO.md` 里 MUST 新增这一条，写清"谁失效、在哪一行、由哪个 change 修"；`out/build/x64-debug/bin/GPUBackendTester.exe --backend vulkan --test TestScriptHost --headless 30` 的日志里 MUST 能读到那句"函数表大小不符"（这是已知后果的取证，不是本 change 的回归）

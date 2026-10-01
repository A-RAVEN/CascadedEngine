> 每条任务的 **Oracle** 必须能失败；标了「红态」的，必须把**改动前**的命令与真实输出贴进对话，只贴绿不算证据。
> 构建只走 `python build.py`（项目唯一合法入口）。本 change **一行都不改** `Reflection.h` / `Serialization.h` / `Hasher.h`。
> **前置条件**：`wire-codegen-real-type` 必须先落地 —— 它把接入清单从三列改成四列（多一列"生成物入库目录"），并要求既有黄金样本在那一步逐字节不变；本 change 会改表结构、必然改黄金样本，所以顺序不能反。

## 1. 分类按规范化类型判定（单独一步，先确认不动既有产物）

- [ ] 1.1 分类前取类型的规范化形式，别名不再落进 `Opaque`
  **Oracle**：新增的分类夹具编进 CoreTests，用 `static_assert` 直接断言表的种类值 —— `unsigned int` 与 `uint32_t` 都是 `TypeKind::UInt`、`unsigned long long` 与 `uint64_t` 都是 `TypeKind::UInt`（断言写的是 `TypeDescriptor<T>::fields()[i].kind`，不是"程序退出 0"）。**红态（改动前）**：同一断言在改动前**编译失败**（`uint32_t` 那一项实际是 `TypeKind::Opaque`），把编译错误原文贴出来
- [ ] 1.2 既有生成物不受影响
  **Oracle**：`Tools/CodeGen/generated/EmitSample.{Reflection,Serialization,Hash}.h` 三个文件在 1.1 之后**逐字节不变**（`EmitSample` 全是 `int` / `float` / `bool` / `double`），`python build.py --config Debug` 退出码 0
- [ ] 1.3 根因写对（`CXType_Elaborated` 不是 `CXType_Typedef`）
  **Oracle**：工具加一段只在本任务内使用的诊断输出（打 `clang_getCursorType(...).kind` 的数值），跑分类夹具得到 `uint32_t` 是 119、`unsigned int` 是别的值，把输出贴进证据文件；诊断代码随后删除

## 2. 容器识别与元素描述

- [ ] 2.1 按结构与运行时同口径识别容器（有 `size()` 且有 `value_type` 的记录类型；C 数组按 extent），不按名字列白名单
  **Oracle**：夹具里 `castl::vector<unsigned>`、`castl::string`、`castl::unordered_map<unsigned, unsigned>`、`castl::set<int>` 四者都带 `Container` 标志，且 `vector` 的元素描述给出 `UInt`、`unordered_map` 带 `Unordered` 且给出 `key` / `element` 两层。**红态（改动前）**：四者全是 `TypeKind::Opaque` 且没有任何标志位 —— 贴出改动前的表
- [ ] 2.2 字段表增加元素描述（`key` / `element` 两个指向同构描述的指针）与四位标志（`Container` / `Unordered` / `HasOpaqueElement` / `Wrapper`），定义放在 `CACore/header/CodeGenMeta.h`；**不改 `TypeKind` 枚举**
  **Oracle**：夹具里 `castl::vector<castl::vector<int>>` 能读出两层容器（外层元素的元素是 `Int`）；`castl::vector<int*>` 与 `castl::vector<castl::shared_ptr<T>>` 的 `HasOpaqueElement` 都置位；`int a[2][3]` 的元素描述是 `int[3]`（不是 `int`）
- [ ] 2.3 新增夹具头与测试单元，并**进接入清单**：`Tools/CodeGen/TestContainer.h` + `Test/CoreTests/CodeGenContainerTest.cpp`，清单按 `wire-codegen-real-type` 之后的**四列**格式加一条（头文件、类型名、模块根 `Tools/CodeGen`、生成物入库目录 `Tools/CodeGen/generated`）
  **Oracle**：`python build.py --config Debug` 的 codegen 段打印出该类型的三个产物路径；把清单条目删掉后这些产物不再生成（红态可核）
- [ ] 2.4 表结构变更后把生成物**复制入库**并确认 git 记录到改动
  **Oracle**：先把 `out/build/x64-debug/codegen/EmitSample.<种类>.h` 复制到 `Tools/CodeGen/generated/`，再跑 `git status --short Tools/CodeGen/generated/`，要求输出恰好三行 ` M Tools/CodeGen/generated/EmitSample.<种类>.h` —— 把这三行真实输出贴出来（注意：`build.py` 只写 build 目录、**从不写回仓库**，所以"被 git 记录为修改"必须由这条复制动作产生，不能指望构建）
- [ ] 2.5 编译期断言：工具认出的容器集合与运行时口径一致
  **Oracle**：同一个编译单元里对同一批类型断言"表里带 `Container` 标志" ⇔ `containerStates<T>::is_container_with_size`；把一个夹具换成非容器（`struct Plain { int a; };`）后断言仍成立（对照组，防止断言恒真）

## 3. 无序容器的规范顺序（含嵌套与哈希范围）

- [ ] 3.1 生成代码对 `unordered_map` / `unordered_set` 成员：摊平成元素数组（`map` 收成 `pair<K,V>`，去掉 const）→ 按**参与排序的一侧**排序（`map` 按键、`set` 按元素）→ 按数组形状写出
  **Oracle**：两个内容相同、插入顺序不同的 `unordered_map` 序列化后**逐字节相同**。**红态（改动前）**：两次产物字节不同（贴出两份产物与首个不同字节的偏移）
- [ ] 3.2 嵌套层级同样稳定：元素类型的读写改为**调用该元素类型自己的生成序列化器**（而不是把整个元素交给旧运行时），逐层摊平
  **Oracle**：`struct Inner { castl::unordered_map<unsigned, unsigned> m; }; struct Outer { castl::vector<Inner> items; };`，两个只有 `items[0].m` 插入顺序不同的对象 → 产物逐字节相同、哈希相同。**红态（改动前实测）**：112 字节、首个不同字节在第 20 字节、哈希 `c188763afb62ef98` 与 `1020e97f4c8baf58`
- [ ] 3.3 哈希按同一套规范顺序累加，并把**浮点规范化递归到元素**（元素里的 `float` / `double` 也按 `-0.0 → +0.0`、NaN 归一的规则参与，不再整块按字节算）
  **Oracle**：两个 `castl::unordered_map<uint32_t, float>`，值只差 `+0.0f` / `-0.0f`：生成的相等判断为真时两个哈希**必须相同**。**红态（改动前实测）**：相等为真、哈希 `1a2742e842fb0203` 与 `1a27c2e842fbdb83` 不同
- [ ] 3.4 读回来逐个插入，与写入顺序无关
  **Oracle**：把排序后写出的字节读回空容器，读回内容与写出内容相等；另测读进非空容器（注意旧运行时在"元素个数不为 0"时不 clear，这条要如实记录结论）
- [ ] 3.5 对照组与回归：内容不同则字节与哈希都不同；有序容器产物不变
  **Oracle**：内容不同的两个 `unordered_map` → 字节不同且哈希不同（防止 3.1/3.3 恒真）；有序容器用**事先存档的对照物**比 —— 改动前先对含 `castl::vector<int>` / `castl::map<int,int>` / `castl::string` 的夹具生成三份产物存进 `openspec/changes/codegen-container-semantics/evidence/before/`（贴出命令与输出），改动后对同一夹具再生成，与那份 before 逐字节对拍

## 4. 拒绝路径

- [ ] 4.1 四条拒绝：① 参与排序的一侧没有全序（`map` 键 / `set` 元素；`map` 的值不检查）；② 元素含裸指针 / 托管指针 / 句柄且请求序列化；③ 重复键容器（`multimap` / `multiset` / `unordered_multimap` / `unordered_multiset`，按规范化类型名判定）；④ 键或元素不可默认构造
  **Oracle**：四条各有一个具名夹具与非零退出、stderr 点名成员与原因。**红态（改动前）**：`castl::vector<int*>` 出序列化是 exit 0 且写出"个数 + 空元素"；`castl::vector<castl::shared_ptr<T>>` 同样 exit 0；`std::multiset<int>` 被当成普通容器 exit 0 —— 三条红态输出都要贴
- [ ] 4.2 只请求反射时 ①③④ 不受影响
  **Oracle**：同一批夹具 `--emit-reflection` 全部 exit 0 且有产物
- [ ] 4.3 新增退出码与拒绝条件写进 `Tools/CodeGen/README.md` 的退出码表
  **Oracle**：`git grep -n -F "<新退出码各自的含义>" -- Tools/CodeGen/README.md` 逐条命中（带文件限定；`verify_readme.py` 只跑命令示例、**从不检查退出码表**，所以这里必须用 grep），并跑一次 `verify_readme.py` 确认 0 处不符

## 5. 包装器标记与对外文档

- [ ] 5.1 元数据标出包装器及其被包装类型（`FieldFlags::Wrapper`），读写语义仍沿用旧运行时（`managed_wrapper_traits` 解包、裸指针与托管指针不写 / 读回空 / 哈希按地址）
  **Oracle**：包装器成员夹具的表里 `Wrapper` 置位，并断言被包装类型名与 `HasOpaqueElement` 的**具体取值**（写出期望值，不是"能读出"）；另把改动前后同一夹具的生成读写代码贴出来，逐字节相同（证明没有新增包装器语义）
- [ ] 5.2 `Tools/CodeGen/README.md` 写明容器规则与磁盘形状契约：先写元素个数、再按规范顺序逐个写；`map` 写键值对数组；多维数组按维度嵌套写；无序容器的排序规则；四条拒绝条件
  **Oracle**：`git grep -n -F "<契约里的关键句>" -- Tools/CodeGen/README.md` 命中（带文件限定），且 `verify_readme.py` 0 处不符

## 6. 编译验证与修复

- [ ] 6.1 跑 `python build.py --config Debug` 直到 `BUILD SUCCESSFUL`（失败则分析并修到通过），随后跑 `CoreTests.exe` 与 `python Tools/CodeGen/verify_readme.py`
  **Oracle**：构建退出码 0；`CoreTests.exe` 全部用例 PASSED、退出码 0；`verify_readme.py` 0 处不符

## 1. 修复 VMA 泄漏（D1 — 诊断已于 2026-08-23 spike 完成）

> **诊断结论（spike，非 task）**：泄漏 allocation 为 `VulkanBuffer::Init` / `VulkanTexture::Init` 创建。**泄漏对象不限于"恰好 3 个"（对抗验证[AUDIT-5]更正）**——凡经 `CreateGPUBuffer`/`CreateGPUTexture` 创建且未显式释放的对象（含 `IMGUIContext::m_Fontimage`，IMGUIContext.cpp:491 创建、:631 仅 `m_Fontimage={}`）均按同机制泄漏；实测触发 `vmaDestroyAllocator` abort 的单测为 TestTriangleWithImageBuffer/TestDoublePass/TestComputeBuffer（exit 3），TestIMGUI 单独跑抛 unknown exception（未走完 CreateGPUTexture，独立问题）。根因 = `VulkanBuffer`/`VulkanTexture`/`GPUBuffer`/`GPUTexture`/`VulkanSubobjectBase` 均无析构函数调 `Release()` 且后端无调用者；`CreateGPUBuffer`/`CreateGPUTexture` 返回的 `shared_ptr` 归零时对象被销毁但 `m_Allocation` 从未 `vmaDestroyBuffer`/`vmaDestroyImage`。

**修复 = 双层（L1 对象死即释放 + L3a 登记 VMA allocation 扫残留，见 design D5；用户拍板两点）**：① L1——GPU 资源的 `shared_ptr` 计数归零时对象必须自行正确释放其 VMA allocation（含 `m_Fontimage`）；② L3a——即使资源没被释放拖到 teardown，backend 收尾**必须能扫到并释放**它（登记 VMA allocation 而非 weak_ptr 对象，旧的 weak 方案扫不到死对象是 bug）。**D3D12 超范围声明（[AUDIT-10]）**：D3D12 同构 allocation 泄漏不在本 change 范围；**GAP-C**：`VulkanShaderStruct::Release()`（VulkanShaderStruct.cpp:55）无条件 `GetDevice()`，与 VulkanTexture.cpp:183 同类 pApp 悬垂隐患，建议并行补判成员非空的守卫。

- [ ] 1.1 **L1 — 选型（方案 B 自定义 deleter）**：`RenderBackend_Vulkan::CreateGPUBuffer`/`CreateGPUTexture` 返回的 `shared_ptr` 装自定义 deleter，refcount 归零时调 `Release()` — **必须放弃 `make_shared`**[AUDIT-7]：`castl::shared_ptr`=**std::shared_ptr**（`CACore/CMakeLists.txt:55 USING_EASTL=0`、`CASharedPtr.h:7 #include <memory>`），`std::make_shared` **无带 deleter 重载**。实现改 `new` + 二参 deleter：`castl::shared_ptr<VulkanBuffer>(new VulkanBuffer(), [](VulkanBuffer* p){ p->Release(); delete p; })`（VulkanTexture 同理）。**注意**：旧后端 `CRenderBakend_Vulkan.cpp:30-33` 是"工厂返回裸指针 + deleter 调 `ReleaseGPUBuffer(p)`"，非 `new VulkanBuffer()+delete p`——勿作字面先例。若 `make_shared` 后再 `.get()`+二参构造 → 双所有权/双 delete；用别名构造虽共享 control-block 但沿用默认 deleter → 泄漏回归。**不采用方案 A（析构函数）**[AUDIT-1/H2]：类 default move 浅拷贝，双份析构各 Release → double-free。方案 B（deleter）对象 `new` 堆建、从不被 move，规避。
- [ ] 1.2 **L1 — Release() 真幂等**：`VulkanBuffer`/`VulkanTexture` 的 `Release()` 改为**判 `m_Allocation`（VMA 持有句柄）而非判 handle，且无条件复位全部成员**——**保留现行 view→image 顺序**[AUDIT-8]（VulkanTexture.cpp:185 destroyImageView 先于 :191 FreeImage，正确）——
  - `VulkanBuffer::Release()`：`if (m_Allocation) GetApp()->GetMemoryManager().FreeBuffer(m_Buffer, m_Allocation);` 然后 `m_Buffer=nullptr; m_Allocation=VK_NULL_HANDLE; m_MappedPtr=nullptr;`
  - `VulkanTexture::Release()`：头部 `if (m_Allocation==VK_NULL_HANDLE) return;`；`if (m_ImageView) GetDevice().destroyImageView(m_ImageView); if (m_Image && m_Allocation) GetApp()->GetMemoryManager().FreeImage(m_Image, m_Allocation);` 然后 `m_Image=nullptr; m_Allocation=VK_NULL_HANDLE; m_ImageView=nullptr;`（**view 在 image 之前**；`if(m_ImageView)` 分支才取 `GetDevice()`——把 :183 无条件 `auto device=GetDevice()` 移入该分支，防 pApp 悬垂）
  - 依据：VMA 保证 `vmaCreateBuffer`/`vmaCreateImage` 成功则句柄+allocation 成对，`m_Allocation` 非空蕴含句柄有效。
- [ ] 1.3 **L3a — 登记 VMA allocation 表（核心）**：`VulkanMemoryManager` 增加 `m_Allocations: castl::unordered_map<VmaAllocation, AllocRecord>`（`AllocRecord{ Kind{Buffer|Image|Memory}; vk::Buffer buffer; vk::Image image; VulkanSubobjectBase* owner; }`）。登记：`AllocateBuffer`/`AllocateImage`/**`AllocateMemory`** 成功后 `m_Allocations[allocation] = {..., owner}`（**`Allocate*` 加 `owner` 参数，`VulkanBuffer::Init`/`VulkanTexture::Init` 必须传 `this`——不得默认 nullptr 后不传，否则 sweep 无法 `owner->Release()`、闭环破**；GraphLocal 裸资源/别名池/虚拟块物理分配传 nullptr=ownerless，kind=Memory）。**⚠️ Round-5 [AUDIT-R5-1]：`AllocateMemory` 也是分配路径，必须一并进表**（别名池 `VulkanResourceAliasing.cpp:209`、`virtual-block` 物理提交 `:473` 主 happy-path 均走 `AllocateMemory`；tasks 原只写 `AllocateBuffer`/`AllocateImage`，与 design D5 场景4「单个物理 `m_AliasedPoolAllocation` 进登记表（AllocateMemory ownerless）」不符——漏登记则 L3a sweep 兜不住该块、`vmaDestroyAllocator` 仍可能 outstanding）。摘除：`FreeBuffer`/`FreeImage`/**`FreeMemory`** 全改**幂等** `auto it=m_Allocations.find(alloc); if(it==end) return; [vmaDestroyBuffer/Image/vmaFreeMemory]; m_Allocations.erase(it);`（"是否已释放"真值存表，非对象成员）。
- [ ] 1.4 **L3a — sweep 扫残留必释放**：`VulkanMemoryManager::Release()` 在 `vmaDestroyAllocator` **之前**（覆盖 1.3 登记的全部 kind，**含 kind=Memory——`AllocateMemory` 的别名池物理块/虚拟块物理提交**；[AUDIT-R5-1]）：
  ```
  // (a) 先快照 owner(只收集不改 map, 防 owner->Release() erase 迭代器失效)
  for (auto& [alloc,rec] : m_Allocations) if (rec.owner) owners.push_back(rec.owner);
  // (b) 带 owner(对象仍存活)→ 虚调用 owner->Release(): 清空对象 m_Allocation + 经幂等 Free* 摘除
  for (auto* owner : owners) owner->Release();
  // (c) 剩余(ownerless 别名池/raw)→ 按 kind 幂等 FreeBuffer/Image/FreeMemory
  // (d) m_Allocations.clear(); 再 vmaDestroyAllocator
  ```
  用户要求②达成：**任何未释放的 allocation（无论对象死活）都在表里，sweep 必扫到释放**。扫残留必须早于 `vmaDestroyAllocator`（sweep 全程 `m_Allocator` 与 `device` 仍有效，因 backend Release :575 sweep < :581 device.destroy < :598 instance.destroy）。
- [ ] 1.5 **L1+L3a double-free 消除（登记表幂等作唯一真值）**：所有释放路径（L1 deleter→`VulkanX::Release`→`Free*`，L3a sweep→`owner->Release()`→`Free*`）必经同一幂等 `Free*`（find-or-return→vmaDestroy→erase）。L3a 先 `owner->Release()` 清空对象 `m_Allocation` → 之后对象再死（deleter `Release()`）见 `m_Allocation==null` 直接 return → **不 double-free、不 deref 已销毁 pApp**。补回归用例：「对象活到 backend Release 后，被 L3a sweep 释放，其 deleter 二次 Release 成 no-op」。
- [ ] 1.6 **GAP-C — VulkanShaderStruct 守卫**：`VulkanShaderStruct::Release()`（VulkanShaderStruct.cpp:55）现无条件 `GetDevice()`，与 VulkanTexture.cpp:183 同类 pApp 悬垂隐患。改为仅当确有可释放对象（成员非空）时才取 `GetApp()`/`GetDevice()`。
- [ ] 1.7 **防御 — move 双释放纵深**：`VulkanBuffer`/`VulkanTexture` 的 `=default` move（VulkanBuffer.h:13-14）建议 `= delete` move ctor/assign（或自定义 move 置空源对象 m_Buffer/m_Allocation/m_Image/m_ImageView），封死浅拷贝双释放（配合登记表幂等）。
- [ ] 1.8 **禁止**改 `VulkanMemoryManager::Release()` 去"容忍泄漏/禁断言"（掩盖非修复）；确认 `m_MemoryManager.Release()`（`vmaDestroyAllocator`）时 allocator 无 outstanding allocation，断言自然消失。

## 2. 修复 d3d12 窗口销毁 UAF（D2 — 2026-08-23 修正根因）

> **根因修正**：dump 栈非"`~shared_ptr<IWindow>` deleter→`glfwDestroyWindow` 双销毁"，而是 `WindowSystem_ImplGlfw_WindowFocusCallback`(WindowSystem_Impl.cpp:67) → `std::function` invoker（`std::_Func_class<IWindow*,bool>::operator()`，functional:930），`Rax=0xDDDDDDDDDDDDDDDD` = 回调函数对象已释放。即"销毁窗口时 GLFW 派发 window-focus 事件打到已释放的 `s_WindowSystem`/`m_WindowFocusCallback`"——逆序 teardown 中窗口销毁时序与回调对象生命周期错位。

- [ ] 2.1 核对逆序 teardown 中窗口销毁与 GLFW 回调对象（`s_WindowSystem`/`m_WindowFocusCallback` 等 `std::function` 成员）的释放顺序：窗口是否在 `WindowSystem` 实例销毁前被销毁？销毁时 GLFW 是否仍持有指向已释放 `WindowSystem`/回调对象的引用？
- [ ] 2.2 修法（按新根因）：保证窗口销毁（`glfwDestroyWindow`）发生在 `WindowSystem`（含其回调 `std::function` 成员）仍有效时；或在窗口销毁前解除 GLFW 对该窗口的回调（`glfwSetWindowFocusCallback(window, nullptr)` 等），避免销毁期间派发事件打到已释放接收者。`s_WindowSystem` 全局指针在 `WindowSystem` 析构时置空。
- [ ] 2.3 防御兜底：`WindowImpl::Release()` 改为幂等（`if (m_Window) { glfwDestroyWindow(m_Window); m_Window=nullptr; }`），防重复销毁。**注意**：幂等只防重复销毁，不防"回调打到已释放接收者"，所以不能只靠幂等。

## 3. 恢复 MiniDump 捕获（D3）

- [ ] 3.1 确认 auto-dismiss hook（`_CrtSetReportHook2` + `MessageBoxTimeout`）后 CRT abort/`abort()` 是否仍走 MiniDump 捕获路径；若否，让 MiniDump handler 覆盖 `abort`/`SIGABRT`（`_set_abort_behavior`/`signal(SIGABRT)`）或在 abort 前主动写 `crash_*.dmp`。
- [ ] 3.2 验证：vulkan teardown abort（VMA 断言或 device-lost）仍产生 `crash_*.dmp`；auto-dismiss 弹窗保留（**问题可见 + 自动关闭**，不阻塞），不得完全静默。

## 4. 构建 + 回归（D4）

- [ ] 4.1 `python build.py --config Debug` 全量构建成功（只经项目脚本）。
- [ ] 4.2 vulkan 全量 7 测试 `--headless 1/200`：退出码 0 + 无新增 `crash_*.dmp` + validation-log 无新增错误（区分已知 IMGUI draw-bind VUID-04007/07312）。**必须用全量 7 测试**（真实触发路径），单测试 `--test` 仅作对照。
- [ ] 4.3 d3d12 全量 7 测试 `--headless 200`：退出码 0 + 无新增 dump（窗口销毁修复验证）。
- [ ] 4.4 验收：任何新 dump 用 `Tools/read_dump.py` 快读（cdb 交叉验证）。

## 5. Review & Adversarial Verify（审查闭环——最后一个任务）

- [ ] 5.1 对全部修改做对抗验证审查：VMA 生命周期（`vmaDestroyAllocator` 前提 = 无 outstanding allocation，`vk_mem_alloc.h` 语义，`[VkMemoryAllocator vmaDestroyAllocator](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/struct_vma_allocator.html)`）；GLFW 回调生命周期与线程约束（`[glfwSetWindowFocusCallback](https://www.glfw.org/docs/latest/group__window.html#ga543c7e1f80d7384ff585ba6e7f60d59b)`、`glfwDestroyWindow` 在窗口回调期间派发行为）；MiniDump `abort`/SEH 捕获 API 正确性（`_set_abort_behavior`/`signal`/`SetUnhandledExceptionFilter`/`_CrtSetReportHook2`）。按 CLAUDE.md 引用官方文档（MCP `web-reader`/`web-search-prime`，**禁用 WebSearch/WebFetch**）。
- [ ] 5.2 对抗者独立复查审查者引用的每个文档 URL 真实性（存在性 + API 在该页 + 语义一致）。
- [ ] 5.3 每轮审查先做回归检查（VMA 是否真无泄漏、窗口是否单点销毁、dump 是否真写出、是否引入新资源生命周期回归），记录 Review Log。
- [ ] 5.4 新增 [AUDIT] task 前与 Review Log 去重；审查发现的新问题作为 [AUDIT] task 追加，循环直到无新问题或 3 轮；**停下的那一刻交付 Review Log 报告**。

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| — | 2026-08-23 | — | 诊断（spike，非 task）：VMA 泄漏 = `VulkanBuffer::Init`/`VulkanTexture::Init`；d3d12 窗口 UAF = 回调函数对象释放（非双销毁） | 已并入本 tasks 的修复决策 | — |
| 1 | 2026-08-23 | workflow 对抗验证（5 假设） | 见下方 | 见下方 | — |
| 2 | 2026-08-23 | 对抗者复核 | 见下方 | 见下方 | — |

**Round 1 — 对抗验证 5 假设（workflow）发现与处置：**
- **[H2] L1 幂等性 PARTIAL**：`VulkanBuffer`/`VulkanTexture`/`VulkanSubobjectBase` default-move 对 `vk::Buffer`/`VmaAllocation` 是浅拷贝（vulkan_handles.hpp:25-38），moved-from 仍持同一 `m_Allocation` → 双销毁双 `FreeBuffer` → double-free；且 `VulkanTexture::Release()` 无条件 `GetDevice()`（VulkanTexture.cpp:183）→ pApp 悬垂崩。→ **[AUDIT-1]** 定稿方案 B（deleter，对象不被移动）；release 改判 `m_Allocation`（非 handle）+ 无条件复位 + 判非空才取 pApp。
- **[H3] L3a 容器类型 PARTIAL**：`std::weak_ptr` 无 `operator==`/`std::hash`，`cacore::hash` 落 aggregateHasher（Hasher.h:40,115）在未反射 std 类型上无 hash 产出 → `unordered_map<weak_ptr>` 编译不过；且接口 `GPUBuffer`/`GPUTexture` 无 `Release()` → `weak_ptr<GPUBuffer>->lock()->Release()` 编译不过。→ **[AUDIT-2]** 用"裸指针 key + weak_ptr value"（`m_WindowHandles` 范式），pointee 用具体类型 `VulkanBuffer`/`VulkanTexture`。
- **[H4/H5] 必要性夸大**：L3a 现有 7 测试 0 命中，"缺一不可"夸大；"backend 销毁后不用 GPU 资源、UAF 应得"辩护的是不可达场景（`Release()` 仅 ~RenderBackend 调、设 m_Released）；半死对象被使用是静默 UB/断言非"干净报数"。→ **[AUDIT-4]** 改为"L1 缺一不可，L3a 前瞻性防御"；删"UAF 应得"作 L3a 依据，改诚实声明。
- **[H5] D3D12 "COM 自释放"是事实错误**：`GPUResource` 存裸 `D3D12MA::Allocation*`（GPUResource.h:19，非 ComPtr 无析构）；`D3DImageObject::Release()` 只释放 view 描述符（D3DImageObject.cpp:11-22）未释放 allocation → D3D12 wrapper 死亡时同样泄漏。→ **[AUDIT-3]** 修正设计/proposal 对比表述（非 COM 自释放）。**注意：此处"真实区别是分配器是否断言（D3D12MA 无断言）"本身是错的，见 Round-2 [AUDIT-9]**；D3D12 泄漏真实成因=窗口 UAF 先退出或 RelWithDebInfo 断言 no-op。
- **[H1/F1] "恰好 3 个泄漏/TestIMGUI 干净"表征不可靠**：`IMGUIContext::m_Fontimage` 经 `CreateGPUTexture`（IMGUIContext.cpp:491）、`Release()` 仅 `m_Fontimage={}`（:631）丢弃 shared_ptr、`~VulkanTexture` trivial → 其 `m_Allocation` 同样不释放=同机制泄漏；且 TestIMGUI 单独跑抛 unknown exception（未走完 CreateGPUTexture）。→ **[AUDIT-5]** 更正为"凡经 CreateGPUBuffer/CreateGPUTexture 创建且未显式释放的对象（含 m_Fontimage）均按同一机制泄漏"；TestIMGUI 属独立问题另案。

**Round 2 — 修正后复核（workflow）发现与处置：**
- **[R1-AUDIT-1 PARTIAL]**：修正方向对，但处方含 2 处新伤。—— **[AUDIT-7]**：`castl::shared_ptr`（castl=EASTL，Round-3 更正为 **std**，见下）`make_shared` 无带 deleter 重载，"装 deleter + 保留 make_shared"自相矛盾；须放弃 make_shared 改 `new`+二参 deleter。**[AUDIT-8]**：tasks 1.2 伪代码把 VulkanTexture 释放顺序写反（image→view），应保留现行 view→image（VulkanTexture.cpp:185→191）。
- **[R2-AUDIT-2 CORRECT]**：L3a 容器/pointee 具体类型符合源码事实；AUDIT-2 修正准确落地。
- **[R3-AUDIT-3 又引入新事实错误]**：把"COM 自释放"换成"分配器是否断言"仍是错的——D3D12MA **也有**同款断言（D3D12MemAlloc.cpp:8219 `m_pMetadata->IsEmpty()`、"THE MOST IMPORTANT ASSERT"、:8271 `Unfreed committed allocations found!`、:92 `#define D3D12MA_ASSERT(cond) assert(cond)`）。真实差异=两者 Debug 都 abort、Release 都静默。→ **[AUDIT-9]**。
- **[R4-AUDIT-4/5 CORRECT]**：L3a 前瞻性防御表述、m_Fontimage 同机制泄漏表征均准确。
- **[R5 new gaps]**：**[GAP-A]** D3D12 泄漏有正当性却无任务（scope 不一致）→ **[AUDIT-10]**（用户裁定：D3D12 声明超范围）。**[GAP-C]** `VulkanShaderStruct::Release()`(VulkanShaderStruct.cpp:55) 无条件 `GetDevice()` pApp 悬垂隐患 → 捕[ AUDIT-10]。**[回归确认]** Round-1 改的是计划工件非源码；**[依赖]** L1-B deleter + L3a 二次 Release 需幂等守卫才安全 → tasks 1.5。

**Round 3 — 收尾回归（workflow）发现与最终判定：**
- **[F1-AUDIT-7 PARTIAL]**：可操作结论（放弃 make_shared 改 new+二参 deleter）成立，但两条支撑事实错误——① `castl::shared_ptr`=**std**（`CACore/CMakeLists.txt:55 USING_EASTL=0`、`CASharedPtr.h:7 #include <memory>`），非 EASTL（F4 验证者曾误判 EASTL，F1/源码证 std——连验证者也会被表象迷惑）；② 旧后端 `CRenderBakend_Vulkan.cpp:30-33` 是"工厂 `NewGPUBuffer` 返回裸指针 + deleter 调 `ReleaseGPUBuffer(p)`"，非 `new VulkanBuffer()+delete p`。→ 已在 tasks 1.1 / design Risk:77 更正表述（"castl=std"、旧后端勿作 new+delete 字面先例）。
- **[F2-AUDIT-8 CORRECT]**：VulkanTexture.cpp:187 destroyImageView 先于 :193 FreeImage，tasks 1.2 保留 view→image 且注明"与现行一致"，未反转。补充：FreeImage 守卫建议保留 `&& m_Image`（防非对称态传空 image）→ 已改 tasks 1.2。
- **[F3-AUDIT-9 CORRECT]**：D3D12MemAlloc.cpp:92/8219/8271 与 VMA vk_mem_alloc.h:10492 同为 assert()→Debug abort/Release 静默，"两者 Debug 都 abort、Release 都静默"准确。
- **[F4-整体 CORRECT]**：方案 L1B(custom deleter)+L3a(weak_ptr 扫描)无逻辑缺陷，能根治 3 个 VMA 崩溃；AUDIT-8/9/10 按源码事实落地；AUDIT-7 结论可实施仅表述需更正（已完成）；tasks 1.2 FreeImage 守卫应保留 `&& m_Image`（已改）。

**最终判定（Round 3 末）——方案可实施**：核心 L1B+L3a 逻辑无缺陷，AUDIT-1..10 全部按源码事实修正完成（AUDIT-7 的 2 条支撑事实已在 Round-3 更正）。实施前待办：① 把 design Risk:77"接口类型重建绕过 deleter"升级为独立回归 task；② GAP-C 顺带核对 `VulkanWindowHandle::Release`（VulkanWindowHandle.cpp:53/224）同类无条件取 pApp；③ tasks.md 所有 task 仍 `- [ ]` 未勾选（计划工件，未落地源码）。

**Round 4 — 用户拍板两点 → L3a 架构根本性修正（[AUDIT-12]）**：
用户指出旧 L3a(weak_ptr<对象>)是 bug：weak 只能扫到"活得"对象，死对象(m_Fontimage)weak 已 expired 扫不到 → 所谓"兜底"扫不到真实泄漏。用户拍板两点：① L1——shared_ptr 计数归零时对象必须自行正确释放（含 m_Fontimage）；② L3a——即使资源未释放拖到 teardown，**必须能扫到并释放**（真正兜底）。
→ 修正：L3a 改**登记 VMA allocation**（`{VmaAllocation, vk 句柄, owner}`）而非对象；`Free*` 幂等(find-or-return→vmaDestroy→erase，"已释放"真值存表)；sweep 在 `vmaDestroyAllocator` 前扫残留——带 owner 调 `owner->Release()`（清空对象 m_Allocation），ownerless 直接 vmaDestroy。**double-free 消除**：所有释放路径必经同一幂等 `Free*`，L3a 先释放+清空对象句柄，对象之后析构 deleter `Release()` 见 null 直接 return。**L1 管"死在 teardown 前"，L3a 管"活到 teardown 时"（m_Fontimage），两者互补**；旧"L3a 0 命中/前瞻性防御/UAF应得"表述全部作废。tasks 1.3-1.5 / design D5 / proposal / spec 已按此重写。
> **重要**：synthesis 引用的 `Test/VulkanRendererBackendTester/Main.cpp`（判 m_Fontimage 活到 teardown 的唯一依据）**该文件不存在**——其论据部分基于错误路径。但"登记 allocation 而非对象"的架构修正本身满足用户要求②，不受此影响。m_Fontimage 归属(L1 或 L3a)取决于 IMGUIContext 在 backend 销毁时是否先释放，未定死；不影响架构，实现后由回归验证确认。

**Round 5 — 补充对抗验证（实施前，2026-08-26；聚焦前 4 轮未覆盖的两正交维度 = L3a 登记表覆盖缺口 + 外部 API 文档真实性/语义，CLAUDE.md 硬性要求）**：37 agents / 1.88M tokens（wf_0fa61dfc-624）。

**确认（真缺口/修正）**：
- **[AUDIT-R5-1]** tasks 1.3 登记表与 design D5 场景 4 **不一致——漏 `AllocateMemory`**（别名池 `VulkanResourceAliasing.cpp:209`、`virtual-block` 物理提交 `:473` 主 happy-path 均走 `AllocateMemory`；`tasks` 只写 `AllocateBuffer`/`AllocateImage` → 该块不进表，L3a sweep 兜不住、`vmaDestroyAllocator` 仍可能 outstanding；`FreeMemory` 也须幂等化）。
- **[AUDIT-R5-2]** `m_Fontimage` 实为 **L1** 而非"活到 teardown"的 L3a 案例——逆序下 IMGUIContext(index7) 先于 backend(index2) 销毁（CaModuleManager.cpp:63 逆序 + `delete`），其在 backend 前析构走 L1 deleter。修正前 4 轮多处误当 L3a 实证的表述。
- **[AUDIT-R5-3]** `Allocate*` 加 owner 必须**强制传 this**（若默认 nullptr 后调用点不传 → 登记 ownerless → sweep 无法 `owner->Release()`，闭环破）。
- **外部 API 核查**（均 MCP 抓取核验真实 + 语义一致）：`vmaDestroyAllocator` 前提确为"无 outstanding allocation 否则断言"（VMA 3.x `vk_mem_alloc.h:10492`，`VMA_ASSERT_LEAK==VMA_ASSERT`，Debug 生效/Release 静默）；`_set_abort_behavior`/`signal`/`SetUnhandledExceptionFilter`/`_CrtSetReportHook2` 均为真实 MS Learn 文档；**abort→dump 的正确机制是 `signal(SIGABRT)` 而非 `SetUnhandledExceptionFilter`**（后者不覆盖 abort）——tasks 3.1 已含 `signal(SIGABRT)`，方向正确。

**Refute（不采信，避免误改）**：double-free 链不闭合（L3a 先释放+清空 `m_Allocation` → deleter no-op，闭环**确实闭合**）；`VulkanLinearMemoryManager` 直连 vma（staging 自管 `Release`、不进表**正确**）；L3a 必要性夸大（用户②已拍板）；`Allocate*` 加 owner 破坏既有调用（6 处需改，可加在末尾默认参、staging 不影响）。

**回归检查**：前 4 轮已决问题（castl=std、view→image、D3D12 也有断言、登记 allocation 非对象）在 6b0d50a 现状无回归（本轮改的是计划工件，非源码）。

**Round 6（未来）**：实施后源码落地再跑对抗验证（源码级，非当前阶段）。

### Round-5 [AUDIT] 去重结果（本轮新增，均未与前 4 轮重复）

| AUDIT | 来源 | 决策 | 状态 |
|-------|------|------|------|
| [AUDIT-R5-1] tasks 1.3 登记表漏 `AllocateMemory` | HIGH→MED | tasks 1.3 补 `AllocateMemory` 登记（kind=Memory, ownerless）+ `FreeMemory` 幂等；tasks 1.4 / spec / proposal Impact 同步 | ✅ 已修 |
| [AUDIT-R5-2] `m_Fontimage` 属 L1 非 L3a | MED | design/proposal 多处"活到 teardown"实例改为 L1 归属 + [AUDIT-R5-2] 标注 | ✅ 已修 |
| [AUDIT-R5-3] owner 参数强制传 this | MED | tasks 1.3 明确"必须传 this，不得默认 nullptr 后不传" | ✅ 已修 |
| [AUDIT-R5-4] 外部 API 文档真实性/语义 | 核查 | vmaDestroyAllocator / signal / _set_abort_behavior / SetUnhandledExceptionFilter / _CrtSetReportHook2 均真实 + 语义一致；abort→dump 机制 = signal(SIGABRT) | ✅ 核验，无 URL 捏造 |
| [AUDIT-R5-5] 文档锚点行号漂移 | 核对 | 时序本身正确（`m_MemoryManager.Release()` 实为 :604、`m_Device.destroy()` :629），仅 tasks/proposal 锚点行号漂移 ~5；proposal L9 描述该漂移 | 备注（非阻塞） |

# CascadedEngine 路线图

> 这份文件只回答两个问题：**现在处在哪个阶段**、**接下来沿哪些阶段到达产品目标**。
> 它不记任务、不记欠账、不记改动细节 —— 进度事实源只有 `openspec/changes/<change>/tasks.md`，
> 欠账只有 `docs/TODO.md`，本文件只做指针。

**最后更新**：2026-11-03
**维护规则**：见 `CLAUDE.md` 的「文档维护」一节

---

## 一、定位

已确证的技术定位（`docs/project-baseline.md` 第 14 行原话）：

> CascadedEngine 是一个多后端渲染引擎，采用可插拔的图形后端架构。引擎使用 Render Graph (GPUGraph) 架构进行帧编排，支持 D3D12 和 Vulkan 两种渲染后端。

**产品目标：初步可用的游戏引擎 + 编辑器。** 使用者能够导入资源、创建并保存场景、编写 C# 脚本、进行输入交互与原生渲染，并通过网页编辑器制作和导出独立运行的游戏程序。C++ 负责原生执行、GPU 资源和同步，C# 承载高级渲染概念、游戏脚本与编辑器服务；物理、音频、运行时界面等按验证游戏需要补齐。

---

## 二、阶段

阶段划分的依据不是规划，是从已发生的事情里归纳：凡是「一组 change 共同完成同一件事」，就记成一个阶段。

### 阶段 1：双后端可用（已完成）

目标：Vulkan 后端能完整执行 GPUGraph 的所有 Pass 类型，与 D3D12 同级。

依据：`docs/vulkan-backend-alignment.md` 的 Phase 1（7 项）与 Phase 2（4 项）全部标「✅ 已完成」，该文件同时记录了 6 次关闭事件（2026-05-01 至 2026-05-11）。

### 阶段 2：C# 脚本通路（已完成）

目标：托管侧能加载、能调引擎接口、能建出肉眼可见的窗口。

链路是六个 change，按归档日期排：

| 顺序 | change | 归档 |
|---|---|---|
| 1 | `csharp-bindable-surface` | 2026-10-18 |
| 2 | `csharp-hostfxr-bootstrap` | 2026-10-18 |
| 3 | `csharp-bindings-generator` | 2026-10-20 |
| 4 | `binding-stub-and-shapes` | 2026-10-25 |
| 5 | `csharp-window-creation` | 2026-10-25 |
| 6 | `csharp-visible-window` | 2026-10-26（10/10） |

产物是五份能力规范：`csharp-host`、`csharp-bindings`、`csharp-bindable-surface`、`csharp-window-runtime`、`csharp-script`。

### 阶段 3：收尾（已完成）

| 项 | 状态 | 位置 |
|---|---|---|
| Vulkan GPU 查询体系（时间戳 / 遮挡 / 管线统计） | 11/11，2026-11-01 已归档 | `openspec/changes/archive/2026-11-01-vulkan-gpu-queries/` |

本阶段另外完成的两项：

- **IMGUI 字体改 RGBA32 + identity swizzle** —— 14/14，2026-10-26 归档到 `openspec/changes/archive/2026-10-26-fix-imgui-font-whitebox/`。因果判据：把采样视图加回 `SingleChannel(eR)`，vp0 白像素从 97 变成 233。
- **后端对齐 Phase 3 的 `RunTestCode`** —— 复核为「不是差距」，无需实现，见下注。

> **「RunTestCode」为什么不是差距**（2026-10-26 复核）：
> `Interface/RenderInterface/header/CRenderBackend.h` 的基类是 `virtual void RunTestCode(){};` 空实现；
> `D3D12RenderBackend/private/RenderBackend_D3D12.cpp` 的 override 函数体 **40 行全是注释**，一句活代码也没有；
> 全仓 **0 处调用**。两边都空，谈不上「Vulkan 缺这一项」。
> `docs/vulkan-backend-alignment.md` 第 68 行写的「D3D12 已实现 / Vulkan 未实现」，把「有一个 override 壳子」当成了实现。

---

## 三、通往产品目标的后续阶段

以下阶段均为方向。阶段 4 的两个 change 均已归档（第二个 11/11，2026-11-03 归档到 `openspec/changes/archive/2026-11-03-csharp-render-backend-triangle/`），其余内容**尚未立 OpenSpec change**。编号表达目标依赖，不代表已批准的任务清单，也不要求所有工作严格串行。

### 阶段 4：C# 渲染闭环（下一阶段）

目标：以现有 GPUGraph 为底层执行接口，让 C# 能创建所需资源、设置着色器参数、构建并提交图，再由 C++ 完成原生执行、同步与呈现。相机、材质、灯光等高级概念优先在 C# 引擎框架中组织，不在首版另建一套 C++ 高层对象体系。

建议按两个先后衔接的 change 推进；第一个已归档到 `openspec/changes/archive/2026-11-02-csharp-render-binding-foundations/`（16/16，2026-11-02），第二个已归档到 `openspec/changes/archive/2026-11-03-csharp-render-backend-triangle/`（11/11，2026-11-03）：

| 顺序 | change 方向 | 阶段结果 |
|---|---|---|
| 1 | C# 渲染绑定的生成器与句柄基础能力 | `CodeGen.exe` 能投影首个切片所需的数据记录、枚举与标志位、名称与路径值、字节数据；智能指针实参与跨接口资源句柄具备完整转换、类型校验和失效规则。 |
| 2 | C# 简单三角形纵向切片（已归档到 `openspec/changes/archive/2026-11-03-csharp-render-backend-triangle/`，11/11，2026-11-03） | 通过窄纯虚适配面以句柄建图与提交，原生线程调度器留在引擎内部；由 C# 构建并提交的简单三角形在 D3D12 与 Vulkan 上均通过原生真实像素检查。 |

两个 change 均已归档；后续方向还包括**其它 GPU 后端接口的 C# 绑定**：按现有后端测试与产品需要，分批覆盖着色器参数、纹理与采样、多阶段图、计算、读回和 GPU 查询等能力。这些内容均尚未立 OpenSpec change，需分别明确接口范围、资源所有权与可失败的验收，不代表通用渲染框架已获批准。首个切片与未决问题见 `TODO/006-csharp-render-backend-binding.md`。

### 阶段 5：游戏运行时基础

目标：补齐资源导入与引用、序列化、场景与实体组件、层级变换、时间与输入，以及 C# 脚本注册、挂载和生命周期。稳定对象编号、资源所有权和主线程边界先于大规模接口扩张；首版不要求完整 ECS，也不要求向游戏脚本开放原生线程调度。

阶段结果：C# 脚本能够操作场景对象，场景能够保存和加载，资源与脚本生命周期能够形成一致闭环。

### 阶段 6：网页编辑器闭环

目标：网页负责编辑器界面，C# 编辑器服务负责命令、事件、选择、撤销与编辑器对象编号，C++ 保留原生视口和引擎状态。编辑器接口与游戏脚本接口分开，资源导入、场景编辑和 GPU 诊断逐步接入同一工作流。

阶段结果：使用者能够通过网页界面操作真实引擎状态，完成资源与场景编辑，并保留原生 Vulkan/D3D12 渲染。架构与首个窗口属性面板切片见 `TODO/005-web-editor.md`。

### 阶段 7：独立程序与验证游戏

目标：补齐独立程序入口、资产打包和 C# 运行时部署，选定一款验证游戏，并按其需要补物理、音频、运行时界面等能力。性能数据用于决定哪些 C# 热路径需要下沉到 C++，不预先按概念名称划分语言边界。

阶段结果：从编辑器制作内容到导出、启动并运行独立游戏程序形成完整闭环。验证游戏尚未选定，其类型与必需功能是本阶段的未决范围决策。

---

## 四、指针（本文件不复制内容）

| 想知道什么 | 去哪 |
|---|---|
| 某条要求算不算数 | `openspec/specs/<能力>/spec.md` |
| 某个改动做到哪了 | `openspec/changes/<change>/tasks.md` |
| 还欠什么 | `docs/TODO.md` |
| 后端还差什么 | `docs/vulkan-backend-alignment.md` |
| 有哪些模块 | `docs/project-baseline.md` |
| 试过并失败的做法 | `docs/failed-approaches.md` |

---

## 五、维护

**什么时候必须改这份文件**：产品目标或后续阶段方向变化时；一个 change 归档时；某个阶段从「进行中」变成「已完成」时；后续阶段立项时。

**怎么发现它过期**（规格已定，脚本尚未实现）：

`python Tools/check_roadmap.py` 必须做三件事，任一不满足即非零退出：

1. 本文件正文里出现的每个 `openspec/changes/...` 与 `docs/....md` 反引号路径都能 `Test-Path`。**含 `<` 或 `>` 的模板片段跳过** —— 本文件里有 3 处这样的占位符（`<change>/tasks.md`、`<名字>/`、`archive/<日期>-<名字>/`），不跳过的话判据会对着模板报假红。
2. 「阶段 2」表里列出的每个 change 名，都能在 `openspec/changes/archive/` 下找到名字里含它的目录。
3. 「阶段 3」表里每行「状态」写的分数与对应 `tasks.md` 的实际勾选数一致。

第 2 条和第 3 条是这一整节的关键：没有它们，这个文件可以永远写着「已完成」和「12/14」而没人发现它错了。

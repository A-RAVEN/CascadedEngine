# CascadedEngine 路线图

> 这份文件只回答两个问题：**现在处在哪个阶段**、**下一个阶段做什么**。
> 它不记任务、不记欠账、不记改动细节 —— 进度事实源只有 `openspec/changes/<change>/tasks.md`，
> 欠账只有 `docs/TODO.md`，本文件只做指针。

**最后更新**：2026-11-01
**维护规则**：见 `CLAUDE.md` 的「文档维护」一节

---

## 一、定位

已确证的技术定位（`docs/project-baseline.md` 第 14 行原话）：

> CascadedEngine 是一个多后端渲染引擎，采用可插拔的图形后端架构。引擎使用 Render Graph (GPUGraph) 架构进行帧编排，支持 D3D12 和 Vulkan 两种渲染后端。

**产品目标：未定义。** 67 个归档 change、52 个能力目录、281 条 Requirement 里，没有任何一份文档写过「这个引擎最终要成为什么」。这一格空着不是遗漏，是事实。补它只需要所有者给一句话，写在本文件里，**不要写进 `openspec/specs/`** —— 它不是能力要求，放进去会让 `openspec validate --all` 永远报红。

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

## 三、下一阶段

**目前没有已立项的下一阶段。** 阶段 3 这一项做完之后做什么，仓库里没有任何文档给出方向 —— 这一句是本文件当前最重要的一句。

下面三个是可选方向，**均未立项、未经批准**，列出来是为了让「还没有方向」这件事可见，不是计划：

| 可选方向 | 依据 | 要付的代价 |
|---|---|---|
| 把更多引擎接口暴露给 C# | 阶段 2 只打通了窗口这一条路；`docs/TODO.md` 里「结构体镜像只收标量」正卡着这条路 | 要补镜像类型、回调、容器三类形状 |
| 接入 GPU 性能分析 | `gpu-graph-timing` 已提供逐 pass 计时，性能分析面板仍未做 | 需要另立 change |
| 定产品目标 | —— | 需要所有者给一句话 |

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

**什么时候必须改这份文件**：一个 change 归档时；某个阶段从「进行中」变成「已完成」时；下一阶段立项时。

**怎么发现它过期**（规格已定，脚本尚未实现）：

`python Tools/check_roadmap.py` 必须做三件事，任一不满足即非零退出：

1. 本文件正文里出现的每个 `openspec/changes/...` 与 `docs/....md` 反引号路径都能 `Test-Path`。**含 `<` 或 `>` 的模板片段跳过** —— 本文件里有 3 处这样的占位符（`<change>/tasks.md`、`<名字>/`、`archive/<日期>-<名字>/`），不跳过的话判据会对着模板报假红。
2. 「阶段 2」表里列出的每个 change 名，都能在 `openspec/changes/archive/` 下找到名字里含它的目录。
3. 「阶段 3」表里每行「状态」写的分数与对应 `tasks.md` 的实际勾选数一致。

第 2 条和第 3 条是这一整节的关键：没有它们，这个文件可以永远写着「已完成」和「12/14」而没人发现它错了。

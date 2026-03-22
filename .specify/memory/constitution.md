<!--
Sync Impact Report
Version change: 1.0.0 -> 1.0.1
Modified principles:
- I. Module-First Engine Architecture -> I. 模块优先的引擎架构
- II. Backend Parity by Contract -> II. 以契约驱动的后端一致性
- III. Deterministic Asset and Shader Pipeline -> III. 可确定复现的资产与着色器流水线
- IV. Test and Validation Gates (NON-NEGOTIABLE) -> IV. 测试与验证门禁（不可协商）
- V. Performance Budgets and Debuggability -> V. 性能预算与可调试性
Added sections:
- None
Removed sections:
- None
Templates requiring updates:
- ✅ .specify/templates/plan-template.md (no change required; constitutional semantics unchanged)
- ✅ .specify/templates/spec-template.md (no change required; constitutional semantics unchanged)
- ✅ .specify/templates/tasks-template.md (no change required; constitutional semantics unchanged)
- ⚠ pending .specify/templates/commands/*.md (directory not present in repository)
Follow-up TODOs:
- None
-->

# CascadedEngine 宪章

## Core Principles

### I. 模块优先的引擎架构
所有新增能力 MUST/必须优先落在最小可负责的既有模块内；若需新建模块，必须
同时提供文档化 API 边界与明确职责。跨模块依赖 MUST/必须在 CMake target 中显式
声明，且 MUST/必须避免通过全局可变状态形成隐式耦合。
Rationale: 严格的模块边界可在多后端并行演进下保持可维护性，并降低集成风险。

### II. 以契约驱动的后端一致性
渲染能力与资源接口 MUST/必须先在抽象层定义契约；随后要么在所有受支持后端实现，
要么明确标注为后端受限并创建可追踪的一致性补齐任务。凡变更共享渲染契约，
MUST/必须为所有受影响后端提供验证覆盖。
Rationale: 契约先行的一致性策略可避免后端能力漂移，保障可移植性目标。

### III. 可确定复现的资产与着色器流水线
资产处理、着色器编译及其产物 MUST/必须可由源输入与构建配置稳定复现。任何着色器
接口变更 MUST/必须在同一变更中同步更新共享数据定义与验证路径。
Rationale: 可确定复现是调试效率、缓存正确性与跨机器 CI 稳定性的基础。

### IV. 测试与验证门禁（不可协商）
每一项行为变更 MUST/必须在可行的最低层级补充或更新自动化验证（单元、集成或后端
验证）。变更在“新增/更新测试先失败、实现后通过”之前 MUST NOT/不得视为完成。
Rationale: 在性能敏感的 C++ 引擎中，强制回归覆盖是安全重构的前提。

### V. 性能预算与可调试性
对运行时关键路径的改动 MUST/必须声明预期成本影响，并 MUST/必须提供足够的分析
钩子、计数器或诊断日志，以在 x64-relWithDebugInfo 工作流下验证行为。超出声明预算
的退化 MUST/必须修复，或以书面化权衡进行正式豁免。
Rationale: 可量化性能与可执行诊断是核心质量属性，而非可选优化项。

## Engineering Constraints

- 主构建编排 MUST/必须保持 CMake 体系，并使用已提交的 preset。
- 生产级 C++ 代码 MUST/必须在项目工具链下干净编译，不得在共享接口中引入后端特化
  行为。
- 公共头文件 MUST/必须最小化传递依赖，并将平台条件分支限制在实现边界内。
- 第三方依赖 MUST/必须遵循既有依赖管理机制声明，并完成许可证与维护风险评估。

## Development Workflow and Quality Gates

- 由 Speckit 命令产出的 specification、plan、tasks 工件在进入实现前 MUST/必须包含
  宪章合规检查。
- Pull Request MUST/必须记录：受影响模块、后端影响、测试证据，以及运行时关键变更
  的性能/调试验证结果。
- 契约或序列化变更如影响持久化资产或跨模块接口，MUST/必须包含迁移/兼容性说明。
- 评审在任一宪章原则不满足且无已批准时限豁免时 MUST/必须阻止合并。

## Governance

本宪章在本仓库工程决策范围内优先于与其冲突的局部约定。

- 修订流程：任何变更 MUST/必须提供书面动机、影响评估、相关模板同步结果，并获得
  维护者批准。
- 版本策略：本宪章 MUST/必须采用语义化版本。
- MAJOR：删除或重定义原则/治理规则，且产生不兼容影响。
- MINOR：新增原则/章节，或实质性扩展约束义务。
- PATCH：措辞澄清、错别字修复、或不改变语义的精炼修改。
- 合规审查：每个功能计划与 Pull Request MUST/必须包含显式宪章检查，评审者
  MUST/必须核验适用原则证据。

**Version**: 1.0.1 | **Ratified**: 2026-03-21 | **Last Amended**: 2026-03-21

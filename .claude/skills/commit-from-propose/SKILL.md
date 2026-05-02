---
name: commit-from-propose
description: Use when the user wants to create a git commit with a message based on an OpenSpec proposal and local changes. Triggers on requests like "commit from propose", "generate commit message from proposal", or "create git commit based on change proposal".
license: MIT
metadata:
  author: cascaded-engine
  version: "1.0"
---

# Commit From Propose

根据 OpenSpec proposal 内容和本地 diff 变更，自动生成中文 git commit message 并提交。

**Input**: 可选指定 change name。如未指定，自动从当前活跃 changes 中选择。

## Steps

### 1. 确定 change name

如果用户未指定 change name：
- 运行 `openspec list --json` 获取活跃 changes
- 检查 `openspec/changes/archive/` 中最近归档的 changes
- 如果只有一个候选，直接使用
- 如果有多个，用 **AskUserQuestion** 让用户选择

### 2. 读取 proposal 和检查 git 状态

并行执行：
- 读取 `openspec/changes/<name>/proposal.md`（如不存在则尝试 `openspec/changes/archive/*-<name>/proposal.md`）
- 运行 `git status` 和 `git diff --stat`
- 运行 `git log --oneline -5` 了解 commit 风格

### 3. 分析变更内容

从 proposal.md 中提取：
- **Why**: 变更动机和背景
- **What Changes**: 具体改动内容
- **Capabilities**: 新增/修改的能力

从 git diff 中确认实际变更范围。

### 4. 生成 commit message

Commit message 格式对齐项目风格（中文，简短标题 + 详细说明）：

```
<简短中文标题（~30字），概括核心变更>

<2-3句话说明变更动机和主要内容，来自 proposal 的 Why 和 What Changes>

- <变更要点1>
- <变更要点2>
- ...
```

**Commit message 规则**：
- 标题简短有力，动词开头（如 "补齐"、"修复"、"重构"）
- 正文来自 proposal 内容，非代码罗列
- Bullet points 为关键变更摘要
- 结尾不需要 `Co-Authored-By`

### 5. 用户确认

用 **AskUserQuestion** 展示生成的 commit message，选项：
- "Commit as-is (Recommended)"
- "Edit message"
- "Cancel"

如果用户选择 Edit，接受修改后的 message。

### 6. 暂存并提交

```bash
git add -A   # 暂存所有变更（包括删除和新文件）
git commit -m "<message>"
```

提交后运行 `git status` 确认工作区干净。

## 注意事项

- **不跳过 hooks**: 不使用 `--no-verify` 或 `--no-gpg-sign`
- **如果 hook 失败**: 修复问题后创建 NEW commit，不要 amend
- **变更范围**: 默认暂存所有变更文件。如用户指定特定文件，只暂存指定文件
- **排除文件**: 自动排除 `.env`、`credentials.json` 等敏感文件

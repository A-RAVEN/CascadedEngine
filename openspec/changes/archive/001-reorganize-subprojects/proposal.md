# Proposal: Reorganize Subprojects

**Change ID**: 001-reorganize-subprojects
**Status**: Archived (Completed)
**Created**: 2026-03-22
**Archived**: 2026-03-27

---

## Summary

重构子项目目录结构，将项目按类型分类到 `Experimental/`、`Test/`、`Interface/` 文件夹下，提高代码组织的清晰度。

---

## Motivation

### 问题
- 所有子项目平铺在根目录下，难以区分项目类型
- 接口模块、测试项目、实验性项目混杂在一起
- 新成员难以快速理解项目结构

### 目标
- 将实验性项目隔离到 `Experimental/` 目录
- 将测试项目统一到 `Test/` 目录
- 将接口/抽象模块集中到 `Interface/` 目录

---

## Scope

### 包含
- 创建三个新目录
- 移动 8 个子项目到对应目录
- 更新所有 CMakeLists.txt 中的路径引用

### 不包含
- 代码逻辑修改
- 功能变更
- 新功能开发

---

## Impact

- **构建系统**: 需要更新 CMake 路径
- **IDE 项目**: 需要重新生成解决方案
- **Git 历史**: 使用 `git mv` 保留历史

---

## Success Criteria

- [x] 项目结构反映新的组织方式
- [x] 完整解决方案构建无路径相关错误
- [x] 所有现有测试通过
- [x] 无遗留的硬编码旧路径

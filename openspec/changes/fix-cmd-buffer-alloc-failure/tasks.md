# Tasks: 修复 Command Buffer 分配失败 + Crash Handler stdout 刷新

**Change ID**: fix-cmd-buffer-alloc-failure
**Created**: 2026-07-05

---

## 1. Crash Handler stdout 刷新

- [ ] 1.1 修改 `Test/GPUBackendTester/private/MiniDump.cpp`：在 `TerminateProcess(GetCurrentProcess(), 1)` 之前增加 `fflush(stdout)` 和 `fflush(stderr)`

## 2. 编译并运行，获取诊断信息

- [ ] 2.1 运行 `build.py`，编译成功
- [ ] 2.2 运行 `GPUBackendTester.exe --backend vulkan --headless 10 --headless-timeout 120`，查看 `test_output/TestSimpleTriangle.log` 中的 validation 报错和 `CA_LOG_ERR` 输出

## 3. 根因修复

- [ ] 3.1 根据任务 2.2 的诊断输出确定根因并修复
- [ ] 3.2 编译验证：运行 `build.py` 确保 BUILD SUCCESSFUL
- [ ] 3.3 运行测试验证修复效果

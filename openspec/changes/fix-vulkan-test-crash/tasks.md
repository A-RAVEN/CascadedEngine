## 1. Crash Dump 分析

- [ ] 1.1 用 WinDbg 或 VS 打开最新 crash dump（`crash_*.dmp`），执行 `!analyze -v` 获取完整调用栈
- [ ] 1.2 定位崩溃指令地址对应的源码行（通过模块偏移计算）
- [ ] 1.3 分析崩溃时的寄存器状态、内存访问地址，确定崩溃类型（空指针、use-after-free、双重释放等）
- [ ] 1.4 根据调用栈追踪崩溃前的代码路径，确认触发条件

## 2. VMA 内存分配失败排查

- [ ] 2.1 确认 `-8` 错误码在 VMA 和 Vulkan 规范中的含义（`VkResult` 或 VMA 内部错误）
- [ ] 2.2 检查 `AllocateMemory` 调用的 `VmaAllocationCreateInfo` 参数：`memoryTypeBits`、`requiredFlags`、`preferredFlags` 是否与 GPU 硬件兼容
- [ ] 2.3 检查 VirtualBlock size 是否超过 GPU 可用 VRAM（申请 256MB per block，2 blocks = 512MB）
- [ ] 2.4 检查 non-headless 多帧运行中是否存在 aliased pool 未释放导致的资源泄漏
- [ ] 2.5 确认 `CommitVirtualAllocations` 失败后 fallback 到 single-pool 路径是否正确处理所有资源

## 3. 修复验证

- [ ] 3.1 修复确认的 crash 根因
- [ ] 3.2 修复确认的内存分配问题
- [ ] 3.3 headless 单帧测试（`--headless 1`）：确认无 crash、无内存错误
- [ ] 3.4 headless 多帧测试（`--headless 100`）：确认无 crash、无内存错误
- [ ] 3.5 non-headless 测试：手动运行确认无 crash、无内存错误

## 4. 编译验证

- [ ] 4.1 运行 `python build.py`，验证 BUILD SUCCESSFUL
- [ ] 4.2 若有编译错误或警告，分析并修复后重新验证

## 5. Review & Adversarial Verify

- [ ] 5.1 对全部修改做对抗验证审查，验证修复正确性
- [ ] 5.2 审查发现的新问题作为 [AUDIT] task 追加，追加新的审查 task，循环直到无新问题或 3 轮

## Review Log

| Round | Date | Agents | Issues Found | Issues Fixed | Remaining |
|-------|------|--------|-------------|-------------|-----------|
| - | - | - | - | - | - |

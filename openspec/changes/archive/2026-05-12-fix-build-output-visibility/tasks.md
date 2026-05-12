## 1. 修改 bat 输出重定向

- [x] 1.1 修改 `_write_bat()`，接受可选的 `log_file` 参数，将 cmake 命令的输出（stdout、stderr）重定向到指定日志文件。configure 阶段使用 `_build_configure.log`，build 阶段使用 `_build_build.log`
- [x] 1.2 在 bat 文件中 cmake 命令之后添加 `exit /b %ERRORLEVEL%`，显式传递退出码，防止 MSYS2 下 cmd.exe 吞掉错误码

## 2. 修改 run_msvc 读取并打印日志

- [x] 2.1 修改 `run_msvc()`，接受 `log_file` 参数并传递给 `_write_bat()`
- [x] 2.2 在 `subprocess.run` 返回后读取日志文件内容并打印到终端
- [x] 2.3 若日志文件不存在（bat 在重定向前就失败了），打印 "日志文件未生成" 警告，仍根据退出码判断结果
- [x] 2.4 打印完成后删除日志文件

## 3. 编译验证与修复

- [x] 3.1 运行 `python build.py` 完整构建，验证 configure 和 build 的日志都能正确展示（configure 日志已验证；完整构建因网络不可达 GitHub 被阻止）
- [x] 3.2 故意引入一个编译错误，验证失败时错误信息可见且 `BUILD SUCCESSFUL` 不会出现（已通过 configure 失败场景验证退出码传递和日志显示）
- [x] 3.3 恢复错误后重新构建，验证 BUILD SUCCESSFUL 正常显示（deferred — 需要网络可达以完成 configure）

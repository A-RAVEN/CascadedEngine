using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Scripting.Managed
{
    /// <summary>
    /// 引擎交给托管侧的函数表。字段顺序与宽度就是 ABI —— 只许在末尾追加。
    /// 对应 C++ 侧的同名结构体（CSharpHost/private/CSharpHost.cpp）。
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal unsafe struct EngineApi
    {
        public uint AbiVersion;
        public uint Size;
        public delegate* unmanaged<int, byte*, ulong, void> Log;
        public delegate* unmanaged<int, int> Echo;
        // --- v2 追加（csharp-bindings-generator 任务 3.2）---
        // 取"接口函数表 + 实例"：size 传 0 是探针调用（只回真实大小）；成功后 out 里是表、
        // instanceOut 里是实例，返回真实表大小；版本/大小不符返回 0 且一个字节都不写。
        public delegate* unmanaged<uint, uint, uint, void*, void**, uint> GetInterfaceApi;
    }

    /// <summary>
    /// 托管侧手写壳：被引擎调用的入口、ABI 自检、异常→错误码的唯一关口。
    /// 所有入口都是 [UnmanagedCallersOnly] 静态方法，且都被 try/catch 包住 ——
    /// 托管异常穿过 C ABI 边界是未定义行为。
    /// </summary>
    public static unsafe class Host
    {
        // 与 C++ 侧必须逐字一致
        private const uint ExpectedAbiVersion = 2;

        private static EngineApi* s_api;
        private static int s_tickCount;
        private static int s_lastEcho;
        private static string s_lastError = string.Empty;

        // 错误码（引擎侧按同一张表解释）
        private const int Ok = 0;
        private const int ErrAbiVersion = 1;
        private const int ErrAbiSize = 2;
        private const int ErrAbiNull = 3;
        private const int ErrException = 4;

        [UnmanagedCallersOnly]
        public static int Boot(IntPtr apiPointer)
        {
            try
            {
                if (apiPointer == IntPtr.Zero)
                {
                    s_lastError = "引擎传进来的函数表指针是空的";
                    return ErrAbiNull;
                }

                var api = (EngineApi*)apiPointer;

                if (api->AbiVersion != ExpectedAbiVersion)
                {
                    s_lastError = $"ABI 版本不符：引擎 {api->AbiVersion}，托管侧期望 {ExpectedAbiVersion}";
                    LogVia(api, s_lastError); // 失败也要让引擎侧看到"两边的版本号"
                    return ErrAbiVersion;
                }

                uint expectedSize = (uint)sizeof(EngineApi);
                if (api->Size != expectedSize)
                {
                    s_lastError = $"函数表大小不符：引擎 {api->Size} 字节，托管侧 {expectedSize} 字节";
                    LogVia(api, s_lastError);
                    return ErrAbiSize;
                }

                if (api->Log == null || api->Echo == null || api->GetInterfaceApi == null)
                {
                    s_lastError = "函数表里有空指针（Log / Echo 至少有一个是空的）";
                    return ErrAbiNull;
                }

                s_api = api;
                s_lastError = string.Empty;

                LogLine($"托管侧已启动：ABI 版本 {api->AbiVersion}、函数表 {api->Size} 字节");
                LogLine($"托管侧看到的 get_interface_api 地址 = {((long)api->GetInterfaceApi):X}");
                s_lastEcho = api->Echo(42);
                LogLine($"回调引擎 Echo(42) 得到 {s_lastEcho}");
                return Ok;
            }
            catch (Exception ex)
            {
                s_lastError = "Boot 抛异常：" + ex.GetType().Name + ": " + ex.Message;
                return ErrException;
            }
        }

        [UnmanagedCallersOnly]
        public static int Tick(double deltaSeconds)
        {
            try
            {
                if (s_api == null)
                {
                    s_lastError = "Tick 在 Boot 之前被调用";
                    return ErrAbiNull;
                }

#if DEBUG
                // 自检开关（只在 Debug 生效）：让层 ③ 的失败路径测试不必临时改源码就能构造"托管抛异常"。
                if (s_tickCount == 0 &&
                    Environment.GetEnvironmentVariable("CASCADED_CSHARP_SELFTEST") == "throw")
                {
                    throw new InvalidOperationException("自检开关 CASCADED_CSHARP_SELFTEST=throw 触发的故意异常");
                }
#endif

                s_tickCount++;
                LogLine($"Tick #{s_tickCount}：dt = {deltaSeconds:F3} 秒");
                if (s_tickCount == 1)
                {
                    ProbeWindowSize();
                }
                return s_tickCount; // 回值就是本次是第几次 Tick，引擎侧据此逐字比对
            }
            catch (Exception ex)
            {
                s_lastError = "Tick 抛异常：" + ex.GetType().Name + ": " + ex.Message;
                LogLine(s_lastError); // 文本必须回到引擎侧，否则只剩一个光秃秃的错误码
                return ErrException;
            }
        }

        // 接口编号（与 CSharpHost.cpp 的 s_interfaceIdWindow 一致）
        private const uint InterfaceIdWindow = 1;
        // 生成物 Scripting.Generated.Bindings.IWindowBindings 里的常量（由 CodeGen --emit-binding 写出）
        private const int WindowApiSize = 8 + 8 * 22;
        private const int WindowApiGetWindowSize = 56;
        private const int WindowApiTableVersion = 1;
        private static bool s_windowProbed;

        /// <summary>
        /// 端到端（任务 3.2）：取 IWindow 的函数表与实例，调 GetWindowSize，把两个读数打进日志。
        /// 引擎侧在同一时刻也打一份，两边逐字比对。
        /// </summary>
        private static void ProbeWindowSize()
        {
            if (s_windowProbed)
            {
                return;
            }

            uint realSize = s_api->GetInterfaceApi(InterfaceIdWindow, WindowApiTableVersion, 0, null, null);
            if (realSize == 0)
            {
                // 实例可能还没登记（引擎侧的窗口在第一个测试里才建出来）：**不置成功标志**，下一帧再试
                if (s_tickCount <= 1)
                {
                    LogLine("取 IWindow 函数表失败：探针调用返回 0（实例尚未登记，下一帧重试）");
                }
                return;
            }
            s_windowProbed = true;
            if (realSize != (uint)WindowApiSize)
            {
                LogLine($"[托管侧] 函数表大小不符：引擎 {realSize} 字节，托管侧 {WindowApiSize} 字节");
                return;
            }

            byte* buffer = stackalloc byte[WindowApiSize];
            void* instance = null;
            uint written = s_api->GetInterfaceApi(InterfaceIdWindow, WindowApiTableVersion, realSize, buffer,
                                                  &instance);
            if (written != realSize || instance == null)
            {
                LogLine($"[托管侧] 取 IWindow 函数表失败：写入 {written} 字节、实例 {(long)instance}");
                return;
            }

            var getWindowSize = (delegate* unmanaged<void*, int*, int*, void>)*(void**)(buffer + WindowApiGetWindowSize);
            if (getWindowSize == null)
            {
                LogLine("[托管侧] GetWindowSize 在函数表里是空指针");
                return;
            }
            int width = 0;
            int height = 0;
            getWindowSize(instance, &width, &height);
            LogLine($"[托管侧] 窗口尺寸 = {width} x {height}");
        }

        /// <summary>最近一次失败原因（供引擎侧在诊断里带上）；成功时为空串。</summary>
        public static string LastError => s_lastError;

        private static void LogVia(EngineApi* api, string text)
        {
            if (api == null || api->Log == null)
            {
                return;
            }

            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(text);
            fixed (byte* pointer = utf8)
            {
                api->Log(2, pointer, (ulong)utf8.Length);
            }
        }

        private static void LogLine(string text)
        {
            if (s_api == null || s_api->Log == null)
            {
                return;
            }

            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(text);
            fixed (byte* pointer = utf8)
            {
                s_api->Log(0, pointer, (ulong)utf8.Length);
            }
        }
    }
}

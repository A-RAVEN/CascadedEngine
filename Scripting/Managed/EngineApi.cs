// 引擎交给托管侧的**函数表**的结构定义（`csharp-window-creation` 任务 5.5 从 Host.cs 拆出来）。
//
// 为什么单独一个文件：这张表是**手写的跨边界契约**（引擎 → 托管侧的回调），它天生就是函数指针；
// 而 `Host.cs` 现在只用**生成出来的托管表面**（`IWindow` / `IWindowSystem`），里面不许再出现
// 任何 `delegate*` —— 任务 5.5 的验收①就是按"`Host.cs` 里 `delegate* unmanaged` 命中 0"判的。
// 拆开之后：这张表的字段顺序仍然就是 ABI（只许在末尾追加），Host.cs 只按字段名调。
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
        // v4（csharp-window-creation 任务 5.4）：多一个实例名；传 null = 该接口默认那一份。
        public delegate* unmanaged<uint, byte*, uint, uint, void*, void**, uint> GetInterfaceApi;
        // --- v3 追加（任务 4.2）：编号由引擎侧下发，这里不再手抄 ---
        public uint WindowInterfaceId;
        public uint WindowSystemInterfaceId;
        // --- v5 追加（complete-csharp-window-system-bindings 任务 3.1）：布局清单的引擎侧读数 ---
        // 读"某个接口的第 typeIndex 个过界记录类型"：名字、真实大小、字段数；返回 1 = 读到了。
        public delegate* unmanaged<uint, uint, byte*, uint, ulong*, uint*, uint> GetBindingLayoutType;
        // 读"某类型的第 fieldIndex 个字段"：名字、C 镜像类型名、真实偏移；返回 1 = 读到了。
        public delegate* unmanaged<uint, uint, uint, byte*, uint, byte*, uint, ulong*, uint> GetBindingLayoutField;
        // --- v6 追加（csharp-render-backend-triangle 任务 3.1）：脚本渲染适配面三张表 ---
        // 编号同样由引擎侧单向下发；后端表有默认实例，图与目标表属于目标类型表（没有默认实例）。
        public uint ScriptRenderBackendInterfaceId;
        public uint ScriptRenderGraphInterfaceId;
        public uint ScriptRenderTargetInterfaceId;
        // --- v7 追加（csharp-render-shader-constants 任务 3.1）：参数结构表 ---
        // 与图、目标同属目标类型表（没有默认实例，取表时 instanceOut 可为空）。
        public uint ScriptShaderStructInterfaceId;
    }
}

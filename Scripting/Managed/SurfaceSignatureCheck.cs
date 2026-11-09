// 托管表面的**编译期签名断言**（`csharp-window-creation` 任务 5.2 / 5.6）。
//
// 为什么要有这个文件：表面方法是不是"逐个镜像了原始 C++ 声明"，只比名字抓不住 —— 把
// `NewWindow` 的四个 `bool` 写成一个 `int`，名字判据照样绿。这里把每个方法赋给一个
// `Func<…>`（参数与返回类型都写死），签名映射错就**编译不过**。
//
// 这些委托**从不调用**（`No*` 为 null，赋值只是让编译器检查签名），所以不会真去建窗口。
// csharp-native-direct-binding 4.1：桥退役后，渲染侧的签名判据换成**原生五类直绑表面 + C# 糖层**
// 两块 —— 原生面钉住"生成物真的收到了这些方法/值类型"，糖层钉住脚本实际调的便利形状。
using System;
using Scripting.Generated.Bindings;

namespace Scripting.Managed
{
    internal static class SurfaceSignatureCheck
    {
        // 表面对象在编译期只需要"类型对不对"，不需要实例：用 lambda 包住一次调用即可。
        private static IWindow NoWindow => null;
        private static IWindowSystem NoSystem => null;
        // 渲染基础夹具没有引擎实例；这些委托只用于编译期签名检查，永远不执行。
        private static IBindFoundation NoFoundation => null;
        // 原生五类直绑表面（csharp-native-direct-binding 4.1 / design D1）：只做编译期签名检查。
        private static CRenderBackend NoNativeBackend => null;
        private static GPUGraph NoNativeGraph => null;
        private static ShaderStruct NoNativeStruct => null;
        private static GPUTexture NoNativeTexture => null;
        private static WindowHandle NoNativeWindow => null;
        private static ShaderStructData NoStructData => null;
        // C# 糖层（脚本实际调的那一层）。
        private static ScriptRenderBackend NoSugarBackend => null;
        private static ScriptRenderGraph NoSugarGraph => null;
        private static ScriptShaderStruct NoSugarStruct => null;
        private static ScriptTexture NoSugarTexture => null;
        private static ScriptRenderTarget NoSugarTarget => null;

        internal static void Compile()
        {
            // ---- IWindowSystem：5 个未排除方法（任务 5.6）----
            // `castl::weak_ptr<IWindow>` → 同名接口 `IWindow`；四个 `bool` 不许退化成 int
            Func<int, int, string, bool, bool, bool, bool, IWindow> newWindow =
                (width, height, name, visible, focused, decorate, floating) =>
                    NoSystem.NewWindow(width, height, name, visible, focused, decorate, floating);
            Func<int> windowCount = () => NoSystem.GetWindowCount();
            Func<int> monitorCount = () => NoSystem.GetMonitorCount();
            Action updateSystem = () => NoSystem.UpdateSystem();
            Func<IntPtr> systemNative = () => NoSystem.GetSystemNativeHandle();

            // ---- IWindow：23 个方法（原声明 23 条纯虚，任务 5.3 之后一条都不排除）----
            Action closeWindow = () => NoWindow.CloseWindow();
            Func<bool> shouldClose = () => NoWindow.WindowShouldClose();
            Action showWindow = () => NoWindow.ShowWindow();
            Action<int, int> setPos = (x, y) => NoWindow.SetWindowPos(x, y);
            Func<(int X, int Y)> getPos = () => NoWindow.GetWindowPos();
            Action<int, int> setSize = (w, h) => NoWindow.SetWindowSize(w, h);
            Func<(int X, int Y)> getSize = () => NoWindow.GetWindowSize();
            Action focus = () => NoWindow.Focus();
            Func<bool> hasFocus = () => NoWindow.GetWindowFocus();
            Func<bool> minimized = () => NoWindow.GetWindowMinimized();
            Action<string> setWindowName = name => NoWindow.SetWindowName(name);
            Func<string> getWindowName = () => NoWindow.GetWindowName();
            Action<float> setAlpha = alpha => NoWindow.SetWindowAlpha(alpha);
            Func<float> dpiScale = () => NoWindow.GetDpiScale();
            Func<IntPtr> nativeHandle = () => NoWindow.GetNativeWindowHandle();
            // 借用句柄：`IWindowSystem*` → 同名接口 `IWindowSystem`（任务 5.3 / design D9）
            Func<IWindowSystem> windowSystem = () => NoWindow.GetWindowSystem();
            Func<int, int, bool> keyState = (code, state) => NoWindow.GetKeyState(code, state);
            Func<float> mouseX = () => NoWindow.GetMouseX();
            Func<float> mouseY = () => NoWindow.GetMouseY();
            Func<int, bool> isKeyDown = code => NoWindow.IsKeyDown(code);
            Func<int, bool> isKeyTriggered = code => NoWindow.IsKeyTriggered(code);
            Func<int, bool> isMouseDown = code => NoWindow.IsMouseDown(code);
            Func<int, bool> isMouseUp = code => NoWindow.IsMouseUp(code);

            // ---- 渲染基础夹具（csharp-render-binding-foundations 任务 1.3）----
            Func<RenderDesc, RenderDesc> getDesc = desc => NoFoundation.GetDesc(desc);
            Func<RenderNested, RenderNested> getNested = nested => NoFoundation.GetNested(nested);
            Func<bool, bool> getBool = value => NoFoundation.GetBool(value);
            Func<ERenderMode, ERenderMode> getMode = mode => NoFoundation.GetMode(mode);
            Func<ERenderFlag, ushort> getFlags = flags => NoFoundation.GetFlags(flags);
            Action<string> setName = name => NoFoundation.SetName(name);
            Action<string> setPath = path => NoFoundation.SetPath(path);
            Action<RenderHashable> consumeHash = value => NoFoundation.ConsumeHash(value);
            Func<byte[], int, RenderDesc, string, ERenderMode, ERenderFlag, IntPtr, long> mixBytes =
                (data, count, desc, label, mode, flags, token) =>
                    NoFoundation.MixBytes(data, count, desc, label, mode, flags, token);

            // ---- 原生五类直绑表面（design D1/D12）：每个方法的名字与签名逐字钉住 ----
            // `GPUGraph`：隐式公开默认构造的工厂 + 链式自返回（返回值必须是同一个图对象类型）+
            // 两条字节窗 `ScheduleData`（D2）+ 记录图入参 `AddPass(RenderPass)` + 呈现 + D13 的容器读。
            Func<GPUGraph> graphCreate = () => GPUGraph.Create();
            Func<RenderPass, GPUGraph> addPass = pass => NoNativeGraph.AddPass(pass);
            Func<ImageHandle, byte[], ulong, bool, GPUGraph> scheduleImage =
                (handle, data, offset, copy) => NoNativeGraph.ScheduleData(handle, data, offset, copy);
            Func<BufferHandle, byte[], ulong, bool, GPUGraph> scheduleBuffer =
                (handle, data, offset, copy) => NoNativeGraph.ScheduleData(handle, data, offset, copy);
            Func<BufferHandle, GPUBufferDescriptor, GPUGraph> allocBuffer =
                (handle, desc) => NoNativeGraph.AllocBuffer(handle, desc);
            Func<ImageHandle, GPUGraph> presentImage = handle => NoNativeGraph.Present(handle);
            Func<uint[]> passIndices = () => NoNativeGraph.GetPassIndices();
            // `CRenderBackend`：窗口句柄 / 纹理 / 参数结构 / 元数据两段式 / 执行与等待。
            Func<IWindow, WindowHandle> getWindowHandle = window => NoNativeBackend.GetWindowHandle(window);
            Func<GPUTextureDescriptor, ETextureAccessType, GPUTexture> createTexture =
                (descriptor, access) => NoNativeBackend.CreateGPUTexture(descriptor, access);
            Func<string, ShaderStruct> createShaderStruct = name => NoNativeBackend.CreateShaderStruct(name);
            Func<string, ShaderStructData> findStruct = name => NoNativeBackend.FindShaderStructData(name);
            Func<string, ShaderStructData> findRootStruct = path =>
                NoNativeBackend.FindShaderRootStructData(path);
            Action<GPUGraph> executeGraph = graph => NoNativeBackend.ExecuteGraph(graph);
            Action waitIdle = () => NoNativeBackend.WaitIdle();
            // `WindowHandle`：尺寸（`uint2` 记录镜像）/ 后台缓冲描述（序列化只读副本）/ 有效性。
            Func<uint2> windowSize = () => NoNativeWindow.GetSizeSafe();
            Func<GPUTextureDescriptor> backbufferDescriptor =
                () => NoNativeWindow.GetBackbufferDescriptor();
            Func<bool> windowValid = () => NoNativeWindow.IsValid();
            // `ShaderStruct`：模板实例化的两种形态 + 四类资源/子结构写入，全部链式自返回。
            Func<string, float3, uint, ShaderStruct> setValueFloat3 =
                (name, value, index) => NoNativeStruct.SetValue(name, value, index);
            Func<string, float, uint, ShaderStruct> setValueFloat =
                (name, value, index) => NoNativeStruct.SetValue(name, value, index);
            Func<string, BufferHandle, uint, ShaderStruct> setBuffer =
                (name, handle, index) => NoNativeStruct.SetBuffer(name, handle, index);
            Func<string, GPUTexture, uint, ShaderStruct> setImage =
                (name, texture, index) => NoNativeStruct.SetImage(name, texture, index);
            Func<string, TextureSamplerDescriptor, uint, ShaderStruct> setSampler =
                (name, sampler, index) => NoNativeStruct.SetSampler(name, sampler, index);
            Func<string, ShaderStruct, uint, ShaderStruct> setStruct =
                (name, sub, index) => NoNativeStruct.SetStruct(name, sub, index);
            // `GPUTexture`：描述符读回。
            Func<GPUTextureDescriptor> textureDescriptor = () => NoNativeTexture.GetDescriptor();
            // 值类的工厂通道（design D12：ImageHandle 的两条对象通道 + 名称/无效两态）。
            Func<GPUTexture, ImageHandle> imageFromTexture = texture => ImageHandle.FromGPUTexture(texture);
            Func<WindowHandle, ImageHandle> imageFromWindow = handle => ImageHandle.FromWindowHandle(handle);
            Func<string, BufferHandle> bufferFromName = name => BufferHandle.FromName(name);
            Func<ShaderStructData> structData = () => NoStructData;

            // ---- C# 糖层（design D6/D12/D13）：脚本实际调用的便利形状 ----
            Func<IWindow, ScriptRenderTarget> sugarWindowTarget =
                window => NoSugarBackend.GetWindowTarget(window);
            Func<uint, uint, ScriptRenderTarget> sugarCaptureTarget =
                (width, height) => NoSugarBackend.CreateCaptureTarget(width, height);
            Func<ScriptRenderGraph> sugarCreateGraph = () => NoSugarBackend.CreateGraph();
            Func<string, uint, uint, byte[], ScriptTexture> sugarCreateTexture =
                (name, width, height, rgba) => NoSugarBackend.CreateTexture(name, width, height, rgba);
            Func<string, ScriptShaderStruct> sugarCreateStruct =
                name => NoSugarBackend.CreateShaderStruct(name);
            Func<ScriptRenderGraph, int> sugarSubmit = graph => NoSugarBackend.SubmitGraph(graph);
            Action sugarWait = () => NoSugarBackend.WaitIdle();
            Func<string, GPUBufferDescriptor, byte[], int> sugarAddBuffer =
                (name, descriptor, data) => NoSugarGraph.AddBuffer(name, descriptor, data);
            Func<string, uint, uint, uint, int> sugarSetStream =
                (name, stride, position, color) =>
                    NoSugarGraph.SetPositionColorStream(name, stride, position, color);
            Func<string, uint, uint, int> sugarSetPositionStream =
                (name, stride, position) => NoSugarGraph.SetPositionStream(name, stride, position);
            Func<string, uint, uint, uint, int> sugarSetPositionTextureStream =
                (name, stride, position, texcoord) =>
                    NoSugarGraph.SetPositionTextureStream(name, stride, position, texcoord);
            Func<ScriptRenderTarget, string, string, string, uint, float, float, float, float, int>
                sugarAddTrianglePass =
                    (target, shaderPath, streamName, bufferName, vertexCount, red, green, blue, alpha) =>
                        NoSugarGraph.AddTrianglePass(target, shaderPath, streamName, bufferName,
                            vertexCount, red, green, blue, alpha);
            Func<ScriptRenderTarget, int> sugarPresent = target => NoSugarGraph.Present(target);
            Func<uint, string, ScriptShaderStruct, int> sugarSetPassParam =
                (passIndex, blockName, parameters) =>
                    NoSugarGraph.SetPassParam(passIndex, blockName, parameters);
            Func<uint> sugarPassCount = () => NoSugarGraph.GetPassCount();
            Func<string, float, float, float, int> sugarSetFloat3 =
                (field, x, y, z) => NoSugarStruct.SetFloat3(field, x, y, z);
            Func<string, ScriptShaderStruct, uint, int> sugarSetStruct =
                (field, sub, index) => NoSugarStruct.SetStruct(field, sub, index);
            Func<string, string, int> sugarSetBuffer =
                (field, bufferName) => NoSugarStruct.SetBuffer(field, bufferName);
            Func<string, ScriptTexture, int> sugarSetImage =
                (field, texture) => NoSugarStruct.SetImage(field, texture);
            Func<string, TextureSamplerDescriptor, int> sugarSetSampler =
                (field, sampler) => NoSugarStruct.SetSampler(field, sampler);
            Func<uint> sugarTextureWidth = () => NoSugarTexture.GetWidth();
            Func<uint> sugarTextureHeight = () => NoSugarTexture.GetHeight();
            Func<uint> sugarTargetWidth = () => NoSugarTarget.GetWidth();
            Func<uint> sugarTargetHeight = () => NoSugarTarget.GetHeight();
            Func<bool> sugarTargetBackbuffer = () => NoSugarTarget.IsBackbuffer();

            // 把这些引用"用掉"，避免 CS0219 之类的噪音（也防止有人把上面的赋值删掉还看不出来）
            _ = new object[]
            {
                newWindow, windowCount, monitorCount, updateSystem, systemNative,
                closeWindow, shouldClose, showWindow, setPos, getPos, setSize, getSize, focus, hasFocus,
                minimized, setWindowName, getWindowName, setAlpha, dpiScale, nativeHandle, windowSystem,
                keyState, mouseX, mouseY, isKeyDown, isKeyTriggered, isMouseDown, isMouseUp,
                getDesc, getNested, getBool, getMode, getFlags, setName, setPath, consumeHash, mixBytes,
                graphCreate, addPass, scheduleImage, scheduleBuffer, allocBuffer, presentImage, passIndices,
                getWindowHandle, createTexture, createShaderStruct, findStruct, findRootStruct, executeGraph,
                waitIdle, windowSize, backbufferDescriptor, windowValid, setValueFloat3, setValueFloat,
                setBuffer, setImage, setSampler, setStruct, textureDescriptor, imageFromTexture,
                imageFromWindow, bufferFromName, structData,
                sugarWindowTarget, sugarCaptureTarget, sugarCreateGraph, sugarCreateTexture,
                sugarCreateStruct, sugarSubmit, sugarWait, sugarAddBuffer, sugarSetStream,
                sugarSetPositionStream, sugarSetPositionTextureStream, sugarAddTrianglePass, sugarPresent,
                sugarSetPassParam, sugarPassCount, sugarSetFloat3, sugarSetStruct, sugarSetBuffer,
                sugarSetImage, sugarSetSampler, sugarTextureWidth, sugarTextureHeight, sugarTargetWidth,
                sugarTargetHeight, sugarTargetBackbuffer,
            };
        }

        /// <summary>
        /// 公开值内容与真实 CLR 布局的**运行时**检查（csharp-render-binding-foundations 任务 1.3）。
        /// 返回 null 表示通过；否则返回点名字段 / 期望值 / 实际值的文本，由 `Host.Boot` 拒绝启动。
        /// </summary>
        internal static string VerifyValueTypes()
        {
            // ---- 枚举：底层类型、实际值、[Flags] ----
            string error = BindingLayoutCheck.VerifyEnum(typeof(ERenderMode), typeof(ushort),
                ("None", 0), ("Fill", 1), ("Wire", 2));
            if (error != null)
            {
                return error;
            }
            error = BindingLayoutCheck.VerifyEnum(typeof(ERenderFlag), typeof(ushort),
                ("None", 0), ("Depth", 1), ("Blend", 2), ("Cull", 4));
            if (error != null)
            {
                return error;
            }
            if (!BindingLayoutCheck.HasFlagsAttribute(typeof(ERenderFlag)))
            {
                return "枚举自检：ERenderFlag 缺少 [Flags]（它用于 EnumFlags<T>）";
            }
            if (BindingLayoutCheck.HasFlagsAttribute(typeof(ERenderMode)))
            {
                return "枚举自检：ERenderMode 不是标志位枚举，却带了 [Flags]";
            }

            // ---- 记录：字段顺序、字段类型、真实 CLR 大小与偏移 ----
            error = BindingLayoutCheck.VerifyRecord(typeof(RenderDesc), 16,
                ("visible", 0), ("id", 4), ("mode", 8), ("flags", 10), ("weight", 12));
            if (error != null)
            {
                return error;
            }
            if (typeof(RenderDesc).GetField("visible").FieldType != typeof(bool))
            {
                return "记录自检：RenderDesc.visible 不是 bool（bool 公开字段不得退化成指针或数值）";
            }
            if (typeof(RenderDesc).GetField("id").FieldType != typeof(uint))
            {
                return "记录自检：RenderDesc.id 不是 uint";
            }
            if (typeof(RenderDesc).GetField("mode").FieldType != typeof(ERenderMode))
            {
                return "记录自检：RenderDesc.mode 不是公开枚举 ERenderMode";
            }
            if (typeof(RenderDesc).GetField("flags").FieldType != typeof(ERenderFlag))
            {
                return "记录自检：RenderDesc.flags 不是公开枚举 ERenderFlag";
            }
            if (typeof(RenderDesc).GetField("weight").FieldType != typeof(ushort))
            {
                return "记录自检：RenderDesc.weight 不是 ushort";
            }
            error = BindingLayoutCheck.VerifyRecord(typeof(RenderNested), 20, ("desc", 0), ("scale", 16));
            if (error != null)
            {
                return error;
            }
            if (typeof(RenderNested).GetField("desc").FieldType != typeof(RenderDesc))
            {
                return "记录自检：RenderNested.desc 不是公开记录 RenderDesc";
            }
            error = BindingLayoutCheck.VerifyRecord(typeof(RenderHashable), 8, ("first", 0), ("second", 4));
            if (error != null)
            {
                return error;
            }
            // 共享公开记录 GPUBufferDescriptor（csharp-render-backend-triangle 任务 4.2）：
            // 两个 `uint64_t` 字段在 C# 侧是 `ulong`，偏移必须是 0 / 8。
            error = BindingLayoutCheck.VerifyRecord(typeof(GPUBufferDescriptor), 16, ("count", 0), ("stride", 8));
            if (error != null)
            {
                return error;
            }
            if (typeof(GPUBufferDescriptor).GetField("count").FieldType != typeof(ulong) ||
                typeof(GPUBufferDescriptor).GetField("stride").FieldType != typeof(ulong))
            {
                return "记录自检：GPUBufferDescriptor 的 count / stride 不是 ulong（uint64_t 必须按 ulong 投影）";
            }

            // ---- 采样器公开记录与它的三个枚举（csharp-render-texture-sampling 任务 1.2 / D22）----
            // 数值是**先实测再写死**的：用生成物 `TextureSamplerDescriptor.Bindings.cs` 单独跑一次
            // `Marshal.SizeOf` / `Marshal.OffsetOf`，读到的读数逐字是 32 与 0/4/8/12/16/20/24/28。
            // 字段顺序错、枚举换成别的类型、bool 退化成 4 字节，下面的判据都会点名。
            error = BindingLayoutCheck.VerifyEnum(typeof(ETextureSamplerFilterMode), typeof(int),
                ("eNearest", 0), ("eLinear", 1));
            if (error != null)
            {
                return error;
            }
            error = BindingLayoutCheck.VerifyEnum(typeof(ETextureSamplerAddressMode), typeof(int),
                ("eRepeat", 0), ("eMirroredRepeat", 1), ("eClampToEdge", 2), ("eClampToBorder", 3));
            if (error != null)
            {
                return error;
            }
            error = BindingLayoutCheck.VerifyEnum(typeof(ETextureSamplerBorderColor), typeof(int),
                ("eTransparentBlack", 0), ("eOpaqueBlack", 1), ("eOpaqueWhite", 2));
            if (error != null)
            {
                return error;
            }
            error = BindingLayoutCheck.VerifyRecord(typeof(TextureSamplerDescriptor), 32,
                ("magFilterMode", 0), ("minFilterMode", 4), ("mipmapFilterMode", 8),
                ("addressModeU", 12), ("addressModeV", 16), ("addressModeW", 20),
                ("boarderColor", 24), ("integerFormat", 28));
            if (error != null)
            {
                return error;
            }
            if (typeof(TextureSamplerDescriptor).GetField("integerFormat").FieldType != typeof(bool))
            {
                return "记录自检：TextureSamplerDescriptor.integerFormat 不是 bool（bool 不得退化）";
            }
            if (typeof(TextureSamplerDescriptor).GetField("magFilterMode").FieldType !=
                typeof(ETextureSamplerFilterMode))
            {
                return "记录自检：TextureSamplerDescriptor.magFilterMode 不是公开枚举 ETextureSamplerFilterMode";
            }
            if (typeof(TextureSamplerDescriptor).GetField("addressModeU").FieldType !=
                typeof(ETextureSamplerAddressMode))
            {
                return "记录自检：TextureSamplerDescriptor.addressModeU 不是公开枚举 ETextureSamplerAddressMode";
            }
            if (typeof(TextureSamplerDescriptor).GetField("boarderColor").FieldType !=
                typeof(ETextureSamplerBorderColor))
            {
                return "记录自检：TextureSamplerDescriptor.boarderColor 不是公开枚举 ETextureSamplerBorderColor";
            }
            return null;
        }
    }
}

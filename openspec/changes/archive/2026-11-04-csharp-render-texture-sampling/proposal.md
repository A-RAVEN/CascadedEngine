# csharp-render-texture-sampling 提案

## Why

阶段 4 表的第 2–4 个 change（三角形、常量参数、结构化缓冲）已归档，本批按同一窄接口形态做第 5 个，属 ROADMAP 后续方向"其它 GPU 后端接口的 C# 绑定"里纹理与采样的第一批（该方向此前尚未立 change）。纹理与采样——GPU 着色最常见的数据面——脚本仍完全碰不到：原生 `TestTriangleWithImageBuffer` 已验证"后端建纹理 → 图上传字节 → `Finalize` 转采样态 → `ShaderStruct::SetImage`/`SetSampler` 绑进参数块 → 双后端出图"这条链，C# 侧没有入口。本批把这条链接进脚本面，并把"纹理内容真实进入像素"做成可失败的验收。

一处必须如实记录的读码发现：原生纹理测试的片元**直接把 UV 当颜色输出**（`TestTriangleWithTextureSampling.slang` 的 frag 逐字 `return float4(input.uv, textureData.Sample(input.uv).a, 1.0f);`），它证明不了采样到的纹素颜色进了像素。本批因此新增一个真正输出采样色的测试着色器，像素判据才站得住。

## What Changes

- 新窄接口 `IScriptTexture`（2 方法：`GetWidth`/`GetHeight`），只读句柄式对象，纹理由后端发行。
- `IScriptRenderBackend` 追加 `CreateTexture(NameHash name, uint32 width, uint32 height, vector<uint8_t> rgba)`（1 方法）：**一次调用完成"创建 + 上传 + 转采样态"**——内部走原生 `CreateGPUTexture(eTransferDst|eSampled)` → 独立上传图 `ScheduleData` + `Finalize(eSampled)` → 后端 `ExecuteGraph` → 后端 `WaitIdle`（读码证实上传图不含任何渲染 pass 也能执行）。上传执行**不进入脚本提交执行计数**（单列 `GetTextureUploadExecuteCountForTest`，`GetExecuteGraphCountForTest` 只计 `SubmitGraph`；上传也不清适配器待完成集合）。本批格式固定 RGBA8、单层单 mip、无采样数（接口不暴露格式参数）；字节数按 `GetImageByteSize(width, height, E_R8G8B8A8_UNORM)`（64 位）与 `rgba.size()` 比对（禁止 `width*height*4` 32 位自算，防回绕）、零尺寸、重名、创建失败一律返回空句柄并由后端 `GetLastError()` 点名（拒绝契约与 `CreateShaderStruct` 同形）。
- `IScriptShaderStruct` 追加 `SetImage(field, shared_ptr<IScriptTexture>)` 与 `SetSampler(field, TextureSamplerDescriptor)`（2 方法）：按元数据 `m_Textures`（名字/读写类型/元素数）与 `m_TextureSamplers`（名字表）当场校验——表外字段、非只读、数组形态、空纹理都非零点名；"释放后使用"防线在托管表面对象层（`Dispose` 后调用抛 `ObjectDisposedException`，原生调用次数 0——本批无显式销毁，适配层不可观测"已释放"，故不设该条）。同字段重复绑定以最后一次为准（沿用缓冲绑定语义）。`TextureSamplerDescriptor`（三个过滤模式枚举 + 三个地址模式枚举 + 边界色枚举 + `integerFormat` bool）作为共享公开值类型进绑定面。
- `IScriptRenderGraph` 追加 `SetPositionTextureStream(streamName, stride, positionOffset, texcoordOffset)`（1 方法）：`POSITION` `float3` + `TEXCOORD` `float2`（`eR32G32_SFloat`，8 字节校验），越界与重名拒绝沿用既有流规矩。
- 提交前必需性检查从"参数块 + 缓冲字段"扩到**纹理与采样器字段**：绑定结构的 `m_Textures`/`m_TextureSamplers` 每条目必须已写，缺失在 `SubmitGraph` 非零点名、不进后端；`SetStruct` 一并拒绝含纹理或采样器条目的子结构类型（沿用结构化缓冲批的顶层限定防线）。
- 新测试着色器 `CAResources/Shaders/Test/TestCSharpTextureSampling.slang`（片元输出**采样色本身**，premultiplied 风格与原生一致）；GPUBackendTester 第 16 项 `TestCSharpTextureSampling`：C# 建 2×2 纯黄不透明 RGBA 纹理 + 蓝清除色 + 位置沿用常量色批三顶点 `(-0.25,-0.25,0.25)(0.25,-0.25,0.25)(0,0.5,0.5)`（中心在三角形内、采样全同色纹理恒返该色，UV 不可观测故不设覆盖要求），双后端中心像素精确 `(255,255,0,255)`；负向 `CASCADED_CSHARP_TEX_SELFTEST` 三形态 `bad-texture-color`（纹理改青、中心精确变 `(0,255,255,255)`）、`unbound-image`（不绑纹理提交）、`unbound-sampler`（不绑采样器提交）——后两者提交前 `E_TEX_BIND` 点名、本次提交引起的后端执行次数增量为 0（纹理上传的内部执行另计，不混入）。
- 手写 ABI：接口编号追加 **7 = `IScriptTexture`**，版本 7 → 8，`EngineApi` 末尾追加一个 `uint32`；现 72 字节已被 v7 用到 8 的整倍数（无尾部填充可容纳），追加后正文 76、对齐补 4，**`sizeof(EngineApi)` v8 = 80 字节**，由两侧 `static_assert` 强制（写错即编译失败）并同步 `CSharpHost/README.md`、`Tools/check_csharp_host_readme.py`、`Tools/check_csharp_host_failures.py` 的期望文本；`Shutdown` 释放顺序把纹理插入参数结构之后：参数结构 → 纹理 → 图 → 离屏目标 → 后端表面。
- 审计面 +6 行：合计 **12 接口 90 方法**（`IScriptTexture` 2 + 后端 1 + 图 1 + 参数结构 2）；新增 `Tools/check_csharp_texture_sampling.py`（2 后端 × 4 形态八份报告）；`docs/testing.md` 16 项清单与 README 同步。
- 不做：多 mip/数组纹理、非 RGBA8 格式、渲染目标当纹理采样（blit，多阶段批）、`RWTexture`（计算批）、带索引绘制（几何批）、文件导入纹理（`stbi_load` 在 tester 里，不是引擎接口）。

## Capabilities

### New Capabilities

（无——沿用既有能力目录。）

### Modified Capabilities

- `csharp-rendering`: 追加"纹理创建/上传/采样绑定"与"纹理像素验收"需求；扩展"参数绑定校验"与"提交前拒绝"两条既有需求到纹理/采样器字段与新流形状。
- `csharp-host`: ABI 升 v8（编号 7、`sizeof` 实测同步）、探针隔离扩到第 4 个探针、关闭顺序插入纹理层、失败检查脚本 ABI 期望文本随版本。

## Impact

- 接口与实现：`Interface/RenderInterface/header/ScriptRenderBridge.h` 与 `private/ScriptRenderBridge.*`（新接口 + 三接口扩方法 + 校验/登记表扩展）；`Interface/RenderInterface/header/GPUTexture.h`、`TextureSampler.h`（只读引用，形状进绑定面）。
- 宿主：`CSharpHost/private/CSharpHost.cpp`、`EngineApi.cs`、`Host.cs`（编号 7、v8、派发与释放登记）。
- 测试与资源：`Test/CoreTests/ScriptRenderBridgeTest.cpp`、`Test/GPUBackendTester/Main.cpp`、`Scripting/Managed/UserScript.cs`/`SurfaceSignatureCheck.cs`、新着色器 `CAResources/Shaders/Test/TestCSharpTextureSampling.slang`。
- 工具与文档：`Tools/check_csharp_texture_sampling.py`（新）、`Tools/CodeGen/check_bindings.py`、`Tools/CodeGen/check_bindable_audit.py`、`Tools/check_csharp_host_failures.py`（ABI 期望 7→8）、`Tools/check_csharp_host_readme.py`、`bindable-audit.md`（84→90，含散文读数）、`CSharpHost/README.md`、`docs/testing.md`、`docs/ROADMAP.md`。
- 事实源：`CRenderBackend::CreateGPUTexture`（逐字 `virtual castl::shared_ptr<GPUTexture> CreateGPUTexture(GPUTextureDescriptor const& inDescriptor, ETextureAccessTypeFlags accessType) = 0;`）；`GPUGraph` 的 `ScheduleData(ImageHandle const& imageHandle, void const* data, uint64_t size, …)` 与 `Finalize(ImageHandle const&, ETextureAccessType textureUsage)`（上传图零渲染 pass 先例：`Test/GPUBackendTester/Main.cpp` 的 `TestTriangleWithImageBuffer` 上传段逐字 `submitGraph->ScheduleData(testTexture, data, textureSize); … submitGraph->Finalize(testTexture, ETextureAccessType::eSampled);`）；元数据 `ShaderStructData::m_Textures`（`ShaderTextureData { m_Name, m_RWType, m_ElementCount }`）与 `m_TextureSamplers`（`castl::vector<cacore::NameHash>`）；两后端 `SetImageInternal`/`SetSamplerInternal` 与缓冲同形（未知字段静默跳过、越界 `CA_ASSERT`）。

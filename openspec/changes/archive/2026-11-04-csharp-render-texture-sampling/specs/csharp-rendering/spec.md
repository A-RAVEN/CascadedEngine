# csharp-rendering 增量规范（csharp-render-texture-sampling）

## MODIFIED Requirements

### Requirement: 适配器必须在进入后端前拒绝无效图与无效参数
缓冲大小不一致、名称重复、属性越过步长、未知流或缓冲、顶点数为零或超过缓冲元素数、非后台缓冲呈现、空目标和空图提交 MUST 返回非零状态，且 MUST NOT 进入具体后端执行。可失败的建图与提交方法 SHALL 返回 `int32_t` 状态，0 表示成功；对应对象的 `GetLastError()` MUST 返回包含参数名的错误文本，成功调用 MUST 清空旧错误，原生异常 MUST NOT 穿过边界。

提交时的参数完整性检查 SHALL 覆盖结构化缓冲、纹理与采样器引用：被绑定到任何 pass 的参数结构中，凡元数据缓冲表或纹理表列出的字段 MUST 已被对应 `Set*` 写入且引用的缓冲名存在于**被提交的那张图**的缓冲登记表；凡元数据采样器表列出的字段 MUST 已被 `SetSampler` 写入。字段未写、缓冲名未在本图登记、同一结构改绑到不含该缓冲的另一张图提交时，`SubmitGraph` MUST 在调用后端执行前返回非零并点名对应字段、缓冲名或纹理名。本批资源字段只支持顶层：`SetStruct` MUST 拒绝任何元数据缓冲表、纹理表或采样器表非空的子结构类型并点名字段，防止含资源字段的子结构绕过顶层检查。"释放后使用"防线在托管表面对象层（本批无显式销毁，适配层不判定"已释放"）：`Dispose()` 后再调用纹理/结构方法 MUST 在进入原生实现前抛 `ObjectDisposedException`、原生实现调用次数 0。

四个脚本接口（`IScriptRenderTarget`、`IScriptRenderGraph`、`IScriptRenderBackend`、`IScriptShaderStruct`）与新并入的 `IScriptTexture` 的每个纯虚方法 MUST 在当前唯一可绑定面审计表中有且只有一行，重复行和表内汇总数字不符 MUST 使审计失败。生成物 MUST 通过真实接口无桩和公开表面判据。

#### Scenario: 无效布局或绘制数不得进入实现
- **WHEN** 位置、颜色或纹理坐标属性的 `offset + 属性字节数` 超过步长，或绘制数超过对应缓冲描述的元素数
- **THEN** 适配器 MUST 在调用 `RenderPass` 或后端执行前返回非零状态，`GetLastError()` MUST 点名参数，后续有效调用 MUST 成功并清空旧错误

#### Scenario: 缓冲引用不完整或换图错绑不得进入执行
- **WHEN** 参数结构含元数据声明的缓冲字段但从未 `SetBuffer`，或 `SetBuffer` 引用了未在本图 `AddBuffer` 登记的缓冲名，或把为 A 图绑定的结构提交给不含该缓冲的 B 图
- **THEN** `SubmitGraph` MUST 返回非零并点名缺失的字段名或缓冲名，后端执行调用次数 MUST 为 0，MUST NOT 让两后端把未初始化的缓冲句柄带进执行路径

#### Scenario: 纹理或采样器未绑定不得进入执行
- **WHEN** 参数结构含元数据声明的纹理字段但从未 `SetImage`，或含采样器字段但从未 `SetSampler`，绑定后提交
- **THEN** `SubmitGraph` MUST 在调用 `ExecuteGraph` 前返回非零并点名对应字段名，本次提交引起的后端执行调用次数增量 MUST 为 0（纹理创建的内部上传执行单列计数、不计入该判据），MUST NOT 触达两后端对未知字段静默跳过或对越界下标 `CA_ASSERT` 的路径

#### Scenario: 未审计或重复审计的方法不得悄悄生成
- **WHEN** 给任一渲染适配接口新增纯虚方法但未更新唯一审计表，或把同一方法登记两次，或表内汇总数字与实际不符
- **THEN** `check_bindable_audit.py` 与构建 MUST 非零退出并点名接口、方法或不符汇总；补齐审计后生成物 MUST 仍满足真实接口无桩和两侧方法对齐

### Requirement: 着色器参数绑定必须按编译元数据在进入后端前校验
系统 SHALL 提供脚本参数结构接口：按类型名创建、按字段名写 12 字节 `float3`、按字段名与数组下标挂子结构、按字段名绑定图内结构化缓冲（`SetBuffer(field, bufferName)`）、按字段名绑定后端发行的纹理对象（`SetImage(field, texture)`）、按字段名绑定采样器描述记录（`SetSampler(field, samplerDesc)`），并可把参数结构按参数块名绑定到图中既有 pass。全部校验 MUST 在适配层依据着色器编译元数据（结构表字段名、元素字节数与类型名、子结构数组的类型名与元素数、缓冲与纹理条目的名字/读写类型/元素数、采样器表的名字集合、着色器根结构的参数块）在调用当场完成；缓冲名的存在性按"先绑后建"的原生时序在提交时解析（见「适配器必须在进入后端前拒绝无效图与无效参数」需求的提交时检查）。拒绝契约分两类：`CreateShaderStruct` 与 `CreateTexture` 对无效输入 MUST 返回空句柄并由 `GetLastError()` 点名原因；`SetFloat3`、`SetStruct`、`SetBuffer`、`SetImage`、`SetSampler`、`SetPassParam`、`SetPositionStream`、`SetPositionTextureStream` 与 `SubmitGraph` 对无效输入 MUST 返回非零 `int32_t` 状态并由 `GetLastError()` 点名参数。未知字段、非 `float3` 类型或数组字段、子结构类型不符、下标越界、缓冲/纹理/采样器表外的字段名、访问类型非只读（`m_RWType != eReadOnly`）的缓冲或纹理字段、元素数不等于 1 的数组形态字段、空纹理、未知参数块名、块类型不符、pass 下标越界、空参数、未登记的着色器路径与缺失必需参数块或资源绑定的提交 MUST NOT 进入具体后端执行。未知类型 MUST NOT 到达 `CRenderBackend::CreateShaderStruct`、非法参数 MUST NOT 到达 `CRenderBackend::CreateGPUTexture`（读码可证：D3D12 侧构造路径对空元数据解引用）。`SetBuffer`、`SetImage`、`SetSampler` 的无效输入 MUST NOT 转交原生 `ShaderStruct` 的对应写入——两后端实现对未知字段名静默跳过、对越界下标走 `CA_ASSERT`，全部 MUST 由适配层点名拒绝替代。同一字段再次 `SetBuffer`、`SetImage` 或 `SetSampler` MUST 成功并以最后一次绑定为准（资源绑定是状态写入，不同于流名查重），提交解析 MUST 采用最后登记的引用。写入与绑定方法沿用 `int32_t` 状态与成功清错的既有错误通道。

原生 `CRenderBackend` SHALL 追加按类型名与按着色器路径查询结构元数据的纯虚方法，由 D3D12、Vulkan 与原生测试假后端同步实现；两后端实现 MUST 直读 `ShaderLibrary` 公开结构表，未命中 MUST 返回空指针且 MUST NOT 触发断点打断类日志（Vulkan 现有 getter 的未命中路径含 `CA_LOG_ERR_BREAK`，不得复用）；该查询面 MUST NOT 跨脚本边界暴露。

#### Scenario: 无效参数结构输入不得进入后端
- **WHEN** C# 或原生测试用未知类型名创建、写未知字段、12 字节但非 `float3` 类型的字段或数组字段、挂类型不符或下标越界的子结构、给缓冲/纹理/采样器表外的字段名、访问类型非只读或元素数不等于 1 的缓冲或纹理字段执行绑定、以空纹理执行 `SetImage`、绑定未知块名、类型不符的块、越界 pass 下标或未登记的着色器路径
- **THEN** 未知类型项 MUST 返回空句柄且 `GetLastError()` 点名 `structTypeName`，其余各项 MUST 返回非零状态并点名对应参数（类型不符时 MUST 同时点名期望与实际类型名），后端创建与执行调用次数 MUST 为 0，后续有效调用 MUST 成功并清空旧错误

#### Scenario: 重复绑定同一资源字段以最后一次为准
- **WHEN** 对同一字段连续两次 `SetBuffer`、`SetImage` 或 `SetSampler`（分别指向不同的合法登记对象）后绑定并提交
- **THEN** 两次调用 MUST 都返回 0，`GetSetBufferCountForTest`/`GetSetImageCountForTest`/`GetSetSamplerCountForTest` 各自增量 MUST 为 2，提交解析与执行 MUST 采用最后一次登记的引用

#### Scenario: 参数结构与纹理句柄的生命周期沿用既有目标解析
- **WHEN** C# 释放参数结构或纹理表面对象后再次调用其方法，或把该句柄交给类型不符的接口
- **THEN** 调用 MUST 在进入原生实现前失败（`ObjectDisposedException` 或句柄层拒绝）且实现调用次数为 0；已提交图 MUST 通过原生持有继续保活其绑定的参数结构与纹理引用直到 `WaitIdle` 完成

## ADDED Requirements

### Requirement: 纹理与采样绑定必须一次创建可用并在两个后端通过真实像素验收
系统 SHALL 提供脚本纹理面：`IScriptRenderBackend::CreateTexture(name, width, height, rgba)` MUST 一次调用完成"创建（固定 RGBA8、单层单 mip、无采样数）→ 图内上传 → `Finalize` 转采样态 → 后端同步等待"，成功返回的 `IScriptTexture` 只读报告 `GetWidth`/`GetHeight`；上传执行 MUST NOT 走脚本提交路径的适配器 `WaitIdle`/执行计数（单列上传执行计数，`GetExecuteGraphCountForTest` 与 `GetWaitIdleCountForTest` 只反映脚本提交）；`width/height` 为零、字节数不等于 `GetImageByteSize(width, height, E_R8G8B8A8_UNORM)`（64 位公开字节数来源，禁止 32 位 `width*height*4` 自算）、名字与既有纹理重复、底层创建失败时 MUST 返回空句柄并由后端 `GetLastError()` 点名原因，MUST NOT 到达两后端 `CreateGPUTexture` 的非法参数路径。纹理对象由适配器保活至宿主关闭，本批 MUST NOT 提供显式销毁；关闭释放顺序 MUST 把纹理排在参数结构之后、图之前。`IScriptRenderGraph::SetPositionTextureStream` SHALL 支持 `POSITION float3 + TEXCOORD float2`（`eR32G32_SFloat`，8 字节属性校验），重名与越界拒绝沿用流面规矩，并 MUST 生成带 `TEXCOORD` 偏移与该格式的真实顶点描述（可经图回读核对，不只靠像素）。

验收测试 `TestCSharpTextureSampling`（GPUBackendTester 第 16 项）SHALL 用新测试着色器 `Shaders/Test/TestCSharpTextureSampling`（片元输出**采样色本身**，premultiplied；原生 `TestTriangleWithTextureSampling` 的片元输出 UV 插值，不满足"纹理内容进像素"的判据，MUST NOT 复用其出图像素期望）：C# 建 `2×2` 四纹素全 `(255, 255, 0, 255)` 的纹理与 `LinearClamp` 采样器绑进 `textureBlock`，清除色 `(0, 0, 1, 1)`，窗口与离屏双 pass 同逻辑提交。正常模式背景像素 MUST 精确 `(0, 0, 255, 255)`、中心 MUST 精确 `(255, 255, 0, 255)`，判据 MUST 来自同一次提交的 `Readback` 单层 RGBA8 离屏目标并核对呈现清单、等待计数与待完成清理。Debug 负向 SHALL 包含：`bad-texture-color`（纹理全改 `(0, 255, 255, 255)`，中心 MUST 精确 `(0, 255, 255, 255)`，否则 `E_TEX_PIXEL` 判红——证明字节→纹理→采样→像素整链参与）、`unbound-image` 与 `unbound-sampler`（漏对应绑定即提交，`SubmitGraph` MUST 提交前非零并点名 `testTexture`/`testSampler`，`E_TEX_BIND`，本次提交引起的后端执行次数增量为 0——上传执行单列不计入）。真实后端负例 MUST 覆盖尺寸不符创建与表外字段 `SetImage` 各一次每后端、拒绝且进程不崩。`Tools/check_csharp_texture_sampling.py` MUST 对账 8 份报告（2 后端 × normal/bad-texture-color/unbound-image/unbound-sampler），缺正常证据、负向被错误接受都非零；报告目录不存在按"判据自身失效"退出 2。

#### Scenario: 两个后端都绘制出纹理驱动的像素
- **WHEN** 分别以 D3D12 和 Vulkan 运行正常的 `TestCSharpTextureSampling`
- **THEN** 两个后端的离屏目标 MUST 与窗口描述快照同尺寸并为 RGBA8，背景与中心 MUST 精确等于 `(0, 0, 255, 255)` 与 `(255, 255, 0, 255)`，两个 pass 的参数表 MUST 各含 1 条以 `textureBlock` 为键的真实绑定，且纹理字段经只读核对口（`GetBoundImageForTest`）读出 MUST 与该次 `CreateTexture` 返回并登记的纹理为**同一对象**（对象同一性，计数替代不了），报告 MUST 为 `pass`

#### Scenario: 漏绑纹理或采样器必须在提交前被点名拒绝
- **WHEN** 在 Debug 下用 `unbound-image` 跳过 `SetImage`、或用 `unbound-sampler` 跳过 `SetSampler`，绑定并提交
- **THEN** `SubmitGraph` MUST 在调用 `ExecuteGraph` 前返回非零并分别点名 `testTexture` 或 `testSampler`，后端执行调用次数 MUST 为 0，测试 MUST 以 `E_TEX_BIND` 失败，两种形态 MUST NOT 用像素内容充当判据

#### Scenario: 非法纹理创建与非法纹理字段必须当场拒绝且不崩
- **WHEN** 经真实适配器以字节数不等于 `GetImageByteSize(width, height, E_R8G8B8A8_UNORM)` 的尺寸创建纹理（含乘积在 32 位会回绕的超大尺寸配空数组，如 `65536×16384`）、或对纹理表外的字段名执行 `SetImage`
- **THEN** 创建 MUST 返回空句柄且后端 `GetLastError()` 点名尺寸原因（64 位实际字节数与收到的字节数都要出现在文本），`SetImage` MUST 返回非零并点名字段名，两后端进程 MUST NOT 崩溃，真实纹理创建调用与执行次数 MUST 保持为 0（被拒项）

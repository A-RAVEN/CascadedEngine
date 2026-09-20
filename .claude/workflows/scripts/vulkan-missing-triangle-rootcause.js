export const meta = {
  name: 'vulkan-missing-triangle-rootcause',
  description: 'Root-cause why GPU tests TestTriangleWithConstantColor and TestTriangleWithStructuredBufferColor render NO triangle on the Vulkan backend (green/blue clear only) while TestSimpleTriangle/ImageBuffer/DoublePass/ComputeBuffer render fine. Candidate causes: (A) shader vertex-attribute reflection missing POSITION for pos-only/ParameterBlock shaders so the pipeline has 0 vertex attributes => degenerate triangle; (B) ShaderStruct color descriptor not resolved/written/bound; (C) vertex buffer bound to wrong binding index; (D) negative-z geometry clipped. Determine which, with file:line evidence, and verify against the D3D12 backend reference.',
  phases: [
    { title: 'Trace' },
    { title: 'Verify' },
  ],
}

const TRACE_SCHEMA = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' },
  hypothesis: { type: 'string', enum: ['A-reflection', 'B-descriptor', 'C-vtxbind', 'D-geometry', 'other'] },
  verdict: { type: 'string', enum: ['confirm', 'refute', 'uncertain'] },
  evidence: { type: 'string' },
  location: { type: 'string' },
}, required: ['claim', 'hypothesis', 'verdict', 'evidence'] } } }, required: ['findings'] }

const VERDICT_SCHEMA = { type: 'object', properties: { verdict: { type: 'string', enum: ['CONFIRMED', 'REFUTED', 'UNCERTAIN'] }, why: { type: 'string' }, fileLine: { type: 'string' } }, required: ['verdict', 'why'] }

const R = 'E:/Projects/CascadedEngine'
const VKEXEC = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp'
const BINDINST = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.cpp'
const SHADERSTRUCT = R + '/VulkanRenderBackendNew/private/VulkanObjects/VulkanShaderStruct.cpp'
const MAIN = R + '/Test/GPUBackendTester/Main.cpp'
const SHADERS = R + '/CAResources/Shaders/Test'

const CTX = [
  'CONTEXT: GPU backend tester renders 8 tests; on the Vulkan backend, 6 render correctly but TestTriangleWithConstantColor (clear = blue, expects a red->yellow gradient triangle) and TestTriangleWithStructuredBufferColor (clear = green, expects a MAGENTA triangle from a StructuredBuffer) produce a PURE-CLEAR frame (no triangle at all). All 8 tests return exit 0. Vulkan validation layer is ON: ConstantColor yields only VUID-VkShaderModuleCreateInfo-pCode-08740 (shader-code DrawParameters capability, benign), StructuredBufferColor yields ZERO VUID. So the failure is SILENT (no VUID), validator-clean.',
  'The two failing shaders and their color source:',
  '- ' + SHADERS + '/TestTriangleWithConstantColor.slang: struct VertexInput{float3 position:POSITION; uint vertexID:SV_VertexID;}; vert(){result.position=float4(input.position,1.0f); result.color=constantColorBlock.colorStruct[input.vertexID].color;} frag(){return float4(input.color,1.0);} — ParameterBlock<ConstantColor> constantColorBlock with ColorSubStruct colorStruct[3].',
  '- ' + SHADERS + '/TestTriangleWithStructuredBufferColor.slang: struct VertexInput{float3 position:POSITION;}; struct StructuredColor{StructuredBuffer<float3> colorStructuredBuffer;}; ParameterBlock<StructuredColor> structuredColorBlock; frag(){return float4(structuredColorBlock.colorStructuredBuffer[0],1.0);}',
  '- ' + SHADERS + '/TestSimpleTriangle.slang (WORKS): VertexInput{float3 position; float3 color}; vert color=input.color; no ParameterBlock.',
  '- ' + SHADERS + '/TestNaiveTriangle.slang (WORKS via ComputeBuffer RED quad): its FRAGMENT shader returns a HARD-CODED red constant float4(1,0,0,1) — it reads NO descriptor in the raster. The ShaderStruct descriptor there is consumed by the COMPUTE pass (RWVertexBuffer), not the raster. So NaiveTriangle does NOT prove a ShaderStruct descriptor is readable in a Vulkan raster. Its raster vertex buffer is written by compute.',
  'CONFOUND (after refutation): variable (2) vertex z = -0.25/0 — the WORKING ComputeBuffer raster uses the SAME z {-0.25,-0.25,0} and still renders a red quad, so negative/zero z is RULED OUT as a cause (hypothesis D geometry/clip is refuted). Two variables remain correlated: (1) vertex struct attribute count — failing = POSITION-only (1 attribute), working = >=2 attributes; (2) raster color source — failing = read from a descriptor (ParameterBlock cbuffer OR StructuredBuffer), working = vertex attribute (SimpleTriangle COLOR) or constant (NaiveTriangle red). Note: across the whole suite, the ONLY raster tests whose fragment/vertex color is consumed from a DESCRIPTOR are the two FAILING ones. Hypotheses A (single-attribute reflection) and B (descriptor read in raster) remain viable and co-correlated; you must separate them by reading CODE, not test data.',
].join('\n')

const CHAR1 = `You are a Vulkan-rendering debugger. ${CTX}\n\nYOUR ASSIGNMENT (hypothesis A — shader vertex-attribute reflection):\nRead how shader vertex attributes are reflected and consumed. In ${VKEXEC} BuildPipelineStates (~line 1727-1808), the vertex input state is built from pFileInfo->reflectionData.m_VertexAttributes. Determine whether for a POSITION-ONLY shader the reflection reports the POSITION attribute and with what m_Location. Specifically: (1) Find where m_VertexAttributes is populated (the Slang reflection code — search VulkanRenderBackendNew and ShaderCompilerSlang for m_VertexAttributes / VertexAttribute reflection). (2) For the two failing shaders vs SimpleTriangle, does reflection emit the POSITION semantic? (3) Look for SLANG parameter-category / data-layout reasons a pos-only VertexInput might be reflected differently, or SV_VertexID affecting reflection (ConstantColor uses vertexID). (4) Conclusion: does BuildPipelineStates end up with 0 vertex attributes (=> pipeline reads no positions => degenerate/invisible triangle) for the failing shaders? Check whether an EMPTY vertexAttributes causes a 0-attribute pipeline, and whether that is silently valid (no VUID). Report claim/verdict/evidence/location. If you find m_VertexAttributes is fine, refute A with evidence.`

const CHAR2 = `You are a Vulkan-rendering debugger. ${CTX}\n\nYOUR ASSIGNMENT (hypothesis B — ShaderStruct color descriptor bound & written):\nTrace the descriptor binding for the two failing tests through ${BINDINST} (BuildResources + BuildDescriptors 5.3 cbuffer / 5.5 buffer), ${SHADERSTRUCT}, and ${VKEXEC} (ReflectResourceBindings + CollectShaderBindings + bindDescriptorSets in RecordRenderPass). Determine: (1) For StructuredBufferColor: does the StructuredBuffer<float3> colorStructuredBuffer binding resolve? i.e. resourceManager.GetBuffer(bufferHandle) returns a non-null vk::Buffer, and SetStorageBuffer is called with the correct spaceID/bindingID, and the descriptor set is actually bound at draw? (2) For ConstantColor: does the constantColorBlock cbuffer resolve (cbuffer.gpuBufferResourceId from cbufferManager.GetResourceId) and get written via SetUniformBuffer? (3) Is the set/binding written into the SAME descriptor set/layout that the pipeline's pipelineLayout uses (reflection setLayoutInfos)? (4) Read the fragment shader reflection to see which set/binding it expects. IMPORTANT correctness check: if the color descriptor were NOT bound, the fragment reads zero => a BLACK triangle (visible on blue/green), NOT an invisible one. So a confirm of B must explain how a missing descriptor yields invisibility specifically. Report claim/verdict/evidence.`

const CHAR3 = `You are a Vulkan-rendering debugger. ${CTX}\n\nYOUR ASSIGNMENT (hypothesis C — vertex buffer binding index; geometry is pre-weakened):\nNOTE: the working ComputeBuffer raster uses the SAME z {-0.25,0} as the failing tests yet renders a red quad, so hypothesis D (clip-space/depth z-culling) is effectively REFUTED. Focus on C: vertex-buffer binding index correctness. Read ${VKEXEC} BuildPipelineStates vertex-input creation (~1727-1808) and RecordRenderPass bindVertexBuffers (~2320-2360). For a POSITION-ONLY single stream (both failing tests), BuildPipelineStates builds 1 binding (index 0) + 1 attribute (location 0). bindVertexBuffers binds via vertexBindingStreamOrder. Determine: (1) is the vertex buffer bound to the SAME binding index the pipeline actually reads? Walk the single-attribute case end-to-end: order size, seenStreamKeys, whether any empty-order or fallback path binds the buffer to an index the pipeline does NOT read. (2) Could the vertex shader end up reading (0,0,0)/degenerate positions for all 3 vertices (=> 0-area triangle => invisible, VUID-clean)? (3) Verify whether this single-attribute path differs in any way from SimpleTriangle's 2-attribute path in how the binding index is computed/published. Report claim/verdict/evidence. Prefer REFUTED if you find the binding index is actually correct.`

const CHAR4 = `You are a Vulkan-rendering debugger. ${CTX}\n\nYOUR ASSIGNMENT (cross-check — isolate the true discriminating variable):\nThe ComputeBuffer test renders a RED quad on Vulkan via TestNaiveTriangle, whose FRAGMENT shader returns a constant red and reads NO descriptor; its vertex buffer is produced by a compute pass. Read ${MAIN} TestComputeBuffer (~804-930) and trace its raster renderContent. Establish the FACTUAL mechanism and then reason about what SINGLE variable truly separates the two FAILING raster tests from the five WORKING ones (SimpleTriangle COLOR vertex-attr; NaiveTriangle constant + compute-written positions). CRITICAL: because NaiveTriangle renders a constant-colored red quad that reads NO raster descriptor, and SimpleTriangle reads color from a VERTEX ATTRIBUTE, the working set never reads a raster DESCRIPTOR for color — so decide whether (a) the discriminating variable is "raster reads a descriptor for color" (supports B) or (b) "vertex input has only 1 attribute" (supports A), or (c) something else. Explicitly address how a fully-invisible (background-clear) result, rather than a black/wrong-color triangle, constrains which hypothesis is consistent. Report claim/verdict/evidence. DO NOT assume NaiveTriangle reads a descriptor in raster — verify the actual shader first.`

const VERIFY = (label, n) => `You are an adversarial verifier. ${CTX}\n\nReview this trace claim from a debugger about the Vulkan missing-triangle bug:\n\nCLAIM ${label}:\n${n}\n\nIndependently RE-READ the actual cited code/shaders in ${R} and decide: is the claim CONFIRMED (real, backed by the actual code you re-read), REFUTED (false or contradicted), or UNCERTAIN (cannot verify from code). Be maximally skeptical — the debugger is most likely to over-claim. Prefer REFUTED/UNCERTAIN on doubt. Output verdict CONFIRMED/REFUTED/UNCERTAIN + why (with file:line you actually read) + fileLine.`

phase('Trace')
const t = await parallel([
  () => agent(CHAR1, { label: 'trace-A-reflection', phase: 'Trace', schema: TRACE_SCHEMA }),
  () => agent(CHAR2, { label: 'trace-B-descriptor', phase: 'Trace', schema: TRACE_SCHEMA }),
  () => agent(CHAR3, { label: 'trace-C-D-geometry', phase: 'Trace', schema: TRACE_SCHEMA }),
  () => agent(CHAR4, { label: 'trace-control', phase: 'Trace', schema: TRACE_SCHEMA }),
])

const results = t.filter(Boolean).flatMap(r => r.findings || [])
log('Collected ' + results.length + ' traces')

phase('Verify')
const verified = await parallel(results.map((f) =>
  () => agent(VERIFY(f.hypothesis, f.claim + '\n' + f.evidence), { label: 'verify:' + f.hypothesis, phase: 'Verify', schema: VERDICT_SCHEMA })
    .then((v) => ({ ...f, verified: v }))
))
log('Verified ' + verified.filter(Boolean).length)

return {
  traces: results,
  verified: verified.filter(Boolean),
  confirmed: verified.filter(Boolean).filter(f => f.verified?.verdict === 'CONFIRMED'),
  bottomLine: 'inspect verified[] for which hypothesis (A/B/C/D) is CONFIRMED with file:line',
}

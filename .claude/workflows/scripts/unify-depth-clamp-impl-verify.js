export const meta = {
  name: 'unify-depth-clamp-impl-verify',
  description: 'Adversarial review of the IMPLEMENTATION of change unify-depth-clamp-switch: 4 independent reviewers with IDENTICAL criteria judge every claim (Vulkan depthClamp feature enabled, depthClampEnable mapping sign, D3D12 inverse mapping in both paths, GPU pipeline-state consistency, test-z fix, empirical render+byte-identity, no new VUID, no regression, API doc semantics via MCP) against real code + test_output + MCP docs; per-claim majority = confirm.',
  phases: [
    { title: 'Verify' },
    { title: 'Aggregate' },
  ],
}

const POINTS = { type: 'object', properties: { points: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, verdict: { type: 'string', enum: ['pass', 'issue', 'uncertain'] },
  defect: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['claim', 'verdict', 'defect', 'evidence', 'confidence'] } } }, required: ['points'] }

const AGG = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, pass: { type: 'integer' }, issue: { type: 'integer' }, uncertain: { type: 'integer' }, defect: { type: 'string' },
}, required: ['claim', 'pass', 'issue', 'uncertain'] } }, mustFix: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' } }, required: ['verdicts', 'mustFix', 'bottomLine'] }

const R = 'E:/Projects/CascadedEngine'
const VKEXEC = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp'
const VKRT = R + '/VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp'
const D3DPIPE = R + '/D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp'
const D3DPSTATE = R + '/D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp'
const MAIN = R + '/Test/GPUBackendTester/Main.cpp'
const TODO = R + '/docs/TODO.md'

const CLAIMS = [
  'I1 VULKAN feature: RenderBackend_Vulkan.cpp now (a) declares vk::PhysicalDeviceFeatures deviceFeatures{}; (b) sets deviceFeatures.depthClamp = features2.features.depthClamp after getFeatures2; (c) sets deviceCreateInfo.pEnabledFeatures = &deviceFeatures. Verify these three edits exist and are correct (so a depthClampEnable=VK_TRUE pipeline is valid, no VUID-00782). Also verify the device actually SUPPORTS depthClamp (features2.features.depthClamp === VK_TRUE on this GPU) so enabling it is valid.',
  'I2 VULKAN mapping: VulkanGraphExecutor.cpp now sets rasterizationState.depthClampEnable = pipelineStateData.rasterizationStates.enableDepthClamp ? VK_TRUE : VK_FALSE. Verify DEFAULTS correct: with default enableDepthClamp=false => depthClampEnable=VK_FALSE => CLIPS near plane (z<0 culled), matching the new semantic. Verify no sign error, and that line 3 (depthClampEnable) is on the rasterizationState struct.',
  'I3 D3D12 GPUPipelineInstance inverse: line ~159 now DepthClipEnable = rasterizeStates.enableDepthClamp ? FALSE : TRUE. Verify INVERSE is correct: enableDepthClamp=false (default) => DepthClipEnable=TRUE => clip z<0 (matches Vulkan default depthClampEnable=FALSE clips). Verify the DEFAULT now CLIPS (so D3D12 behavior changed from no-clip to clip — the intended BREAKING consistency change).',
  'I4 D3D12 PipelineStatesObject inverse: line ~31 now DepthClipEnable = srcRasterizerState.enableDepthClamp ? FALSE : TRUE (was direct enableDepthClamp). Verify BOTH D3D12 paths now use the SAME inverse mapping => consistent. Verify no path was left with the old direct/hardcoded behavior.',
  'I5 TEST-z fix: Main.cpp TestTriangleWithConstantColor AND TestTriangleWithStructuredBufferColor testBuffer z now {0.25,0.25,0.5} (was {-0.25,-0.25,0}). Verify both changed, and 0.25/0.5 matches the working SimpleTriangle z (Main.cpp ~153-155) so the triangles render in-volume on both backends with default clip.',
  'I6 EMPIRICAL render+consistency: read test_output/result_vulkan.json and result_d3d12.json — all 8 tests exit 0/pass on BOTH backends. md5sum test_output/TestTriangleWithConstantColor_vulkan.png vs _d3d12.png and TestTriangleWithStructuredBufferColor_vulkan.png vs _d3d12.png — both must be IDENTICAL (byte-for-byte). Also confirm the PNGs are non-clear (the triangle is actually drawn): e.g. sizes differ from a pure-clear frame; read the images if you can.',
  'I7 EMPIRICAL no-regression: SimpleTriangle/ImageBuffer/DoublePass/readback vulkan vs d3d12 md5 must be IDENTICAL (unchanged from prior). ComputeBuffer is EXPECTED to DIFFER (baseline 中同样存在的时间动画 compute — sin(time) in TestComputeVertexBuffer.slang:24 => sub-pixel vertex motion => tiny PNG diff) — this is NOT a regression from the depth-clamp change; confirm ComputeBuffer md5 differs but the change did not touch compute.',
  'I8 EMPIRICAL no-new-VUID: check all test_output/*_validation.log for vulkan. Except ConstantColor (which has only the benign VUID-VkShaderModuleCreateInfo-pCode-08740 shader-code warning, baseline 中同样存在), ALL should be 0 bytes. Confirm NO new VUID-...-depthClampEnable-00782 or any other new validation error appeared. If ConstantColor log is >0 bytes, confirm it contains ONLY 08740.',
  'I9 API semantics (MCP ONLY, use web-reader/web-search-prime, do NOT use WebSearch/WebFetch): verify (a) VkPipelineRasterizationStateCreateInfo::depthClampEnable TRUE => clamp instead of clipping primitives to z planes (render z<0); FALSE (default) => clip. (b) depthClampEnable requires the depthClamp feature (VUID ...-00782). (c) D3D12 RasterizerDesc::DepthClipEnable FALSE => no z clipping (z<0 renders), so enableDepthClamp maps INVERSE. Cite real URLs.',
  'I10 DESIGN/SPEC/impl consistency: read ' + R + '/openspec/changes/unify-depth-clamp-switch/design.md + specs/pipeline-depth-clamp/spec.md — confirm the implemented defaults/mapping match the design decisions D1 (single field default false=clip), D2 (Vulkan depthClampEnable=field, D3D12 DepthClipEnable=!field), D3 (default false => tests need z>=0), and the spec Requirement 1/3. Flag any mismatch between what was implemented and what the spec requires.',
  'I11 TEST harness note: task says "8 tests" (CLAUDE.md says 7 — stale). Confirm the tester runs 8 tests (Main.cpp registration) and all 8 are exercised. Also note TestIMGUI still shows white-block font (separate TODO 2026-08-30, out of scope) but still exits 0 — confirm the change did not touch IMGUI and the TODO documents it as separate.',
].join('\n')

const JUDGE = [
  'You are an INDEPENDENT adversarial reviewer of the IMPLEMENTATION of OpenSpec change unify-depth-clamp-switch, which unifies the near-plane depth-clamp/clip rasterization switch so Vulkan and D3D12 behave identically (default false = standard near-plane clip). Read the REAL implementation in the cited files and the REAL test output in ' + R + '/test_output.',
  'CRITICAL RULE 1: verify every claim against REAL code / REAL test_output. Read the actual edited lines, the actual result_vulkan.json / result_d3d12.json, md5sum the actual PNGs, read the actual validation logs. Do not trust the implementer summary.',
  'CRITICAL RULE 2 (MCP ONLY): for API-semantics claim I9, use MCP tools web-reader / web-search-prime. Do NOT use WebSearch/WebFetch. If unavailable, mark uncertain.',
  'CRITICAL RULE 3: you may run md5sum/cat/grep on test_output and Read the edited source files. You are NOT required to rebuild or rerun tests.',
  'Verdict rules: verdict = pass ONLY if accurate AND backed by real code/file/md5/URL you verified. issue = false/overstated/sign-error/missing edit/mismatch. uncertain = cannot verify.',
  'For I1-I5 (the technical crux) and I7 (the ComputeBuffer "expected DIFFER") be maximally skeptical — prefer issue/uncertain on any doubt. But ALSO be fair: a pre-existing, non-regression DIFFER on the time-animated ComputeBuffer is NOT a defect of this change.',
].join('\n')

const angle = (n) =>
'You are reviewer #' + n + '/4. Every reviewer uses the IDENTICAL rules below; judge independently, do not assume others\' votes.\n\n' + JUDGE + '\n\nCLAIMS TO JUDGE (each -> one vote):\n' + CLAIMS + '\n\nOutput the full list: claim; verdict pass/issue/uncertain; defect (precise with real file:line or md5 or URL); evidence (what you verified); confidence high/medium/low.'

phase('Verify')
const results = await parallel([
  () => agent(angle(1), { label: 'verify-1', phase: 'Verify', schema: POINTS }),
  () => agent(angle(2), { label: 'verify-2', phase: 'Verify', schema: POINTS }),
  () => agent(angle(3), { label: 'verify-3', phase: 'Verify', schema: POINTS }),
  () => agent(angle(4), { label: 'verify-4', phase: 'Verify', schema: POINTS }),
])
const allPts = results.filter(Boolean).flatMap(r => r.points || [])
log('Collected ' + allPts.length + ' verdicts')

phase('Aggregate')
const agg = await agent(
'Aggregate these verdicts. For each claim id I1-I11 output {claim, pass, issue, uncertain} = counts across reviewers, and defect = the concrete defect(s) if any reviewer said issue.\nSet mustFix = array of claim ids where issue count >= 2 (2+ of 4 flags a defect). Claims with issue 1 = worth attention; 0 = accepted.\nThen bottomLine = blunt verdict: is the unify-depth-clamp-switch IMPLEMENTATION correct and complete (does it actually make the two triangle tests render identically on both backends with no new VUID and no regression), or does it need revision? Name each specific fix.\n\nVERDICTS JSON:\n' + JSON.stringify(allPts),
  { label: 'aggregate', phase: 'Aggregate', schema: AGG })

return { verdicts: (agg?.verdicts || []), mustFix: (agg?.mustFix || []), bottomLine: (agg?.bottomLine || ''), rawVotes: allPts.length }

export const meta = {
  name: 'verify-dedup-impl',
  description: 'Adversarially verify the IMPLEMENTED fix-vulkan-dedup-key-identity (code + test evidence), not just the artifacts. Independent reviewers check: (1) VulkanShaderResourceSet content-key fix correctness (operator== vs hash consistency, unordered_map key contract, 6 iteration sites, compute + raster surfaces), (2) PipelineLibraryCache rasterization-key fix (fields / GenerateHashKey / stateCombo from converted vk struct), (3) D3D12 no-regression, (4) truthfulness of the PIL/exit-code/VUID evidence, (5) MCP re-verify of cited Vulkan API doc URLs. Refutation + APPROVE/REVISE synthesis with auditAdditions.',
  phases: [
    { title: 'Review' },
    { title: 'Refute' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const C = R + '/openspec/changes/fix-vulkan-dedup-key-identity'

const GROUND = [
  'REPO: ' + R + '  CHANGE DIR: ' + C,
  'STATE: tasks 1.1-6.2 DONE, 7.1/7.2 (review) pending. Implementation committed? NO - code is in working tree (VulkanRenderBackendNew, Test/GPUBackendTester). Build: `python build.py --config Debug` -> BUILD SUCCESSFUL (only pre-existing warnings).',
  'SOURCE-LEVEL CLAIMS TO VERIFY (read the REAL files):',
  '(A) VulkanGraphExecutor.h:223-233 VulkanShaderResourceSet::operator== -> content-based: compares shaderInfo.path AND resourceDic (unordered_map<NameHash,VulkanShaderStruct const*>) member-by-member (same size + each [name,ptr] present with equal ptr). hash member (size_t, h:209) is computed in Init. custom_hash_trait<graphics_backend::VulkanShaderResourceSet> in GLOBAL namespace cacore (h:244-254) redirects cacore::hash to `hasher.hash(obj.hash)`. static_asserts cacore::equal_test<..> + cacore::hashable<..> (h:262-263).',
  '(B) VulkanGraphExecutor.cpp:205-215 Init hash loop: hash = hash_combine(path); then XOR-combine over resourceDic of ( name.GetHash(), structPtr ) pairs -> order-independent so equal-content-set => equal hash. createOrGetBindingInstance (cpp:1096-1107) uses m_ShaderResourceInstances.find(resourceSet) / m_ShaderResourceInstances[resourceSet] (FULL content key, not .hash). Map key type changed to VulkanShaderResourceSet (h:370). Six iteration destructuring sites (cpp:490/583/601/631/670/839) now `[resourceSet, instance]`. Reset() clears map (cpp:3005).',
  '(C) PipelineLibraryCache.h:38-47 RenderStateCombination added polygonMode/cullMode/frontFace/depthClampEnable (NOT lineWidth). cpp:99-104 GenerateHashKey hashes those 4 via static_cast<uint32_t>. VulkanGraphExecutor cpp stateCombo (cpp:1931-1934) copies polygonMode/cullMode/frontFace/depthClampEnable FROM THE CONVERTED vk::PipelineRasterizationStateCreateInfo (rasterizationState, built cpp:1835-1844).',
  '(D) D3D12 paths untouched: no D3D12 source modified; the fix is Vulkan-only.',
  'TEST EVIDENCE CLAIMS (verify these are TRUTHFUL by re-measuring, do not trust the narrative):',
  'Build: Debug BUILD SUCCESSFUL. All 8 tests (TestSimpleTriangle, TestTriangleWithConstantColor, TestTriangleWithStructuredBufferColor, TestTriangleWithImageBuffer, TestDoublePass, TestComputeBuffer, TestReadback, TestIMGUI) x vulkan+d3d12 headless exit 0; grep of stderr logs for VUID/validation errors is EMPTY.',
  'IMGUI offset: `--headless 8 --capture 5` produced TestIMGUI_viewport{0,1,2}_{vulkan,d3d12}.png. Cross-correlation (numpy) of non-background masks: v1 vulkan-vs-d3d12 shift=(dx=0,dy=0) meanDiff=0.000; v2 similarly (0,0)/0.000. This means the vulkan side-window content is IDENTICAL to d3d12 (offset (副窗pos-主窗pos) GONE). imgui-diag: viewport[0] main imguipos=(361,390), viewport[1]/[2] imguipos=(421,450).',
  'Compute dual-dispatch (4.2): TestComputeBuffer now has 2 dispatches same shader "TestComputeVertexBuffer", different per-dispatch struct (computeParams->vbuffer/time=duration; computeParams2->vbuffer1/time=duration+1). Captures TestComputeBuffer.png (dispatch1) + TestComputeBuffer_d1.png (dispatch2). Measured: vulkan d0 vs d1 meanDiff=10.7 (differ -> distinct constants); vulkan d1 vs d3d12 d1 meanDiff=0.04 (~identical -> fix works); d1 bbox (256,123,767,339), d0 bbox (256,83,767,381).',
  'Dual-cull (4.3): TestDoublePass renders a red triangle twice via TestNaiveTriangle with SetPipelineState(CullFront) vs (CullBack) to cullFrontRT/cullBackRT. Measured: vulkan cullFront vs cullBack meanDiff=21.25 (differ); cullFront bbox=(0,0,0,0) EMPTY, cullBack bbox=(256,128,766,383) visible; vulkan cullFront vs d3d12 cullFront meanDiff=0.0; vulkan cullBack vs d3d12 cullBack meanDiff=0.0.',
  'API DOC URLS CLAIMED: [VkPipelineRasterizationStateCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRasterizationStateCreateInfo.html) and [Pipelines: Pipeline Cache](https://docs.vulkan.org/spec/latest/chapters/pipelines.html).',
].join('\n')

const COMMON = [
  'You are an adversarial reviewer of the IMPLEMENTED change fix-vulkan-dedup-key-identity. Read the REAL source (VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.h/.cpp, private/PipelineLibrary/PipelineLibraryCache.h/.cpp, Test/GPUBackendTester/Main.cpp, CACore/header/Hasher.h, D3D12RenderBackend) and the actual test artifacts (out/build/x64-debug/bin/test_output/*.png + logs) to verify EVERY claim. You may Read/Grep/run read-only commands. Do NOT modify. Maximally rigorous: flag overclaim, is-a-claim-really-true-from-code, hash/equality inconsistency (a==b must imply hash(a)==hash(b)), incorrect namespace for custom_hash_trait, missed iteration site, D3D12 regression, test-evidence overstatement, invalid test logic, or any gap vs the spec (openspec/changes/fix-vulkan-dedup-key-identity/specs/**). Point at concrete file:line. For test evidence, actually re-open the PNGs with PIL/numpy and re-measure the claimed deltas.',
  'GROUND:\n' + GROUND,
].join('\n\n')

const REV = { type: 'object', properties: { issues: { type: 'array', items: { type: 'object', properties: {
  area: { type: 'string' }, severity: { type: 'string', enum: ['blocker', 'mustfix', 'shouldfix', 'nit'] },
  detail: { type: 'string' }, evidence: { type: 'string' }, fixSuggestion: { type: 'string' },
}, required: ['area', 'severity', 'detail', 'evidence', 'fixSuggestion'] } }, total: { type: 'number' } }, required: ['issues', 'total'] }

const REF = { type: 'object', properties: { claim: { type: 'string' }, verdict: { type: 'string', enum: ['holds', 'refuted'] },
  counterDetail: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['claim', 'verdict', 'counterDetail', 'evidence', 'confidence'] }

const SYNTH = { type: 'object', properties: { verdict: { type: 'string', enum: ['approve', 'revise'] },
  blockers: { type: 'array', items: { type: 'string' } }, mustfix: { type: 'array', items: { type: 'string' } },
  bottomLine: { type: 'string' }, auditAdditions: { type: 'array', items: { type: 'string' } } }, required: ['verdict', 'blockers', 'mustfix', 'bottomLine'] }

const ROLE = (n, focus) => 'You are reviewer #' + n + '. ' + COMMON + '\n\nYOUR FOCUS:\n' + focus

const REVIEWS = [
  ['binding-key', 'Verify the binding-instance content-key fix. Read VulkanGraphExecutor.h (operator== 223-233, hash member, custom_hash_trait namespace 244-254, static_asserts 262-263, m_ShaderResourceInstances type) and VulkanGraphExecutor.cpp (Init hash 205-215, createOrGetBindingInstance 1096-1107, the 6 iteration sites 490/583/601/631/670/839, Reset clear). Confirm: (a) a==b IMPLIES hash(a)==hash(b) (operator== content-based AND the `hash` member is a deterministic function of the same content; is there ANY path where equal content yields different hash member, or different content yields equal hash that WRONGLY merges?); (b) m_ShaderResourceInstances is now keyed by the full VulkanShaderResourceSet, so two passes sharing path+binding-names but different struct pointers get distinct instances; (c) the 6 iteration sites were ALL updated (no leftover [hash,instance]); (d) custom_hash_trait is in the GLOBAL cacore namespace (a graphics_backend::cacore one would be a DIFFERENT template the hasher never uses); (e) Reset() clears the map so no cross-frame growth. Flag any hash/equality inconsistency, missed site, or template-instantiation failure risk (e.g. is the map key valid for EASTL equal_to + cacore::hash).'],
  ['pipeline-cache', 'Verify the PipelineLibraryCache rasterization-key fix. Read PipelineLibraryCache.h (RenderStateCombination fields 38-47, MISSING lineWidth correctly), PipelineLibraryCache.cpp GenerateHashKey (hashes the 4 fields), and VulkanGraphExecutor.cpp stateCombo (copies from the CONVERTED vk::PipelineRasterizationStateCreateInfo rasterizationState 1835-1844 -> 1931-1934). Confirm: (a) the 4 fields polygonMode/cullMode/frontFace/depthClampEnable are genuinely read into the VkPipeline (via rasterizationState) and now into the key; (b) lineWidth correctly omitted (backend hardcodes 1.0f and never reads per-pass value) -> no key dimension that never varies; (c) stateCombo reads the CONVERTED vk struct (consistent with existing convention topology/sampleCount/depthCompareOp) NOT the raw enum; (d) is there ANY other RenderStateCombination construction site that needs the new fields, or any place relying on the old (shorter) key? (e) does adding these fields preserve the cache as hash-only (matches D3D12) with no requirement violated.'],
  ['d3d12-no-regression', 'Verify D3D12 is genuinely untouched (read git status / diff - only VulkanRenderBackendNew, Test/GPUBackendTester, openspec changed; no D3D12RenderBackend). Confirm the D3D12 binding-instance path (GPUResourceBindingInstance.h ShaderResourceSet operator<=> = default content key) and pipeline-state key (GPUPipelineInstance.h hash-only) are as the change describes, and that all d3d12 tests still pass. Confirm the claim "D3D12 reference is content-keyed for binding instance but hash-only for pipeline state" is ACCURATE. Flag any overclaim about D3D12.'],
  ['test-evidence', 'Independently REPLICATE the test evidence with PIL/numpy - do NOT trust the narrative. Re-open out/build/x64-debug/bin/test_output/TestIMGUI_viewport{0,1,2}_{vulkan,d3d12}.png, TestComputeBuffer{,_d1}_{vulkan,d3d12}.png, TestDoublePass_cull{Front,Back}_{vulkan,d3d12}.png. Re-compute: (a) IMGUI v1/v2 cross-correlation shift vulkan-vs-d3d12 (reported (0,0) meanDiff 0.000 - is that really true, or did the mask/threshold make two DIFFERENT images appear identical? ALSO check v0 and that the images are non-trivial); (b) TestComputeBuffer d0 vs d1 differ (reported 10.7) and vulkan d1 ~= d3d12 d1 (reported 0.04); (c) TestDoublePass cullFront EMPTY vs cullBack visible (reported (0,0,0,0) vs (256,128,766,383)) and vulkan==d3d12 (0.0). Verify directly the deltas, and also confirm the PNGs are actually non-blank / the tests really rendered. Flag any evidence that is fabricated, trivial-tautological (e.g. two identical images compared), or too weak to support the spec scenario.'],
  ['api-doc-mcp', 'Part A: re-verify the 2 cited Vulkan API doc URLs are REAL and the cited API exists + semantics match (use ONLY MCP internet tools mcp__web-reader__webReader / mcp__web-search-prime__web_search_prime, NOT WebFetch/WebSearch): (i) https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRasterizationStateCreateInfo.html - confirm polygonMode/cullMode/frontFace/depthClampEnable/lineWidth members; (ii) https://docs.vulkan.org/spec/latest/chapters/pipelines.html - confirm the VkPipelineCache pipeline-cache section says construction results are reused by passing the same cache, i.e. keyed by creation inputs so differing rasterization can coexist; and that VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT carries pre-raster (incl. rasterization) state. Cite real URLs + verdicts. Part B: re-read the source to confirm the change does NOT claim D3D12 pipeline-state is a full content key (it must say hash-only). Flag any hallucinated URL, missing claim, or overstated semantics.'],
]

phase('Review')
const revs = await parallel(REVIEWS.map(([k, focus], i) => () =>
  agent(ROLE(i + 1, focus), { label: 'review:' + k, phase: 'Review', schema: REV })))
const issues = revs.filter(Boolean).flatMap(r => r.issues || [])
const blockers = issues.filter(i => i.severity === 'blocker').map(i => i.evidence + ' :: ' + i.fixSuggestion)
const mustfix = issues.filter(i => i.severity === 'mustfix').map(i => i.evidence + ' :: ' + i.fixSuggestion)
log('Review issues: ' + issues.length + ' (blockers:' + blockers.length + ' mustfix:' + mustfix.length + ')')

phase('Refute')
const CLAIMS = [
  'The VulkanShaderResourceSet content-key fix (operator== content-based + hash folds per-pass struct pointer + unordered_map keyed by the full set + 6 iteration sites updated) is correct and consistent, resolving the IMGUI multi-window offset for both raster and compute surfaces.',
  'The PipelineLibraryCache rasterization-key fix (RenderStateCombination + GenerateHashKey + stateCombo from converted vk struct) is correct and resolves the same-shader-different-rasterization pipeline collapse; lineWidth correctly omitted.',
  'The test evidence is truthful and sufficient: IMGUI v1/v2 vulkan==d3d12 (no offset), compute d1 vulkan==d3d12 (dispatch2 independent), cullFront/cullBack distinct + vulkan==d3d12, all 16 runs exit 0 with no VUID.',
  'D3D12 is untouched and correctly characterized (binding content-keyed, pipeline-state hash-only).',
]
const refute = await parallel(CLAIMS.map((c, ci) => () =>
  agent('Adversarially REFUTE this claim; default to REFUTED if it does not hold from the code/artifacts. ' + COMMON + '\n\nCLAIM: ' + c, { label: 'refute:' + ci, phase: 'Refute', schema: REF })))

phase('Synthesize')
const synth = await agent(
  'You are the final judge for the IMPLEMENTED change fix-vulkan-dedup-key-identity. Decide APPROVE (implementation + evidence are correct, ready to close) or REVISE with blockers/mustfix + auditAdditions (new [AUDIT-] task titles needed). Cross-check reviewers + refutations. Be skeptical: verify claims are genuinely proved by code+reproduced evidence, not asserted. Do NOT rubber-stamp.\n\n' + COMMON + '\n\nREVIEWER ISSUES:\n' + JSON.stringify(issues) + '\n\nREFUTATIONS:\n' + JSON.stringify(refute),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { issues, blockers, mustfix, refute, synth }

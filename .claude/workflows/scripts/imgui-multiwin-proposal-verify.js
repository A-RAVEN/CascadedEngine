export const meta = {
  name: 'imgui-multiwin-proposal-verify',
  description: 'Adversarially verify the OpenSpec change fix-vulkan-dedup-key-identity (proposal/design/specs/tasks) BEFORE implementation. Multi reviewers: correctness of the root-cause mechanism + fix + line numbers, spec scenario format/testability, design decisions + D3D12-honesty, task completeness + build-validation mandate, and a dedicated MCP checker for any external Vulkan API validity. Then a refutation pass + synthesis.',
  phases: [
    { title: 'Review' },
    { title: 'Refute' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const C = R + '/openspec/changes/fix-vulkan-dedup-key-identity'

const GROUND = [
  'REPO: ' + R,
  'CHANGE DIR: ' + C + '  (files: proposal.md, design.md, specs/vulkan-shader-binding/spec.md, specs/vulkan-pipeline-cache/spec.md, tasks.md)',
  'CONFIRMED GROUND TRUTH (source-verified, do not re-litigate): IMGUI multi-viewport side-window offset is caused by VulkanShaderResourceSet::Init (VulkanGraphExecutor.cpp:205-211) hashing ONLY shader-path + binding names, ignoring the per-pass VulkanShaderStruct* in resourceDic ([name, structPtr]); operator== is hash-only (VulkanGraphExecutor.h:214-215); createOrGetBindingInstance (VulkanGraphExecutor.cpp:1085-1096) returns FIRST instance on hash hit -> all ImGui viewport passes collapse to the MAIN instance -> side windows bind MAIN constant -> content shifts by (secondary_pos-main_pos). D3D12 correct (ShaderResourceSet operator<=> = default incl. struct pointers, GPUResourceBindingInstance.h:27-33). The CHANGE proposes: (1) make VulkanShaderResourceSet dedup identity include the per-pass struct pointer (fold into hash + content operator== + key instance map), fixing raster (1119-1121) and compute (1144-1146) surfaces; (2) add rasterization (polygonMode/cullMode/frontFace/depthClampEnable/lineWidth) to PipelineLibraryCache RenderStateCombination/GenerateHashKey because VkPipeline is built from it but key omits it. m_ShaderResourceInstances is cleared each Reset() (VulkanGraphExecutor.cpp:2972-2985) so pointer-keying is per-frame bounded.',
].join('\n')

const COMMON = [
  'You are an adversarial reviewer of the OpenSpec change `fix-vulkan-dedup-key-identity`. Read the actual artifact files (proposal.md, design.md, specs/**/spec.md, tasks.md) in ' + C + ' AND the real source (VulkanRenderBackendNew, D3D12RenderBackend) to check every claim. You may Read/Grep/run read-only commands. Do NOT modify. Be maximally rigorous: flag any overclaim, line-number drift, spec scenario that is not testable / not 4-hashtag, design decision that is wrong, D3D12-honesty issue, or task that violates the project rule "last task must be a Review & Adversarial Verify task" or "build only via python build.py". Point at concrete file:line.',
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
  ['rootcause-fix', 'Verify the CORE claim + fix. Read VulkanGraphExecutor.cpp (200-215, 1085-1096, 1119-1150, 2972-2985) and VulkanGraphExecutor.h (205-220, 315-325). Confirm: (a) the hash drops structPtr; (b) createOrGetBindingInstance returns first-on-hit; (c) folding structPtr into the hash (or content-keying) is a CORRECT and SUFFICIENT fix for BOTH raster and compute surfaces; (d) changing operator== is consistent with the hash; (e) m_ShaderResourceInstances cleared in Reset() truly bounds per-frame growth (confirm Reset() is called per execution and there is no other persistent holder). Flag any design decision that is wrong or incomplete.'],
  ['pipeline-cache', 'Verify the PipelineLibraryCache claim. Read PipelineLibraryCache.h (1-45), PipelineLibraryCache.cpp (10-110), VulkanGraphExecutor.cpp (1660-1700, 1815-1835, 1890-1935, 1960-1980). Confirm: (a) RenderStateCombination/GenerateHashKey truly omit rasterization; (b) VkPipeline is built from rasterizationState; (c) adding the rasterization fields to the key + hashing them + populating stateCombo is correct & sufficient; (d) D3D12 GPUPipelineStateKey really compares full pipeline state incl rasterization. Flag any mismatch.'],
  ['specs-format', 'Verify spec files are valid for the openspec schema: every scenario uses EXACTLY 4 hashtags (####), every requirement has >=1 scenario, SHALL/MUST used (not should/may), delta operations (ADDED/MODIFIED/REMOVED/RENAMED) correct, modified-capability folder name matches an existing openspec/specs capability name, new capability folder matches proposal. Flag any formatting/schema issue that fails silently (3-hashtag, bullets, missing scenario).'],
  ['tasks-completeness', 'Verify tasks.md: every task is a `- [ ] N.M desc` checkbox in a ## group; tasks ordered by dependency and atomic; covers all spec scenarios; the LAST section is a Review & Adversarial Verify task; build verification uses `python build.py` (NOT ninja/cmake --build); includes the compute-double-dispatch + double-rasterization test cases + IMGUI multi-window regression + D3D12 non-regression. Flag missing or rule-violating tasks.'],
  ['d3d12-honesty', 'Audit D3D12-honesty + scope: does the change claim D3D12 is correct/reference (true?) and does it AVOID touching D3D12? Does it honestly scope OUT RenderPassCacheKey sampleCount/loadOp, D3D12 EnsureResourceBindingInstance null-return, D3D12 HashObj hash-only equality as non-goals with correct reasoning? Flag any overclaim about D3D12 being SAFE or about the non-goals being correctly excluded (confirm each exclusion is genuinely not a dedup-collapse).'],
  ['mcp-api-validity', 'USE MCP INTERNET TOOLS ONLY (mcp__web-reader__webReader and/or mcp__web-search-prime__web_search_prime — do NOT use WebFetch/WebSearch). Validate the external Vulkan API semantics the change depends on, citing real doc URLs: (a) VkPipelineCache is a storage cache keyed by the pipeline-creation inputs, not by a custom host-side hash — confirm building two VkPipeline with different rasterization state (both stored in the same VK pipeline cache) is correct and that the cache does NOT depend on the host-side RenderStateCombination key; (b) with VK_EXT_graphics_pipeline_library, the pre-raster library contains rasterization state, so two passes differing in cullMode/frontFace/polygonMode need DISTINCT pre-raster libraries/pipelines — confirm this makes the missing-rasterization-in-key a real correctness bug and the added-fields fix correct; (c) confirm binding a descriptor set / UBO whose IMGUIScale_Pos differs per draw is valid. For each, confirm the API really exists + semantics match the design claim; cite the doc URL and note verification result.'],
]

phase('Review')
const revs = await parallel(REVIEWS.map(([k, focus], i) => () =>
  agent(ROLE(i + 1, focus), { label: 'review:' + k, phase: 'Review', schema: REV })))
const issues = revs.filter(Boolean).flatMap(r => r.issues || [])
const blockers = issues.filter(i => i.severity === 'blocker').map(i => i.evidence + ' :: ' + i.fixSuggestion)
const mustfix = issues.filter(i => i.severity === 'mustfix').map(i => i.evidence + ' :: ' + i.fixSuggestion)
log('Review issues: ' + issues.length + ' (blockers:' + blockers.length + ' mustfix:' + mustfix.length + ')')

phase('Refute')
// Adversarially refute the top claims the change is built on
const CLAIMS = [
  'The IMGUI side-window offset is fully explained by VulkanShaderResourceSet::Init hash dropping the per-pass struct pointer (the change\'s premise).',
  'Folding the per-pass struct pointer into the dedup identity is a correct, sufficient, non-regressing fix for both the raster AND compute binding-instance surfaces.',
  'Adding rasterization to PipelineLibraryCache RenderStateCombination/GenerateHashKey correctly fixes the same-shader-different-rasterization pipeline collapse.',
  'D3D12 needs no change and its backend is correctly keyed (reference); the change does not regress D3D12.',
]
const refute = await parallel(CLAIMS.map((c, ci) => () =>
  agent('Adversarially REFUTE this claim; default to REFUTED if it does not hold from code. ' + COMMON + '\n\nCLAIM: ' + c, { label: 'refute:' + ci, phase: 'Refute', schema: REF })))

phase('Synthesize')
const synth = await agent(
  'You are the final judge for change fix-vulkan-dedup-key-identity. Decide APPROVE (ready to implement) or REVISE with blockers/mustfix list. Consider all reviewer issues + refutations. Cross-check: is the fix correct + sufficient? Are the specs schema-valid + testable? Are tasks complete + rule-compliant? Is D3D12-honesty sound? List any [AUDIT-] additions needed for a follow-up review. Be rigorous; do NOT rubber-stamp.\n\n' + COMMON + '\n\nREVIEWER ISSUES:\n' + JSON.stringify(issues) + '\n\nREFUTATIONS:\n' + JSON.stringify(refute),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { issues, blockers, mustfix, refute, synth }

export const meta = {
  name: 'imgui-multiwin-final-verify',
  description: 'Adversarially verify the FINAL (committed, post-revision) OpenSpec change fix-vulkan-dedup-key-identity before apply. Independent reviewers check: (1) cross-artifact consistency of the committed design (Option A content key ONLY, content-reverify task 3.4 consistent with spec/proposal/design, lineWidth dropped, D3D12-honesty, PNG filename, test-count stays 8), (2) schema validity + spec scenario testability, (3) task-rule compliance (last task is Review & Adversarial Verify; build only via python build.py), (4) the binding-instance fix is correct+sufficient for raster AND compute surfaces, (5) MCP check of the Vulkan pipeline-cache / GPL pre-raster semantics the change relies on. Refutation pass + APPROVE/REVISE synthesis.',
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
  'FINAL STATE (already reviewed pre-revision; this verifies the committed revision): artifacts = proposal.md, design.md, specs/vulkan-shader-binding/spec.md, specs/vulkan-pipeline-cache/spec.md, tasks.md. `openspec validate` returns valid. Tasks 0/19 done (not yet implemented).',
  'CONFIRMED MECHANISM (source-verified): VulkanShaderResourceSet::Init (VulkanGraphExecutor.cpp:205-211) hashes only shader-path + binding NAMES, ignoring the per-pass VulkanShaderStruct* in resourceDic ([name, structPtr]); operator== hash-only (VulkanGraphExecutor.h:214-216); m_ShaderResourceInstances unordered_map<size_t,...> (h:321); createOrGetBindingInstance (cpp:1085-1096) returns FIRST on hash hit -> all ImGui viewport passes collapse to MAIN instance -> side windows bind MAIN imguiCommon (IMGUIScale_Pos.zw = main_pos) -> content shifts by (secondary_pos-main_pos). m_ShaderResourceInstances cleared in Reset() (cpp:2972-2985) -> per-frame bounded. D3D12 binding instance IS content-keyed (ShaderResourceSet operator<=> = default incl. per-viewport struct ptr, GPUResourceBindingInstance.h:27-33).',
  'COMMITTED DESIGN (what to verify): (1) VulkanShaderResourceSet dedup identity includes per-pass struct pointer via FULL content key (Option A ONLY; Option B/size_t hash-only explicitly rejected in design.md + tasks.md 2.3); hash order-stable (design Risk); (2) PipelineLibraryCache key adds polygonMode/cullMode/frontFace/depthClampEnable (lineWidth DROPPED because backend hardcodes 1.0f, VulkanGraphExecutor.cpp:1827): RenderStateCombination fields + GenerateHashKey hash them + stateCombo filled from the CONVERTED vk rasterizationState; AND TryGetCachedPipeline content-reverifies on hash hit (task 3.4) to avoid a wrong VkPipeline on 2^-64 collision. (3) D3D12-honesty: GPUPipelineStateKey input covers full CPipelineStateObject incl rasterization but equality is hash-only (Hasher.h:258-261); only binding instance is a true content key. (4) PNG path TestIMGUI_viewport<N>_<backend>.png; test-count stays 8 (4.2/4.3 extend existing bodies).',
].join('\n')

const COMMON = [
  'You are an adversarial reviewer of the FINAL OpenSpec change fix-vulkan-dedup-key-identity. Read the actual committed artifacts in ' + C + ' AND the real source (VulkanRenderBackendNew, D3D12RenderBackend, CACore/header/Hasher.h, Test/GPUBackendTester/Main.cpp) to check every claim. You may Read/Grep/run read-only commands. Do NOT modify. Maximally rigorous: flag overclaim, line-number drift, spec scenario not testable/not 4-hashtag, wrong design decision, D3D12-honesty issue, task rule violation, or any inconsistency introduced during the revision. Point at concrete file:line.',
  'GROUND (final state):\n' + GROUND,
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
  ['consistency', 'Cross-artifact consistency of the final design. Read all artifacts. Verify: (a) Option A committed everywhere and Option B explicitly rejected (design.md, tasks.md 2.3, no hash-only equality leak); (b) content-reverify (task 3.4) is consistent across spec (vulkan-pipeline-cache SHALL), proposal (New Capabilities), design (Decision 2), tasks (3.4) — no dangling mandate without a task, no task without a mandate; (c) lineWidth absent from key everywhere + rationale; (d) PNG path TestIMGUI_viewport<N>_<backend>.png consistent across proposal/tasks/source comment; (e) test-count stays 8 consistently; (f) D3D12-honesty consistent. Flag ANY residual contradiction/carry-over from the earlier version.'],
  ['schema-testability', 'Verify spec schema + testability. Every scenario uses EXACTLY 4 hashtags (####), every requirement has >=1 scenario, SHALL/MUST (not should/may), ADDED Requirements used (both spec files), modified-capability folder name matches an existing openspec/specs capability (vulkan-shader-binding), new capability folder matches proposal (vulkan-pipeline-cache). Confirm each scenario is behaviorally observable by a backend test (or honestly framed as an invariant) — NOT an internal implementation representation. Flag any scenario that reads as implementation detail or is untestable.'],
  ['task-rules', 'Verify tasks.md compliance + completeness vs specs. Every task `- [ ] N.M` checkbox in ## group; ordered by dependency; atomic; covers ALL spec scenarios (incl. the compute two-dispatch + dual-rasterization + IMGUI regression + D3D12 non-regression + content-reverify 3.4); the LAST section (## 7) is a Review & Adversarial Verify task; build verification uses python build.py (not ninja/cmake); IMGUI capture uses the correct PNG path. Flag missing/rule-violating tasks.'],
  ['binding-fix-soundness', 'Verify the binding-instance fix is correct + sufficient for BOTH surfaces. Read VulkanGraphExecutor.cpp (200-215, 1085-1096, 1119-1150, 2972-2985) and .h (205-220, 315-325). Confirm: (a) content-keying VulkanShaderResourceSet (hash includes each structPtr + operator== content-based) dedups correctly and splits main vs secondary and dispatch vs dispatch; (b) m_ShaderResourceInstances Reset() truly bounds per-frame growth; (c) changing the key type does not break the 6 iteration sites (482/575/593/623/662/831) and the shader-struct identity is stable within a frame (IMGUIContext.cpp:813). Flag any gap: e.g. does the compute path truly carry distinct per-dispatch pointers, and does the fix cover it?'],
  ['d3d12-honesty-mcp', 'Part A (read code): confirm D3D12 binding instance is content-keyed (GPUResourceBindingInstance.h:27-33) and GPUPipelineStateKey equality is hash-only (GPUPipelineInstance.h:68-104 + Hasher.h:258-261) — the change must describe BOTH accurately and NOT claim D3D12 pipeline-state is a full content key. Part B (MCP): USE MCP INTERNET TOOLS ONLY (mcp__web-reader__webReader / mcp__web-search-prime__web_search_prime; NOT WebFetch/WebSearch) to validate: (i) a VkPipelineCache stores pipelines keyed by pipeline-creation inputs (not a host-side hash) so two pipelines with different rasterization state can coexist in one cache; (ii) with VK_EXT_graphics_pipeline_library the pre-raster library carries rasterization state so differing cullMode/frontFace need distinct libs/pipelines (making the missing-rasterization-in-key a real bug and the added-fields fix correct). Cite real doc URLs + note verification. Flag any overclaim.'],
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
  'The final change (dedup identity = content key incl per-pass struct pointer + pipeline-cache key adds rasterization + content-reverify on hash hit) is internally consistent across proposal/design/specs/tasks.',
  'The binding-instance content-key fix correctly + sufficiently resolves the IMGUI multi-window offset for both the raster and compute surfaces, non-regressing.',
  'The pipeline-cache rasterization-key + content-reverify fix correctly resolves the same-shader-different-rasterization pipeline collapse, and D3D12 needs no change.',
  'The change honestly characterizes D3D12 (binding instance content-keyed; pipeline-state input covers rasterization but equality hash-only) with no overclaim.',
]
const refute = await parallel(CLAIMS.map((c, ci) => () =>
  agent('Adversarially REFUTE this claim; default to REFUTED if it does not hold from the code/artifacts. ' + COMMON + '\n\nCLAIM: ' + c, { label: 'refute:' + ci, phase: 'Refute', schema: REF })))

phase('Synthesize')
const synth = await agent(
  'You are the final judge for change fix-vulkan-dedup-key-identity (final committed state). Decide APPROVE (ready to apply) or REVISE with blockers/mustfix. Cross-check the reviewers + refutations. The change was already revised once (schema, Option B removal, lineWidth, D3D12-honesty, PNG path, test-count, content-reverify closure). Verify those are correctly reflected and no NEW inconsistency. Do NOT rubber-stamp.\n\n' + COMMON + '\n\nREVIEWER ISSUES:\n' + JSON.stringify(issues) + '\n\nREFUTATIONS:\n' + JSON.stringify(refute),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { issues, blockers, mustfix, refute, synth }

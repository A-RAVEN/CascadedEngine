export const meta = {
  name: 'unify-depth-clamp-verify',
  description: 'Adversarial correctness review of the OpenSpec artifacts (proposal/design/specs/tasks) for change unify-depth-clamp-switch: 4 independent reviewers with IDENTICAL criteria judge every claim (root-cause accuracy, API mapping correctness, default-false decision, D3D12 inverse mapping, Vulkan depthClamp feature requirement, test-z fix, spec testability, task completeness, no overclaim) against real code + MCP-verified API docs; per-claim majority = confirm.',
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
const CDIR = R + '/openspec/changes/unify-depth-clamp-switch'
const PROPOSAL = CDIR + '/proposal.md'
const DESIGN = CDIR + '/design.md'
const SPEC = CDIR + '/specs/pipeline-depth-clamp/spec.md'
const TASKS = CDIR + '/tasks.md'
const VKEXEC = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp'
const VKRT = R + '/VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp'
const D3DPIPE = R + '/D3D12RenderBackend/private/GPUGraph/GPUPipelineInstance.cpp'
const D3DPSTATE = R + '/D3D12RenderBackend/private/GPUObjects/PipelineStatesObject.cpp'
const CPIPELINE = R + '/Interface/RenderInterface/header/CPipelineStateObject.h'
const MAIN = R + '/Test/GPUBackendTester/Main.cpp'

const CLAIMS = [
  'C1 ROOT-CAUSE accurate: VULKAN ignores enableDepthClamp — read ' + VKEXEC + ' (~line 1823-1831) confirm depthClampEnable is NOT set (default VK_FALSE) and no mapping from rasterizationStates.enableDepthClamp; confirm device feature depthClamp is NOT enabled (grep ' + VKRT + '). D3D12 at ' + D3DPIPE + ' (~line 155-159) hardcodes DepthClipEnable=FALSE. Verify via MCP that depthClampEnable=FALSE => Vulkan CLIPS the z=0 near plane (z<0 culled).',
  'C2 API-SEMANTICS (MCP, MUST use web-reader/web-search-prime, do NOT use WebSearch/WebFetch): verify (a) VkPipelineRasterizationStateCreateInfo::depthClampEnable TRUE => clamp depth instead of clipping primitives to z planes; (b) depthClampEnable=TRUE REQUIRES the depthClamp feature (VUID); (c) D3D12 RasterizerDesc::DepthClipEnable FALSE => no z/center clipping (z<0 renders). Cite real URLs.',
  'C3 DESIGN decision default=false (clip) is faithfully reflected in proposal/design/spec/tasks, and is consistent: with default false both backends clip z<0 => the two tests (z=-0.25) are culled on BOTH => consistent. Confirm all 4 artifacts agree on default false and the "same value => same behavior" claim.',
  'C4 D3D12 INVERSE mapping is correct: design says DepthClipEnable = !enableDepthClamp. Verify against D3D semantics that (enableDepthClamp=true => clamp no-clip => DepthClipEnable=FALSE) and (false => clip => DepthClipEnable=TRUE). The inverse is required because Vulkan depthClampEnable is the OPPOSITE of D3D DepthClipEnable. Confirm no sign error.',
  'C5 Vulkan feature prerequisite correctly handled: design says enable depthClamp feature at device creation. Confirm the plan avoids VUID-VkPipelineRasterizationStateCreateInfo-depthClampEnable-00782 (depthClampEnable=VK_TRUE requires depthClamp feature). Tasks 2.1/2.2 cover feature + mapping.',
  'C6 TEST-z fix correct & sufficient: changing ConstantColor/StructuredBufferColor vertex z from {-0.25,-0.25,0} to {0.25,0.25,0.5} makes them render under default clip on BOTH backends (positive clip z is inside the volume). Confirm the new z matches other working tests (SimpleTriangle z=0.25/0.5) and that no OTHER behavior depends on the negative z.',
  'C7 SPEC quality: read ' + SPEC + ' — requirements use SHALL/MUST, each has at least one 4-hashtag (####) Scenario in WHEN/THEN form, and the scenarios are testable. Flag any requirement with <1 scenario, 3-hashtag scenario, or non-normative SHOULD-only wording.',
  'C8 TASKS quality: read ' + TASKS + ' — every task is a "- [ ] X.Y" checkbox, grouped, ordered by dependency, atomic/testable, and a build+verify section (5.1/5.2) exists that uses python build.py per CLAUDE.md (NOT build.bat) — the review-closure task (6.1) is the last item. Flag any non-checkbox or missing build-verify section.',
  'C9 NO OVERCLAIM: the artifacts must NOT claim to fix the IMGUI white-text bug (that is a separate TODO 2026-08-30 with different root cause: R8/RGBA swizzle/channel mismatch) and must NOT claim the switch changes anything beyond near-plane clip vs clamp. Confirm scope isolation.',
  'C10 ROOT-CAUSE vs TEST-DATA separation honesty: earlier there was uncertainty whether the triangle vanished due to (a) clip-space z clipping or (b) a test-data/attribute issue. The workflow that produced this change determined the CAUSE is the z-clip pipeline switch mismatch (D3D12 renders z<0, Vulkan clips). Confirm the proposal does NOT overstate or misattribute (it must not claim this was a binding/buffer bug, and the design correctly notes D3D12 behavior is the BREAKING change).',
  'C11 IMPLEMENTATION-PRUNENESS of spec/tasks: confirm the change is scoped to pipeline-state mapping + feature + test-z, and does NOT pull in unrelated refactors (no other rasterizer/depth changes beyond depthClampEnable). Flag any scope creep.',
].join('\n')

const JUDGE = [
  'You are an INDEPENDENT adversarial reviewer of an OpenSpec change proposal. Context: change unify-depth-clamp-switch fixes a Vulkan bug where TestTriangleWithConstantColor / TestTriangleWithStructuredBufferColor render NO triangle (vertices z=-0.25, near-plane-invalid) because the near-plane clip/clamp rasterization switch is inconsistent between backends (Vulkan clips z<0; D3D12 hardcodes DepthClipEnable=FALSE and renders z<0). The proposal defines a single upper-level enableDepthClamp field, default false (=standard near-plane clip), that BOTH backends honor consistently. Read the real artifacts at ' + PROPOSAL + ', ' + DESIGN + ', ' + SPEC + ', ' + TASKS + '.',
  'CRITICAL RULE 1 (verify, do not assume): for every claim, READ the real code/shaders/tasks at the cited location and the real artifact text. Do not trust the implementer\'s summary.',
  'CRITICAL RULE 2 (MCP ONLY): for API-semantics claims (C2, and the depth-clamp/depth-clip semantics in C1/C4/C5), you MUST use the MCP tools mcp__web-reader__webReader and/or mcp__web-search-prime__web_search_prime to fetch the Vulkan/D3D12 spec pages. Do NOT use WebSearch/WebFetch. If a tool is unavailable, mark that claim uncertain.',
  'CRITICAL RULE 3: for empirical/code claims you may read the files; you are NOT required to build/run.',
  'Verdict rules: verdict = pass ONLY if the claim is accurate AND backed by real artifact/URL/code you verified. verdict = issue if false/overstated/cites non-existent URL/mismatches code/spec/tasks (e.g. wrong mapping sign, missing feature prerequisite, non-testable spec, non-checkbox task, missing build-verify). verdict = uncertain if you cannot verify.',
  'For C3/C4/C5 (the technical crux), be maximally skeptical — prefer issue/uncertain on doubt (sign error, feature prerequisite, spec testability).',
].join('\n')

const angle = (n) =>
'You are reviewer #' + n + '/4. Every reviewer uses the IDENTICAL rules below; you judge independently, do not assume others\' votes.\n\n' + JUDGE + '\n\nCLAIMS TO JUDGE (each -> one vote):\n' + CLAIMS + '\n\nOutput the full list: claim; verdict pass/issue/uncertain; defect (precise, with real file:line or URL); evidence (what you verified); confidence high/medium/low.'

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
'Aggregate these verdicts. For each claim id C1-C11 output {claim, pass, issue, uncertain} = counts across reviewers, and defect = the concrete defect(s) if any reviewer said issue.\nSet mustFix = array of claim ids where issue count >= 2 (lean-skeptical: 2+ of 4 flags a defect). Claims with issue 1 = worth attention; 0 = accepted.\nThen bottomLine = blunt verdict: are the 4 artifacts (proposal/design/specs/tasks) for unify-depth-clamp-switch correct, internally consistent, and ready to implement, or do they need revision? Name each specific fix (e.g. inverse-mapping sign error, missing depthClamp feature, non-testable spec scenario, non-checkbox task, scope creep, overclaim).\n\nVERDICTS JSON:\n' + JSON.stringify(allPts),
  { label: 'aggregate', phase: 'Aggregate', schema: AGG })

return { verdicts: (agg?.verdicts || []), mustFix: (agg?.mustFix || []), bottomLine: (agg?.bottomLine || ''), rawVotes: allPts.length }

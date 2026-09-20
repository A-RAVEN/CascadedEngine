export const meta = {
  name: 'imgui-multiwin-hazards-sweep',
  description: 'Exhaustively sweep VulkanRenderBackendNew + D3D12RenderBackend (and shared Interface/GPUGraph) for ALL "dedup by insufficient key" hazards that mirror the confirmed IMGUI multi-viewport bug (a binding/pipeline/cbuffer dedup hash or equality that ignores a per-pass identity field such as the shader-struct pointer / constant value, collapsing distinct entities onto the FIRST one). For each candidate: same root-cause family vs distinct; severity; file:line. Adversarial verify before asserting.',
  phases: [
    { title: 'Sweep' },
    { title: 'Cluster' },
    { title: 'Verify' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'

const GROUND = [
  'REPO: ' + R,
  'CONFIRMED BUG (the template to match): VulkanShaderResourceSet::Init (VulkanGraphExecutor.cpp:205-211) computes a dedup hash from ONLY shader path hash + each binding NAME hash, IGNORING the per-viewport VulkanShaderStruct* pointer in resourceDic ([name, structPtr]) even though it is destructured and available. operator== is hash-only (VulkanGraphExecutor.h:214-215). createOrGetBindingInstance (VulkanGraphExecutor.cpp:1085-1096) does m_ShaderResourceInstances.find(resourceSet.hash) and returns the FIRST instance on a hit. So all IMGUI viewport passes (same shader path Shaders/Imgui + same binding names imguiCommon/imguiTextureBinding) collapse to the MAIN viewport instance -> every secondary pass binds MAIN constant buffer -> shader subtracts main_pos from secondary vertices -> content shifts by (secondary_pos - main_pos). D3D12 is correct because ShaderResourceSet uses operator<=> = default comparing per-viewport struct pointers (GPUResourceBindingInstance.h:27-33) so viewports get distinct instances.',
  'THE FAMILY TO FIND: any place that (a) computes a cache/dedup key (hash/map-key/comparison) that EXCLUDES a field which actually differentiates two otherwise-identical-looking passes/entities, then (b) resolves that key to the FIRST-created or a single shared object, causing the wrong-entity(cbuffer/UAV/descriptor/pipeline/root-signature/vertex-buffer/scissor) to be bound. Especially: per-pass struct pointers, per-viewport/entity IDs, per-batch uniforms/constants, mutable state.',
  'ALSO IN SCOPE — the COMPUTE path flagged earlier: VulkanGraphExecutor around 1144+ (and any compute/deferred collect-bindings) that might share the same Init/hash/dedup pattern. And D3D12 parity: does D3D12 have any analogous under-keyed map (pipeline/PSO/root-signature/PB/cbuffer/descriptor cache) that would collapse distinct entities the way Vulkan does?',
  'NOT in scope: the already-confirmed Ignatio font swizzle fix; the +2/Size nudge; negative-height viewport equivalence (refuted); anything not a dedup/collision-cache hazard.',
].join('\n')

const PROBE = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  hazard: { type: 'string' }, family: { type: 'string', enum: ['same-root-cause', 'distinct-hazard', 'infra-similar'] },
  severity: { type: 'string', enum: ['high', 'medium', 'low'] },
  mechanism: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
  triggeredByImGui: { type: 'boolean' },
}, required: ['hazard', 'family', 'severity', 'mechanism', 'evidence', 'confidence', 'triggeredByImGui'] } } }, required: ['findings'] }

const VERD = { type: 'object', properties: { hazard: { type: 'string' }, verdict: { type: 'string', enum: ['confirmed', 'refuted'] },
  counterMechanism: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['hazard', 'verdict', 'counterMechanism', 'evidence', 'confidence'] }

const SYNTH = { type: 'object', properties: { hazards: { type: 'array', items: { type: 'object', properties: {
  hazard: { type: 'string' }, family: { type: 'string' }, severity: { type: 'string' }, fileLine: { type: 'string' },
  fixScope: { type: 'string' }, includeInProposal: { type: 'boolean' },
}, required: ['hazard', 'family', 'severity', 'fileLine', 'fixScope', 'includeInProposal'] } },
  summary: { type: 'string' }, coverageGaps: { type: 'array', items: { type: 'string' } } }, required: ['hazards', 'summary', 'coverageGaps'] }

const COMMON = [
  'You are a dedup-cache hazard auditor for the CascadedEngine GPU backends. The confirmed IMGUI multi-viewport bug is a "binding-instance dedup key ignores the per-pass shader-struct pointer" — your job is to find EVERY other place that under-keys a dedup/cache map in a way that could collapse two distinct entities onto one. Read the REAL code: ' + R + '/VulkanRenderBackendNew/private, ' + R + '/D3D12RenderBackend/private, ' + R + '/Interface/RenderInterface, ' + R + '/IMGUIContext/IMGUIContext.cpp.',
  'GROUND TRUTH:\n' + GROUND,
  'You may Read/Grep any file, run read-only commands. Do NOT build/modify. Be rigorous: only flag a hazard you can PROVE from code; say honestly if a spot is safe. Cite concrete file:line.',
].join('\n\n')

const angle = (n, focus) => 'You are auditor #' + n + '. ' + COMMON + '\n\nYOUR SWEEP FOCUS (enumerate candidates with proof):\n' + focus

const PROBES = [
  ['vk-hashcombine-sweep', 'Sweep ALL cacore::hash_combine / custom-hash usages across CASCADE + VulkanRenderBackendNew (VulkanGraphExecutor.cpp/.h, VulkanResourceBindingInstance.cpp, RenderBackend_Vulkan.cpp, VulkanObjects/*). For EACH hash that keys a cache/map that dedups passes/bindings/pipelines/cbuffers/resources, determine whether it hashes EVERY identity-relevant field (esp. the shader-struct pointer, per-pass constant, per-viewport/entity id) or under-keys. List every dedup DECISION POINT (find(key) / get_or_create / m_*[hash]) and state whether a collision with differing per-pass state is possible. Heavy overlap with the confirmed rule is EXPECTED — list each distinct site.'],
  ['vk-compute-path', 'The confirmed bug is the RASTER collect-bindings path (VulkanGraphExecutor.cpp:1085-1121 via VulkanShaderResourceSet Init at 205-211). Check the COMPUTE / deferred path (VulkanGraphExecutor.cpp around 1144+, and any ComputeBatch / RWState / Dispatch binding-instance build) for the SAME pattern: does it use the same VulkanShaderResourceSet (thus same under-keyed hash), a separate dedup map keyed by hash-only, or a get_or_create that ignores per-pass struct/constant? Prove whether the compute path is also vulnerable (same bug family) or correctly keyed.'],
  ['vk-other-dedup', 'Beyond VulkanShaderResourceSet: sweep VulkanGraphExecutor & VulkanResourceBindingInstance & RenderBackend_Vulkan for ANY other m_*Instances / find(hash) / unordered map keyed by a computed hash or an incomplete key that returns FIRST-on-collision — e.g. descriptor set cache, pipeline/PipelineLibrary cache, sampler cache, framebuffer cache, cbuffer/alias cache, RenderPassCacheKey, VkPipelineLayout/dynamic-state dedup, GPUFrameManager reusepools, submit-batch cache. For each: does the key include all differentiating fields? Flag any that could bind the WRONG object when two passes differ only in a field the key omits.'],
  ['d3d12-parity', 'The workflow said D3D12 ShaderResourceSet is correct (operator<=> = default compares struct pointers). But AUDIT THE WHOLE D3D12 BACKEND for analogous under-keyed dedup: D3D12RenderBackend/private/GPUGraph (GPUGraphExecutor.cpp, GPUResourceBindingInstance.h/.cpp, GPUPipelineInstance.cpp, PipelineStatesObject.cpp, RenderPassCacheKey), GPUObjects/* (CommandListManager, FrameBoundResourceManager), rendering pipeline/root-signature/PSO/PB caches, descriptor/cbuffer caches. For each map keyed by hash/incomplete key that resolves to first-or-shared: state whether it can collapse two distinct entities and bind the wrong one. Report confirmed + safe.'],
  ['shared-graph-interface', 'The GPU graph is shared (Interface). AUDIT the shared graph / executor layer + renderable/bindable-key builders used by BOTH backends for under-keying that manifests in a backend (e.g. RenderPassCacheKey, pipeline-state combine, shader-binding struct building, GPUTexture/GPUBuffer dedup, ShaderStruct/push-constant packing). Also sweep IMGUIContext per-viewport state (m_ShaderStruct/m_VertexBuffer/m_IndexBuffer/m_Sissors/m_TextureBindings) for any cross-viewport caching/sharing/reuse that could hand one viewport another viewport geometry — i.e. a non-backend hazard in the same family.'],
]

phase('Sweep')
const res = await parallel(PROBES.map(([k, focus], i) => () =>
  agent(angle(i + 1, focus), { label: 'sweep:' + k, phase: 'Sweep', schema: PROBE })))

const all = res.filter(Boolean).flatMap(r => r.findings || [])
log('Sweep candidates: ' + all.length)

// Cluster by hazard identity (dedup on hazard text) so verify doesn't double-count the same site
const seen = {}; const deduped = []
for (const f of all) { const k = f.hazard.trim(); if (!seen[k]) { seen[k] = true; deduped.push(f) } }
log('Clustered: ' + deduped.length)
phase('Cluster')

phase('Verify')
function hashCode(s){let h=0;for(let i=0;i<s.length;i++)h=(h*31+s.charCodeAt(i))|0;return h}
const verified = await parallel(deduped.map((f, vi) => () =>
  agent('Adversarially REFUTE this dedup-cache hazard; default to REFUTED if it does not hold from code (i.e. the key DOES include the differentiating field, or the find/collision cannot produce a wrong binding). Prove or refute.\n\n' + COMMON + '\n\nHAZARD: ' + f.hazard + '\nFAMILY: ' + f.family + '\nMECHANISM: ' + f.mechanism + '\nEVIDENCE: ' + f.evidence,
    { label: 'verify:' + Math.abs(hashCode(f.hazard)) + ':' + vi, phase: 'Verify', schema: VERD })));

phase('Synthesize')
const confirmed = verified.filter(Boolean).filter(v => v.verdict === 'confirmed')
log('Confirmed after adversarial verify: ' + confirmed.length + '/' + deduped.length)

const synth = await agent(
  'You are the synthesis judge for the dedup-hazard audit. Given the confirmed IMGUI bug and all candidates + adversarial verdicts, produce the final hazard list to scope a fix proposal. Classify each hazard: same-root-cause-as-IMGUI (definitely fix now), distinct-hazard (fix now if it is a real wrong-binding risk), or low/infra (note, maybe defer). For each give a one-line fix scope (what field to add to the key / what equality to change / which map keying to replace) and whether it should go in the proposal. The confirmed IMGUI bug is definitely in the proposal.\n\n' + COMMON + '\n\nCANDIDATES:\n' + JSON.stringify(deduped) + '\n\nADVERSARIAL VERDICTS:\n' + JSON.stringify(verified),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { total: all.length, clustered: deduped.length, confirmed: confirmed.length, synth, all }

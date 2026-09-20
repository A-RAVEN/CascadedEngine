export const meta = {
  name: 'imgui-multiwin-offset-rootcause',
  description: 'Narrow the Vulkan-only secondary-viewport IMGUI offset to the exact backend code. Empirical fact established: ImGui vertex data + shader args are identical and CORRECT across backends (vtx0 = meshScalePos.zw + titlebar; displayPos == meshScalePos.zw == viewport pos), main viewport correct on both, secondary viewport offset by (secondary_pos - main_pos) on Vulkan, correct on D3D12. So a per-viewport draw-time binding / viewport-mapping adds (secondary_pos - main_pos). Probes pinpoint it; adversarial verify; synthesise.',
  phases: [
    { title: 'Probe' },
    { title: 'Verify' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'

const GROUND = [
  'REPO: ' + R,
  'SYMPTOM: IMGUI multi-viewport — MAIN window renders correctly on BOTH backends (byte-identical PNG, content bbox (1,1,1023,511)). SECONDARY (floating) windows: on D3D12 content bbox (1,1,...) correct; on VULKAN the ENTIRE window content is shifted down-right to (61,61) = (secondary_pos - main_pos) + ~1px (another layout showed (349,341) = same formula). Every secondary viewport shifts by (its POS - main POS); main shifts by 0.',
  'MEASURED (instrumented PrepareSingleViewGUIResources, identical across backends): imguipos=viewport->Pos; displayPos==meshScalePos.zw==imguipos; first vertex vtx0 = (imguipos.x, imguipos.y + 19) (title bar ~19px). E.g. a run: main imguipos=(329,358) vtx0=(329,358); secondary imguipos=(389,418) vtx0=(389,437). So the ImGui VERTEX position, the shader DISPLAY pos (IMGUIScale_Pos.zw) and the per-viewport pos are ALL internally consistent and CORRECT — the engine hands the shader the right constants and right geometry.',
  'STRONG ROOT DIRECTION (narrowed, needs the exact line): the shader transform is Pos=(inVert - IMGUIScale_Pos.zw)*(2/Size) - 1 (+ 2/Size) and the backend maps NDC->framebuffer. For a secondary viewport the vertex subtraction uses the CORRECT constant (secondary pos), so the ONLY way the rendered result equals "subtract MAIN pos" is that the actual constant bound AT DRAW TIME, or the pass viewport/renderArea, injects (secondary_pos - main_pos). Decidedly NOT IMGUI data (proved identical+correct). Look in VulkanRenderBackendNew for the draw-time per-viewport constant/framebuffer/viewport binding that could leak the main window\'s position.',
  'DO NOT re-litigate: (a) negative-height viewport equivalence (round-1/2 refuted as equivalent to D3D12 positive-height for an origin-targeting pass); (b) +2/Size nudge (~1px cross-backend, already known; NOT the 60px); (c) font swizzle fix (unrelated); (d) shared framebuffer (each secondary renders+acquires its own swapchain).',
].join('\n')

const PROBE = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  hypothesis: { type: 'string' }, verdict: { type: 'string', enum: ['confirmed', 'refuted', 'uncertain'] },
  mechanism: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['hypothesis', 'verdict', 'mechanism', 'evidence', 'confidence'] } } }, required: ['findings'] }

const VERD = { type: 'object', properties: { hypothesis: { type: 'string' }, verdict: { type: 'string', enum: ['confirmed', 'refuted'] },
  counterMechanism: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['hypothesis', 'verdict', 'counterMechanism', 'evidence', 'confidence'] }

const SYNTH = { type: 'object', properties: { rootCause: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
  mechanism: { type: 'string' }, evidenceFileLines: { type: 'array', items: { type: 'string' } },
  fixDirection: { type: 'string' }, coverageGaps: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' },
}, required: ['rootCause', 'confidence', 'mechanism', 'evidenceFileLines', 'fixDirection', 'coverageGaps', 'bottomLine'] }

const COMMON = [
  'You are a backend root-cause investigator for the Vulkan-only secondary-viewport IMGUI offset. The goal: find the EXACT line in VulkanRenderBackendNew (or the shared graph layer it uses) that, AT DRAW TIME, makes a secondary viewport\'s IMGUI content shift by (secondary_pos - main_pos) — while D3D12 is correct. Read the REAL code. Engine dirs: ' + R + '/VulkanRenderBackendNew/private, ' + R + '/D3D12RenderBackend/private, ' + R + '/IMGUIContext/IMGUIContext.cpp, ' + R + '/CAResources/Shaders/Imgui.slang, and the ImGui reference ' + R + '/out/build/x64-debug/_deps/imgui-src/backends/imgui_impl_vulkan.cpp.',
  'GROUND TRUTH:\n' + GROUND,
  'You may Read/Grep any file and run read-only commands. Do NOT build or modify. Be maximally rigorous: if you cannot PROVE the line from code, say so and rate low/uncertain rather than guessing. Cite concrete file:line.',
].join('\n\n')

const angle = (n, focus) => 'You are probe #' + n + ', an independent investigator. ' + COMMON + '\n\nYOUR FOCUS (resolve THIS question with hard code evidence):\n' + focus

const PROBES = [
  ['constant-binding', 'LEADING HYPOTHESIS. The per-viewport ImGui constant uniform "IMG Common / IMGUIScale_Pos" is created per viewport in PrepareSingleViewGUIResources (m_ShaderStruct -> SetValue IMGUIScale_Pos = viewport pos) and bound per pass in DrawSingleView via .SetParam(imguiCommon, pUserData->m_ShaderStruct). Trace how that ShaderStruct constant is actually uploaded + bound in the VULKAN render path: VulkanShaderStruct / ShaderStruct impl, VulkanResourceBindingInstance, the per-pass descriptor set / constant-buffer pool. Determine: could the SECONDARY viewport pass be bound with the MAIN viewport\'s constant buffer (same pool slot reused, deferred/async upload ordering, shared descriptor set, upload-order last-writer-wins, or a constant buffer keyed by name not by viewport) such that the bound IMGUIScale_Pos = main_pos? If yes -> culprit identified -> verdict confirmed. If the per-viewport constant is provably bound correctly -> verdict refuted (then the offset is in the pass viewport/renderArea).'],
  ['pass-viewport-renderarea', 'Determine the actual vk::Viewport + render area used by a SECONDARY-viewport IMGUI render pass in VulkanGraphExecutor (see the pass record ~2209-2215 default viewport from firstAttachmentDesc, and 2367-2378 dynamic viewport/scissor). (a) Is the pass viewport purely {0,H,W,-H} from the target image descriptor, or can it be affected by the window position / a per-pass offset? (b) When a pass targets a per-viewport image (secondary backbuffer or the per-viewport capture RT), is there ANY path that sets a non-zero viewport offset (offsetX/offsetY) or a renderArea whose offset encodes a window position? Compare to how the MAIN viewport pass (correct) differs. Confirm/refute whether the pass viewport/renderArea adds (secondary_pos - main_pos).'],
  ['vertex-scissor-binding', 'Confirm each SECONDARY viewport draws its OWN per-viewport CPU geometry: pUserData->m_VertexBuffer / m_IndexBuffer / m_Sissors / m_TextureBindings are per-viewport (allocated+populated in PrepareSingleViewGUIResources, keyed by IMGUI Buffer Handle with unique=true, scissor computed as ClipRect - viewport->Pos). Trace the UPLOAD + BINDING of those per-viewport buffers into the GPU in the Vulkan path: is there any sharing/reuse/pooling that could bind the MAIN viewport\'s vertex buffer/scissor to a secondary viewport\'s draw? If a secondary draw accidentally uses main\'s geometry (whose vertices encode main\'s global pos), the result would shift by (secondary_pos - main_pos). Confirm/refute.'],
  ['d3d12-diff', 'Isolate the DIVERGENCE: read how the D3D12 backend records + executes the SAME per-viewport ImGui passes (IMGUIContext is backend-agnostic; the passes are identical). Look at D3D12RenderBackend/private/GPUGraph/GPUGraphExecutor.cpp pass-level viewport (~1050-1054) + RSSetViewports/RSSetScissorRects (~1846-1847) and how it binds the per-pass constant/root signature. Identify the CONCRETE mechanical difference that makes D3D12 correct where Vulkan shifts secondary viewports by (secondary_pos - main_pos). This pinpoints the culprit value/slot name to check in the Vulkan path.'],
]

phase('Probe')
const res = await parallel(PROBES.map(([k, focus], i) => () =>
  agent(angle(i + 1, focus), { label: 'probe:' + k, phase: 'Probe', schema: PROBE })))

const confirmed = res.filter(Boolean).flatMap(r => r.findings || []).filter(f => f.verdict === 'confirmed')
const uncertain = res.filter(Boolean).flatMap(r => r.findings || []).filter(f => f.verdict === 'uncertain')
log('Probes: ' + confirmed.length + ' confirmed, ' + uncertain.length + ' uncertain')

// dedup by hypothesis head (mostly a single leading hypothesis expected)
const seen = {}; const deduped = []
for (const f of confirmed) { const k = f.hypothesis.split(':')[0].trim(); if (!seen[k]) { seen[k] = true; deduped.push(f) } }

phase('Verify')
const verified = await parallel(deduped.map(f => () =>
  agent('You are an ADVERSARIAL REFUTATION reviewer. Try to REFUTE this root-cause hypothesis for the Vulkan secondary-viewport IMGUI offset; default to REFUTED if it does not hold up from code. Prove it or mark refuted.\n\n' + COMMON + '\n\nHYPOTHESIS: ' + f.hypothesis + '\nMECHANISM: ' + f.mechanism + '\nEVIDENCE: ' + f.evidence,
    { label: 'verify:' + f.hypothesis.split(':')[0].trim(), phase: 'Verify', schema: VERD })))
const passed = verified.filter(Boolean).filter(v => v.verdict === 'confirmed')
log('Survived adversarial verification: ' + passed.length + '/' + deduped.length)

phase('Synthesize')
const synth = await agent(
  'You are the synthesis judge. Given the empirical fact (secondary viewport shifts by (secondary_pos - main_pos) on Vulkan, correct on D3D12; ImGui data + shader args proven identical+correct), name the EXACT root cause and fix direction. For each probe report confirmed/refuted/uncertain + evidence. If the leading hypothesis (constant binding leaks main pos) is confirmed, give the precise file:line + the fix. If it is refuted, give the next most likely. Cite real file:line; note coverage gaps.\n\n' + COMMON + '\n\nPROBE FINDINGS:\n' + JSON.stringify(res) + '\n\nADVERSARIAL VERDICTS:\n' + JSON.stringify(verified),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { confirmed, uncertain, verified, synth, rawFinds: res.length }

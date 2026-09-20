export const meta = {
  name: 'imgui-multiwin-offset-investigate',
  description: 'Investigate why IMGUI multi-viewport secondary windows render OFFSET on the Vulkan backend but not D3D12. 6 independent probes test distinct hypotheses against the REAL code: (1) IMGUI ImDrawData vertex/ClipRect coordinate space (global screen vs viewport-local) deciding whether subtracting viewPort->Pos is right; (2) whether Vulkan RecordRenderPass GetDescriptor(attachments[0]) resolves per-window swapchain size or a global/main; (3) Vulkan negative-height default viewport vs D3D12 positive pass-level viewport NDC->pixel mapping; (4) net y-orientation no double-flip; (5) scissor ClipRect-Pos overflow/underflow for non-zero Pos; (6) whether each secondary viewport truly renders+present to its OWN window swapchain. Then adversarial verification of the leading root cause + synthesis.',
  phases: [
    { title: 'Probe' },
    { title: 'Verify' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'

const GROUND = [
  'REPO: ' + R,
  'SYMPTOM: IMGUI multi-viewport — SECONDARY platform windows render their GUI content OFFSET/misaligned relative to their window on the VULKAN backend; main window is fine; D3D12 backend is fine for all windows.',
  'SHARED (backend-agnostic) logic in ' + R + '/IMGUIContext/IMGUIContext.cpp: (a) PrepareSingleViewGUIResources :723 computes meshScale_Pos = glm::vec4(2/viewPort->Size.x, 2/viewPort->Size.y, viewPort->Pos.x, viewPort->Pos.y) and :749 SetValue("IMGUIScale_Pos", meshScale_Pos). (b) :781 m_Sissors.push_back(glm::ivec4(pcmd->ClipRect.x - viewPort->Pos.x, pcmd->ClipRect.y - viewPort->Pos.y, ClipRect.z-ClipRect.x, ClipRect.w-ClipRect.y)) — scissor = ClipRect minus viewPort->Pos. (c) DrawSingleView :851-862 emits a per-drawcall Scissor(sc) but NEVER sets a viewport. (d) :871 headless capture only targets the MAIN viewport (IsMainViewport gate), so the multi-window offset is NOT visible in headless TestIMGUI_*.png — only in live interactive runs.',
  'SHADER ' + R + '/CAResources/Shaders/Imgui.slang: :12-15 TransformIMGUIVertex(inVert) = (inVert - IMGUIScale_Pos.zw) * IMGUIScale_Pos.xy - 1.0; :43 output.Pos = float4(TransformIMGUIVertex(input.Pos) + IMGUIScale_Pos.xy, 0, 1); :44 output.Pos.y = -output.Pos.y;. So IMGUIScale_Pos.zw=viewPort->Pos (offset), .xy=2/Size (scale); final NDC = (inVert-Pos)*scale - 1 + scale, then y flipped.',
  'VULKAN: ' + R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanGraphExecutor.cpp RecordRenderPass :2209 firstAttachmentDesc = GetDescriptor(graph, attachments[0]); :2211 vk::Viewport viewport{0.0f, (float)firstAttachmentDesc.height, (float)firstAttachmentDesc.width, -(float)firstAttachmentDesc.height, 0.0f, 1.0f}; (negative-height top-left); :2213 vk::Rect2D scissor{{0,0},{firstAttachmentDesc.width, firstAttachmentDesc.height}}; :2214 setViewport; :2215 setScissor. Dynamic-state path :2367-2378: if drawcall.GetViewPort() valid -> dynViewport{x, y+height, width, -height}; if drawcall.GetScissor() valid -> dynScissor{{sc->x,sc->y},{sc->width,sc->height}}. Pipeline marks eViewport+eScissor dynamic (:1818).',
  'D3D12: ' + R + '/D3D12RenderBackend/private/GPUGraph/GPUGraphExecutor.cpp :1050 passLevelViewport = ViewRectData{...} then :1087 drawcallLevelViewport = SelectIfValid(drawcall.GetViewPort(), batchLevel/passLevel); :1090 ViewRectDataToD3D12Viewport(...); :1846 RSSetViewports(1,&defaultViewport); :1847 RSSetScissorRects(1,&defaultSissors). ViewRectDataToD3D12Viewport at InterfaceTranslation.h:787 (D3D12_VIEWPORT {TopLeftX,TopLeftY,width,height}, y=0 top, positive height).',
  'KNOWN UNRELATED: the just-fixed font white-box change touched ONLY Vulkan image-view swizzle (VulkanTexture GetOrCreateSampledImageView + GetTextureView overload + sampled-binding view). It does NOT touch viewport/scissor/swapchain — so the multi-window OFFSET is a DIFFERENT issue. Confirm this in your analysis.',
].join('\n')

const PROBE = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  hypothesis: { type: 'string' }, verdict: { type: 'string', enum: ['confirmed', 'refuted', 'uncertain'] },
  mechanism: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['hypothesis', 'verdict', 'mechanism', 'evidence', 'confidence'] } } }, required: ['findings'] }

const VERD = { type: 'object', properties: { hypothesis: { type: 'string' }, verdict: { type: 'string', enum: ['confirmed', 'refuted'] },
  counterMechanism: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['hypothesis', 'verdict', 'counterMechanism', 'evidence', 'confidence'] }

const SYNTH = { type: 'object', properties: { rootCause: { type: 'string' }, verdict: { type: 'string', enum: ['vulkan-loc-alignment', 'vulkan-swapchain-size', 'viewport-scissor-mismatch', 'dpi-scale', 'other', 'uncertain'] },
  mechanism: { type: 'string' }, evidenceFileLines: { type: 'array', items: { type: 'string' } },
  fixDirection: { type: 'string' }, relatedToFontFix: { type: 'string', enum: ['unrelated', 'possibly-related', 'caused-by-font-fix'] },
  isRelated: { type: 'string' }, coverageGaps: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' },
}, required: ['rootCause', 'verdict', 'mechanism', 'evidenceFileLines', 'fixDirection', 'relatedToFontFix', 'isRelated', 'coverageGaps', 'bottomLine'] }

const COMMON = [
  'You are investigating why IMGUI multi-viewport SECONDARY windows render OFFSET on the Vulkan backend but not D3D12. Read the REAL code and the IMGUI vendored source (in ' + R + '/out/build/x64-debug/_deps/imgui-src/) to resolve which hypothesis is true.',
  'GROUND TRUTH (established, verify / correct if genuinely wrong):\n' + GROUND,
  'You may Read/Grep any file (including imgui-draw.cpp/imgui.cpp) and run read-only commands. Do NOT build or modify anything.',
  'Be maximally skeptical; cite concrete file:line for every conclusion. Note whether the finding is causally related to the just-fixed font swizzle change or is a separate bug.',
].join('\n\n')

const angle = (n, focus) => 'You are probe #' + n + '/6, an independent investigator. ' + COMMON + '\n\nYOUR FOCUS (resolve THIS hypothesis to confirmed/refuted/uncertain with hard evidence):\n' + focus

const PROBES = [
  ['vtxspace', 'HYPOTHESIS (A): the offset is because the engine subtracts viewPort->Pos (in BOTH the shader meshScale_Pos and the scissor) but IMGUI multi-viewport DrawData vertex positions + ClipRect are NOT in global/screen coordinates — they are viewport-LOCAL (0,0 = that viewport window top-left). If IMGUI gives local coords, subtracting a non-zero viewPort->Pos OVER-shifts content by the screen position, exactly producing "secondary-window offset" (main has Pos=0 so unaffected; D3D12 would be affected too UNLESS D3D12 path gets Pos=0 for all viewports). TASK: read the vendored imgui source (imgui.cpp / imgui_draw.cpp) for how per-viewport ImDrawData is built: what are DrawData->DisplayPos/DisplaySize and in what coordinate space are vertex positions and ClipRect for a SECONDARY viewport? Quote the lines. Decide: is the engine\'s use of viewPort->Pos correct, or an over-shift?'],
  ['descriptor', 'HYPOTHESIS (B): Vulkan RecordRenderPass :2209 GetDescriptor(graph, attachments[0]) does NOT resolve to the per-window swapchain descriptor for a secondary window — it returns a global/main-window (or stale) descriptor, so the default viewport/scissor at :2211-2213 uses the MAIN window size for secondary windows, distorting/offsetting NDC->pixels. TASK: trace GetDescriptor(graph, ImageHandle) in the Vulkan backend (and GetRenderPassGPUData / how attachments[0] maps to a texture descriptor for an External/Backbuffer window handle). Compare with D3D12\'s :1050 pass-level ViewRectData derivation. Decide whether the Vulkan viewport size is correct per window or wrong for secondary windows.'],
  ['viewportderive', 'HYPOTHESIS (C): the Vulkan default viewport (:2211, negative-height {y=height,height=-height}) and the D3D12 default viewport (ViewRectDataToD3D12Viewport positive-height y=0) differ in a way that mis-positions content on Vulkan. TASK: trace BOTH default viewports and confirm they map NDC (from the shared Imgui shader) to the framebuffer consistently for a per-window render target. Compute the framebuffer pixel of an IMGUI vertex at (0,0) on both. Flag an inconsistency (e.g. Vulkan needs negative height but ALSO the shader flips y -> double flip; or the scissor y-origin differs).'],
  ['yflip', 'HYPOTHESIS (D): the Vulkan negative-height viewport + the shader y-flip (Imgui.slang:44) net to a DOUBLE y-flip for secondary windows (or a mismatch that vertical-offset or mirrors content), while D3D12 positive-height + same shader is correct. TASK: rigorously determine the net vertical orientation on Vulkan vs D3D12 using the same shader. If both net-correct, refute (D). Pay attention to whether the flip interacts with the non-zero Pos for secondary.'],
  ['scissor', 'HYPOTHESIS (E): the scissor m_Sissors = ClipRect - viewPort->Pos can under/overflow for a secondary window (non-zero Pos): if ClipRect is global and correctly subtracted it is fine; if local it goes negative -> Vulkan setScissor with negative x/y clips content off / offset. TASK: cross-check the scissor path: how DrawSingleView\'s scissor reaches vkCmdSetScissor (VulkanGraphExecutor :2374-2378 dynScissor) and whether the Vulkan scissor reference (top-origin framebuffer pixels) matches the shader\'s subtracted-Pos convention. Compare D3D12\'s RSSetScissorRects. Flag a mismatch.'],
  ['perwinpresent', 'HYPOTHESIS (F): each secondary viewport does NOT render+present to its OWN window swapchain on Vulkan — it renders into a shared/main framebuffer (so content appears at the wrong place in the secondary window) — whereas D3D12 correctly targets each window backbuffer. TASK: verify in VulkanGraphExecutor/RenderBackend_Vulkan that a raster pass targeting a secondary window\'s ImageHandle resolves to that window\'s swapchain (vkQueuePresent per window backbuffer), matching D3D12 per-window behavior. Flag if secondary windows all drain to the main backbuffer or share one framebuffer.'],
]

phase('Probe')
const res = await parallel(PROBES.map(([k, focus], i) => () =>
  agent(angle(i + 1, focus), { label: 'probe:' + k, phase: 'Probe', schema: PROBE })))

const confirmed = res.filter(Boolean).flatMap(r => r.findings || []).filter(f => f.verdict === 'confirmed')
const uncertain = res.filter(Boolean).flatMap(r => r.findings || []).filter(f => f.verdict === 'uncertain')
log('Probes: ' + confirmed.length + ' confirmed, ' + uncertain.length + ' uncertain')

const seen = {}; const deduped = []
for (const f of confirmed) { const k = f.hypothesis.split(':')[0].trim(); if (!seen[k]) { seen[k] = true; deduped.push(f) } }

phase('Verify')
const verified = await parallel(deduped.map(f => () =>
  agent('You are an ADVERSARIAL REFUTATION reviewer. Try to REFUTE this confirmed hypothesis for the IMGUI Vulkan multi-window offset. Read the REAL code it cites; default to REFUTED if it does not hold up. Confirmed only if you cannot find a concrete counter-vul mechanism.\n\n' + COMMON + '\n\nHYPOTHESIS: ' + f.hypothesis + '\nMECHANISM: ' + f.mechanism + '\nEVIDENCE: ' + f.evidence,
    { label: 'verify:' + f.hypothesis.split(':')[0].trim(), phase: 'Verify', schema: VERD })))
const passed = verified.filter(Boolean).filter(v => v.verdict === 'confirmed')
log('Survived adversarial verification: ' + passed.length + '/' + deduped.length)

phase('Synthesize')
const synth = await agent(
  'You are the synthesis judge. Determine the DEFINITIVE root cause of the IMGUI multi-viewport secondary-window offset on Vulkan (D3D12 fine; main window fine). (1) What is the root cause — is it that IMGUI draw data is viewport-local so subtracting viewPort->Pos is an over-shift, OR a Vulkan viewport/scissor per-window resolution bug, OR a swapchain/DPI mismatch? (2) Is it RELATED to or CAUSED BY the just-fixed font swizzle change (which only touched image-view swizzle), or a separate/independent bug? (3) What is the fix direction? Cite real file:line. Note coverage gaps.\n\n' + COMMON + '\n\nPROBE FINDINGS:\n' + JSON.stringify(res) + '\n\nADVERSARIAL VERDICTS:\n' + JSON.stringify(verified),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { confirmed, uncertain, verified, synth, rawFinds: res.length }

export const meta = {
  name: 'imgui-multiwin-offset-round2',
  description: 'Round-2 static-code investigation to RAISE/LOWER confidence on the leading hypothesis for the IMGUI Vulkan multi-window secondary offset (logical-vs-physical size split). 6 probes determine from CODE alone (no runtime DPI needed): (1) the DECISIVE linchpin — does the Vulkan swapchain extent derivation provably read a DIFFERENT (physical px) source than the ImGui viewPort->Size (logical pts), or are they provably identical?; (2) DPI-awareness state (SetProcessDpiAwareness/Context, manifest, GLFW_SCALE_FRAMEBUFFER, glfwGetFramebufferSize vs glfwGetWindowSize); (3) swapchain-recreate timing vs ImGui viewport resize (one-frame-out-of-sync provable?); (4) scissor clamp gap reachability / VUID; (5) Imgui.slang:43 +IMGUIScale_Pos.xy ~1px nudge effect; (6) sweep the "other" bucket. Then adversarial verification + synthesis to HIGH/refuted.',
  phases: [
    { title: 'Probe' },
    { title: 'Verify' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const IMGUI = R + '/out/build/x64-debug/_deps/imgui-src'

const GROUND = [
  'REPO: ' + R,
  'SYMPTOM: IMGUI multi-viewport — SECONDARY platform windows render their GUI content OFFSET on the VULKAN backend; main window fine; D3D12 fine. Only visible in live interactive runs (headless capture gated to main viewport, IMGUIContext.cpp:871-873).',
  'ROUND-1 DEFINITIVE NEGATIVES (do not re-litigate): NOT over-shift (-Pos subtraction is the canonical IMGUI contract, draw_data->DisplayPos==viewport->Pos for all viewports imgui.cpp:5650-5651); NOT Vulkan negative-height viewport (2211 maps identically to D3D12 positive-height); NOT shared framebuffer (each secondary window renders+acquires+presents its own swapchain); NOT the font-swiggle fix (git diff: only image-view swizzle touched).',
  'ROUND-1 LEADING HYPOTHESIS (medium confidence, NOT proven): logical-vs-physical size split. ImGui viewport rect is LOGICAL (meshScale_Pos=2/viewPort->Size IMGUIContext.cpp:723, scissor=ClipRect-Pos :781, io.DisplayFramebufferScale hardcoded 1.0 :647, viewPort->Size from glfwGetWindowSize via Window_Impl.cpp:67-69). Vulkan swapchain/framebuffer is PHYSICAL (ChooseSwapchainExtent from surface currentExtent VulkanWindowHandle.cpp:114,:268-280 -> firstAttachmentDesc -> viewport/renderArea VulkanGraphExecutor.cpp:2209-2215). Reference imgui_impl_vulkan closes the gap: fb_size=DisplaySize*FramebufferScale (:514-515), scale/translate from logical (:499-504), scissor scaled+clamped (:614-621, "vkCmdSetScissor() won\'t accept values that are off bounds"). This engine omits both: scissor passed raw (VulkanGraphExecutor.cpp:2377), transform uses logical while viewport/attachment uses physical.\n\nDECISIVE UNRESOLVED: whether the swapchain extent derivation and the ImGui viewPort->Size are provably reading DIFFERENT values (physical px vs logical pts) such that DPI>1 or per-monitor placement makes them diverge, OR are provably always identical (making the split vacuous).',
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
  'You are round-2 investigator for the IMGUI Vulkan multi-window secondary offset. The goal is to RAISE the leading hypothesis (logical-vs-physical size split) to HIGH confidence or REFUTE it, from STATIC CODE ONLY (no runtime needed). Read the REAL code (engine + vendored GLFW/GLFW header + ' + IMGUI + '/backends/imgui_impl_vulkan.cpp).',
  'GROUND TRUTH:\n' + GROUND,
  'You may Read/Grep any file and run read-only commands. Do NOT build or modify anything.',
  'Be maximally rigorous and honest: if a hypothesis cannot be PROVEN from code (needs runtime DPI), say so explicitly and rate low/uncertain rather than guessing. Cite concrete file:line.',
].join('\n\n')

const angle = (n, focus) => 'You are probe #' + n + '/6, an independent investigator. ' + COMMON + '\n\nYOUR FOCUS (resolve THIS question with hard code evidence):\n' + focus

const PROBES = [
  ['extent-source', 'DECISIVE LINCHPIN. Determine whether the Vulkan swapchain framebuffer extent and the ImGui viewPort->Size provably READ DIFFERENT SOURCES such that they can diverge under DPI/per-monitor, OR are provably always identical. Trace BOTH end-to-end and report the EXACT glfw/win32 value each reads: (a) VulkanWindowHandle.cpp ChooseSwapchainExtent (:114,:268-280) — does it use surface currentExtent, glfwGetFramebufferSize (PHYSICAL px), or glfwGetWindowSize (LOGICAL pt)? And GetSizeSafe (:65-74). (b) ImGui viewPort->Size source: Window_Impl.cpp GetWindowSize -> glfwGetWindowSize (logical pt)? And IMGUIContext GetWindowSize (:542-566). (c) Does the codebase call glfwGetFramebufferSize ANYWHERE for the swapchain (physical) vs glfwGetWindowSize for ImGui (logical)? If (a) uses physical framebuffer px and (b) uses logical window pt, that is a PROVEN divergence source -> verdict confirmed. If both call glfwGetWindowSize (logical) -> they are coupled -> the DPI split is VACUOUS -> verdict refuted (hypothesis needs DPI to even be possible).'],
  ['dpi-aware', 'Determine the DPI-awareness / scaling state. (a) Search the whole repo for SetProcessDpiAwareness, SetProcessDpiAwarenessContext, SetThreadDpiAwarenessContext, EnableNonClientDpiScaling, manifest dpiAware, GLFW_SCALE_FRAMEBUFFER, GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_SCALE_TO_MONITOR, or any DPI/WM_DPICHANGED handling. (b) Look at how windows are created (Window_Impl.cpp / pWindowSystem NewWindow) and whether GLFW is set to any scale mode. Decide: is the app DPI-aware or not, and does GLFW report framebuffer px != window px under that state? This determines whether the physical/logical split is even COUPLED (possible) or the OS bitmap-stretches (vacuous for the offset).'],
  ['recreate-timing', 'Determine the swapchain-recreate vs ImGui viewport resize ordering. Trace: when ImGui resizes/moves a secondary viewport, ImGui_Impl_SetWindowSize/SetWindowPos (IMGUIContext.cpp:568-572/:551-557) -> Window_Impl SetSize/SetPos -> GLFW; and when is VulkanWindowHandle RecreateSwapchain (:375) invoked (per present/frame? per size callback?). Decide from code whether meshScale_Pos (from viewPort->Size at IMGUIContext.cpp:723) and the swapchain extent (from m_Extent) can run a FRAME OUT OF SYNC (provable transient/permanent offset) from the event ordering alone. Rate confirmed/refuted/uncertain.'],
  ['scissor', 'Determine if the scissor gap is real and is the offset. VulkanGraphExecutor.cpp:2374-2379 passes drawcall.GetScissor() (from IMGUIContext m_Sissors=ClipRect-Pos, unscaled+unclamped, IMGUIContext.cpp:781) RAW to vkCmdSetScissor. (a) For a secondary window, can ClipRect-Pos go negative or beyond the framebuffer (out-of-bounds scissor)? (b) Is out-of-bounds scissor a Vulkan VUID violation (will validation error)? (c) Is the reported offset a SCISSOR-CLIPPING/position artifact (content drawn at wrong rect) vs a VERTEX-positioning artifact? Compare with the reference clamp at ' + IMGUI + '/backends/imgui_impl_vulkan.cpp:614-621. Rate confirmed/refuted/uncertain.'],
  ['slang-nudge', 'Quantify Imgui.slang:43 `output.Pos = TransformIMGUIVertex(input.Pos) + IMGUIScale_Pos.xy`. TransformIMGUIVertex(:12-15)=(inVert-IMGUIScale_Pos.zw)*IMGUIScale_Pos.xy - 1.0. So final.x = (inVert-Pos.x)*(2/Size.x) - 1 + (2/Size.x). Determine the NET effect on the framebuffer pixel of an IMGUI vertex — is it a uniform ~1px shift on EVERY window (both backends and main) introduced by the extra +IMGUIScale_Pos.xy, or does it cancel? The reported offset is a LARGE misalignment on secondary windows only; a uniform ~1px nudge is smaller and cross-window. Decide whether this is the reported offset or a separate minor defect. Rate confirmed/refuted/uncertain.'],
  ['other', 'Explicitly sweep the "OTHER" bucket for a per-secondary-window positional cause that is NOT the DPI split. Checks: (a) viewport->Pos staleness vs actual window position (GetWindowPos source Window_Impl.cpp:59-61 glfwGetWindowPos) — is the offset actually a Pos-window mismatch?; (b) monitor work-area / viewport->WorkPos vs Pos; (c) the commented-out Platform_RenderWindow/Platform_SwapBuffers callbacks (IMGUIContext.cpp:619-620) — does the engine\'s own per-viewport present path mis-track which window a viewport targets?; (d) any place a secondary viewport\'s texture/backbuffer descriptor is derived from the MAIN window instead of its own window. Report any PROVEN per-secondary-window offset source; if none, verdict refuted (the "other" bucket is empty; the DPI split remains the leading candidate).'],
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
  agent('You are an ADVERSARIAL REFUTATION reviewer for round-2. Try to REFUTE this confirmed hypothesis for the IMGUI Vulkan multi-window offset; default to REFUTED if it does not hold up. Prove it from code (or mark refuted if it needs runtime DPI and is only "possible" not "proven").\n\n' + COMMON + '\n\nHYPOTHESIS: ' + f.hypothesis + '\nMECHANISM: ' + f.mechanism + '\nEVIDENCE: ' + f.evidence,
    { label: 'verify:' + f.hypothesis.split(':')[0].trim(), phase: 'Verify', schema: VERD })))
const passed = verified.filter(Boolean).filter(v => v.verdict === 'confirmed')
log('Survived adversarial verification: ' + passed.length + '/' + deduped.length)

phase('Synthesize')
const synth = await agent(
  'You are the round-2 synthesis judge. Determine the confidence of the leading hypothesis (logical-vs-physical size split) for the IMGUI Vulkan multi-window secondary offset: is it now PROVEN from static code (HIGH), or is it only POSSIBLE and still needs runtime DPI (MEDIUM/LOW)? (1) If the swapchain extent provably reads a physical framebuffer size different from the logical viewPort->Size, RAISE it to HIGH. (2) If they provably read the identical value, the split is VACUOUS -> the root cause falls in the "other" bucket -> give the most likely alternative. (3) For each probe report confirmed/refuted/uncertain and its evidence. Give the fix direction. Cite real file:line. Note coverage gaps.\n\n' + COMMON + '\n\nPROBE FINDINGS:\n' + JSON.stringify(res) + '\n\nADVERSARIAL VERDICTS:\n' + JSON.stringify(verified),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { confirmed, uncertain, verified, synth, rawFinds: res.length }

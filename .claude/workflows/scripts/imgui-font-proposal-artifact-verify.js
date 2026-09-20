export const meta = {
  name: 'imgui-font-proposal-artifact-verify',
  description: 'Adversarial verification of fix-imgui-font-whitebox artifacts (proposal/design/specs/tasks) before apply: 4 independent reviewers judge content claims C1-C6 against REAL code + prior ground truth (Vulkan-only root cause, D3D12 correct, D1 feasibility, D2 caveat, spec/task format, honesty), majority = confirm.',
  phases: [
    { title: 'Verify' },
    { title: 'Aggregate' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const CHANGE = R + '/openspec/changes/fix-imgui-font-whitebox'

const POINTS = { type: 'object', properties: { points: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, verdict: { type: 'string', enum: ['pass', 'issue', 'uncertain'] },
  defect: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['claim', 'verdict', 'defect', 'evidence', 'confidence'] } } }, required: ['points'] }

const AGG = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, pass: { type: 'integer' }, issue: { type: 'integer' }, uncertain: { type: 'integer' }, defect: { type: 'string' },
}, required: ['claim', 'pass', 'issue', 'uncertain'] } }, mustFix: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' } }, required: ['verdicts', 'mustFix', 'bottomLine'] }

const GROUND = [
  'REPO: ' + R,
  'PRIOR INVESTIGATION (definitive, completed 10-agent root-cause workflow): the IMGUI font white-box is VULKAN-ONLY. D3D12 renders correct thin letterform glyphs (test_output/TestIMGUI_d3d12.png, white=97px, recognizable "Options"); Vulkan renders solid blocks (test_output/TestIMGUI_vulkan.png, white=233px). ROOT CAUSE: VulkanTexture.cpp:161-177 builds vk::ImageViewCreateInfo WITHOUT viewInfo.components (identity), and grep over VulkanRenderBackendNew for "swizzle|ComponentMapping|SingleChannel|components" = 0 matches, so the interface GPUTextureSwizzle is never translated to vk::ComponentMapping. Also VulkanGraphLocalResourceManager::GetTextureView(handle) only takes the handle and returns pTexture->GetImageView() (the identity view from Init), DROPPING the bound GPUTextureView swizzle. D3D12 encodes the swizzle correctly: InterfaceTranslation.h:607-621 Shader4ComponentMapping with EColorChannelToInt(eR)=0 -> FROM_MEMORY_COMPONENT_0 = source R.',
  'Base facts: EColorChannel::eR=0 (Interface/RenderInterface/header/Common.h:141-143). GPUTextureView has NO format field (GPUTexture.h:70-78) so CreateDefaultForSampling R8G8B8A8 arg is not stored (only feeds FormatHasDepth). SingleChannel(channel) = {channel,channel,channel,channel} (GPUTexture.h:64-67). IMGUI font uses GetTexDataAsAlpha8 -> E_R8_UNORM (IMGUIContext.cpp:489-493) + CreateDefaultForSampling(E_R8G8B8A8_UNORM, SingleChannel(eR)) (IMGUIContext.cpp:753-754). Imgui.slang:14 samples .aaaa; :52 frag = input.Color * SampleFont(uv). GetTexDataAsAlpha8 sets bytesPerPixel=1 (imgui_draw.cpp:2515). GetTexDataAsRGBA32: each pixel IM_COL32(255,255,255,coverage) so coverage in A ONLY, RGB constant 255 (imgui_draw.cpp:2518-2532).',
  'TestIMGUI uses the MODULE IMGUIContext (Test/GPUBackendTester/CMakeLists.txt:30 target_link_libraries(IMGUIContext); Main.cpp:36 include <IMGUIContext/IMGUIContext.h>) = ' + R + '/IMGUIContext/IMGUIContext.cpp, NOT Test/VulkanRendererBackendTester/private/IMGUIContext.cpp (a different duplicate).',
].join('\n')

const COMMON = [
  'You are an INDEPENDENT adversarial reviewer of the OpenSpec change fix-imgui-font-whitebox (proposal.md / design.md / specs/texture-swizzle-honoring/spec.md / tasks.md in ' + CHANGE + '). Judge each claim against the REAL code and the prior ground truth below. You may Read the artifacts and any source file. Do NOT build or modify anything.',
  'GROUND TRUTH:\n' + GROUND,
  'Verdict: pass ONLY if accurate AND backed by real code/artifact/file you verified. issue = false/overstated/unsupported/misimplementable. uncertain = cannot verify.',
  'Be maximally skeptical of the "Vulkan-only, D3D12-fine" premise AND of the D1 design feasibility: if there is an ALREADY-existing swizzle-aware view path in the Vulkan backend that the design missed, or GetTextureView threading is infeasible, say so with file:line.',
].join('\n\n')

const CLAIMS = [
  'C1 ROOT-CAUSE attribution: proposal+design claim the white-box is VULKAN-ONLY and D3D12 is correct (not a bug). Verify against real code: VulkanTexture.cpp:161-177 has NO viewInfo.components; VulkanRenderBackendNew has no swizzle->vk::ComponentMapping translation (grep); VulkanGraphLocalResourceManager::GetTextureView(handle) drops the view and returns identity GetImageView(); D3D12 InterfaceTranslation.h:615-620 encodes the swizzle. Flag if D3D12 is actually ALSO broken (over-credited) or if the "Vulkan-only" claim is overstated.',
  'C2 D1 design is implementable AND no existing path: design proposes threading the bound GPUTextureView swizzle into image-view resolution (VulkanTexture swizzle-aware view + GetTextureView(handle,view) overload + VulkanResourceBindingInstance passes the view). Verify this is feasible against the real binding path (VulkanResourceBindingInstance.h:32 bindings holds pair<ImageHandle,GPUTextureView>; VulkanShaderStruct.cpp:113-125 SetImageInternal stores the view) and that NO already-existing mechanism creates a swizzle-aware view (which would make D1 redundant). Flag if an existing path exists or the threading is infeasible.',
  'C3 D2 caveat correctness: design+proposal state GetTexDataAsRGBA32 puts coverage in A ONLY (RGB constant 255, imgui_draw.cpp:2532 IM_COL32(255,255,255,coverage)), and that keeping SingleChannel(eR) with RGBA8 data would make .aaaa read R=255 => STILL white, so the swizzle MUST become identity. Verify this is accurate; flag if coverage is actually in RGB+A (not A-only) making the caveat wrong, or if the "must drop swizzle" is a misstatement.',
  'C4 SPEC correctness: specs/texture-swizzle-honoring/spec.md uses ADDED Requirements with ### + #### (4-hashtag) scenarios, SHALL for normative requirements, each requirement has >=1 scenario, and scenarios test realistically (SingleChannel(R) R8 -> .aaaa reads R; identity unchanged; IMGUI renders glyphs). Flag format misuse or a scenario that contradicts the actual fix direction.',
  'C5 TASKS: tasks.md uses - [ ] checkboxes grouped by ## heading, ordered by dependency (D1 implement -> build+verify -> D2 optional -> review closure), uses python build.py (not manual ninja/cmake), and the LAST section is Review & Adversarial Verify. Verify the checkbox format parses and the ordering/scope match the design (D1 must be verified BEFORE D2 so the swizzle path is exercised).',
  'C6 HONESTY: artifacts must NOT claim D3D12 was a bug, must NOT use "pre-existing"/"已有问题"/"之前就存在" phrasing, must NOT overstate confidence (e.g. presenting an inferred fix as empirically proven), and must acknowledge the one un-done step (no runtime proof the fix renders non-white until tasks 2.2 chases it). Scan proposal.md/design.md/tasks.md. Flag any overclaim or forbidden phrasing.',
].join('\n')

const angle = (n) => 'You are reviewer #' + n + '/4. Identical rules for all; judge independently, do not assume others.\n\n' + COMMON + '\n\nCLAIMS TO JUDGE (each -> one vote):\n' + CLAIMS + '\n\nOutput the full list: claim; verdict pass/issue/uncertain; defect (precise with file:line or quote); evidence (what you verified); confidence high/medium/low.'

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
  'Aggregate these verdicts for change fix-imgui-font-whitebox. For each claim id C1-C6 output {claim, pass, issue, uncertain} = counts across reviewers, and defect = concrete defect(s) if any reviewer said issue.\nmustFix = claims with issue count >= 2 (2+ of 4 flags a defect).\nbottomLine = blunt verdict: are the artifacts correct, honest, and implementable, or do they need revision before /opsx:apply? Name each specific fix. Pay special attention to C2 (is D1 truly feasible, or is there an existing swizzle-aware path making the plan wrong?) and C3 (is the D2 caveat actually right?).\n\nVERDICTS JSON:\n' + JSON.stringify(allPts),
  { label: 'aggregate', phase: 'Aggregate', schema: AGG })

return { verdicts: (agg?.verdicts || []), mustFix: (agg?.mustFix || []), bottomLine: (agg?.bottomLine || ''), rawVotes: allPts.length }

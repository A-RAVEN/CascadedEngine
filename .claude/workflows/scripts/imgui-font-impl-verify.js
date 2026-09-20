export const meta = {
  name: 'imgui-font-impl-verify',
  description: 'Adversarial review of the IMPLEMENTATION of change fix-imgui-font-whitebox (D1: Vulkan honors GPUTextureSwizzle -> vk::ComponentMapping). 4 independent reviewers with IDENTICAL criteria judge claims I1-I8 against the REAL edited code + real test_output evidence + MCP API docs: swizzle-aware view derivation + identity reuse, EColorChannel->ComponentSwizzle mapping, GetTextureView overload threading + one-arg identity delegate, sampled vs storage(UAV) descriptor branching (storage must keep identity components per VkImageViewCreateInfo.components), Release cleanup of cached swizzle views, empirical vulkan "Options" white=97 matching d3d12, no-regression full-suite exit 0, and MCP-verified API semantics.',
  phases: [
    { title: 'Verify' },
    { title: 'Aggregate' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const VKTEX_H = R + '/VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.h'
const VKTEX_CPP = R + '/VulkanRenderBackendNew/private/VulkanObjects/VulkanTexture.cpp'
const GLM_H = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.h'
const GLM_CPP = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanGraphLocalResourceManager.cpp'
const BIND_CPP = R + '/VulkanRenderBackendNew/private/GPUGraph/VulkanResourceBindingInstance.cpp'

const POINTS = { type: 'object', properties: { points: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, verdict: { type: 'string', enum: ['pass', 'issue', 'uncertain'] },
  defect: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['claim', 'verdict', 'defect', 'evidence', 'confidence'] } } }, required: ['points'] }

const AGG = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, pass: { type: 'integer' }, issue: { type: 'integer' }, uncertain: { type: 'integer' }, defect: { type: 'string' },
}, required: ['claim', 'pass', 'issue', 'uncertain'] } }, mustFix: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' } }, required: ['verdicts', 'mustFix', 'bottomLine'] }

const CLAIMS = [
  'I1 VulkanTexture::GetOrCreateSampledImageView: verify it (a) returns m_ImageView when view.swizzle == GPUTextureSwizzle::Create() (identity reuse, no new view, no behavior change), (b) for non-identity builds vk::ImageViewCreateInfo with viewInfo.components = ConvertComponentMapping(view.swizzle), honoring baseMip/mipCount/baseLayer/layerCount with 0/~0u clamps to descriptor mipLevels/layers, (c) caches by full GPUTextureView. Read ' + VKTEX_CPP + '. Flag any VUID risk in the new view creation (format must equal image format since not MUTABLE — VUID-12397; sentinel mip/layer validity VUID-01718/06725).',
  'I2 ConvertComponentMapping + ChannelToComponentSwizzle: verify SingleChannel(eR) -> {eR,eR,eR,eR} and default -> eIdentity, mirroring the proven legacy VulkanRenderBackend/InterfaceTranslator.h EColorChannelToVkComponentSwizzle. Read ' + VKTEX_CPP + '. Flag any mapping error (e.g. eA mis-mapped) that would make .aaaa read the wrong source.',
  'I3 GetTextureView overload: verify the two-arg GetTextureView(handle, view) resolves External via pTexture->GetOrCreateSampledImageView(view), and the one-arg GetTextureView(handle) DELEGATES to GetTextureView(handle, GPUTextureView::Create()) so existing handle-only callers (VulkanGraphExecutor.cpp:2240, VulkanResourceBindingInstance.cpp:262) are unchanged. Read ' + GLM_CPP + ' + ' + GLM_H + '. Flag if one-arg no longer returns the prior identity view for external, or if Backbuffer/Internal paths broke.',
  'I4 Binding branching: verify VulkanResourceBindingInstance.cpp now resolves SAMPLED descriptors via GetTextureView(handle, view) (honors swizzle) but STORAGE/UAV (isUAV) via GetTextureView(handle) (identity components), so storage image descriptors keep identity VkComponentMapping (required by VkImageViewCreateInfo.components / VUID). Read ' + BIND_CPP + '. Flag if the sampled path could feed a non-identity view into a storage descriptor (VUID violation).',
  'I5 Release cleanup: verify VulkanTexture::Release destroys all cached m_SwizzleSampledViews before the image+allocation free, and that the L1 m_Allocation==VK_NULL_HANDLE early-return guard still protects the double-free/teardown path. Read ' + VKTEX_CPP + '. Flag if a swizzle view leaks on the early-return/idempotent path or is destroyed after the image.',
  'I6 EMPIRICAL: read ' + R + '/test_output/TestIMGUI_vulkan.png + TestIMGUI_d3d12.png via PIL/numpy: vulkan now renders "Options" letterforms with white_px ~97 matching d3d12 ~97 (NOT the former 233px solid blocks), 4-color palette (0/36/51/255) on both, and TestIMGUI_validation.log is 0 bytes (no new VUID). Confirm the captured TestIMGUI_vulkan.png is the NEW file (post-fix). Flag if the vulkan PNG still shows solid blocks or a large white count.',
  'I7 NO-REGRESSION: verify the full headless suite exits 0 on BOTH backends (vulkan + d3d12) and that the change did not break other tests (only image-view swizzle resolution changed; identity swizzle reuses m_ImageView everywhere else). Check exit codes; flag any non-zero or newly-broken test.',
  'I8 API semantics (MCP ONLY, web-reader/web-search-prime NOT WebSearch/WebFetch): confirm vk::ImageViewCreateInfo::components is a remap from image components to shader vector components and is ONLY required to be identity for YCbCr combined sampler / input attachment / framebuffer attachment / storage image descriptors — ordinary SAMPLED image descriptors may use a non-identity mapping (so the font\'s SingleChannel(R) sampled view with {R,R,R,R} is valid, and the isUAV->identity branch is correct). Cite real URL(s).',
].join('\n')

const angle = (n) => 'You are reviewer #' + n + '/4. Identical rules for all; judge independently, do not assume others. You are adversarially reviewing the IMPLEMENTATION of OpenSpec change fix-imgui-font-whitebox (D1: Vulkan honors GPUTextureSwizzle -> vk::ComponentMapping so a single-channel font texture with SingleChannel(eR) reads R on .aaaa instead of the implicit A=1 white).\n\nREPO: ' + R + '\n\nRead the REAL edited files (' + [VKTEX_CPP, VKTEX_H, GLM_CPP, GLM_H, BIND_CPP].join(', ') + ') and the REAL test_output evidence. You may run md5sum/cat/grep/python(PIL) on test_output. Do NOT build or modify anything.\n\nJudge each claim to pass/issue/uncertain with concrete file:line (or md5/pixel/exit-code) evidence. Be maximally skeptical of I4 (storage descriptor must keep identity components — a leak of a non-identity view there is a real VUID bug) and I6 (the vulkan PNG must genuinely be the NEW post-fix render).\n\nCLAIMS:\n' + CLAIMS + '\n\nOutput the full list: claim; verdict; defect; evidence; confidence.'

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
  'Aggregate these verdicts for change fix-imgui-font-whitebox IMPLEMENTATION. For each claim id I1-I8 output {claim, pass, issue, uncertain} = counts across reviewers, and defect = the concrete defect(s) if any reviewer said issue.\nmustFix = claims with issue count >= 2 (2+ of 4 flags a defect).\nbottomLine = blunt verdict: is the D1 implementation correct and complete (does it actually make the Vulkan IMGUI font render readable glyphs, with the storage-descriptor identity-components path safe, no new VUID, and no regression), or does it need revision? Name each specific fix, paying special attention to I4 and I6.\n\nVERDICTS JSON:\n' + JSON.stringify(allPts),
  { label: 'aggregate', phase: 'Aggregate', schema: AGG })

return { verdicts: (agg?.verdicts || []), mustFix: (agg?.mustFix || []), bottomLine: (agg?.bottomLine || ''), rawVotes: allPts.length }

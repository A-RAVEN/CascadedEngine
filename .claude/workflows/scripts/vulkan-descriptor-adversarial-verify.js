export const meta = {
  name: 'vulkan-descriptor-adversarial-verify',
  description: 'Independent adversarial verification of the fix-vulkan-descriptor-registration 9.2 review: 4 reviewers with IDENTICAL criteria independently judge every claim (URLs, 08114/00331 elimination, regressions, D-A/D-B/D-C/D1/D2 code, depth-use fix, 4.5 residual risk) against real code + logs + MCP-verified docs; per-claim 2+ of 4 flags = confirmed defect (lean-skeptical).',
  phases: [
    { title: 'Verify' },
    { title: 'Aggregate' },
  ],
}

const POINTS = { type: 'object', properties: { points: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' },
  verdict: { type: 'string', enum: ['pass', 'issue', 'uncertain'] },
  defect: { type: 'string' },
  evidence: { type: 'string' },
  confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['claim', 'verdict', 'defect', 'evidence', 'confidence'] } } }, required: ['points'] }

const AGG = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: {
  claim: { type: 'string' }, pass: { type: 'integer' }, issue: { type: 'integer' }, uncertain: { type: 'integer' }, defect: { type: 'string' },
}, required: ['claim', 'pass', 'issue', 'uncertain'] } }, mustFix: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' } }, required: ['verdicts', 'mustFix', 'bottomLine'] }

const ROOT = 'E:/Projects/CascadedEngine'
const CHANGE = ROOT + '/openspec/changes/fix-vulkan-descriptor-registration'
const TEST_OUT = ROOT + '/test_output'

const CLAIMS = [
  'C1 URL-1 real: VMA VmaStatistics allocationBytes = live bytes (not peak). Verify via MCP web-reader/web-search-prime that https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/struct_vma_statistics.html exists and allocationBytes is defined as total bytes occupied by current VmaAllocation objects.',
  'C2 URL-2 real: memory aliasing of resources REQUIRES a memory dependency/barrier between uses. Verify via MCP that https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/resource_aliasing.html exists and says aliased resources need proper synchronization.',
  'C3 08114/00331 eliminated: read ' + TEST_OUT + '/*_validation.log (all 8 tests) — confirm ZERO VUID-*-08114 / VUID-VkWriteDescriptorSet-descriptorType-00331 (under Vulkan). ALSO reconcile the review-log claim "全部 0 VUID": TestTriangleWithConstantColor_validation.log is NON-EMPTY with ~2x VUID-VkShaderModuleCreateInfo-pCode-08740 (a shader-code warning). If the review claimed blanket "0 VUID on all 8", that is an overclaim — flag it. The strict acceptance (08114/00331 = 0) must still pass.',
  'C4 regression byte-identical: the vulkan vs d3d12 captures must be BYTE-IDS (cmp/md5sum identical): TestSimpleTriangle, TestTriangleWithImageBuffer, readback. Verify by running: cmp ' + TEST_OUT + '/TestSimpleTriangle_vulkan.png ' + TEST_OUT + '/TestSimpleTriangle_d3d12.png (and the other two pairs) or md5sum. Byte-identity IS the regression claim.',
  'C5 D-A correct: register RasterShaderResources (VulkanGraphExecutor.cpp) feeds RAST shader image+buffer bindings into SetImageRWState/SetBufferRWState with access/layout mapping; verify the code compiles logically and matches RegisterComputeResources pattern; no attachment/vertex double-feed conflict.',
  'C6 D-B correct: Get*HandleToResource accessors exist; ExtendGraphResourceLifetimes iterates m_*Lifetimes with states.empty() guard, maps handle->resourceId, calls MarkResourceUse(head/end) AFTER BuildResourceUsageRanges and BEFORE AllocateAliasedResources.',
  'C7 D-C correct: m_PersistentVirtualAllocs written in AllocResource, survives FreeResourcesUpToBatch, cleared in ResetVirtualBlocks/Destroy/Release; CommitVirtualAllocations computes per-pool max(offset+size); BindResourcesToPhysicalMemory iterates persistent (not active) to bind ALL resources.',
  'C8 D1补全 correct: top-level Foreach skips already-registered internal handles (Is*HandleRegistered) to avoid orphan resources.',
  'C9 D2 correct: BuildResources image+buffer fallbacks are now inert (no RegisterTemporary*/Register*Handle; warn+skip) — confirm no Phase-5 batch-0 dead resource creation.',
  'C10 (ROUND-3) verify the Round-2 FIX resolved the finding by HONEST DOCUMENTATION (per the Round-2 review\'s accepted option 1): the code comment on AddAliasedResourceMemoryDependencies() (VulkanGraphExecutor.cpp) now explicitly states (a) each batch is its OWN vkQueueSubmit (SubmitBatches commandBufferCount=1, waitForFences between), (b) the vkMemoryBarrier is therefore SUBMISSION-LOCAL / best-effort and does NOT form a cross-submission memory dependency, (c) actual cross-batch memory correctness rides on same-queue waitForFences serialization (execution order) + device-local coherence, (d) the alias barrier is now added ONLY to batch.aquireBarriers (direct); it is intentionally NOT added to batch.computeAquireBarriers (that cross-queue path is unsound without a semaphore — note computeAquireBarriers itself is still alive & used elsewhere in SubmitBatches, only the alias-barrier-to-compute path was dropped). Verify: (1) the code comment is accurate and matches the real code; (2) the alias barrier is added only to aquireBarriers and not to computeAquireBarriers; (3) re-running all tests still gives 0 VUID + byte-identical regression + non-black. Verdict pass IF the implementation is honestly documented (no overclaim) and no regression — the absence of a true cross-submission dependency is now disclosed, not asserted away. Verdict issue ONLY if the comment STILL overclaims spec-soundness, the alias barrier still targets computeAquireBarriers, the code mismatches the comment, or there is a regression.',
  'C11 (ROUND-2) verify the Round-1 wording FIX: tasks.md 8.1/8.3 and the ## Review Log now explicitly state TestIMGUI 08114/00331=0 is INDEPENDENT of this change (external m_Fontimage, resolved by pre-existing GetTextureView external fallback, not in m_LocalResources, skipped by D-B, not aliased by D-C). Confirm NO remaining overclaim in the change artifacts (proposal/tasks/design/Review Log) that attributes IMGUI cleanliness to D-A/B/C. If the wording is now honest and consistent, pass; if any artifact still claims IMGUI coverage, issue.',
  'C12 depth-usage fix correct + no regression: after the Round-1 depth fix, CollectResources registers a DEPTH attachment with ETextureAccessType::eDepthStencil (not eRT). Verify (read VulkanGraphExecutor.cpp CollectResources) both: (a) the ternary selects eDepthStencil == the depth attachment; (b) re-running TestTriangleWithConstantColor gives 08114/00331 = 0 and does NOT re-grow 01209/01758/02633/08931 (ConstantColor log should only have shader-code 08740). Confirm the fix did not break the OTHER 5 passing tests (their logs still 0 VUID, non-black captures).',
].join('\n')

const JUDGE = [
  'You are an INDEPENDENT adversarial reviewer. Context: an OpenSpec change fix-vulkan-descriptor-registration just fixed VUID-08114/00331 (vulkan graph resources bound by shaders never getting a bound descriptor). Review Log (truthful, written by the implementer) is at ' + CHANGE + '/tasks.md (bottom ## Review Log section).',
  'CRITICAL RULE 1: verify each claim against REAL artifacts, do not assume. Read the real git diff (run: git -C ' + ROOT + ' diff -- VulkanRenderBackendNew/ and git -C ' + ROOT + ' status --short VulkanRenderBackendNew/), read the real changed files at cited locations, read the real ' + TEST_OUT + ' logs.',
  'CRITICAL RULE 2: for C1 and C2, you MUST use the MCP tools mcp__web-reader__webReader and/or mcp__web-search-prime__web_search_prime to fetch the URLs — do NOT rely on memory. If a tool is unavailable, mark that claim uncertain and say so. Report whether each URL is REAL (exists, not hallucinated) and whether the described semantics match.',
  'CRITICAL RULE 3: DO NOT use WebSearch/WebFetch for any external verification — only the MCP web tools.',
  'CRITICAL RULE 4: for empirical claims C3/C4/C11, you may run the tester: ' + ROOT + '/out/build/x64-debug/bin/GPUBackendTester.exe --backend vulkan --test <name> --capture 5 --headless 8, or read the existing ' + TEST_OUT + ' logs. If you rerun and it differs from the claim, that is an issue.',
  'Verdict rules: verdict = pass ONLY if the claim is accurate AND backed by the real artifact/URL you verified. verdict = issue if the claim is false, overstated, cites a non-existent URL, mismatches real code/logs, omits a required guard, or is an overclaim. verdict = uncertain if you cannot verify (tool unavailable, artifact not present).',
  'For C10 and C11, be maximally skeptical — these are where the implementer is most likely to overclaim. Prefer issue/uncertain over pass if there is any doubt.',
].join('\n')

const angle = (n) =>
'You are reviewer #' + n + '/4. Every reviewer uses the IDENTICAL rules below; you judge independently, do not assume others’ votes.\n\n' + JUDGE + '\n\nCLAIMS TO JUDGE (each → one vote):\n' + CLAIMS + '\n\nOutput the full list: claim; verdict pass/issue/uncertain; defect (precise, with real file:line or URL or log); evidence (what you verified); confidence high/medium/low.'

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
'Aggregate these verdicts. For each claim id C1-C12, output one verdicts entry {claim, pass, issue, uncertain} = counts across reviewers, and defect = concrete defect(s) if any reviewer said issue.\nSet mustFix = array of claim ids where issue count >= 2 (lean-skeptical: 2+ of 4 reviewers flag a defect). Claims with issue 1 = worth attention. 0 = accepted.\nThen bottomLine = a blunt verdict: is the fix-vulkan-descriptor-registration implementation trustworthy and complete, or does it need revision? Name each specific fix needed (e.g. C10 barrier, C11 overclaim correction, C1/C2 URL false, C12 depth regression).\n\nVERDICTS JSON:\n' + JSON.stringify(allPts),
  { label: 'aggregate', phase: 'Aggregate', schema: AGG })

return { verdicts: (agg?.verdicts || []), mustFix: (agg?.mustFix || []), bottomLine: (agg?.bottomLine || ''), rawVotes: allPts.length }

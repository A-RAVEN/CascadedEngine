export const meta = {
  name: 'audit-readback-carrier',
  description: 'Adversarially audit the drafted harden-readback-carrier change (proposal/design/specs/tasks) against the RECORDED TODO readback conclusions + main resource-readback spec, to find every divergence (not just bpp). Then adversarially verify each finding and synthesise.',
  phases: [
    { title: 'Review' },
    { title: 'Verify' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const C = R + '/openspec/changes/harden-readback-carrier'

// Recorded source-of-truth conclusions (docs/TODO.md readback entries + main spec). Auditors MUST
// also re-open docs/TODO.md (readback entries) and openspec/specs/resource-readback/spec.md.
const intro = 'SOURCE OF TRUTH = docs/TODO.md readback entries + openspec/specs/resource-readback/spec.md. Re-read BOTH (Read tool) — do NOT trust the digest below alone.'
const lines = [
  'TODO [2026-08-28] 压缩/复杂格式读回载体 & bpp 设计原则 — PRINCIPLES (recorded CONCLUSION, not suggestion):',
  '  原则1: compressed readback is BLOCK-LINEAR, NOT pixel-linear. Real size = ceil(W/blockW)*ceil(H/blockH)*blockBytes (+D3D12 256 align). ASTC/PVRTC cannot derive from W/H. Interpret needs format+blockSize+size + block decoder (bcdec); stbi_write_png cannot write it.',
  '  原则2: block params are an ORTHOGONAL axis to resolution (resolution is a descriptor axis, block is a format-lookup axis, not mutually derivable).',
  '  原则3: PUBLIC face = GetImageByteSize(width,height,format) single byte-size query; NOT GetBytesPerPixel. Caller needs how many bytes to read back, not bytes per pixel. bpp is an INTERNAL intermediate only (compressed bpp = compression-ratio stat, no layout meaning).',
  '  待修(MED): GetReadbackBpp/D3D12ReadbackBpp private duplicates DIVERGED; fix = (1) GetImageByteSize as single source of truth, (2) unsupported formats EXPLICITLY error, (3) add E_D32_SFLOAT_S8_UINT case.',
  'TODO [2026-08-28] 缺陷A (mip/layer/MSAA): carrier hardcodes base mip/layer (VK imageSubresource.mipLevel=0,baseArrayLayer=0,layerCount=1; D3D12 SubresourceIndex=0); MSAA not resolved. Fix options: (1) NARROW to single-mip/single-layer/non-MSAA 2D + explicitly reject others, OR (2) add subresource selector + resolve. TODO lists BOTH as valid options.',
  'TODO [2026-08-28] 缺陷B (dst span UAF): IReadbackToken holds bare std::span; async Vulkan Wait() memcpy the dst. Caller resize between Readback()+Wait() yields UAF. Fix options: (1) strengthen+document contract, OR (2) token owns a copy. NOTE the honest framing: contract cannot fully eliminate the callers-invalidate-backing-memory UAF — prose + defensive check is the achievable bound.',
  'TODO [2026-08-28] D3D12异步: D3D12 readback is SYNC (token Wait is no-op, all work in Readback()); must become async to match Vulkan. Fix prefer A: reuse CommandListManager/PooledCommandAllocator/GPUFrameManager. THIS IS A SEPARATE ITEM (scope).',
  'MAIN SPEC resource-readback: promises async-declare+explicit-sync (Wait is the only sync point), one memcpy (no zero-copy abstraction), only engine-built offscreen RT with eTransferSrc (not backbuffer), Vulkan/D3D12 each implement movement+sync with consistent semantics.',
]
const SOT = [intro].concat(lines).join('\n')

const COMMON = [
  'You audit the DRAFTED OpenSpec change harden-readback-carrier (proposal/design/specs/tasks, at ' + C + '). Read the ACTUAL drafted files + the recorded source-of-truth (docs/TODO.md readback entries + openspec/specs/resource-readback/spec.md). Your job: find EVERY point where the drafted change DIVERGES from, CONTRADICTS, or silently weakens a RECORDED source-of-truth conclusion. Concretely: does the public Readback contract expose GetImageByteSize or per-pixel bpp (TODO 原则3)? Is compressed acknowledged as block-linear and excluded correctly? Is subresource narrowing a TODO-valid option (narrow OR selector — did the draft pick one the TODO allows)? Is the OWN cannot-fully-eliminate-UAF honesty present? Is ASYNC correctly scoped OUT (Non-goal) without silently changing D3D12 sync semantics? Does the draft contradict the MAIN SPEC (async-declare+explicit-sync) by exposing sync as a no-op? Point at concrete file:line in the DRAFTED artifacts.',
  'GROUND (recorded conclusions):\n' + SOT,
].join('\n\n')

const AUDIT = { type: 'object', properties: { divergences: { type: 'array', items: { type: 'object', properties: {
  area: { type: 'string' }, severity: { type: 'string', enum: ['blocker', 'mustfix', 'shouldfix', 'nit'] },
  recordedConclusion: { type: 'string' }, draftClaim: { type: 'string' }, draftEvidence: { type: 'string' },
  fix: { type: 'string' },
}, required: ['area', 'severity', 'recordedConclusion', 'draftClaim', 'draftEvidence', 'fix'] } }, total: { type: 'number' } }, required: ['divergences', 'total'] }

const VERDICT = { type: 'object', properties: { finding: { type: 'string' }, verdict: { type: 'string', enum: ['real', 'refuted', 'ambiguous'] },
  detail: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['finding', 'verdict', 'detail', 'evidence', 'confidence'] }

const SYNTH = { type: 'object', properties: { verdict: { type: 'string', enum: ['aligned', 'divergent'] },
  blockers: { type: 'array', items: { type: 'string' } }, mustfix: { type: 'array', items: { type: 'string' } },
  alignSummary: { type: 'string' } }, required: ['verdict', 'blockers', 'mustfix', 'alignSummary'] }

const ROLE = (n, focus) => 'You are auditor #' + n + '. ' + COMMON + '\n\nYOUR FOCUS:\n' + focus

const AUDITS = [
  ['public-contract-bpp', 'Audit the PUBLIC byte-size contract. Recorded TODO 原则3: public face = GetImageByteSize(width,height,format), NOT per-pixel bpp. Read the drafted design.md (D1), proposal.md (What Changes / Modified capabilities), specs/resource-readback/spec.md (Requirement 载体 bpp 单一来源), tasks.md (section 1.x) and the ACTUAL CRenderBackend.h / Common.h. Decide: does the drafted change expose GetFormatBlockSize + caller computes W*H*bpp (DIVERGES from 原则3), or GetImageByteSize? Flag the contradiction precisely with the draft claim. Note whether IsReadbackSupportedFormat whitelist correctly handles compressed (block-linear) rather than treating bpp as meaningful for it.'],
  ['compressed-block-linear', 'Audit compressed handling. Recorded 原则1/2: compressed is block-linear (ceil(W/blockW)*ceil(H/blockH)*blockBytes), NOT W*H*bpp; block params orthogonal; interpret needs block decoder; stbi_write_png cannot. Read drafted proposal (Non-goals), design (D2 IsReadbackSupportedFormat), specs. Decide: does the draft (a) correctly REJECT compressed via IsReadbackSupportedFormat (so the W*H*bpp formula never applies to it), (b) correctly place compressed byte-size in a follow-up/Non-goal (not misclaim the interface handles it), (c) avoid claiming stbi_write_png can export compressed. Flag any place the draft implies compressed is W*H*bpp-readable or that the narrowed interface already supports it.'],
  ['subresource-owns-sync-scope', 'Audit subresource narrowing + OWN honesty + ASYNC scoping. Recorded 缺陷A options = (1) narrow+reject OR (2) selector+resolve (BOTH valid); 缺陷B honest bound = prose+defensive check, cannot fully eliminate UAF; D3D12 async is a SEPARATE item. Read drafted design (D3/D4), proposal (Non-goals), specs (Requirement subresource 收窄 + dst 契约), tasks (sections 3.x/4.x/6.x). Decide: (a) subresource narrow option is one the TODO allows and the draft did not silently break the RGBA8 single-layer test; (b) the OWN fix is honestly framed as cannot-fully-eliminate-UAF (not overclaiming a safety fix); (c) ASYNC is correctly a separate Non-goal AND the draft did NOT accidentally change D3D12 sync semantics (still sync, only format/subresource/contract aligned); (d) MAIN SPEC async-declare+explicit-sync is not contradicted.'],
]

phase('Review')
const revs = await parallel(AUDITS.map(([k, focus], i) => () =>
  agent(ROLE(i + 1, focus), { label: 'audit:' + k, phase: 'Review', schema: AUDIT })))
const divs = revs.filter(Boolean).flatMap(r => r.divergences || [])
const blockers = divs.filter(d => d.severity === 'blocker')
const mustfix = divs.filter(d => d.severity === 'mustfix')
log('Divergences found: ' + divs.length + ' (blockers:' + blockers.length + ' mustfix:' + mustfix.length + ')')

phase('Verify')
const verified = await parallel(divs.map((d) => () =>
  agent('Adversarially verify whether this claimed divergence is REAL. Re-open the recorded source-of-truth (docs/TODO.md readback entries, openspec/specs/resource-readback/spec.md) AND the drafted artifact file (use the path in draftEvidence). Default to REFUTED if the draft actually respects the recorded conclusion. ' + COMMON + '\n\nCLAIMED DIVERGENCE: ' + JSON.stringify(d),
    { label: 'verify:' + (d.area || 'x'), phase: 'Verify', schema: VERDICT }).then(v => ({ ...d, ...v }))))

phase('Synthesize')
const synth = await agent(
  'You are the final judge. Decide ALIGNED (the drafted change respects every recorded TODO readback conclusion + main spec) or DIVERGENT (list blockers/mustfix). Cross-check the audit + verification. Do NOT rubber-stamp: verify divergences are genuine, not that the auditor missed source-of-truth. Return concrete fixes to apply for each real divergence. ' + COMMON + '\n\nAUDIT DIVERGENCES:\n' + JSON.stringify(divs) + '\n\nVERIFICATION:\n' + JSON.stringify(verified),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { divs, blockers, mustfix, verified, synth }

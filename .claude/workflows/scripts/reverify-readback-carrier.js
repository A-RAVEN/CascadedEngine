export const meta = {
  name: 'reverify-readback-carrier',
  description: 'Re-verify the REVISED harden-readback-carrier artifacts after the DIVERGENT audit fixes. For each prior finding, confirm the fix is genuinely present and coherent (not just superficially reworded), adversarially hunt for NEW divergences or internal inconsistencies introduced by the rewrite, verify each finding, and synthesise APPROVE/REVISE.',
  phases: [
    { title: 'Review' },
    { title: 'Refute' },
    { title: 'Synthesize' },
  ],
}

const R = 'E:/Projects/CascadedEngine'
const C = R + '/openspec/changes/harden-readback-carrier'

const intro = 'This is a ROUND-2 audit of the REVISED open spec change at ' + C + '. Round-1 (prior workflow) found DIVERGENT: (blocker) public byte-size contract used GetFormatBlockSize instead of the recorded GetImageByteSize; (mustfix) compressed block-linear carrier dropped and mischaracterized, IsCompressedFormat vacuous gate, desc.samples==1 written incorrectly as int, E_D32_SFLOAT_S8_UINT over-committed as supported; (nit) missing textureType==e2D check, Wait()-anchored wording, async change name mismatch. The author then revised ALL FOUR artifacts. YOUR JOB: (A) FOR REGRESSION, confirm each prior finding is NOW GENUINELY FIXED and coherent (read the actual files; a reworded string that still contradicts elsewhere does NOT count). (B) FOR NEW ISSUES, hunt for new divergences from the recorded source of truth, and any INTERNAL inconsistency introduced by the rewrite (cross-artifact proposal/design/specs/tasks must tell the same story).'
const lines = [
  'RECORDED SOT (docs/TODO.md readback entries + openspec/specs/resource-readback/spec.md), re-read both:',
  '  TODO principle3 (compressed entry): public byte-size face = GetImageByteSize(width,height,format) single query; NOT per-pixel bpp; GetFormatBlockSize internal only.',
  '  TODO principle1: compressed readback is block-linear ceil(W/blockW)*ceil(H/blockH)*blockBytes (+D3D12 256 align), NOT W*H*bpp; needs format+blockSize+size+bcdec; stbi_write_png cannot.',
  '  TODO pending-fix (2)(3): unsupported formats EXPLICITLY error; E_D32_SFLOAT_S8_UINT bpp value aligned to GetFormatBlockSize=8.',
  '  TODO defect-A: narrow to single-mip/single-layer/non-MSAA 2D + reject, OR selector+resolve (both valid).',
  '  TODO defect-B: honest bound = prose + defensive dst.size() check; cannot fully eliminate backing-memory UAF.',
  '  TODO main-spec: async-declare+explicit-sync (Wait is the only sync point); one memcpy (no zero-copy); only engine-built eTransferSrc offscreen RT; Vulkan/D3D12 each implement movement+sync.',
  '  D3D12 async = separate item (fix-readback-async).',
]
const SOT = [intro].concat(lines).join('\n')

const COMMON = [
  'You audit the REVISED change at ' + C + ' (proposal.md, design.md, specs/resource-readback/spec.md, tasks.md). Read the ACTUAL files. Do NOT trust the summary that the findings were fixed - verify from the text. Point at concrete file:line for every claim.',
  'SOT:\n' + SOT,
].join('\n\n')

const AUDIT = { type: 'object', properties: { findings: { type: 'array', items: { type: 'object', properties: {
  area: { type: 'string' }, kind: { type: 'string', enum: ['regression-unfixed', 'new-divergence', 'new-inconsistency', 'confirms-fixed'] },
  severity: { type: 'string', enum: ['blocker', 'mustfix', 'shouldfix', 'nit'] },
  detail: { type: 'string' }, evidence: { type: 'string' }, fix: { type: 'string' },
}, required: ['area', 'kind', 'severity', 'detail', 'evidence', 'fix'] } }, total: { type: 'number' } }, required: ['findings', 'total'] }

const VERDICT = { type: 'object', properties: { finding: { type: 'string' }, verdict: { type: 'string', enum: ['real', 'refuted'] },
  detail: { type: 'string' }, evidence: { type: 'string' }, confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['finding', 'verdict', 'detail', 'evidence', 'confidence'] }

const SYNTH = { type: 'object', properties: { verdict: { type: 'string', enum: ['approve', 'revise'] },
  blockers: { type: 'array', items: { type: 'string' } }, mustfix: { type: 'array', items: { type: 'string' } },
  confirmedFixes: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' } }, required: ['verdict', 'blockers', 'mustfix', 'bottomLine'] }

const ROLE = (n, focus) => 'You are auditor #' + n + '. ' + COMMON + '\n\nYOUR FOCUS:\n' + focus

const AUDITS = [
  ['regress-public-contract', 'REGRESSION CHECK on the Round-1 BLOCKER. Read design.md D1, proposal.md (What Changes and Modified capabilities), specs/resource-readback/spec.md (Requirement 1 plus scenarios), tasks.md section 1.x. Decide: is GetImageByteSize(width,height,format) NOW the single public byte-size face, with GetFormatBlockSize consistently demoted to internal intermediate? Is the D1 rationale now correct, not the old D3D12-needs-width-times-bpp non-sequitur? Does ANY remaining place still expose W times H times bpp as the public basis or contradict GetImageByteSize? Is the non-compressed branch of GetImageByteSize consistent with GetFormatBlockSize?'],
  ['regress-compressed-depth-samples', 'REGRESSION CHECK on the MUSTFIX batches. One, compressed block-linear: is it now correctly characterized as block-linear ceil formula, placed in Non-goal, and never W times H times bpp applied to compressed, with no leftover GetFormatBlockSize-no-value clause? Two, depth-stencil (D32_SFLOAT_S8_UINT and D24_UNORM_S8_UINT): is it now consistently REJECTED as multi-aspect with only bpp value aligned, and NO leftover claim it is supported? Three, samples: is desc.samples compared to EMultiSampleCount::e1 everywhere, with NO literal 1 or greater-than-1 leftover? Four, textureType: is e2D checked? Read design D3, spec Requirement 2, tasks section 3.x.'],
  ['regress-owns-write-iscompress', 'REGRESSION CHECK on OWN honesty plus write-point wording plus IsCompressedFormat vacuous note. Read design D4, spec Requirement 3 (dst contract), tasks section 4.x, and the IsCompressedFormat honest note in spec Requirement 1 and design D2. Decide: is the cannot-fully-eliminate-UAF honesty preserved? Is the write point worded as writing-dst-before (Vulkan Wait memcpy, D3D12 Readback) not over-anchored at Wait? Is the IsCompressedFormat-vacuous caveat present and coherent?'],
  ['new-consistency-hunt', 'NEW-ISSUE HUNT. Re-read ALL FOUR revised files and compare cross-artifact. Hunt for: (a) internal contradictions the rewrite may have introduced, such as GetImageByteSize return type or empty-vs-0 semantics inconsistent across design/spec/proposal, a D3D12 async vs D3D12 sync mention, and depth-stencil GetImageByteSize returning packed bytes while Readback rejects (is that coherent or contradictory?), and the compressed branch in GetImageByteSize vs the rejection; (b) any place still contradicting the recorded SOT, for example a formula that could be misapplied to compressed, or an implied support for a rejected format; (c) proposal/design/specs/tasks disagreeing on the same rule (names, non-goals, change name fix-readback-async unified); (d) the design is implementable (GetImageByteSize signature and contract clear enough for a coder).'],
]

phase('Review')
const revs = await parallel(AUDITS.map(([k, focus], i) => () =>
  agent(ROLE(i + 1, focus), { label: 'audit:' + k, phase: 'Review', schema: AUDIT })))
const findings = revs.filter(Boolean).flatMap(r => r.findings || [])
const blocker = findings.filter(f => f.severity === 'blocker')
const mustfix = findings.filter(f => f.severity === 'mustfix')
log('Findings: ' + findings.length + ' (blocker:' + blocker.length + ' mustfix:' + mustfix.length + ')')

phase('Refute')
const refuted = await parallel(findings.filter(f => f.kind !== 'confirms-fixed').map((f) => () =>
  agent('Adversarially REJECT or CONFIRM this finding on the REVISED artifacts. Re-open the actual files; default to REFUTED if the revision genuinely fixed it and no real problem remains. ' + COMMON + '\n\nFINDING: ' + JSON.stringify(f),
    { label: 'refute:' + (f.area || 'x'), phase: 'Refute', schema: VERDICT }).then(v => ({ ...f, ...v }))))

phase('Synthesize')
const synth = await agent(
  'You are the final judge for ROUND 2. Decide APPROVE (the designed fixes are genuine, every recorded SOT conclusion is now respected, no new blockers) or REVISE (list blockers/mustfix). Cross-check the audit + refutations. Do NOT rubber-stamp: a fix counts only if the revised text actually implements it. ' + COMMON + '\n\nFINDINGS:\n' + JSON.stringify(findings) + '\n\nREFUTATIONS:\n' + JSON.stringify(refuted),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH })

return { findings, blocker, mustfix, refuted, synth }

export const meta = {
  name: 'reverify-audit1-readback-implementation',
  description: 'Round-2 reverify the AUDIT-1 hard-assert fix to the readback multi-mip reject test',
  phases: [
    { title: 'Audit', detail: 'verify the hard-assert fix is correct and the evidence is honest' },
    { title: 'Verify', detail: 'adversarially refute every finding' },
    { title: 'Synthesize', detail: 'bottom-line verdict' },
  ],
}

const ROOT = 'E:/Projects/CascadedEngine'
const FILES = {
  tester: ROOT + '/Test/GPUBackendTester/Main.cpp',
  tasks: ROOT + '/openspec/changes/harden-readback-carrier/tasks.md',
  vulkan: ROOT + '/VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp',
  d3d12: ROOT + '/D3D12RenderBackend/private/RenderBackend_D3D12.cpp',
}

const AUDIT_SCHEMA = {
  type: 'object',
  required: ['findings'],
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        required: ['id', 'title', 'severity', 'detail'],
        properties: {
          id: { type: 'string' },
          title: { type: 'string' },
          file: { type: 'string' },
          line: { type: 'number' },
          severity: { type: 'string' },
          detail: { type: 'string' },
        },
      },
    },
  },
}

const VERDICT_SCHEMA = {
  type: 'object',
  required: ['isReal', 'reason'],
  properties: {
    isReal: { type: 'boolean' },
    reason: { type: 'string' },
  },
}

const SYNTH_SCHEMA = {
  type: 'object',
  required: ['verdict', 'blockers', 'mustfix', 'shouldfix', 'bottomLine'],
  properties: {
    verdict: { type: 'string' },
    blockers: { type: 'array', items: { type: 'string' } },
    mustfix: { type: 'array', items: { type: 'string' } },
    shouldfix: { type: 'array', items: { type: 'string' } },
    bottomLine: { type: 'string' },
  },
}

const AUDITORS = [
  {
    key: 'regress-audit1-fix',
    prompt:
      'Round-2 audit of the AUDIT-1 fix to the harden-readback-carrier implementation. Round-1 found the multi-mip reject negative test in TestReadback was a SOFT check (if mmTok { stderr } else { stdout }), so a subresource-gate regression would not fail the harness. The author changed the mmTok branch to throw std::runtime_error.\n' +
      'Read ' + FILES.tester + ' (find TestReadback and its multi-mip reject block near the capture throw ~:289-293), and cross-check the gate still exists in ' + FILES.vulkan + ' (Readback(image) subresource gate) and ' + FILES.d3d12 + '.\n' +
      'Verify: (1) The mmTok branch now THROWS std::runtime_error (not just stderr), mirroring the closed-loop capture throw pattern at the top of TestReadback. (2) The throw fires ONLY in the wrongly-accepted branch; the correct (null token) branch prints OK and does NOT throw, so normal operation is unaffected. (3) The normal capture path (CreateCaptureRT, offscreen RT - single-mip/single-layer/e2D/e1) still passes the subresource gate (no false rejection caused by the fix). (4) No unrelated change crept into this diff. Report any finding; empty if clean.',
  },
  {
    key: 'evidence-audit1',
    prompt:
      'Round-2 audit of evidence honesty for the AUDIT-1 fix. The claimed evidence: Debug build SUCCESSFUL; after the fix, both backends ran TestReadback and exited 0; the log shows "readback: wrote test_output/readback_vulkan.png pixelBytes=262144 nonEmpty=true" and "[reject] multi-mip readback -> null token OK"; readback PNG is non-blank.\n' +
      'Read ' + FILES.tasks + ' (the Round-1 Implementation audit Review Log entry mentioning AUDIT-1 shouldfix->fixed).\n' +
      'Verify: (1) The evidence is stated honestly (no overclaim that the normal capture was re-run or that depth-stencil/MSAA/3D/unknown rejections were runtime-verified — those remain static). (2) The AUDIT-1 fix is recorded accurately as a shouldfix->fixed, low severity. (3) No claim asserts the multi-mip null-token rejection is now enforced in a way the code does not actually do. Report any honesty gap; empty if clean.',
  },
]

function verifyPrompt(f) {
  return (
    'Adversarially REFUTE this reported finding about the Round-2 AUDIT-1 fix. Read the referenced file and reason against it.\n' +
    'Finding id: ' + f.id + '\nSeverity: ' + f.severity + '\nFile: ' + (f.file || '(unspecified)') + '\nLine: ' + (f.line || 'n/a') + '\nTitle: ' + f.title + '\nDetail: ' + f.detail + '\n\n' +
    'Default to refuted=true if uncertain or not reproducible. Set isReal=true ONLY if you independently confirm it is a genuine defect. Give a concrete reason.'
  )
}

phase('Audit')
const audits = await parallel(
  AUDITORS.map((a) => () =>
    agent(a.prompt, { label: 'audit:' + a.key, phase: 'Audit', schema: AUDIT_SCHEMA })
  )
)

phase('Verify')
const verified = await parallel(
  audits
    .filter(Boolean)
    .flatMap((res) => res.findings || [])
    .map((f) => () =>
      agent(verifyPrompt(f), {
        label: 'verify:' + (f.id || 'finding'),
        phase: 'Verify',
        schema: VERDICT_SCHEMA,
      }).then((v) => ({ ...f, verdict: v }))
    )
)

const confirmed = verified.filter(Boolean).filter((f) => f.verdict && f.verdict.isReal)

phase('Synthesize')
const synth = await agent(
  'Synthesize the Round-2 reverify of the AUDIT-1 hard-assert fix. Total findings: ' + (verified.length) + '. Confirmed: ' + confirmed.length + '. ' +
    (confirmed.length
      ? 'Confirmed findings:\n' + confirmed.map((f) => '- [' + f.severity + '] ' + f.id + ' ' + f.title + ' :: ' + f.detail + '\n').join('')
      : 'No findings survived adversarial refutation.\n') +
    'Produce a verdict (APPROVE/REVISE), blockers, mustfix, shouldfix, bottomLine. The AUDIT-1 fix (throw std::runtime_error in the mmTok branch of the multi-mip reject test) was made in response to Round-1. If it is correct and the evidence is honest, verdict APPROVE with empty blocker/mustfix lists.',
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

return { confirmed, synth }

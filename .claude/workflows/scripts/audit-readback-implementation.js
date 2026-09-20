export const meta = {
  name: 'audit-readback-implementation',
  description: 'Adversarially verify the readback carrier hardening implementation against spec claims a-g',
  phases: [
    { title: 'Audit', detail: 'independent auditors check each claim against code' },
    { title: 'Verify', detail: 'adversarially refute every finding' },
    { title: 'Synthesize', detail: 'rank survivors, bottom-line verdict' },
  ],
}

const ROOT = 'E:/Projects/CascadedEngine'
const FILES = {
  common: ROOT + '/Interface/RenderInterface/header/Common.h',
  header: ROOT + '/Interface/RenderInterface/header/CRenderBackend.h',
  vulkan: ROOT + '/VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp',
  d3d12: ROOT + '/D3D12RenderBackend/private/RenderBackend_D3D12.cpp',
  tester: ROOT + '/Test/GPUBackendTester/Main.cpp',
  spec: ROOT + '/openspec/changes/harden-readback-carrier/specs/resource-readback/spec.md',
  tasks: ROOT + '/openspec/changes/harden-readback-carrier/tasks.md',
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
    key: 'public-contract-bpp',
    prompt:
      'Audit claim (a): GetImageByteSize is the SOLE public carrier-size source and its compression / non-compression branches are consistent with GetFormatBlockSize.\n' +
      'Read ' + FILES.common + ' and ' + FILES.vulkan + ', ' + FILES.d3d12 + ', ' + FILES.tester + ' and ' + FILES.spec + '.\n' +
      'Verify: (1) GetImageByteSize returns width*height*GetFormatBlockSize ONLY for the whitelisted compact formats, and returns 0 for unknown formats (never silently W*H*4 via GetFormatBlockSize default=4). (2) Its whitelist matches GetFormatBlockSize explicit cases (no drift). (3) The compressed branch returns 0 (recorded target) and does NOT compute W*H*bpp for compressed. (4) Both backends use GetImageByteSize for byteSize (not a private bpp table; the old GetReadbackBpp/D3D12ReadbackBpp are gone). (5) No caller derives carrier size as width*height*GetFormatBlockSize(format). Report any divergence as a finding; if a claim holds, return an empty findings array.',
  },
  {
    key: 'format-support-set',
    prompt:
      'Audit claim (b): the readable support set is consistent across both backends and E_D32_SFLOAT_S8_UINT no longer diverges (w*8, not w*4).\n' +
      'Read ' + FILES.common + ', ' + FILES.vulkan + ', ' + FILES.d3d12 + ', ' + FILES.spec + '.\n' +
      'Verify: (1) Single-depth E_D16_UNORM/E_D32_SFLOAT are ACCEPTED (passed through, not rejected). (2) Multi-aspect depth-stencil E_D24_UNORM_S8_UINT/E_D32_SFLOAT_S8_UINT are REJECTED via IsDepthStencilFormat && !IsDepthOnlyFormat (in BOTH backends; no divergence). (3) E_D32_SFLOAT_S8_UINT GetImageByteSize = W*H*8 (GetFormatBlockSize=8), so no more 4 vs 8 split. (4) Unknown/compressed formats are rejected via byteSize==0. (5) The reject predicate is identical between Vulkan and D3D12 (a shared gate, not divergent). Report divergences; empty if clean.',
  },
  {
    key: 'subresource-gate',
    prompt:
      'Audit claim (c): subresource narrowing uses correct EMultiSampleCount/e2D comparisons and does NOT break the existing RGBA8 single-mip capture cases.\n' +
      'Read ' + FILES.vulkan + ', ' + FILES.d3d12 + ', ' + FILES.tester + ', ' + FILES.spec + '.\n' +
      'Verify: (1) The gate is desc.mipLevels>1 || desc.layers>1 || desc.samples != EMultiSampleCount::e1 || desc.textureType != ETextureType::e2D in BOTH backends. (2) No use of samples==1 or samples>1 (both are wrong: e2==1 would be misread as non-MSAA, >1 misses e2). (3) All tester capture RTs (CreateCaptureRT, TestReadback offscreen RT, TestReadback multi-mip reject RT) are single-mip/single-layer/e2D/e1 so the normal capture path still passes the gate. (4) The copy region stays and still uses mip0/layer0/layerCount=1. Report any finding; empty if clean.',
  },
  {
    key: 'dst-size-guard',
    prompt:
      'Audit claim (d): the write-before-dst.size() defensive check is correct (no missed OOB, no false-reject of a legal-size dst).\n' +
      'Read ' + FILES.vulkan + ', ' + FILES.d3d12 + '.\n' +
      'Verify: (1) Vulkan VulkanReadbackToken::Wait() checks m_Dst.size() < m_ByteSize, logs, and skips the write (it does NOT silently clamp to m_Dst.size()). (2) It does NOT falsely reject when m_Dst.size() == m_ByteSize (the equal case must write). (3) D3D12 Readback(image) keeps its dst.size() check before the row-by-row memcpy. (4) The direct host-visible buffer paths (Vulkan buffer, D3D12 buffer) also check dst.size() before memcpy. (5) No code path writes into dst without a size check. Report any finding; empty if clean.',
  },
  {
    key: 'contract-honesty',
    prompt:
      'Audit claims (e)+(f): interface contract wording is consistent with the main spec async-declaration semantics; IsCompressedFormat no-op and the depth-stencil downgrade are stated honestly.\n' +
      'Read ' + FILES.header + ', ' + FILES.common + ', ' + FILES.spec + '.\n' +
      'Verify: (1) CRenderBackend.h comments keep the async-declare + explicit-Wait model (this change did NOT rewrite sync semantics; D3D12 stays synchronous, that is unchanged and a separate change covers async). (2) The comment says GetImageByteSize is the sole public source and callers must NOT derive w*h*GetFormatBlockSize. (3) The comment does NOT claim GetImageByteSize non-zero means readable (it should say size computable, readability gated separately). (4) IsCompressedFormat is documented as always-false (no-op) and is not relied on as a reliable precondition. (5) The depth-stencil multi-aspect downgrade is stated as rejected (not silently supported). Report any honesty gap; empty if clean.',
  },
  {
    key: 'evidence-honesty',
    prompt:
      'Audit claim (g): the implementation evidence is honest and the static-vs-runtime verification split is clearly stated.\n' +
      'Read ' + FILES.tasks + ', ' + FILES.tester + '.\n' +
      'Note the actual implementation evidence: Debug build SUCCESSFUL; both backends ran all 8 tests exit 0; TestReadback captured readback_vulkan.png and readback_d3d12.png (non-blank, 262144 bytes); readback/TestComputeBuffer/TestSimpleTriangle/TestTriangleWithConstantColor PNGs were byte-IDENTICAL across vulkan and d3d12; TestReadback validation log was 0 bytes (no readback VUID); a multi-mip reject case printed "[reject] multi-mip readback -> null token OK" on BOTH backends; only multi-mip is runtime-verified (multi-layer/MSAA/3D/depth-stencil rejections are statically verified).\n' +
      'Verify: (1) Is any evidence claim overstated (e.g. claiming all 5 reject categories were runtime-verified)? (2) Is the static-vs-runtime split disclosed? (3) Does the multi-mip reject case in the tester risk breaking the normal capture flow (it only creates+rejects, never renders)? Report any honesty gap; empty if clean.',
  },
]

function verifyPrompt(f) {
  return (
    'Adversarially REFUTE this reported finding about the harden-readback-carrier implementation. Read the referenced file and reason against it.\n' +
    'Finding id: ' + f.id + '\nSeverity: ' + f.severity + '\nFile: ' + (f.file || '(unspecified)') + '\nLine: ' + (f.line || 'n/a') + '\nTitle: ' + f.title + '\nDetail: ' + f.detail + '\n\n' +
    'Default to refuted=true if the claim is uncertain or not reproducible. Set isReal=true ONLY if you independently confirm it is a genuine defect in the code. Give a concrete reason.'
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
  'Synthesize the adversarial-verification results for the harden-readback-carrier implementation. ' +
    'Total findings: ' + (verified.length) + '. Confirmed: ' + confirmed.length + '. ' +
    (confirmed.length
      ? 'Confirmed findings (each with detail):\n' +
        confirmed.map((f) => '- [' + f.severity + '] ' + f.id + ' ' + f.title + ' :: ' + f.detail + '\n').join('')
      : 'No findings survived adversarial refutation.\n') +
    'Produce a verdict (APPROVE/REVISE), a blockers list, a mustfix list, a shouldfix list, and a bottomLine. ' +
    'Note: the change only hardens the readback carrier contract (byte-size single source, format/subresource rejection, dst-size guard). D3D12 remaining synchronous is INTENTIONAL (async is a separate change). ' +
    'If the change is correct and evidence is honest, verdict APPROVE with empty blocker/mustfix lists.',
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

return { confirmed, synth }

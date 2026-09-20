export const meta = {
  name: 'describe-readback-interface',
  description: 'Independently extract the exact readback interface contract and caller usage from the real code, cross-check for consistency, and synthesize the caller-facing guide',
  phases: [
    { title: 'Research', detail: 'four facets read the real interface/backend/caller code' },
    { title: 'Verify', detail: 'cross-check facets for consistency, contradictions, accuracy' },
    { title: 'Synthesize', detail: 'produce the caller-facing contract + usage walkthrough' },
  ],
}

const ROOT = 'E:/Projects/CascadedEngine'
const FILES = {
  header: ROOT + '/Interface/RenderInterface/header/CRenderBackend.h',
  common: ROOT + '/Interface/RenderInterface/header/Common.h',
  vulkan: ROOT + '/VulkanRenderBackendNew/private/RenderBackend_Vulkan.cpp',
  d3d12: ROOT + '/D3D12RenderBackend/private/RenderBackend_D3D12.cpp',
  tester: ROOT + '/Test/GPUBackendTester/Main.cpp',
  spec: ROOT + '/openspec/changes/harden-readback-carrier/specs/resource-readback/spec.md',
}

const RESEARCH_SCHEMA = {
  type: 'object',
  required: ['facts'],
  properties: {
    facts: {
      type: 'array',
      items: {
        type: 'object',
        required: ['key', 'value'],
        properties: {
          key: { type: 'string' },
          value: { type: 'string' },
        },
      },
    },
  },
}

const VERIFY_SCHEMA = {
  type: 'object',
  required: ['consistent', 'contradictions'],
  properties: {
    consistent: { type: 'boolean' },
    contradictions: { type: 'array', items: { type: 'string' } },
  },
}

const SYNTH_SCHEMA = {
  type: 'object',
  required: ['contract', 'usageSteps', 'constraints', 'errorCases', 'notes'],
  properties: {
    contract: { type: 'string' },
    usageSteps: { type: 'array', items: { type: 'string' } },
    constraints: { type: 'array', items: { type: 'string' } },
    errorCases: { type: 'array', items: { type: 'string' } },
    notes: { type: 'string' },
  },
}

const RESEARCHERS = [
  {
    key: 'interface-surface',
    prompt:
      'Extract the EXACT public readback interface from ' + FILES.header + ' (namespace graphics_backend).\n' +
      'Report as {facts:[{key,value}]}: (1) class IReadbackToken and its 3 virtual methods (Wait/IsReady/Reset) with their exact signatures and documented semantics (Wait = only sync point, after which dst is filled; IsReady = non-blocking; Reset = idempotent release). (2) The two CRenderBackend::Readback overloads (image and buffer) exact signatures: return type (unique_ptr<IReadbackToken>), parameters (ImageHandle const&, std::span<uint8_t> dst / BufferHandle const&, std::span<uint8_t> dst). (3) The documented carrier-size rule (GetImageByteSize is the sole public source; callers must NOT derive width*height*GetFormatBlockSize). (4) The documented dst-lifetime obligation (caller keeps the memory dst refers to alive AND its size unchanged until Wait returns; must not resize/release the backing container before Wait). (5) Any documented support constraint (external resources only; image = single-mip/single-layer/non-MSAA/2D; error = null token).',
  },
  {
    key: 'size-and-format-support',
    prompt:
      'Extract the byte-size and format-support contract from ' + FILES.common + ' and ' + FILES.spec + '.\n' +
      'Report as {facts:[{key,value}]}: (1) GetImageByteSize(width,height,format) return semantics: returns width*height*GetFormatBlockSize for which formats; returns 0 for which (unknown/INT/UINT/compressed, "no compact layout"); non-zero means SIZE COMPUTABLE, NOT readable. (2) The readable support set: single-depth E_D16_UNORM/E_D32_SFLOAT accepted; multi-aspect depth-stencil E_D24_UNORM_S8_UINT/E_D32_SFLOAT_S8_UINT rejected; compressed/unknown rejected. (3) GetFormatBlockSize per-format block size for the whitelisted formats (e.g. E_R8G8B8A8_UNORM=4). (4) IsCompressedFormat always false; (5) the readback reject reasons from the spec scenarios. Quote the exact way the size is derived for the common E_R8G8B8A8_UNORM case.',
  },
  {
    key: 'backend-enforcement',
    prompt:
      'Extract exactly how each backend ENFORCES the readback contract, from ' + FILES.vulkan + ' and ' + FILES.d3d12 + ' (Readback(image) and Readback(buffer) functions).\n' +
      'Report as {facts:[{key,value}]}: (1) The exact reject conditions tested and the error log + null-token return (format unsupported via GetImageByteSize==0; multi-aspect depth-stencil via IsDepthStencilFormat && !IsDepthOnlyFormat; subresource gate mipLevels>1 || layers>1 || samples!=EMultiSampleCount::e1 || textureType!=ETextureType::e2D). (2) The dst.size() check timing per backend: Vulkan async = token->Wait() memcpy checks dst.size() BEFORE writing (rejects a shrunken dst, no silent clamp); D3D12 sync = Readback(image) checks dst.size() before the row-by-row memcpy. (3) The SYNC MODEL difference: Vulkan Readback returns a token whose Wait() performs the copy (async); D3D12 Readback performs the copy synchronously inside the call then returns an already-completed token (Wait() is a no-op). (4) The buffer readback byte-size = descriptor.SizeInByte and its dst.size() check. (5) external-resource-only check. (6) subresource 0 / layer0 / mip0 copy region.',
  },
  {
    key: 'caller-example',
    prompt:
      'Extract the actual CALLER usage pattern from ' + FILES.tester + ' (TestReadback, WriteReadbackPNG, CreateCaptureRT, and any buffer readback).\n' +
      'Report as {facts:[{key,value}]}: (1) How the caller creates the readback source: CreateGPUTexture with ETextureFormat::E_R8G8B8A8_UNORM (or a chosen supported format) and ETextureAccessType::eTransferSrc | eTransferDst | eRT, sized W x H. (2) How the caller sizes dst (e.g. std::vector<uint8_t> px(W*H*4) for RGBA8). (3) The exact call sequence: token = backend->Readback(rt, px); if (!token) {error/null}; token->Wait(); then read px bytes; export to PNG via stbi_write_png. (4) How null-token is handled (logged as "Readback returned null token" and the caller bails). (5) Does the caller RESIZE dst only once and keep it alive until Wait (confirm the vector is not resized/reallocated between Readback and Wait). (6) The existing buffer readback example if any.',
  },
]

function verifyPrompt(researchers) {
  return (
    'Cross-check consistency across the four independent research facets about the readback interface, then verify accuracy against the actual code. Facets are JSON:\n' +
    JSON.stringify(researchers) +
    '\n\nCheck: (1) Do all facets agree on the Readback signature / token semantics (Wait=only sync point / IsReady / Reset)? (2) Do they agree on GetImageByteSize returning size-computable (non-zero) vs readable (separate support set), and that E_R8G8B8A8_UNORM = W*H*4? (3) Do they agree on the supported-format set (single-depth accepted, multi-aspect depth-stencil + compressed + unknown rejected) and the subresource gate (mip/layer/samples==e1/textureType==e2D)? (4) Do they agree on the PER-BACKEND sync model (Vulkan async: Wait performs copy; D3D12 sync: copy inside Readback, token pre-completed)? (5) Do they agree on the caller obligations (dst size, alive until Wait, external + eTransferSrc offscreen only) and null-token on reject? (6) Any doc-vs-code contradiction (comments/spec saying one thing, code doing another)?\n' +
    'Return {consistent: bool, contradictions: [string each naming the exact contradiction and which facet/file it is in]}.'
  )
}

phase('Research')
const research = await parallel(
  RESEARCHERS.map((r) => () =>
    agent(r.prompt, { label: 'research:' + r.key, phase: 'Research', schema: RESEARCH_SCHEMA })
  )
)

// Fail loud if a research agent died — an incomplete facet set would produce an incomplete guide.
const okResearch = research.filter(Boolean)
if (okResearch.length !== RESEARCHERS.length) {
  log('describe-readback-interface: only ' + okResearch.length + '/' + RESEARCHERS.length + ' research agents returned; aborting to avoid an incomplete guide')
  return {
    research: okResearch,
    verify: { consistent: false, contradictions: ['incomplete research: ' + okResearch.length + '/' + RESEARCHERS.length + ' agents returned'] },
    synth: null,
  }
}

phase('Verify')
const verify = await agent(
  verifyPrompt(okResearch),
  { label: 'verify-consistency', phase: 'Verify', schema: VERIFY_SCHEMA }
)

phase('Synthesize')
const synth = await agent(
  'You are writing the authoritative caller-facing guide for the readback interface. Use ONLY the verified research + cross-check below (do not invent). The research facets:\n' +
  JSON.stringify(okResearch) +
  '\n\nConsistency cross-check:\n' + JSON.stringify(verify) +
  '\n\nProduce: contract (concise statement of the public contract in plain language), usageSteps (ordered steps a caller follows, with the EXACT function names and the size formula), constraints (the caller obligations + what is rejected), errorCases (what makes Readback return a null token, and what a caller must check), notes (sync-model difference per backend, GetImageByteSize vs GetFormatBlockSize distinction).',
  { label: 'synthesize-guide', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

return { research, verify, synth }

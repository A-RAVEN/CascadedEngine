export const meta = {
  name: 'review-codegen-marker-attribute-design',
  description: 'Adversarial review of the newly-added marker-macro (CA_REFLECT) design + 6b tasks in the engine-codegen-framework change.',
  phases: [
    { title: 'Find', detail: 'Five facets review design soundness / task quality / spec testability / consistency / external facts' },
    { title: 'Verify', detail: 'Adversarially re-check each facet key claims' },
    { title: 'Synthesize', detail: 'Verdict + [AUDIT-] candidates' },
  ],
}

const FIND_SCHEMA = {
  type: 'object',
  properties: {
    facetName: { type: 'string' },
    verdict: { type: 'string', enum: ['OK', 'REVISE', 'MAJOR_ISSUE'] },
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          severity: { type: 'string', enum: ['critical', 'major', 'minor', 'nit'] },
          claim: { type: 'string' },
          file: { type: 'string' },
          why: { type: 'string' },
        },
        required: ['severity', 'claim', 'why'],
      },
    },
    summary: { type: 'string' },
  },
  required: ['facetName', 'verdict', 'findings', 'summary'],
}

const VERDICT_SCHEMA = {
  type: 'object',
  properties: {
    facetName: { type: 'string' },
    checks: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          check: { type: 'string' },
          ok: { type: 'boolean' },
          correction: { type: 'string' },
        },
        required: ['check', 'ok'],
      },
    },
    summary: { type: 'string' },
  },
  required: ['facetName', 'checks', 'summary'],
}

const SYNTH_SCHEMA = {
  type: 'object',
  properties: {
    overall: { type: 'string', enum: ['OK', 'REVISE'] },
    auditCandidates: { type: 'array', items: { type: 'string' }, description: 'Each as [AUDIT-x] tentative title + why' },
    strengths: { type: 'array', items: { type: 'string' } },
    honestNotes: { type: 'array', items: { type: 'string' }, description: 'uncertainties / things NOT yet verified' },
  },
  required: ['overall', 'auditCandidates', 'strengths', 'honestNotes'],
}

const FACETS = [
  {
    name: 'design-soundness',
    prompt: 'Read-only design review. Read E:/Projects/CascadedEngine/openspec/changes/engine-codegen-framework/design.md (focus D1/D2/D5 + Goals/Non-Goals) and proposal.md (What Changes + Non-goals).\n\nThe design decision under review: reflection annotation = marker macro CA_REFLECT(...) (with options) + libclang AST class-body auto-extraction for engine own types; CA_REFLECTION kept as the non-intrusive channel for external library types; NOT using literal [[careflection::...]] or [[clang::annotate]] attributes.\n\nAssess: (1) Is the dual-mode coherent (marker macro discovery + AST extraction vs CA_REFLECTION grep-for-external)? Any internal contradiction beween proposal/design on the two modes or what stays CA_REFLECTION? (2) Is any claim a false premise (e.g. does CA_REFLECTION actually get grepped, does AST extract class body for marked types, is the marker macro truly MSVC-no-op)? (3) Does the design leave an obvious gap that the new tasks (6b1-6b4) fail to cover (open question #5 CA_REFLECT semantics)?\n\nReturn findings. This is a DESIGN review, not code review. Ground in file content.',
  },
  {
    name: 'task-quality-6b',
    prompt: 'Read-only task-quality review. Read E:/Projects/CascadedEngine/openspec/changes/engine-codegen-framework/tasks.md, group "6b. 标记宏 CA_REFLECT + options + 双模式" (tasks 6b.1-6b.4).\n\nEvaluate each 6b task against the project "合格 task 标准" (TODO/004 section 3): (1) single-agent one-session coherent change, not N-in-1; (2) REAL oracle that can FAIL (not exit 0 / code review / build-successfully-unconditional / snapshot-compare-of-nothing); (3) names REAL existing files (CACore/header/CodeGenMeta.h, Tools/CodeGen/CodeGenMain.cpp); (4) no false premise (e.g. does the tool actually grep the macro? does it need libclang attr?); (5) no GLOB verification.\n\nAlso check: does 6b.3 claim "CA_REFLECTION grep produce fields-only" but the tool currently only does AST (CodeGenMain.cpp matching by spelling)? Flag if a task asserts behavior the current tool does not yet do and has no clear step to add it. Return findings.',
  },
  {
    name: 'spec-testability',
    prompt: 'Read-only review of spec testability. Read E:/Projects/CascadedEngine/openspec/changes/engine-codegen-framework/specs/codegen-framework/spec.md, specifically the newly added Requirement "反射标注用标记宏自动爬取 + 外部库走非侵入通道" and its two scenarios.\n\nAssess: (1) Is each scenario actually TESTABLE (a real WHEN/THEN that maps to a concrete runnable check), or is it void/tautological? (2) The scenario "生成类型表字段数 == class 实际成员数" — is that a well-defined oracle, or circular (how do you know "actual members" without the same tool)? (3) Does the requirement contradict the design (e.g. "外部库 CA_REFLECTION" vs design D2)? Return findings.',
  },
  {
    name: 'cross-artifact-consistency',
    prompt: 'Read-only consistency review across FOUR artifacts in E:/Projects/CascadedEngine/openspec/changes/engine-codegen-framework/: proposal.md, design.md, specs/codegen-framework/spec.md, tasks.md.\n\nCheck: (1) Does every artifact agree on the marker-attribute design (marker macro CA_REFLECT + AST, dual-mode, CA_REFLECTION external channel, NOT literal [[...]])? (2) Any leftover OLD claim contradicting it (e.g. still says "延期", "未验证 C5030", or describes CA_CODEGEN_REGISTER as THE discovery final form instead of minimal precursor)? (3) Do the tasks.md oracles reference the design correctly? Point to each file with the offending line if inconsistent. Return findings.',
  },
  {
    name: 'external-facts',
    prompt: 'External-fact verification (use MCP web tools — web_search_prime / webReader — ONLY, never WebFetch/WebSearch). Verify these claims cited in design.md/proposal.md about MSVC + libclang attributes are REAL and accurate:\n1. MSVC warns C5030 for unknown attribute namespaces (cited developercommunity URL). Confirm the URL exists and the claim (warning, not error) matches.\n2. libclang C API has weak/limited support reading custom-namespaced attributes (cited llvm discourse "Getting custom C++ attribute specifiers from AST"). Confirm.\n3. [[clang::annotate]] sometimes not visible on ClassDecl in AST (cited llvm issue 87813). Confirm the issue is real and the claim matches.\n4. Unreal uses UCLASS()/UPROPERTY() macros + a custom header tool (UnrealHeaderTool) rather than clang attributes for reflection — the "macro + AST/own parser" precedent. Confirm broadly.\n\nFor each: does the URL resolve, does the source say what we claim, and is the claim correctly attributed? Flag any hallucinated/misattributed URL or overstated claim. Return checks.',
  },
]

phase('Find')
log('Fanning out five review facets...')
const find = await parallel(FACETS.map((f) => () =>
  agent(f.prompt + '\n\nReturn structured findings.', { label: 'find:' + f.name, phase: 'Find', schema: FIND_SCHEMA })
))

phase('Verify')
log('Adversarially re-verifying each facet key claims...')
const verify = await parallel(find.map((fr) => () => {
  if (!fr || !fr.findings || fr.findings.length === 0) return Promise.resolve({ facetName: fr?.facetName || 'unknown', checks: [], summary: 'No findings to verify.' })
  const list = fr.findings.filter((x) => x.severity === 'critical' || x.severity === 'major')
    .map((x) => '- ' + x.claim + (x.file ? ' (at ' + x.file + ')' : '') + ' [' + x.severity + '] ' + x.why).join('\n')
  const prompt = 'You are an adversarial verifier for a DESIGN review. For each major/critical finding below, independently RE-READ the actual artifact at its file path (or re-check the cited web claim by re-fetching its URL) and say whether it holds. Do not trust the finding. For web-based claims, use MCP web tools (web_search_prime / webReader ONLY, never WebFetch/WebSearch) to re-fetch.\n\nFacet: ' + fr.facetName + '\nFindings to verify:\n' + (list || '(none major)') + '\n\nReturn structured checks.'
  return agent(prompt, { label: 'verify:' + (fr.facetName || 'x').slice(0, 24), phase: 'Verify', schema: VERDICT_SCHEMA })
}))

phase('Synthesize')
log('Collating verdict + AUDIT candidates...')
const synth = await agent(
  'You synthesize a design-review verdict for the newly-added marker-attribute (CA_REFLECT) design + 6b tasks in the engine-codegen-framework change. Below are five facets and their verification.\n\n' + JSON.stringify(verify).slice(0, 55000) +
  '\n\nProduce SYNTH_SCHEMA. overall = OK if the design+tasks are sound and implementable as written; REVISE if something materially wrong. auditCandidates = concrete [AUDIT-x] titles for any real defect found (only include genuine ones, dedupe by root cause). honestNotes = things genuinely NOT yet verified (e.g. no real 6b implementation exists yet, marker-option grep not yet coded).',
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

return { find, verify, synth }

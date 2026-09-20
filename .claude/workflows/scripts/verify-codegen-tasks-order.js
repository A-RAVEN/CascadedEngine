// verify-codegen-tasks-order.js — ROUND 3 (final ≤3-limit confirm).
// Round 1 found RC-A..RC-E (fixed). Round 2 confirmed A/B/E fixed, but found RC-F (4.2/REQ#5
// target_sources-vs-GLOB mismatch for header-only generated artifacts), RC-G (empty-whitelist vs
// 4.2-missing-source tension), RC-H (spec Scenario SHALL vs 7.2 oracle mismatch). Round-3 verifies
// these 3 fixes and that the spec edits (REQ#5 + Scenario) did not contradict other spec scenarios or
// regress the ordering. Prompts differ from rounds 1/2 to avoid cache replay.

export const meta = {
  name: 'verify-codegen-tasks-order-r3',
  description: 'Round-3 final confirm: RC-F/G/H fixes hold; spec edits (REQ#5 + Scenario) introduce no contradiction',
  phases: [
    { title: 'Audit', detail: '5 finders: RC-F fix, RC-G fix, RC-H/spec-consistency, ordering regression, spec-internal-consistency' },
    { title: 'Verify', detail: 'adversarially confirm/refute each found issue' },
  ],
}

const TASKS  = 'openspec/changes/engine-codegen-framework/tasks.md'
const SPEC   = 'openspec/changes/engine-codegen-framework/specs/codegen-framework/spec.md'
const STD    = 'TODO/004-codegen-proposal-decomposition.md'
const DESIGN = 'openspec/changes/engine-codegen-framework/design.md'

const FINDINGS = {
  type: 'object',
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          summary:  { type: 'string' },
          evidence: { type: 'string' },
          severity: { enum: ['high', 'medium', 'low'] },
        },
        required: ['summary', 'evidence', 'severity'],
      },
    },
  },
  required: ['findings'],
}

const VERDICT = {
  type: 'object',
  properties: {
    isReal:    { type: 'boolean' },
    reasoning: { type: 'string' },
  },
  required: ['isReal', 'reasoning'],
}

const base = `ROUND-3 final review (within the ≤3 round limit). First READ: ${SPEC}, ${TASKS}, ${STD} (§3), ${DESIGN}. Context: Round-2 confirmed RC-A/B/E fixed but surfaced 3 new issues, since FIXED: (RC-F) 4.2 + spec REQ#5 were reframed to the truth that this change generates HEADER-only artifacts — headers added via explicit include path + #include (missing → compile error), .cpp (if any future emitter) via explicit target_sources (no GLOB *.cpp); (RC-G) empty-whitelist is now clarified as a separate "no-annotated-types project" config (nothing wired) vs a wired project whose missing source is an error — mutually exclusive configs; (RC-H) spec Scenario "关键路径行为不变" THEN was amended to a self-consistent falsifiable form (build success + no libclang.dll + headless vulkan/d3d12 exit0/pass), matching task 7.2. Assess the FIXED state STRICTLY against the actual spec/task text.`

phase('Audit')

const DIMS = [
  {
    key: 'rcf-header-cpp',
    prompt: `${base}
DIMENSION: verify RC-F fix. Decide whether spec REQ#5 (${SPEC}, "生成源码以显式源清单纳入构建" + Scenario "缺失生成文件被构建捕获") and task 4.2 (${TASKS}) now REFLECT the header-only reality honestly AND their oracles are RUNNABLE/falsifiable: (a) header case → delete the wired generated header EmitSample.Reflection.h → build error via #include (real, not vacuous); (b) .cpp case → explicit target_sources rule stated for future; (c) no leftover text still claims target_sources applies to a header artifact OR claims a .cpp being verified when none exists. Flag any remaining mismatch. Cite file:line.`,
  },
  {
    key: 'rcg-empty-whitelist',
    prompt: `${base}
DIMENSION: verify RC-G fix. Decide whether ${TASKS} now RECONCILES the empty-whitelist ("EXIT 0, no file, build not blocked", spec 构建期 tool 场景) with 4.2 ("missing wired source → build error") without contradiction: (a) the empty-whitelist case is clearly a separate "no-annotated-types project" config (nothing wired to a source because nothing is generated); (b) the wired project (ImageHandle/BufferHandle registered) is the non-empty-whitelist path; (c) no single build configuration is asked to satisfy both "no file generated + build passes" AND "missing wired source → error". Flag any residual logical tension. Cite file:line.`,
  },
  {
    key: 'rch-spec-consistency',
    prompt: `${base}
DIMENSION: verify RC-H fix. Decide whether the amended spec Scenario "关键路径行为不变" THEN (${SPEC}) is now (a) self-consistent + falsifiable (build success + no libclang.dll + headless vulkan/d3d12 exit0/pass), (b) aligned with task 7.2's oracle, (c) not a silent weakening that drops a spec requirement — rather it honestly reframes to the achievable gate, with the strict "无新增 VUID vs baseline" correctly demoted to an optional strengthening requiring a '提交基线' task. Then check SPEC-INTERNAL consistency: does the amended Scenario or REQ#5 CONTRADICT any other scenario/requirement in ${SPEC} (e.g., the "构建期 tool 由脚本调用" empty-whitelist scenario, or "引擎不链接 libclang")? Flag any contradiction. Cite file:line.`,
  },
  {
    key: 'order-regression',
    prompt: `${base}
DIMENSION: ordering regression after the RC-F/G/H edits. Rebuild the dependency graph from the CURRENT ${TASKS}: (a) is it still acyclic, is 3.3→4.1→4.2→4.3→[6.x]→7.1→7.2→8.1 still a valid topological sort; (b) is 8.1 still genuinely last; (c) did the 4.2/spec edits change any dependency (e.g. 4.2 now depends only on 4.1 + a header, not a .cpp); (d) does the top 执行顺序与依赖声明 block still match the actual task list; (e) no new forward-dep/cycle introduced. Cite file:line.`,
  },
  {
    key: 'atomic-dedup-final',
    prompt: `${base}
DIMENSION: atomicity + dedup final check. Decide (a) every remaining task is still single-coherent-change / atomic (4.1 include-set+empty-whitelist is arguably two things — is it atomic or two separable concerns?); (b) no verification point is double-counted after all edits (negative-gate in 3.3 only; offset-quality in 4.3 only; empty-whitelist in 3.3+4.1; header-missing in 4.2 only); (c) the 遗漏 baseline caveat is honest. Flag any genuine remaining atomicity or duplication defect (at the level already reached, only report things that are REAL and MATERIAL, not wording nits). Cite file:line.`,
  },
]

const results = await pipeline(
  DIMS,
  d => agent(d.prompt, { label: `audit:${d.key}`, phase: 'Audit', schema: FINDINGS }),
  (review, item) => parallel(((review && review.findings) || []).map(f => () =>
    agent(`Adversarially CONFIRM OR REFUTE this finding. Default to refuted (isReal=false) if uncertain or it does not hold up. Independently read ${SPEC} + ${TASKS} to verify.
FINDING: ${f.summary}
EVIDENCE: ${f.evidence}
Return isReal=true ONLY if the issue is real AND material to ordering/compliance; otherwise false, with reasoning.`,
      { label: `verify:${item.key}`, phase: 'Verify', schema: VERDICT })
      .then(v => ({ ...f, dimension: item.key, verdict: v }))
  ))
)

const confirmed = results.flat().filter(Boolean).filter(f => f.verdict && f.verdict.isReal)
return { findings: confirmed, dimensionsReviewed: DIMS.map(d => d.key) }

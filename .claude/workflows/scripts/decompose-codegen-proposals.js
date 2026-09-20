export const meta = {
  name: 'decompose-codegen-proposals',
  description: 'Decompose the engine CodeGen framework engine work (core + reflection + hash/equality + C# binding) into OpenSpec-conformant proposals with single-agent-sized, testable tasks',
  phases: [
    { title: 'Research', detail: 'four agents ground the real work content and the OpenSpec task convention' },
    { title: 'Propose', detail: 'the architect produces an initial proposal + task decomposition' },
    { title: 'Verify', detail: 'adversarially refute every task: is it single-agent sized, testable, OpenSpec-conformant' },
    { title: 'Synthesize', detail: 'final structure, flag oversized/untestable tasks, bottom line' },
  ],
}

const ROOT = 'E:/Projects/CascadedEngine'
const FILES = {
  todo1: ROOT + '/TODO/001-csharp-scripting-framework.md',
  todo2: ROOT + '/TODO/002-reflection-via-codegen.md',
  todo3: ROOT + '/TODO/003-hash-equality-via-codegen.md',
  reflection: ROOT + '/CACore/header/Reflection.h',
  hasher: ROOT + '/CACore/header/Hasher.h',
  backend: ROOT + '/Interface/RenderInterface/header/CRenderBackend.h',
  buildpy: ROOT + '/build.py',
  archivedTasks: ROOT + '/openspec/changes/archive/2026-09-02-fix-vulkan-dedup-key-identity/tasks.md',
  archiveProposal: ROOT + '/openspec/changes/archive/2026-09-02-fix-vulkan-dedup-key-identity/proposal.md',
}

const RESEARCH_SCHEMA = {
  type: 'object',
  required: ['facts'],
  properties: { facts: { type: 'array', items: { type: 'object', required: ['key', 'value'], properties: { key: { type: 'string' }, value: { type: 'string' } } } } },
}

const RESEARCHERS = [
  {
    key: 'work-content-core',
    prompt:
      'You are grounding the ACTUAL implementation work of the CascadedEngine "unified CodeGen framework". Read these three design TODO docs:\n' +
      FILES.todo1 + '\n' + FILES.todo2 + '\n' + FILES.todo3 + '\n' +
      'Also read the real code it must integrate with: ' + FILES.reflection + ' and ' + FILES.hasher + ' and ' + FILES.backend + ' and ' + FILES.buildpy + '\n' +
      'Extract the CONCRETE deliverables as {facts:[{key,value}]}, grouped by: (1) the CORE infrastructure (parser source of truth, the shared type-table representation, the attribute/marker mechanism, the build.py/CMake integration hook, the ABI-schema/consistency guard). For EACH, state what is built, what file it lives in, and what inputs/outputs it has. (2) The EMITTERS: reflection (TypeDescriptor for a type), hash+value-equality (custom_hash_trait + operator==), serialization, and C# binding (C-ABI function-pointer table + C# wrapper). For each emitter state what it consumes from the core and what a GEN artifact (Gen.X.cpp/.h/.cs) contains. (3) Anything that already exists and is reused (the reserved custom_hash_trait slot, the CA_PRIVATE_REFLECTION seed, the visit_members path). Do NOT invent; cite file:line where relevant.',
  },
  {
    key: 'work-content-binding-risk',
    prompt:
      'You are grounding the C# binding + runtime work (the biggest proposal). Read ' + FILES.todo1 + ' (especially the roadmap section) and the real engine loop in ' + ROOT + '/Test/GPUBackendTester/Main.cpp (the UpdateSystem->NewScheduler->ExecuteGraph->WaitIdle loop).\n' +
      'Extract the CONCRETE implementation milestones and their risk as {facts:[{key,value}]}: (1) hostfxr/nethost CoreCLR hosting bootstrap (the exact sequence and where in the C++ host it lives); (2) the C-ABI thunk table generator + the C# wrapper generator; (3) the one engine surface to prove it on FIRST (the roadmap recommends an IMGUI editor window callback); (4) threading/ownership rules (SafeHandle vs borrow, GCHandle, pin, GC.KeepAlive) as concrete verifiable constraints; (5) hot-reload via a collectible AssemblyLoadContext. For each milestone give the VERIFIABLE outcome that proves it works. Be honest about which milestones are the riskiest and why.',
  },
  {
    key: 'openspec-convention',
    prompt:
      'You are grounding the OpenSpec task/decomposition CONVENTION. Read this archived change tasks file:\n' + FILES.archivedTasks + '\nand its proposal: ' + FILES.archiveProposal + '\n' +
      'Also list and skim a few more archived tasks files if quick (glob openspec/changes/archive/0000-*/tasks.md) to see the range.\n' +
      'Extract as {facts:[{key,value}]} the conventions that a NEW series of proposals must follow: (1) how a proposal is scoped to a capability/change vs the delta spec(s) it writes; (2) typical task granularity and numbering (x.y sections, one task = one checkbox, how big a section is); (3) what "verifiable" looks like in these tasks (build via python build.py, headless run, exit code, PNG/byte compare, validation-log/VUID checks, a hard assert vs a soft log); (4) the mandatory Review & Adversarial Verify closure task and the [AUDIT-] loop; (5) how tasks reference API doc URLs or MCP verification. Give concrete examples of a well-sized task vs an oversized one from the files you read.',
  },
]

const DECOMP_SCHEMA = {
  type: 'object',
  required: ['proposals', 'rationale'],
  properties: {
    proposals: {
      type: 'array',
      items: {
        type: 'object',
        required: ['name', 'scope', 'summary', 'tasks'],
        properties: {
          name: { type: 'string' },
          scope: { type: 'string' },
          summary: { type: 'string' },
          tasks: {
            type: 'array',
            items: {
              type: 'object',
              required: ['id', 'desc', 'acceptance', 'test'],
              properties: {
                id: { type: 'string' },
                desc: { type: 'string' },
                acceptance: { type: 'string' },
                test: { type: 'string' },
              },
            },
          },
        },
      },
    },
    rationale: { type: 'array', items: { type: 'string' } },
  },
}

const VERIFY_SCHEMA = {
  type: 'object',
  required: ['sizeOk', 'testable', 'openSpecOk', 'issues', 'suggestion'],
  properties: {
    sizeOk: { type: 'boolean' },
    testable: { type: 'boolean' },
    openSpecOk: { type: 'boolean' },
    issues: { type: 'array', items: { type: 'string' } },
    suggestion: { type: 'string' },
  },
}

const SYNTH_SCHEMA = {
  type: 'object',
  required: ['structure', 'tooBig', 'untestable', 'nonconformant', 'bottomLine', 'nextStep'],
  properties: {
    structure: {
      type: 'array',
      items: {
        type: 'object',
        required: ['phase', 'proposal', 'scope', 'taskCount'],
        properties: { phase: { type: 'string' }, proposal: { type: 'string' }, scope: { type: 'string' }, taskCount: { type: 'number' } },
      },
    },
    tooBig: { type: 'array', items: { type: 'string' } },
    untestable: { type: 'array', items: { type: 'string' } },
    nonconformant: { type: 'array', items: { type: 'string' } },
    bottomLine: { type: 'string' },
    nextStep: { type: 'string' },
  },
}

phase('Research')
const research = await parallel(
  RESEARCHERS.map((r) => () =>
    agent(r.prompt, { label: 'research:' + r.key, phase: 'Research', schema: RESEARCH_SCHEMA })
  )
)
const okResearch = research.filter(Boolean)
if (okResearch.length !== RESEARCHERS.length) {
  log('decompose-codegen-proposals: only ' + okResearch.length + '/' + RESEARCHERS.length + ' research agents returned; aborting')
  return { research: okResearch, proposal: null, verify: [], synth: null, aborted: true }
}

phase('Propose')
const propResult = await agent(
  'You are the primary architect decomposing the CascadedEngine CodeGen framework work into OpenSpec proposals where EVERY task can be completed by a single agent in one execution and has an explicit acceptance criterion + a concrete test.\n' +
  'Ground your decomposition on this research (real engine files + the 3 TODO docs + OpenSpec convention):\n' +
  JSON.stringify(okResearch) +
  '\n\nRequirements for your decomposition:\n' +
  '(1) A DEPENDENCY-ORDERED sequence of PROPOSALS. The shared "core" (parser + type-table + attribute mechanism + build.py integration + ABI-schema guard) is built ONCE inside the FIRST proposal that needs it (do NOT re-derive the parser per proposal).\n' +
  '(2) Each proposal = a capability a developer can review and ship independently, sized so a single REVIEW is tractable and a single VERIFICATION (build + test) is meaningful.\n' +
  '(3) Every task inside a proposal is: a single unit a sub-agent can finish in one run WITHOUT a sprawling multi-file blob; it names the files/functions it touches; it has an ACCEPTANCE (what "done" means, observable) and a TEST (build.py build, a headless run, a unit assertion, a byte/PNG compare, a validation-log/VUID check, a hard assert, a hash/oracle comparison, a "generates expected code" snapshot test).\n' +
  '(4) The C# binding + CoreCLR/AOT runtime is the RISKIEST & BIGGEST -> it comes LAST and is itself split into ordered milestones (bootstrap, thunk generator, one first surface spike, threading/ownership, hot-reload) each independently verifiable. The reflection and hash/equality emitters are smaller and come FIRST to prove the shared core on cheap real deliverables.\n' +
  '(5) Every proposal ends with the mandatory Review & Adversarial Verify closure task (per project convention), and the serialization emitter is either a task inside reflection or its own small proposal - choose the smallest defensible scope.\n' +
  'Output structured proposals with tasks; each task: {id, desc (concrete, names files), acceptance (observable done), test (the concrete verification incl. what command or assertion)}. Include a recommendations list via rationale.',
  { label: 'propose-decomposition', phase: 'Propose', schema: DECOMP_SCHEMA }
)
const props = (propResult && propResult.proposals) || []
const allTasks = props.flatMap((p) => p.tasks.map((t) => ({ pname: p.name, ...t })))

// Fail loud if the architect returned nothing decomposable.
if (!allTasks.length) {
  log('decompose-codegen-proposals: architect returned 0 tasks; aborting')
  return { research: okResearch, proposal: propResult, verify: [], synth: null, aborted: true }
}

phase('Verify')
const verified = await parallel(
  allTasks.map((t) => () =>
    agent(
      'Adversarially REFUTE the OpenSpec-conformant sizes of this task in the CascadedEngine CodeGen framework decomposition. Default to a FAILURE (the relevant boolean = false) if the task is uncertain.\n' +
      'Task: ' + t.desc + '\nProposal: ' + t.pname + '\nAcceptance: ' + t.acceptance + '\nTest: ' + t.test + '\n\n' +
      'Check three things independently and return {sizeOk, testable, openSpecOk, issues[], suggestion}:\n' +
      '(a) sizeOk: can a SINGLE sub-agent finish this in ONE execution (one coherent file/fn change, no sprawling multi-file mega-blob)? If too big, set sizeOk=false and suggest HOW to split it (default to 2-4 smaller tasks).\n' +
      '(b) testable: does it have a CONCRETE acceptance + a test you could actually run/assert (build.py, a unit test, a snapshot-of-generated-code check, a headless run, a hash/equality oracle test, an API doc URL check, a hard assert)? If it is a vage "implement X" with no observable done, set testable=false and state the missing test.\n' +
      '(c) openSpecOk: is it inside ONE proposal boundary, does it fit the spec-driven schema, and does the set of tasks end with a Review & Adversarial Verify closure task? Flag non-conformant boundaries.\n' +
      'If the task test references any EXTERNAL API or documentation URL, verify it with the MCP web tools (web-reader/webReader or web-search-prime/web_search_prime); NEVER use WebFetch/WebSearch (project rule).\n' +
      'Be terse and concrete: every issue should name the exact failure and the exact fix.',
      { label: 'verify-task', phase: 'Verify', schema: VERIFY_SCHEMA }
    ).then((v) => ({ ...t, verdict: v }))
  )
)
const verifiedOk = verified.filter(Boolean)

phase('Synthesize')
const synth = await agent(
  'Synthesize the OpenSpec decomposition of the CascadedEngine CodeGen framework. Ground on the architect proposals and the per-task adversarial verdicts.\n' +
  'ARCHITECT PROPOSALS:\n' + JSON.stringify(props) + '\n\n' +
  'PER-TASK VERDICTS (sizeOk/testable/openSpecOk):\n' + JSON.stringify(verifiedOk) + '\n\n' +
  'Produce output:\n' +
  '- structure: ordered array of {phase, proposal, scope, taskCount} - the final proposal sequence.\n' +
  '- tooBig: array of task labels that failed sizeOk with the split suggestion ("task X in proposal Y -> split into A, B, C").\n' +
  '- untestable: array of task labels that failed testable with the missing test named ("task X -> needs a snapshot-of-generated-code assertion").\n' +
  '- nonconformant: array of proposal/scope boundary problems to fix before writing artifacts.\n' +
  '- bottomLine: how many proposals, total tasks, and a one-line takeaway on whether the granularity fits single-agent-run. Mark it APPROVE if no task needs splitting and all are testable-and-conformant; otherwise REVISE and state exactly what to fix.\n' +
  '- nextStep: the exact next action for the user (which proposal to /opsx:propose first, and what design decision must be resolved before writing it).',
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

return { research: okResearch, proposal: propResult, verify: verifiedOk, synth }

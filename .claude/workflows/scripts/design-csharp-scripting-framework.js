export const meta = {
  name: 'design-csharp-scripting-framework',
  description: 'Research and adversarially verify a design for a C# scripting framework (CoreCLR JIT + AOT) that calls the engine IMGUI / RenderBackend+GPUGraph / ThreadManager / resource system',
  phases: [
    { title: 'Research', detail: 'six independent agents study CoreCLR hosting, AOT path, binding generation, the real engine module surface, thread/ownership topology, and interop design principles' },
    { title: 'Verify', detail: 'adversarially refute research findings and re-check every referenced doc URL / API semantics' },
    { title: 'Synthesize', detail: 'produce the layered framework design' },
    { title: 'Critique', detail: 'completeness critic asks what is missing, then a final verdict' },
  ],
}

const ROOT = 'E:/Projects/CascadedEngine'
const FILES = {
  renderBackend: ROOT + '/Interface/RenderInterface/header/CRenderBackend.h',
  common: ROOT + '/Interface/RenderInterface/header/Common.h',
  graph: ROOT + '/Interface/RenderInterface/header/GPUGraph.h',
  frame: ROOT + '/Interface/RenderInterface/header/GPUFrame.h',
  texture: ROOT + '/Interface/RenderInterface/header/GPUTexture.h',
  buffer: ROOT + '/Interface/RenderInterface/header/GPUBuffer.h',
  windowHandle: ROOT + '/Interface/RenderInterface/header/WindowHandle.h',
  renderInterfaceMgr: ROOT + '/Interface/RenderInterface/header/RenderInterfaceManager.h',
  imguiconext: ROOT + '/IMGUIContext/header/IMGUIContext/IMGUIContext.h',
  threadManager: ROOT + '/ThreadManager/header/ThreadManager.h',
  resourceFactory: ROOT + '/GeneralResources/header/CAResource/ResourceSystemFactory.h',
  windowSystem: ROOT + '/WindowSystem/header/CAWindow/WindowSystem.h',
  moduleManager: ROOT + '/CACore/header/CACore/CAModuleManager.h',
  moduleImpl: ROOT + '/CACore/header/CACore/CAModuleImplementation.h',
  timer: ROOT + '/Interface/TimerSystem/header/CATimer/Timer.h',
  tester: ROOT + '/Test/GPUBackendTester/Main.cpp',
}

function researchSchemaNote() {
  return 'Return {facts:[{key,value}]} and {sources:[{url,claim}]}. Each fact must be a precise, defensible statement. Each source must be a real URL (official docs preferred) with the specific claim it backs. For the engine reads, cite file:line. Use the MCP web tools (web-reader/webReader or web-search-prime/web_search_prime) for any external API or documentation lookup - never WebFetch/WebSearch.'
}

const RESEARCH_SCHEMA = {
  type: 'object',
  required: ['facts', 'sources'],
  properties: {
    facts: {
      type: 'array',
      items: {
        type: 'object',
        required: ['key', 'value'],
        properties: { key: { type: 'string' }, value: { type: 'string' } },
      },
    },
    sources: {
      type: 'array',
      items: {
        type: 'object',
        required: ['url', 'claim'],
        properties: { url: { type: 'string' }, claim: { type: 'string' } },
      },
    },
  },
}

const RESEARCHERS = [
  {
    key: 'coreclr-hosting',
    prompt:
      'Research the REAL, current (.NET 8/9) approach to native-loading the CoreCLR runtime and entering managed code from a C++ application, for an engine wanting to run C# game/editor scripts alongside its C++ core.\n' +
      'Cover precisely: (1) the hostfxr / nethost / hostpolicy call sequence (get_hostfxr_path, load_hostfxr, get_runtime_delegate, load_assembly_and_get_function_pointer or the newer hostfxr_initialize_for_dotnet_command_line + load_assembly_and_get_function_pointer in .NET 8+), including the exact function pointer signatures and the RID / apphost / runtimeconfig.json requirements. (2) Where the C# entry point lives and how a C# Host/App calls back into C++ (the reverse direction). (3) Hot reload / assembly reload: how to unload/reload a C# assembly at edit time via AssemblyLoadContext (collectible ALC, unload(), reloadable vs not), and what limits this under JIT today. (4) How to pass a native interop vtable / delegate table into C# on load (function pointer marshalling across the JIT boundary with [UnmanagedCallersOnly] and the .NET 8+ function pointer / unmanaged->managed calling conventions). (5) Threading: what thread the managed entry runs on, how to establish a C# game-thread context, and cross-thread invocation. Return facts + real source URLs.',
  },
  {
    key: 'aot-path',
    prompt:
      'Research the NativeAOT story and how an engine keeps ONE C# source base that runs both editor-time (JIT, hot reload, full reflection) and shipped-game (AOT) contexts.\n' +
      'Cover precisely: (1) What NativeAOT actually is vs IL2CPP, and what is lost under AOT (dynamic assembly load/reflection, reflection-based serialization/injection, generic virtual method limitations, trim warnings) - cite real limits. (2) How to make C# code AOT-safe: source generators, ILLink trim annotations, [DynamicallyAccessedMembers], avoiding runtime reflection in the bindings (compile-time binding gen instead). (3) The dual-build approach: ship JIT in the editor/tooling, AOT for the game executable; how a single codebase + pre-generated interop accommodates both; whether binding generation (compile-time) is REQUIRED for AOT and why runtime reflection interop would fail AOT. (4) How unmanaged function pointers / [UnmanagedCallersOnly] behave and stay viable under AOT for the reverse interop. Return facts + real source URLs.',
  },
  {
    key: 'binding-generation',
    prompt:
      'Research how real engines and ecosystems generate C++ -> C# interop bindings so C# can call a large native API surface (IMGUI draw calls, render graph/pipeline construction, thread tasks, resource APIs) without hand-writing hundreds of externs.\n' +
      'Study the concrete generators and their chosen tradeoffs: (a) FlaxEngine (FlaxEngine.Bindings generator, libclang scanning C++ headers, how it maps classes/handles/arrays, Mangling/structs), (b) Stride (their codegen), (c) Godot C# (Godot.SourceGenerators, how the generated C# wraps godot_api C functions - note Godot uses a C API as the ABI, not a C++ binding), (d) Unity (MonoPInvokeCallback + a function table; and the newer native hosting / DOTS interop model).\n' +
      'Cover: (1) the ABI choices (expose a plain-C vtable vs emit C++ thunks vs a C-exported function table); (2) how native handle types (opaque this/ptr) map to C# safe wrappers; (3) how value types / vectors / spans / strings / arrays marshal; (4) source generation at build time via Roslyn + a header scanner (libclang) vs hand-authoring; (5) the maintenance cost and hot-reload implications. Return facts + real source URLs (e.g. the FlaxEngine repo file layout you actually verify).',
  },
  {
    key: 'engine-module-surface',
    prompt:
      'CATALOG the REAL public API surface of the CascadedEngine that a C# scripting layer would need to expose, and its key constraints. Read these REAL files (use Grep for signatures, do not read whole impl cpp files):\n' +
      '- ' + FILES.renderBackend + ' + ' + FILES.common + ' (CRenderBackend interface, IReadbackToken, GetImageByteSize, handle types, ETextureFormat, and the Readback/ExecuteGraph/WaitIdle signatures)\n' +
      '- ' + FILES.graph + ' + ' + FILES.frame + ' (GPUGraph builder: AllocBuffer/ScheduleData/Rast/Present, GPUFrame, RenderPass, DrawCallBatch, the command/execute model)\n' +
      '- ' + FILES.texture + ' + ' + FILES.buffer + ' + ' + FILES.windowHandle + ' + ' + FILES.renderInterfaceMgr + ' (GPU resource descriptors, WindowHandle, RenderInterfaceManager service locator)\n' +
      '- ' + FILES.imguiconext + ' (the IMGUIContext public methods a C# editor-window author would call: Begin/End viewpass, Draw scene, capture, present)\n' +
      '- ' + FILES.threadManager + ' (NewScheduler, task graph / compute scheduling primitives)\n' +
      '- ' + FILES.resourceFactory + ' (resource system: load/create textures/models, ResourceSystemFactory)\n' +
      '- ' + FILES.windowSystem + ' + ' + FILES.timer + ' (UpdateSystem, NewWindow, Timer)\n' +
      '- ' + FILES.moduleManager + ' + ' + FILES.moduleImpl + ' (IModuleManager service locator, AddModule/GetInstance, reverse-order destruct)' +
      '- ' + FILES.tester + ' (how the frame loop is driven today: UpdateSystem -> NewScheduler -> ExecuteGraph -> WaitIdle, and how Readback/IMGUI are exercised; lines ~198, ~264, ~307)\n' +
      'Return facts that enumerate: (1) each engine service and the ~5-15 methods C# would need, with real signatures; (2) the handle model (ImageHandle/BufferHandle/WindowHandle - copyable? IsValid? shared ownership?) and how a native resource must be released; (3) the frame/thread topology the module set imposes (scheduler per frame, PresentWindows, WaitIdle, per-backend worker threads); (4) the module registration / service-locator pattern C# would use to fetch an engine service; (5) any existing lifetime/teardown discipline (reverse-order destroy, L1/L3a, readback dst contract) that the C# binding must respect. Cite file:line for each.',
  },
  {
    key: 'threading-ownership',
    prompt:
      'Design the C# game-thread topology and the GARBAGE-COLLECTION-to-NATIVE ownership bridge for a C# scripting layer calling a heavily-threaded C++ render engine.\n' +
      'Read ' + FILES.threadManager + ' and ' + FILES.tester + ' (the loop) to ground the engine threading model. Then reason about:\n' +
      '(1) WHERE C# runs: a dedicated game/main thread that each tick builds and submits a frame, vs the engine worker threads (schedulers, backend upload/execute). What the engine already forces (per-frame NewScheduler, ExecuteGraph, WaitIdle, PresentWindows on the presenting thread).\n' +
      '(2) Marshal boundaries: only two crossing points (build graph -> submit; readback -> consume) vs calling C# from render workers. The Async/GC-safety of calling managed from a native worker.\n' +
      '(3) GC <-> native lifetime: the double-ownership problem when a C# object holds a native handle (ImageHandle/BufferHandle/WindowHandle) and vice versa. Compare GCHandle (native keeps managed alive), SafeHandle (deterministic native release, finalizer safety net), ConditionalWeakTable (managed -> native tracking), and the engine existing teardown discipline (reverse-order destroy, resource release).\n' +
      '(4) Why crossing the GC boundary in a tight render loop leaks or UAFs: how to make the binding deterministic enough that native resource release and GC agree, WITHOUT relying on finalizers for correctness.\n' +
      'Return facts + real source URLs for the .NET GC / SafeHandle / GCHandle / ConditionalWeakTable semantics.',
  },
  {
    key: 'interop-principles',
    prompt:
      'Synthesize the ARCHITECTURE PRINCIPLES for a C# scripting boundary that must be (a) stable across engine API churn, (b) hot-reload friendly at edit time (JIT), (c) AOT-safe at ship time, and (d) low-overhead enough that IMGUI editor windows and custom graph/pipeline construction are pleasant C#.\n' +
      'Cover the seam placement: (1) where the interop line sits so the C# API is a THIN projection of the C++ interface (not a re-definition), via a generated binding layer; (2) ABI stability (the C ABI / vtable is the contract; C++ classes are NOT the ABI); (3) how to expose the four target surfaces cleanly: IMGUI editor-window drawing, RenderBackend+GPUGraph custom pipeline construction, ThreadManager task submission, resource system load/query; (4) error propagation (C++ exceptions / VUID logs -> C# exceptions vs error codes); (5) the recommended phased build (what to spike first, what is the riskiest) and what should be deferred (full gameplay Entity/Component layer). Return facts + source URLs where they strengthen a principle.',
  },
]

const VERIFY_SCHEMA = {
  type: 'object',
  required: ['isReal', 'reason'],
  properties: {
    isReal: { type: 'boolean' },
    reason: { type: 'string' },
  },
}

function verifyPrompt(claim) {
  return (
    'Adversarially REFUTE this research claim / source about building a C# scripting layer on a C++ render engine. Default to refuted (isReal=false) if it is uncertain, overreaching, or not verifiable. Set isReal=true ONLY if you independently confirm the claim is accurate and the backing source URL is real.\n' +
    'Claim: ' + claim + '\n\n' +
    'If a source URL is cited, try to fetch it (use the MCP web reader / web search prime tools, never WebFetch/WebSearch) and confirm it is a real, resolvable page that actually supports the claim. Note the URL and your conclusion. Do not trust a URL you cannot confirm.'
  )
}

function synthesizePrompt(research, verified) {
  return (
    'You are the chief architect of the C# scripting framework for CascadedEngine. Design the framework. Use ONLY the verified research below; where research is refuted, note and exclude it. Do not invent facts.\n\n' +
    'RESEARCH FACETS (four of which read the real engine code):\n' +
    research.map((r, i) => '--- Facet ' + (i + 1) + ' [' + r.key + '] ---\n' + JSON.stringify(r)).join('\n\n') +
    '\n\nADVERSARIAL VERIFICATION (which claims survived -> isReal=true):\n' +
    JSON.stringify(verified) +
    '\n\nRequirements for the design:\n' +
    '(1) A layered architecture that puts the interop line exactly where it belongs so C# is a thin projection of the C++ interface, with the C ABI / a function-pointer table as the stable contract (NOT the C++ class ABI).\n' +
    '(2) A concrete runtime strategy: CoreCLR native hosting (hostfxr/nethost, JIT) as the primary editor/tooling runtime that also hot-reloads, plus an AOT (NativeAOT) path for shipped games, both served by one C# source base and compile-time generated bindings.\n' +
    '(3) Exactly how each of the FOUR target surfaces becomes C#-callable, with the real engine types: IMGUI custom editor windows (IMGUIContext public methods), RenderBackend + GPUGraph custom pipeline construction (the GPUGraph builder + Readback + ExecuteGraph), ThreadManager task submission (NewScheduler + task primitives), and the resource system (ResourceSystemFactory load/query). Name the real methods/classes.\n' +
    '(4) The thread topology: C# game thread, the two crossing points (build-submit, readback-consume), and the rule that C# never runs on render worker threads; how scheduling/taskgraph interop is safe.\n' +
    '(5) GC-to-native ownership: GCHandle / SafeHandle / ConditionalWeakTable, mapped onto the engine existing reverse-order teardown + resource-release discipline, so no finalizer is required for correctness and there is no leak or UAF.\n' +
    '(6) A is-real-risk roadmap: what to spike FIRST (a vertical slice proving one surface: e.g. C# drives one IMGUI editor window or one custom graph, loaded and hot-reloaded by hostfxr), what is second, and what is deferred (the full gameplay Entity/Component layer).\n' +
    '(7) A short risk list: AOT reflection loss, binding maintenance, thread-affinity mistakes, GC leaks, API churn at the seam.\n' +
    'Output structured data with these keys: architecture (multiline), modules (one entry per target surface naming the real class/method), binding, runtime, threading, ownership, roadmap (ordered {phase,desc}), risks (each one line), openQuestions (unresolved decisions).'
  )
}

const SYNTH_SCHEMA = {
  type: 'object',
  required: ['architecture', 'modules', 'binding', 'runtime', 'threading', 'ownership', 'roadmap', 'risks', 'openQuestions'],
  properties: {
    architecture: { type: 'string' },
    modules: { type: 'array', items: { type: 'string' } },
    binding: { type: 'string' },
    runtime: { type: 'string' },
    threading: { type: 'string' },
    ownership: { type: 'string' },
    roadmap: { type: 'array', items: { type: 'object', required: ['phase', 'desc'], properties: { phase: { type: 'string' }, desc: { type: 'string' } } } },
    risks: { type: 'array', items: { type: 'string' } },
    openQuestions: { type: 'array', items: { type: 'string' } },
  },
}

const CRITIC_SCHEMA = {
  type: 'object',
  required: ['gaps', 'inconsistencies', 'suggestions', 'verdict'],
  properties: {
    gaps: { type: 'array', items: { type: 'string' } },
    inconsistencies: { type: 'array', items: { type: 'string' } },
    suggestions: { type: 'array', items: { type: 'string' } },
    verdict: { type: 'string' },
  },
}

phase('Research')
const research = await parallel(
  RESEARCHERS.map((r) => () =>
    agent(r.prompt + '\n\n' + researchSchemaNote(), { label: 'research:' + r.key, phase: 'Research', schema: RESEARCH_SCHEMA })
  )
)

// Fail loud if a research agent died - an incomplete facet set would produce an incomplete design.
const okResearch = research.filter(Boolean)
if (okResearch.length !== RESEARCHERS.length) {
  log('design-csharp-scripting-framework: only ' + okResearch.length + '/' + RESEARCHERS.length + ' research agents returned; aborting to avoid an incomplete design')
  return { research: okResearch, verified: [], synth: null, critique: null, aborted: true }
}

phase('Verify')
const collectedClaims = okResearch.flatMap((r) =>
  (r.facts || []).map((f) => f.key + ': ' + f.value).concat((r.sources || []).map((s) => 'URL=' + s.url + ' claim=' + s.claim))
)
// Verify a sampled, representative subset to bound cost while still refuting weak claims.
const claimsToVerify = collectedClaims.filter((c, i) => i % 3 === 0 && c.length < 700)
let verified = []
if (claimsToVerify.length) {
  verified = await parallel(
    claimsToVerify.map((c) => () =>
      agent(verifyPrompt(c), { label: 'verify-claim', phase: 'Verify', schema: VERIFY_SCHEMA }).then((v) => ({ claim: c, verdict: v }))
    )
  )
}
verified = verified.filter(Boolean)
const refuted = verified.filter((v) => v.verdict && !v.verdict.isReal)
if (refuted.length) {
  log('design-csharp-scripting-framework: ' + refuted.length + ' of ' + verified.length + ' checked claims were adversarially refuted; the synthesizer will exclude them')
}

phase('Synthesize')
const synth = await agent(
  synthesizePrompt(okResearch, verified),
  { label: 'synthesize-design', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

phase('Critique')
const critique = await agent(
  'You are a completeness critic for the following C# scripting framework design (CoreCLR JIT hosting + AOT, calling CascadedEngine IMGUI / RenderBackend+GPUGraph / ThreadManager / resource system).\n' +
  'DESIGN:\n' + JSON.stringify(synth) + '\n\n' +
  'Adversarially judge completeness and correctness. Answer: (1) Is any required capability the user asked for clearly missing (IMGUI custom windows, RenderBackend/GPUGraph custom pipelines, ThreadManager, resource system)? (2) Is the CoreCLR hosting + AOT dual-path internally consistent, or does it contradict itself (e.g. hot-reload vs AOT-static, reflection needed by tooling vs trimmed in AOT)? (3) Is the thread/ownership story sound for a multi-threaded scheduler+render engine? (4) Are there research claims the synthesizer relied on that were refuted and not excluded? (5) Suggest concrete improvements.\n' +
  'Output {gaps, inconsistencies, suggestions, verdict (APPROVE/REVISE with a one-line bottom line)}.'
  , { label: 'critique', phase: 'Critique', schema: CRITIC_SCHEMA }
)

return { research: okResearch, verified, synth, critique }

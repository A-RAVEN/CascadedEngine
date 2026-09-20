export const meta = {
  name: 'investigate-codegen-decision-gates',
  description: 'Ground the two CodeGen decision gates: CA_REFLECTION real shape and parser metadata source (libclang feasibility).',
  phases: [
    { title: 'Find', detail: 'Six parallel facets read the codebase for facts' },
    { title: 'Verify', detail: 'Adversarially re-check each facet key claims (file:line anchors)' },
    { title: 'Synthesize', detail: 'Collate grounded answers + open questions + recommendation' },
  ],
}

// ---- Schemas ----
const FACT_SCHEMA = {
  type: 'object',
  properties: {
    answer: { type: 'string', description: 'The facet question being answered in one sentence' },
    facts: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          claim: { type: 'string', description: 'A concrete factual claim' },
          file: { type: 'string', description: 'Repo-relative file path (or absolute for out-of-repo like CACodeGen)' },
          line: { type: ['number', 'string'], description: 'Line number anchor, or "range" description' },
          confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
        },
        required: ['claim', 'file', 'line'],
      },
    },
    openQuestions: { type: 'array', items: { type: 'string' }, description: 'Genuinely unresolved questions this facet surfaced' },
  },
  required: ['answer', 'facts', 'openQuestions'],
}

const VERDICT_SCHEMA = {
  type: 'object',
  properties: {
    facetName: { type: 'string' },
    verdicts: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          claim: { type: 'string' },
          verdict: { type: 'string', enum: ['CONFIRMED', 'REFUTED', 'PARTIAL'] },
          correction: { type: 'string', description: 'If REFUTED/PARTIAL, the correction with file:line' },
        },
        required: ['claim', 'verdict'],
      },
    },
    summary: { type: 'string', description: 'What the facet answer actually is, grounded' },
  },
  required: ['facetName', 'verdicts', 'summary'],
}

const SYNTH_SCHEMA = {
  type: 'object',
  properties: {
    gate1Answer: { type: 'string', description: 'Grounded answer to gate 1: real shape + whether it is enough for a full parser' },
    gate2Answer: { type: 'string', description: 'Grounded answer to gate 2: parser metadata source decision with reasoning' },
    clangFeasibility: { type: 'string', description: 'Is libclang actually usable as a build-time parser here? What is needed?' },
    buildHook: { type: 'string', description: 'Where codegen slots into build.py/CMake, and the wiring cost' },
    recommendation: { type: 'string', description: 'Single clear recommendation for what to do next (Phase 1 scope)' },
    openQuestions: { type: 'array', items: { type: 'string' } },
  },
  required: ['gate1Answer', 'gate2Answer', 'clangFeasibility', 'buildHook', 'recommendation', 'openQuestions'],
}

// ---- Facet finder prompts ----
const FACETS = [
  {
    name: 'CA_REFLECTION-ceiling',
    prompt: 'Read-only investigation of the engine reflection system. Answer precisely: what does CA_REFLECTION / CA_PRIVATE_REFLECTION / CA_REFLECTION_TEMPLATE actually provide, and is it enough to be a full metadata parser for CodeGen?\n\nFiles to read:\n- E:/Projects/CascadedEngine/CACore/header/Reflection.h (whole file; key: CA_REFLECTION at :82, CA_REFLECTION_TEMPLATE :91, CA_PRIVATE_REFLECTION :99, CATypeMemberDesc :67-73, CATypeDescriptor :58-65, has_type_desc :141-147, visit_members :345-365, structured-binding boilerplate :388-924)\n- E:/Projects/CascadedEngine/Interface/RenderInterface/header/ShaderResourceHandle.h (:116/:175 CA_PRIVATE_REFLECTION, :179-180 CA_REFLECTION)\n- E:/Projects/CascadedEngine/Interface/RenderInterface/header/CPipelineStateObject.h (6 occurrences)\n- E:/Projects/CascadedEngine/Interface/RenderInterface/header/ShaderBindingBuilder.h (7 occurrences)\n\nGrep the whole repo for CA_REFLECTION|CA_PRIVATE_REFLECTION|CA_REFLECTION_TEMPLATE and enumerate how each is used. Then answer:\n1. Does reflection provide ONLY data members (no methods, enums, base classes, access-specifier, per-member attributes)? Give file:line.\n2. Is it compile-time-only (constexpr types) with no runtime name->TypeDescriptor registry? (Check log_reflection :122-133 which appears disabled with early `return;`).\n3. What do existing consumers (Serialization.h, GLMReflectionCommon.h, anything calling visit_members) actually need from it? Are any of those needs NOT met by the current fields-only model?\n4. List every reflected type in the repo and note any that would need methods/enum/base info that the current system cannot give.\n\nGround EVERY claim in file:line. No speculation. If something is ambiguous, put it in openQuestions. Do not propose code changes.' +
      '\n\nENVIRONMENT: Win32, repo root E:/Projects/CascadedEngine. Read files with the Read tool. This is a codebase-fact investigation. Do not compile or build. Do not cite external docs.',
  },
  {
    name: 'clang-libclang-feasibility',
    prompt: 'Read-only investigation to answer: is libclang actually usable as a build-time parser for a CodeGen tool in THIS project, given the real toolchain?\n\nKnown facts to verify (do NOT trust them, re-check):\n1. E:/Projects/CACodeGen/ExternalLibraries/LLVM/bin/x64/libclang.dll exists (confirmed). Also check the same LLVM bin dir for clang.exe, and check CACodeGen/ExternalLibraries/LLVM directory structure, and MAC/version info.\n2. E:/Projects/CACodeGen/ParserLibrary/ is a libclang-based parser (read its CMakeLists.txt and a couple of source files to confirm it links libclang and traverses cursors).\n3. E:/Projects/CACodeGen/clangLog.txt shows codegen was compiled with clang historically.\n4. E:/Projects/CascadedEngine/CMakePresets.json defines a x64-debug-clang preset with clang-cl / ClangCL toolset (:21-:) but the VS bundled clang-cl (C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-cl.exe) is ABSENT. Confirm the preset exists but the tool is not installed.\n5. E:/Projects/CascadedEngine/CMake/ExternalProjects.cmake has a link_llvm function referencing ExternalLib/llvm-project/llvm/lib/cmake/llvm (:17-49), but ExternalLib/llvm-project does NOT exist (only CPM.cmake + CMakeLists.txt). Confirm.\n\nThen answer these decision-critical questions with file:line evidence:\nA. Is libclang (the C API) available and working on this machine via the CACodeGen vendored dll? Can a separate build-time codegen tool link against it WITHOUT requiring the engine to compile with clang-cl?\nB. Confirm the primary engine toolchain is MSVC (cl.exe) — read E:/Projects/CascadedEngine/build.py (configure :149 -CMAKE_CXX_COMPILER=cl) and note a libclang-based codegen would be a SEPARATE executable invoked by build.py, NOT compiled into the engine C++. Is that reading correct? Give build.py line refs.\nC. Does libclang C API give cursor-based AST traversal (clang_visitChildren / clang_getCursorKind / clang_getCursorSemanticParent) good enough to extract methods, enums, base classes, fields, and attributes? This is what determines if it clears the CA_REFLECTION ceiling. Read CACodeGen ParserLibrary to see if it already extracts more than fields.\n\nGround every claim. Use absolute or repo-relative paths. Do not propose code changes. Ambiguity -> openQuestions.' +
      '\n\nENVIRONMENT: Win32. Use the Read tool / Bash `ls` for directory listings. Do NOT compile. Do NOT cite external docs. Check CACodeGen at E:/Projects/CACodeGen (OUTSIDE the engine repo).',
  },
  {
    name: 'fact-trap-verification',
    prompt: 'Read-only verification of six alleged code "facts" that a prior decomposition flagged as false premises (they would mislead implementation). For EACH, read the cited file and report CONFIRMED / REFUTED / PARTIAL with the corrected file:line and a short explanation:\n\n1. "Reflection.h:350 gate condition uses managed_wrapper_traits / type_desc (NOT custom_hash_trait)" — read E:/Projects/CascadedEngine/CACore/header/Reflection.h around :345-365. Is custom_hash_trait even mentioned there? What is the actual condition?\n2. "Hasher.h operator== does NOT branch on CompareMode" — read E:/Projects/CascadedEngine/CACore/header/Hasher.h (HashObj operator== ~:258-261, operator<=> ~:244-247). Does operator== compare hash-value regardless of EHashObjCompareMode, so you cannot just delete the SHA256 path?\n3. "aggregateHasher hashes trivially-copyable types as raw bytes INCLUDING padding" — read Hasher.h :80-126, especially the is_trivially_copyable -> hash_one raw-bytes path ~:86-90. Is padding really hashed?\n4. "NameHash is NOT uint64, roughly 48 bytes" — grep the repo for the NameHash type definition (likely CACore/header/CACore/CAHash.h) and report its true size/layout. This matters because a 16-byte handle capsule assumption would be wrong.\n5. "WriteReadbackPNG return value is ignored at Main.cpp:1101" — read E:/Projects/CascadedEngine/Test/GPUBackendTester/Main.cpp around :1101 (and find WriteReadbackPNG definition) to confirm the bool return is discarded, which would let an empty frame still exit 0.\n6. "--headless-timeout is parsed but never consumed" — grep the tester for --headless-timeout and the watchdog usage. Is it parsed that never used (deadlock would hang forever)?\n\nFor each: give the corrected anchor, the true behavior, and which alleged premise is wrong. This list is from TODO/004 section 4. Ground in file:line. Do not propose code changes.' +
      '\n\nENVIRONMENT: Win32, repo root E:/Projects/CascadedEngine. Read with Read tool / grep. Do not compile.',
  },
  {
    name: 'serialization-hotpath-impact',
    prompt: 'Read-only investigation of how the reflection system is consumed, to judge the migration risk of a CodeGen reflection emitter. Read:\n- E:/Projects/CascadedEngine/CACore/header/Serialization.h (whole file; it does `#include "Reflection.h"`)\n- E:/Projects/CascadedEngine/Test/VulkanRendererBackendTester/private/GLMReflectionCommon.h (reflects glm types)\n- Grep repo for callers of visit_members / CATypeDescriptor / member_tuple_type to see every reflection consumer.\n\nAnswer:\n1. Is reflection used ONLY at compile-time (constexpr, template) or also at runtime (strings, type registry)?\n2. Is it on the per-frame hot path, or only during init/asset-load/cache-build? Give file:line for the call sites.\n3. Which types in the repo are reflected, and would migrating to a generated table (FieldInfo array {name,offset,type,kind,flags}) change behaviour for Serialization / GLM reflection?\n4. Are there types that need reflection but are NOT currently reflectable (non-aggregate, private members, methods, inheritance) — i.e. gaps the current system cannot serve? List them with file:line.\n\nGround in file:line. Do not propose code changes. Ambiguity -> openQuestions.' +
      '\n\nENVIRONMENT: Win32, repo E:/Projects/CascadedEngine.',
  },
  {
    name: 'annotation-precedent',
    prompt: 'Read-only investigation of any existing annotation / attribute / marker mechanism that a future `[[careflection(...)]]` style attribute could extend, and how MSVC handles it. Read:\n- E:/Projects/CascadedEngine/CACore/header/uenum.h (grep for CA_ macros / reflection)\n- E:/Projects/CascadedEngine/CACore/header/Reflection.h :102 namespace careflection (an existing careflection namespace)\n- E:/Projects/CascadedEngine/Interface/RenderInterface/header/CPipelineStateObject.h and ShaderBindingBuilder.h (existing CA_REFLECTION usage)\n- E:/Projects/CascadedEngine/CACore/header/Macros.h — list any reflection/marker/attribute macros already defined.\n\nAnswer:\n1. What macro / marker infrastructure already exists that a CodeGen attribute system could either reuse or must avoid colliding with?\n2. Is there any existing `[[clang::...]]` or `[[careflection]]` attribute in the codebase, or would we introduce the first attribute namespace?\n3. The engine compiles with MSVC (build.py uses cl). MSVC behavior for unknown attribute namespaces: does it warn (C4459/ignored) or error? Look for any existing attribute usage and any /W settings.\n4. What is the least invasive annotation surface: a macro that is a real attribute under clang for the parser and a no-op under MSVC? Note the trade-off if both the codegen scan AND the MSVC compile must both tolerate it.\n\nGround in file:line. Do not propose code changes. Ambiguity -> openQuestions.' +
      '\n\nENVIRONMENT: Win32, repo E:/Projects/CascadedEngine.',
  },
  {
    name: 'build-hook-point',
    prompt: 'Read-only investigation of where a CodeGen tool would slot into the build, to estimate wiring cost. Read fully:\n- E:/Projects/CascadedEngine/build.py (the ONLY sanctioned build path; configure :135-152 uses -G Ninja -DCMAKE_CXX_COMPILER=cl via run_msvc; build :184-191 cmake --build; patch_ninja :155-181)\n- E:/Projects/CascadedEngine/CMakePresets.json\n- E:/Projects/CascadedEngine/CMake/ExternalProjects.cmake (FetchContent pattern: how deps are pulled)\n- E:/Projects/CascadedEngine/CMakeLists.txt (top-level, how targets/modules are registered)\n- E:/Projects/CascadedEngine/ShaderProcessor/CMakeLists.txt (an existing build-time tool precedent: an executable built by CMake, linked via link_dxc)\n\nAnswer:\n1. Is there precedent for a build-time tool that runs before/during the main compile (ShaderProcessor is one)? How is it wired (add_executable + custom command?), and could a CodeGen executable be added the same way? Give CMake lines.\n2. Would generated code be committed to the repo (checked in) or generated fresh at build time (custom command / pre-build)? What is the trade-off, and what does the existing pattern suggest (does the repo check in any generated files)?\n3. Where in build.py would invocation happen (a new step between configure and build, or a CMake custom command)? Give build.py line refs and note run_msvc is MSVC-only — could a separate clang-based codegen run NOT under vcvars (init cache / separate toolchain)?\n4. What is the minimal set of changes to build.py/CMake to hook in a libclang-based codegen at build time?\n\nGround in file:line. Do not propose code changes (this is investigation). Ambiguity -> openQuestions.' +
      '\n\nENVIRONMENT: Win32, repo E:/Projects/CascadedEngine. Do not compile.',
  },
]

// ---- Run ----
phase('Find')
log('Fanning out six facet finders...')
const finderResults = await parallel(FACETS.map((f) => () =>
  agent(f.prompt + '\n\nReturn structured output.', { label: 'find:' + f.name, phase: 'Find', schema: FACT_SCHEMA })
))

phase('Verify')
log('Adversarially re-verifying each facet claim anchors...')
const verifyResults = await parallel(finderResults.map((fr) => () => {
  if (!fr || !fr.facts || fr.facts.length === 0) return { facetName: fr?.answer || 'unknown', verdicts: [], summary: 'No facts to verify.' }
  const claimList = fr.facts.map((x) => x.claim + (x.file ? ' at ' + x.file + ':' + x.line : '')).join('\n- ')
  return agent(
    'You are an adversarial verifier for a codebase-fact investigation. For each claim below, RE-READ the actual source file at the given anchor and independently decide CONFIRMED / REFUTED / PARTIAL. Do not trust the claim or the anchor. If REFUTED or PARTIAL, give the correction with its true file:line.\n\nClaims to verify for facet "' + fr.answer + '":\n- ' + claimList + '\n\nReturn structured verdicts.',
    { label: 'verify:' + (fr.answer || 'facet').slice(0, 28), phase: 'Verify', schema: VERDICT_SCHEMA }
  )
}))

phase('Synthesize')
log('Collating grounded answers...')
const synth = await agent(
  'You are synthesizing a grounded answer to two CodeGen DECISION GATES for the CascadedEngine (C++20, MSVC+Ninja primary, a C# runtime + unified CodeGen framework is being planned). Below are six facets of read-only findings and their adversarial verification, plus one prior inline recon (do not re-litigate it, incorporate it).\n\nINLINE RECON FACTS (verified this session):\n- clang / clang-cl NOT on PATH; VS bundled LLVM toolset NOT installed (so the CMakePresets x64-debug-clang preset is aspirational, not usable for compiling).\n- BUT E:/Projects/CACodeGen/ExternalLibraries/LLVM/bin/x64/libclang.dll EXISTS and was actually used (clangLog.txt shows codegen was compiled with clang; ParserLibrary is libclang-based).\n- E:/Projects/CascadedEngine/CMake/ExternalProjects.cmake link_llvm references ExternalLib/llvm-project which does NOT exist (dead/aspirational).\n- build.py configures the engine with -DCMAKE_CXX_COMPILER=cl (MSVC) via vcvars (build.py :149).\n\nFACET FINDINGS (with verification verdicts):\n' + JSON.stringify(verifyResults).slice(0, 60000) +
  '\n\nProduce the SYNTH_SCHEMA object. Gate 1 = does CA_REFLECTION provide enough metadata (fields only? methods/enum/base/attrs? compile-time only?) to be a full parser, or does it force libclang? Gate 2 = what should the parser metadata source be (libclang AST vs macro-seed on-ramp vs clang annotate), and specifically: since libclang is already vendored+used in CACodeGen, can the engine codegen tool reuse that or needs its own libclang, requiring the engine to ONLY compile with MSVC while the parser runs as a separate build-time tool? Be honest about cost and any open controversy. Recommendation = one clear Phase-1 scope.',
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTH_SCHEMA }
)

return {
  facets: finderResults,
  verification: verifyResults,
  synthesis: synth,
}

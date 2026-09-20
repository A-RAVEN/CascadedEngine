export const meta = {
  name: 'tasks-adversarial-vote',
  description: 'True independent vote: 5 reviewers with IDENTICAL criteria independently judge every task (0.1-9.2) of the hand-patched fix-vulkan-descriptor-registration tasks.md against real code; per-point majority decides',
  phases: [
    { title: 'Vote' },
    { title: 'Aggregate' },
  ],
}

const POINTS = { type: 'object', properties: { points: { type: 'array', items: { type: 'object', properties: {
  task: { type: 'string' },
  verdict: { type: 'string', enum: ['pass', 'issue', 'uncertain'] },
  issue: { type: 'string' },
  evidence: { type: 'string' },
  confidence: { type: 'string', enum: ['high', 'medium', 'low'] },
}, required: ['task', 'verdict', 'issue', 'evidence', 'confidence'] } } }, required: ['points'] }

const AGG = { type: 'object', properties: { verdicts: { type: 'array', items: { type: 'object', properties: {
  task: { type: 'string' }, pass: { type: 'integer' }, issue: { type: 'integer' }, uncertain: { type: 'integer' }, defect: { type: 'string' },
}, required: ['task', 'pass', 'issue', 'uncertain'] } }, mustFix: { type: 'array', items: { type: 'string' } }, bottomLine: { type: 'string' } }, required: ['verdicts', 'mustFix', 'bottomLine'] }

const CHANGE = 'E:/Projects/CascadedEngine/openspec/changes/fix-vulkan-descriptor-registration'
const ROOT = 'E:/Projects/CascadedEngine'
const JUDGE = [
'The target: '+CHANGE+'/tasks.md (full 0.1-9.2), design.md, proposal.md.',
'Working-tree code: run git -C '+ROOT+' diff -- VulkanRenderBackendNew/ and git -C '+ROOT+' status --short VulkanRenderBackendNew/, then Read the real files at each task cited lines.',
'Every reviewer uses the SAME criteria. For EACH task 0.1,0.2,1.1,1.2,1.3,2.1,2.2,2.3,3.1,3.2,3.3,3.4,4.1,4.2,4.3,4.4,4.5,5.1,5.2,6.1,6.2,7.1,8.1,8.2,8.3,8.4,9.1,9.2 judge: verdict = pass only if the task is accurate AND mechanically executable AND does not mislead AND matches the real code AND contributes to closure (08114/00331 gone). verdict = issue if ANY of these fail: wrong file:line/path; a function/cite that does not exist; wording that misleads an implementer (e.g. would misdirect to the wrong file or a non-existent map); an option that contradicts reality; missing a guard that would panic; a claim about current code that mismatches the git diff; a [x] item not actually implemented; a step that cannot be executed as written; a chain gap. verdict = uncertain if you cannot verify.',
'CRITICAL probes (verify in real code, do not assume): 4.3 names BindResourcesToPhysicalMemory + file:line — find the file it truly lives in (VulkanGraphLocalResourceManager.cpp vs VulkanResourceAliasing.cpp) and whether that line exists. 3.2 option 1 says the D1 top-level Foreach retains a handle-to-resourceId map — verify a readable map exists in the executor, or whether GetBufferHandleToResource/GetTextureHandleToResource accessors exist. 4.1 says disable single-pool greedy m_AliasedAllocations — check whether the single-pool FALLBACK path depends on it and whether disabling breaks it. D-B (3.1/3.2/3.4): VulkanResourceUsageRangeData has states[] — check if a resource never fed into any pass has empty states so front()/back() panics; is a guard required and specified. 1.1/1.2/1.3 marked [x]: confirm those changes are really in the working tree. Modify-existing-code tasks: does each name the exact line and is it really only that line?',
].join('\n')

const angle = (n) =>
'You are an INDEPENDENT reviewer #'+n+'/5. You are part of a majority-vote audit: every reviewer uses the IDENTICAL pass/issue criteria below; you vote alone, do not assume what others will say.\n\n'+JUDGE+'\n\nFor each task output one vote: task; verdict pass/issue/uncertain; issue (if issue/uncertain, a precise defect citing real file:line); evidence (file:line you checked); confidence high/medium/low. Output the full list.'

phase('Vote')
const results = await parallel([
  () => agent(angle(1), { label: 'vote-1', phase: 'Vote', schema: POINTS }),
  () => agent(angle(2), { label: 'vote-2', phase: 'Vote', schema: POINTS }),
  () => agent(angle(3), { label: 'vote-3', phase: 'Vote', schema: POINTS }),
  () => agent(angle(4), { label: 'vote-4', phase: 'Vote', schema: POINTS }),
  () => agent(angle(5), { label: 'vote-5', phase: 'Vote', schema: POINTS }),
])

const allPts = results.filter(Boolean).flatMap(r => r.points || [])
log('Collected ' + allPts.length + ' votes')

phase('Aggregate')
const agg = await agent(
'Aggregate these votes into a per-task tally. For each task id 0.1-9.2, output one verdicts entry: { task, pass, issue, uncertain } = counts across all reviewers, and defect = the concrete defect(s) (real file:line) if any reviewer said issue. The criteria are IDENTICAL across reviewers, so a majority on issue is meaningful.\nThen set mustFix = array of task ids where issue count >= 3 (CERTAIN defects to fix before implementation). Tasks with issue 2 = worth fixing / attention; 0-1 = accepted.\nThen bottomLine = a blunt verdict: is the hand-written tasks.md trustworthy to implement as-is, or must it be revised first? Name each specific fix needed.\n\nVOTES JSON:\n'+JSON.stringify(allPts),
  { label: 'aggregate', phase: 'Aggregate', schema: AGG })

return { points: (agg?.verdicts || []), mustFix: (agg?.mustFix || []), bottomLine: (agg?.bottomLine || ''), rawVotes: allPts.length }

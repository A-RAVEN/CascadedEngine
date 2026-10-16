# delta 场景数对账（`retire-transitional-leftovers` 任务 4.2）

目的：`MODIFIED` 会在归档时**整条替换**主规格里的同名要求，漏搬的场景会静默消失。这张表逐条对数。

```powershell
function Count-Scenarios($path) {
  $map = [ordered]@{}; $cur = $null
  foreach ($line in Get-Content -LiteralPath $path -Encoding utf8) {
    if ($line -match '^### Requirement:\s*(.+)$') { $cur = $Matches[1].Trim(); $map[$cur] = 0 }
    elseif ($line -match '^#### Scenario:' -and $cur) { $map[$cur]++ }
  }
  return $map
}
```

| 能力 | 要求 | 主规格场景数 | delta 场景数 | 差异 |
|---|---|---|---|---|
| `codegen-framework` | 生成源码以显式源清单纳入构建 | 2 | 2 | 无（两条都搬了；第二条的正文改成"消费单元先红"） |
| `codegen-framework` | 无序容器的序列化与哈希必须走规范顺序 | 10 | 10 | 无（"元素是内含无序容器的结构体"那条**就地替换**成新行为；"读进非空目标时先清空"只换依据） |
| `codegen-runtime` | 生成物的读写必须自带游标 | 3 | 3 | 无（第二条换成"冻结字面量"对照） |
| `codegen-type-migration` | 私有成员与指针成员两类形状的处置必须写在明处 | 2 | 2 | 无（只把旧宏名换成 `CA_CODEGEN_FRIEND`） |
| `codegen-type-migration` | 迁移必须逐类型可核，且不得留下静默不生效的钩子 | 2 | 2 | 无（只换判据符号） |

**结论**：五条 `MODIFIED` 的场景数逐条相等，没有场景会在归档时丢掉。

两条 `REMOVED`（`codegen-framework` 的「保留 CA_REFLECTION 运行时语义」、`codegen-type-migration` 的
「已有类型的迁移形态是双份描述」）按 OpenSpec 的增量格式带 `**Reason**` 与 `**Migration**`，
归档时整条删除 —— 它们的场景不需要对账。

## 4.3 复核：delta 点名的"今天的代码出处"都还在

| 出处 | 命令 | 读数 |
|---|---|---|
| 守卫实现只断言类型名非空 | `git grep -n -F 'TypeDescriptor<T>::name != nullptr' -- CACore/header/CodeGenMeta.h` | 1 处（`static_assert_abi_consistency` 的正文） |
| 三份消费单元断言文件 | `Test-Path` 三个路径 | 都存在：`Interface/RenderInterface/private/CPipelineStateObjectAssertions.cpp`、`D3D12RenderBackend/private/ShaderLibrary/ShaderLibraryCodeGenAssertions.cpp`、`VulkanRenderBackendNew/private/ShaderLibrary/ShaderLibraryCodeGenAssertions.cpp` |
| 容器读取路径按有无 `clear()` 分派 | `git grep -n -F 'hasClear' -- CACore/header/CodeGenSerialization.h` | 3 处（含 `if constexpr (hasClear<C>::value) c.clear();`） |
| 冻结的期望字节 | `git grep -n -F 'kOldSampleHex' -- Test/CoreTests/CodeGenRuntimeTest.cpp` | 2 处（定义 + 读取） |
| 撤掉的"容器里装结构体"拒收 | `git grep -n -F '无序容器' -- Tools/CodeGen/CodeGenMain.cpp` | **0 处**（正是"该拒收已撤"的依据） |

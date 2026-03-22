$ErrorActionPreference = 'Stop'

$date = '2026-03-21'
$root = 'specs/as-is'
New-Item -ItemType Directory -Path $root -Force | Out-Null

$top = (Get-Content CMakeLists.txt) |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -notmatch '^#' }

$activeSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($line in $top) {
    if ($line -match 'add_subdirectory\s*\(\s*"?([^"\)\s]+)') {
        [void]$activeSet.Add($matches[1])
    }
}

$projects = Get-ChildItem -Path . -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'CMakeLists.txt') } |
    Select-Object -ExpandProperty Name |
    Sort-Object

$rows = @()

foreach ($p in $projects) {
    $cmakePath = Join-Path $p 'CMakeLists.txt'
    $cmake = Get-Content -Path $cmakePath -Raw

    if ($cmake -match 'add_executable\s*\(') {
        $type = 'Executable'
    } elseif ($cmake -match 'add_library\s*\(') {
        $type = 'Library'
    } else {
        $type = 'Unknown Target Type'
    }

    if ($p -match 'Vulkan') {
        $backend = 'Vulkan'
    } elseif ($p -match 'D3D12') {
        $backend = 'D3D12'
    } else {
        $backend = 'General Engine Infrastructure'
    }

    if ($activeSet.Contains($p)) {
        $status = 'Active in top-level build'
    } else {
        $status = 'Currently not enabled by top-level CMakeLists'
    }

    $dir = Join-Path $root $p
    New-Item -ItemType Directory -Path $dir -Force | Out-Null

    $lines = @(
        "# Feature Specification: AS-IS Baseline - $p",
        "",
        "**Feature Branch**: `as-is/$p`  ",
        "**Created**: $date  ",
        "**Status**: Draft  ",
        "**Input**: Current repository state for module \"$p\"",
        "",
        "## User Scenarios & Testing *(mandatory)*",
        "",
        "### User Story 1 - Understand current module role (Priority: P1)",
        "",
        "As an engine contributor, I need a clear baseline description of $p so I can make changes without breaking module boundaries.",
        "",
        "**Why this priority**: Baseline understanding is required before safe refactoring and feature work.",
        "",
        "**Independent Test**: A reviewer can identify module purpose, ownership boundary, and build status from this document alone.",
        "",
        "**Acceptance Scenarios**:",
        "",
        "1. **Given** a new contributor, **When** they read this spec, **Then** they can describe the module purpose and whether it is active in the top-level build.",
        "2. **Given** a planned change, **When** the change impact is assessed, **Then** impacted module boundaries are identifiable from this baseline spec.",
        "",
        "---",
        "",
        "### User Story 2 - Validate current build participation (Priority: P2)",
        "",
        "As a build maintainer, I need to know if $p is currently enabled by the top-level CMake configuration.",
        "",
        "**Why this priority**: Build participation impacts release confidence and test scope.",
        "",
        "**Independent Test**: The spec explicitly records module build participation state and target category.",
        "",
        "**Acceptance Scenarios**:",
        "",
        "1. **Given** the top-level CMake configuration, **When** I compare it with this spec, **Then** the active/inactive status of $p is consistent.",
        "",
        "## Edge Cases",
        "",
        "- Module exists but is not included by top-level CMake.",
        "- Module contains mixed responsibilities that may need decomposition in future planning.",
        "",
        "## Requirements *(mandatory)*",
        "",
        "### Functional Requirements",
        "",
        "- **FR-001**: System MUST record the as-is target category for $p as \"$type\".",
        "- **FR-002**: System MUST record the as-is backend domain for $p as \"$backend\".",
        "- **FR-003**: System MUST record top-level build status for $p as \"$status\".",
        "- **FR-004**: System MUST preserve this module baseline as a planning input artifact.",
        "- **FR-005**: System MUST allow future updates of this baseline when build topology changes.",
        "",
        "### Constitution Alignment *(mandatory)*",
        "",
        "- **CA-001 Module Boundaries**: $p is documented as a distinct module boundary with explicit ownership context.",
        "- **CA-002 Backend Parity**: Backend domain is recorded as \"$backend\" for parity tracking.",
        "- **CA-003 Determinism**: Build participation status is captured to support deterministic planning scope.",
        "- **CA-004 Validation Gates**: This baseline must be reviewed against CMake files before downstream implementation work.",
        "- **CA-005 Performance and Debuggability**: Runtime/perf-sensitive changes derived from this module must declare measurable budgets in implementation plans.",
        "",
        "### Key Entities *(include if feature involves data)*",
        "",
        "- **ModuleBaseline**: Captures module name, target category, backend domain, and build participation status.",
        "",
        "## Success Criteria *(mandatory)*",
        "",
        "### Measurable Outcomes",
        "",
        "- **SC-001**: 100% of top-level subprojects with CMakeLists have an as-is spec file.",
        "- **SC-002**: 100% of as-is spec files include module target category, backend domain, and build participation state.",
        "- **SC-003**: Reviewers can locate module baseline information in under 60 seconds per module.",
        "- **SC-004**: Baseline artifacts are sufficient to start planning without re-scanning module CMake files for basic metadata."
    )

    Set-Content -Path (Join-Path $dir 'spec.md') -Value $lines -Encoding utf8

    $rows += [PSCustomObject]@{
        Project = $p
        Type = $type
        Backend = $backend
        Status = $status
        SpecPath = (Join-Path $dir 'spec.md')
    }
}

$index = @(
    '# As-Is Specify Index',
    '',
    "Generated: $date",
    '',
    '| Subproject | Type | Backend Domain | Build Status | Spec |',
    '|---|---|---|---|---|'
)

foreach ($r in $rows) {
    $rel = $r.SpecPath -replace '\\', '/'
    $index += "| $($r.Project) | $($r.Type) | $($r.Backend) | $($r.Status) | [$($r.Project)]($rel) |"
}

Set-Content -Path (Join-Path $root 'README.md') -Value $index -Encoding utf8

Write-Output ("CREATED:{0}" -f $rows.Count)
Write-Output ("INDEX:{0}" -f (Join-Path $root 'README.md'))

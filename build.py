#!/usr/bin/env python3
"""CascadedEngine build script with automatic MSVC environment setup.

Usage:
    python build.py                 # configure + build (all targets, RelWithDebInfo)
    python build.py --no-configure  # build only (skip cmake configure)
    python build.py --config Debug  # use the Debug configuration (validation layers enabled)
    python build.py TargetName      # build a specific target
"""

import os
import sys
import subprocess
import json
import re
import shlex
from pathlib import Path

PROJECT_DIR = Path(__file__).parent.resolve()

# config name -> CMake preset name (preset sets binaryDir to out/build/<preset>)
CONFIGS = {
    "RelWithDebInfo": "x64-relWithDebugInfo",
    "Debug": "x64-debug",
}
DEFAULT_CONFIG = "RelWithDebInfo"

BUILD_DIR: Path | None = None
NINJA_FILE: Path | None = None

# Codegen manifest: header (relative to PROJECT_DIR, the file the sidecar parses), the type name, and
# the CMake module root that owns it (used to pick that module's compile env from compile_commands.json).
# This change registers NO project type (proposal Non-goals / tasks 9.15+9.25): the manifest points at
# the self-contained fixture under Tools/CodeGen/ so the build-time path still has a subject without
# coupling to engine headers. The generated file is byte-compared against the committed golden, so this
# entry checks something real (that the compile_commands-derived env produces the same output as the
# hand-written include flags used by the golden test) instead of emitting an unconsumed file.
CODEGEN_ANNOTATED = [
    ("Tools/CodeGen/TestEmit.h", "EmitSample", "Tools/CodeGen"),
]

# Committed goldens (same content the golden-diff test uses); compared byte-for-byte after generation.
CODEGEN_GOLDEN_DIR = PROJECT_DIR / "Tools" / "CodeGen" / "generated"

# Every emitter gets its own build-time generation + byte-comparison (goal terminal state 1, last
# paragraph). Before this, only the reflection emitter was driven from build.py, so the serialization
# and hash emitters were never exercised at build time: rewriting either of them and NOT regenerating
# its committed golden left both GeneratedSerializerTest and GeneratedHashTest passing, and the build
# reported success — i.e. two thirds of the "generation chain" had no build-time evidence at all.
CODEGEN_EMITTERS = [
    ("--emit-reflection", "Reflection"),
    ("--emit-serialization", "Serialization"),
    ("--emit-hash", "Hash"),
]

# Extra include roots the sidecar needs on top of the module env: the fixtures include CodeGenMeta.h,
# which lives in CACore/header and is NOT on the CodeGen tool's own include path.
CODEGEN_EXTRA_INCLUDES = ["CACore/header"]

COMSPEC = os.environ.get("COMSPEC", r"C:\Windows\System32\cmd.exe")


def find_vs_path() -> Path:
    vswhere = (
        Path("C:/Program Files (x86)")
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if vswhere.exists():
        r = subprocess.run(
            [str(vswhere), "-latest", "-property", "installationPath"],
            capture_output=True, text=True,
        )
        if r.returncode == 0 and r.stdout.strip():
            return Path(r.stdout.strip())
    for p in (
        r"C:\Program Files\Microsoft Visual Studio\18\Community",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community",
    ):
        pp = Path(p)
        if (pp / "VC" / "Auxiliary" / "Build" / "vcvars64.bat").exists():
            return pp
    raise FileNotFoundError("Visual Studio not found.")


def _write_bat(cmd: str, log_file: str | None = None) -> Path:
    vs = find_vs_path()
    v = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    ninja = (
        vs / "Common7" / "IDE" / "CommonExtensions"
        / "Microsoft" / "CMake" / "Ninja"
    )
    # Find MSVC toolchain binary directory
    msvc_base = vs / "VC" / "Tools" / "MSVC"
    msvc_ver = sorted(msvc_base.glob("*"), reverse=True)
    msvc_bin = msvc_ver[0] / "bin" / "Hostx64" / "x64" if msvc_ver else None

    lines = [
        "@echo off",
        # Sanitize PATH: MSYS2 injects entries that confuse cmd.exe parser.
        # Keep only Windows system paths before loading vcvars.
        'set "PATH=%SystemRoot%\\system32;%SystemRoot%;%SystemRoot%\\System32\\Wbem;%SystemRoot%\\System32\\WindowsPowerShell\\v1.0"',
        # Add vswhere path so vcvars can find VS installation
        'set "PATH=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer;%PATH%"',
        f'call "{v}" >nul 2>&1',
        "if %ERRORLEVEL% NEQ 0 (echo VCVARS FAILED & exit /b 1)",
    ]
    if ninja.exists():
        lines.append(f'set "PATH={ninja};%PATH%"')
    # Ensure cmake is in PATH (removed by sanitization, not added by vcvars)
    cmake_bin = Path(r"C:\Program Files\CMake\bin")
    if cmake_bin.exists():
        lines.append(f'set "PATH={cmake_bin};%PATH%"')
    # Ensure git is in PATH — CMake FetchContent/CPM runs git during configure
    # (e.g. stb populate update step); sanitization removed it.
    # Only Git\cmd is added: Git\mingw64\bin\git.exe fails to exec subcommands
    # (submodule etc.) from a plain cmd environment (exit 123), while the
    # cmd\git.exe shim resolves its real binary internally and works.
    git_cmd = Path(r"C:\Program Files\Git\cmd")
    if git_cmd.exists():
        lines.append(f'set "PATH={git_cmd};%PATH%"')
    # Ensure MSVC compiler is in PATH (vcvars may fail silently in some environments)
    if msvc_bin and msvc_bin.exists():
        lines.append(f'set "PATH={msvc_bin};%PATH%"')
        # Also set CC/CXX as fallback
        lines.append(f'set "CC={msvc_bin}\\cl.exe"')
        lines.append(f'set "CXX={msvc_bin}\\cl.exe"')
    lines.append(f'{PROJECT_DIR.drive}')
    lines.append(f'cd /d "{PROJECT_DIR}"')
    if log_file:
        lines.append(f'{cmd} >> "{PROJECT_DIR / log_file}" 2>&1')
    else:
        lines.append(cmd)
    lines.append("exit /b %ERRORLEVEL%")

    bat = PROJECT_DIR / "_build_run.bat"
    bat.write_text("\r\n".join(lines) + "\r\n", encoding="ascii")
    return bat


def run_msvc(cmd: str, log_file: str | None = None) -> None:
    print(f"\n  {cmd}")
    bat = _write_bat(cmd, log_file)

    # cmd /c: MSYS2 may convert /c paths, but we pass Windows-style paths so it's safe.
    result = subprocess.run(
        ["cmd", "/c", str(bat)],
        cwd=str(PROJECT_DIR),
    )

    if log_file:
        log_path = PROJECT_DIR / log_file
        if log_path.exists():
            print(log_path.read_text(encoding="utf-8", errors="replace"))
            log_path.unlink()
        else:
            print(f"  [WARNING] Log file not found: {log_file}")

    if result.returncode != 0:
        print(f"\nERROR: command failed (exit {result.returncode})")
        print(f"  Bat file kept at: {bat}")
        sys.exit(result.returncode)
    bat.unlink(missing_ok=True)


def configure() -> None:
    print("=" * 60)
    print("[1/2] Configuring CMake...")
    print("=" * 60)
    # GIT_EXECUTABLE pinned to Git\cmd\git.exe: a stale cache entry pointing at
    # Git\mingw64\bin\git.exe makes FetchContent's `git submodule update`
    # fail with "submodule appears to be a git command" (that binary cannot
    # exec subcommands from a plain cmd environment).
    # Quoted: the path contains a space — without quotes cmd splits it and
    # CMake misreads the tail ("Files/Git/...") as a source directory.
    git = '"C:/Program Files/Git/cmd/git.exe"'
    # Check for init cache file
    init_cache = PROJECT_DIR / "_init_cache.cmake"
    if init_cache.exists():
        run_msvc(f"cmake -S {PROJECT_DIR} -B {BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE={config} -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -C {init_cache} -DGIT_EXECUTABLE={git}",
                 "_build_configure.log")
    else:
        run_msvc(f"cmake --preset {CONFIGS[config]} -DGIT_EXECUTABLE={git}", "_build_configure.log")


def patch_ninja() -> None:
    if not NINJA_FILE.exists():
        return
    print("  Patching build.ninja for CMake 4.x compatibility...")
    text = NINJA_FILE.read_text(encoding="utf-8")
    cmake_long = r"C:\Program Files\CMake\bin\cmake.exe"
    inner = f'@echo off\r\nfor %A in ("{cmake_long}") do @echo %~sA'
    bat = PROJECT_DIR / "_short_path.bat"
    bat.write_text(inner + "\r\n", encoding="ascii")
    r = subprocess.run(
        ["cmd", "/c", str(bat)],
        capture_output=True, text=True,
    )
    bat.unlink(missing_ok=True)
    cmake_short = r.stdout.strip()
    if cmake_short:
        text = text.replace(cmake_long, cmake_short)

    fwd = lambda p: str(p).replace("\\", "/")
    for rel in (
        "_deps/directxtex-src/DirectXTex/Shaders/CompileShaders.cmd",
        "_deps/directxtex-src/DDSView/hlsl.cmd",
    ):
        abs_cmd = fwd(BUILD_DIR / rel)
        cmd_name = rel.rsplit("/", 1)[-1]
        text = text.replace(f" {cmd_name} >", f" {abs_cmd} >")
    NINJA_FILE.write_text(text, encoding="utf-8")


def codegen() -> list[str]:
    """Build-time CodeGen step (openspec engine-codegen-framework task 4.1): run the libclang sidecar
    over the annotated headers, feeding each owning module's exact compile env (include dirs + defines)
    so libclang lays the types out correctly. Runs in the MSVC environment (via run_msvc so it inherits
    vcvars + sanitized PATH).

    Returns a list of human-readable failures; the caller turns a non-empty list into a non-zero build
    exit (task 9.9: a real tool failure and a success must NOT be indistinguishable). **An empty
    manifest is a legitimate configuration, not a failure** (task 3.3: empty whitelist => EXIT 0, no
    output, must not block the build). Everything abnormal - sidecar missing, sidecar non-zero exit,
    no compile env for a module, missing compile_commands.json - is a failure.

    Manifest (tasks 9.15 / 9.25): this change registers NO project type, so the manifest points at the
    self-contained fixture under Tools/CodeGen/. The generated file is byte-compared against the
    committed golden, so the entry checks a real property (the compile_commands-derived env produces
    the same bytes as the hand-written flags the golden test uses) instead of emitting an unused file.
    """
    print()
    print("=" * 60)
    print("[1.5] CodeGen: generating reflection headers for annotated types...")
    print("=" * 60)

    # The sidecar is a CMake build target; ensure it is built first (idempotent). It is a standalone
    # tool, not an engine DLL the tester loads — building it here is safe and does NOT cause the
    # "stale product" inconsistency the build.py single-target warning cares about.
    run_msvc(f"cmake --build {BUILD_DIR} --target CodeGen", "_build_codegen_tool.log")

    cc_file = BUILD_DIR / "compile_commands.json"
    if not cc_file.exists():
        return [f"compile_commands.json not found at {cc_file} (configure must export it)"]
    entries = json.loads(cc_file.read_text(encoding="utf-8"))

    if not CODEGEN_ANNOTATED:
        print("  manifest is empty: nothing to generate (legitimate configuration, not a failure)")
        return []

    gen_dir = BUILD_DIR / "codegen"
    gen_dir.mkdir(parents=True, exist_ok=True)
    sid = BUILD_DIR / "bin" / "CodeGen.exe"
    env_by_root = _compile_env_by_root(entries)
    extra = [f"-I{p}" for p in CODEGEN_EXTRA_INCLUDES]
    failures: list[str] = []

    for rel_header, type_name, owner_root in CODEGEN_ANNOTATED:
        env = env_by_root.get(owner_root)
        if not env:
            failures.append(f"no compile env for module '{owner_root}' (type {type_name})")
            continue
        if not sid.exists():
            failures.append(f"codegen sidecar not found: {sid}")
            break
        for emit_flag, suffix in CODEGEN_EMITTERS:
            out_file = gen_dir / f"{type_name}.{suffix}.h"
            cmd = [str(sid), rel_header, type_name, *env, *extra,
                   emit_flag, "-o", str(out_file)]
            try:
                r = subprocess.run(cmd, cwd=str(PROJECT_DIR), capture_output=True, text=True)
            except FileNotFoundError:
                failures.append(f"codegen sidecar not found: {sid}")
                break
            if r.returncode != 0:
                failures.append(
                    f"CodeGen '{type_name}' {emit_flag} exit {r.returncode}: {r.stderr.strip()}")
                continue
            golden = CODEGEN_GOLDEN_DIR / out_file.name
            if not golden.exists():
                failures.append(f"CodeGen '{type_name}' {emit_flag}: no golden at {golden} to "
                                f"compare against")
                continue
            if out_file.read_bytes() != golden.read_bytes():
                failures.append(
                    f"CodeGen '{type_name}' {emit_flag}: generated {out_file} differs byte-for-byte "
                    f"from golden {golden} — the compile_commands-derived env does NOT reproduce the "
                    f"golden test's hand-written flags")
                continue
            print(f"  generated {out_file} (matches golden)")
        if not sid.exists():
            break

    return failures


def _msvc_to_clang_env(command: str) -> list[str]:
    """Translate an MSVC cl.exe compile command into the libclang args needed to *parse* a header:
    keep include dirs (/I, -I) and defines (/D, -D) and the language standard; drop the source file,
    output/pch/optimization/warning flags that are meaningless or harmful to a parse-only invocation.

    Task 9.4: `/MDd` (and `/MTd`) must NOT simply be dropped. cl.exe defines `_DEBUG` and
    `_ITERATOR_DEBUG_LEVEL=2` implicitly for those runtimes, and dropping the flag dropped those macros
    too, so libclang laid the types out with the RELEASE STL rules. Measured with Tools/CodeGen/
    TestStdLayout.h: `tail` sits at byte 40 under /MD rules and at byte 48 under /MDd rules (debug STL
    gives std::string a _Container_proxy*, +8 bytes) — i.e. every offset after a std::string was wrong.
    """
    tokens = shlex.split(command, posix=False)
    args: list[str] = []
    debug_runtime = False
    for raw in tokens:
        t = raw.strip('"')  # shlex(posix=False) keeps quote chars; strip so the arg is a bare path/def
        if t.lower() in ("/mdd", "/mtd"):
            debug_runtime = True
            continue
        # /I and /D (MSVC slash-form) get the SAME len>2 guard as -I/-D (dash-form): a bare '/I' or '/D'
        # (len 2) would otherwise emit a malformed empty '-I'/'-D' arg to libclang (AUDIT-2).
        if (t.startswith('/I') and len(t) > 2) or (t.startswith('-I') and len(t) > 2):
            args.append('-I' + t[2:])
        elif (t.startswith('/D') and len(t) > 2) or (t.startswith('-D') and len(t) > 2):
            args.append('-D' + t[2:])
        elif t.startswith('/std:') or t.startswith('-std:'):
            args.append('-std=' + t[5:])
        # everything else is MSVC-only (output, pch, warns, opt, /Yc /Yu /Fp /Fo /Fd, -c <file>) -> drop

    if debug_runtime:
        for d in ("_DEBUG", "_ITERATOR_DEBUG_LEVEL=2"):
            if not any(a.upper() == ('-D' + d).upper() for a in args):
                args.append('-D' + d)
    return args


def _compile_env_by_root(entries: list[dict]) -> dict[str, list[str]]:
    """For each owning module root, pick ONE representative TU (a compile_commands entry whose source
    file lives under that root) and return that TU's normalized clang env. Never merges across roots
    (avoids the CA_STATIC_BUILD 0/1 union conflict + include-root-order sensitivity)."""
    by_root: dict[str, list[str]] = {}
    for e in entries:
        f = e.get("file", "").replace("\\", "/")
        for _, _, root in CODEGEN_ANNOTATED:
            if root in f and root not in by_root:
                by_root[root] = _msvc_to_clang_env(e.get("command", "") or "")
    return by_root


def build(target: str | None = None) -> None:
    print()
    print("=" * 60)
    label = target or "all targets"
    print(f"[2/2] Building {label}...")
    print("=" * 60)
    t = f" --target {target}" if target else ""
    run_msvc(f"cmake --build {BUILD_DIR}{t}", "_build_build.log")


def main() -> None:
    global config, BUILD_DIR, NINJA_FILE

    # Consume --config <name> here so its value never reaches the
    # target-argument scan below.
    config = DEFAULT_CONFIG
    args = []
    i = 0
    while i < len(sys.argv[1:]):
        a = sys.argv[1:][i]
        if a == "--config":
            if i + 1 >= len(sys.argv[1:]) or sys.argv[1:][i + 1].startswith("--"):
                print(f"ERROR: --config requires a value, one of: {list(CONFIGS)}")
                sys.exit(1)
            config = sys.argv[1:][i + 1]
            i += 2
            continue
        args.append(a)
        i += 1
    if config not in CONFIGS:
        print(f"ERROR: unknown config '{config}', expected one of: {list(CONFIGS)}")
        sys.exit(1)

    BUILD_DIR = PROJECT_DIR / "out" / "build" / CONFIGS[config]
    NINJA_FILE = BUILD_DIR / "build.ninja"

    configure_step = "--no-configure" not in args
    target = next((a for a in args if not a.startswith("--")), None)

    print("=== CascadedEngine Build Script (Python) ===")
    print(f"  Visual Studio : {find_vs_path()}")
    print(f"  Config        : {config} (preset {CONFIGS[config]})")
    print(f"  Build dir     : {BUILD_DIR}")

    if configure_step:
        configure()
        patch_ninja()
    # Task 9.13: codegen runs on BOTH paths. It used to sit inside the configure branch, so the
    # documented incremental path (`--no-configure`) silently consumed stale generated headers — or,
    # as measured, silently regenerated nothing at all after a generated file was deleted.
    # It needs compile_commands.json, which a previous configure leaves on disk; if that file is
    # missing, codegen() reports it and the build fails with a codegen-specific message (task 9.9),
    # never a bare C1083.
    codegen_failures = codegen()
    build(target)

    if codegen_failures:
        print()
        print("=" * 60)
        print(f"CODEGEN FAILED ({len(codegen_failures)}) — build not reported as successful")
        for msg in codegen_failures:
            print(f"  - {msg}")
        print("=" * 60)
        sys.exit(1)

    print()
    print("=" * 60)
    print("BUILD SUCCESSFUL")
    print("=" * 60)


if __name__ == "__main__":
    main()
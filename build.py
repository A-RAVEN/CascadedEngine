#!/usr/bin/env python3
"""CascadedEngine build script with automatic MSVC environment setup.

Usage:
    python build.py                 # configure + build (all targets)
    python build.py --no-configure  # build only (skip cmake configure)
    python build.py TargetName      # build a specific target
"""

import os
import sys
import subprocess
from pathlib import Path

PROJECT_DIR = Path(__file__).parent.resolve()
BUILD_DIR = PROJECT_DIR / "out" / "build" / "x64-relWithDebugInfo"
NINJA_FILE = BUILD_DIR / "build.ninja"

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
    # Check for init cache file
    init_cache = PROJECT_DIR / "_init_cache.cmake"
    if init_cache.exists():
        run_msvc(f"cmake -S {PROJECT_DIR} -B {BUILD_DIR} -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -C {init_cache}",
                 "_build_configure.log")
    else:
        run_msvc("cmake --preset x64-relWithDebugInfo", "_build_configure.log")


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


def build(target: str | None = None) -> None:
    print()
    print("=" * 60)
    label = target or "all targets"
    print(f"[2/2] Building {label}...")
    print("=" * 60)
    t = f" --target {target}" if target else ""
    run_msvc(f"cmake --build {BUILD_DIR}{t}", "_build_build.log")


def main() -> None:
    configure_step = "--no-configure" not in sys.argv
    target = next((a for a in sys.argv[1:] if not a.startswith("--")), None)

    print("=== CascadedEngine Build Script (Python) ===")
    print(f"  Visual Studio : {find_vs_path()}")
    print(f"  Build dir     : {BUILD_DIR}")

    if configure_step:
        configure()
        patch_ninja()
    build(target)

    print()
    print("=" * 60)
    print("BUILD SUCCESSFUL")
    print("=" * 60)


if __name__ == "__main__":
    main()
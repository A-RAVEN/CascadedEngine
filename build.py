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


def _write_bat(cmd: str) -> Path:
    vs = find_vs_path()
    v = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    ninja = (
        vs / "Common7" / "IDE" / "CommonExtensions"
        / "Microsoft" / "CMake" / "Ninja"
    )

    lines = [
        "@echo off",
        # Sanitize PATH: MSYS2 injects entries that confuse cmd.exe parser.
        # Keep only Windows system paths before loading vcvars.
        'set "PATH=%SystemRoot%\\system32;%SystemRoot%;%SystemRoot%\\System32\\Wbem;%SystemRoot%\\System32\\WindowsPowerShell\\v1.0"',
        f'call "{v}" >nul 2>&1',
        "if %ERRORLEVEL% NEQ 0 (echo VCVARS FAILED & exit /b 1)",
    ]
    if ninja.exists():
        lines.append(f'set "PATH={ninja};%PATH%"')
    lines.append(f'{PROJECT_DIR.drive}')
    lines.append(f'cd /d "{PROJECT_DIR}"')
    lines.append(cmd)

    bat = PROJECT_DIR / "_build_run.bat"
    bat.write_text("\r\n".join(lines) + "\r\n", encoding="ascii")
    return bat


def run_msvc(cmd: str) -> None:
    print(f"\n  {cmd}")
    bat = _write_bat(cmd)

    # cmd //c: MSYS2 converts //c back to /c (avoids C:/ mangling).
    result = subprocess.run(
        ["cmd", "//c", str(bat)],
        cwd=str(PROJECT_DIR),
    )
    if result.returncode != 0:
        print(f"\nERROR: command failed (exit {result.returncode})")
        print(f"  Bat file kept at: {bat}")
        sys.exit(result.returncode)
    bat.unlink(missing_ok=True)


def configure() -> None:
    print("=" * 60)
    print("[1/2] Configuring CMake...")
    print("=" * 60)
    run_msvc("cmake --preset x64-relWithDebugInfo")


def patch_ninja() -> None:
    if not NINJA_FILE.exists():
        return
    print("  Patching build.ninja for CMake 4.x compatibility...")
    text = NINJA_FILE.read_text(encoding="utf-8")
    cmake_long = r"C:\Program Files\CMake\bin\cmake.exe"
    inner = f'for %A in ("{cmake_long}") do @echo %~sA'
    r = subprocess.run(
        ["cmd", "//c", inner],
        capture_output=True, text=True,
    )
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
    run_msvc(f"cmake --build {BUILD_DIR}{t}")


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
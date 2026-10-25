#!/usr/bin/env python3
"""
Ensemble Unified Build & Packaging Orchestrator
Cross-platform build system for Linux, Windows, and AppImage.
Supports CLI one-liners for CI/CD and an interactive graphical UI (Tkinter).
"""

import argparse
import os
import platform
import shutil
import stat
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List, Optional
import re
import urllib.request

REPO_ROOT = Path(__file__).resolve().parent

DEFAULT_VERSION = "1.3.0"
APPIMAGETOOL_URL = "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
INNO_SCRIPT = REPO_ROOT / "resources" / "ensemble.iss"


# ==============================================================================
# Compiler / CMake flag knowledge base (drives GUI tooltips + CLI flag linting)
# ==============================================================================

@dataclass(frozen=True)
class FlagInfo:
    flag: str                 # literal flag; a trailing "=" means "takes a value"
    desc: str                 # human-readable explanation
    family: str = "gnu"       # "gnu" (GCC/Clang/MinGW), "msvc", or "any"
    lang: str = "both"        # "both", "cxx" (C++ only) or "c" (C only)
    warn: str = ""            # optional caution shown in yellow


COMPILER_FLAGS: List[FlagInfo] = [
    # --- Optimisation ---
    FlagInfo("-O0", "No optimisation. Fastest compile, best debugging experience."),
    FlagInfo("-O1", "Basic optimisations without large compile-time cost."),
    FlagInfo("-O2", "Standard release optimisation level (CMake Release uses -O3 by default)."),
    FlagInfo("-O3", "Aggressive optimisation incl. vectorisation. Larger binaries."),
    FlagInfo("-Os", "Optimise for size (what MinSizeRel uses)."),
    FlagInfo("-Og", "Optimise while keeping the debugging experience intact."),
    FlagInfo("-Ofast", "-O3 plus -ffast-math.", warn="Breaks strict IEEE float semantics; may change results."),
    FlagInfo("-flto", "Link-Time Optimisation across translation units. Slower link, faster binary.",
             warn="All objects/libraries must be built with the same compiler for LTO."),
    FlagInfo("-ffast-math", "Relaxed floating-point rules for speed.", warn="Can break NaN/Inf handling."),
    FlagInfo("-funroll-loops", "Unroll loops with a known trip count."),
    FlagInfo("-fomit-frame-pointer", "Free up the frame pointer register.", warn="Makes profiling/stack traces harder."),
    FlagInfo("-fno-omit-frame-pointer", "Keep frame pointers; better perf/profiler stack traces."),
    # --- CPU targeting ---
    FlagInfo("-march=", "Target CPU architecture, e.g. -march=x86-64-v2, -march=znver3."),
    FlagInfo("-march=native", "Tune for the CPU of the build machine.",
             warn="Binary may crash (illegal instruction) on other CPUs - avoid for distributed releases."),
    FlagInfo("-march=x86-64-v2", "SSE4.2/POPCNT baseline. Safe for virtually all PCs since ~2009."),
    FlagInfo("-march=x86-64-v3", "AVX2/FMA baseline (Haswell / Zen 1 and newer).",
             warn="Will not run on older or low-end CPUs."),
    FlagInfo("-mtune=", "Schedule instructions for a CPU without changing the required ISA, e.g. -mtune=generic."),
    FlagInfo("-mtune=generic", "Balanced tuning for current mainstream CPUs."),
    # --- Debug info ---
    FlagInfo("-g", "Emit debug information."),
    FlagInfo("-g3", "Maximum debug info, including macro definitions."),
    FlagInfo("-ggdb", "Debug info tailored for GDB."),
    FlagInfo("-gsplit-dwarf", "Put debug info into separate .dwo files; speeds up linking."),
    # --- Warnings ---
    FlagInfo("-Wall", "Enable the common set of warnings."),
    FlagInfo("-Wextra", "Extra warnings beyond -Wall."),
    FlagInfo("-Wpedantic", "Warn on non-ISO-standard extensions."),
    FlagInfo("-Werror", "Treat all warnings as errors.", warn="Qt headers or new compilers may break the build."),
    FlagInfo("-Wshadow", "Warn when a variable shadows another."),
    FlagInfo("-Wconversion", "Warn on implicit narrowing conversions (noisy with Qt)."),
    FlagInfo("-Wno-deprecated-declarations", "Silence warnings about deprecated APIs (common with Qt upgrades)."),
    FlagInfo("-Wold-style-cast", "Warn on C-style casts in C++.", lang="cxx"),
    FlagInfo("-Wnon-virtual-dtor", "Warn when a polymorphic class lacks a virtual destructor.", lang="cxx"),
    # --- Language ---
    FlagInfo("-std=", "Language standard, e.g. -std=c++20 or -std=c17."),
    FlagInfo("-std=c++17", "ISO C++17.", lang="cxx", warn="Project CMake already sets the C++ standard; overriding may fail."),
    FlagInfo("-std=c++20", "ISO C++20.", lang="cxx"),
    FlagInfo("-std=c++23", "ISO C++23 (needs recent GCC/Clang).", lang="cxx"),
    FlagInfo("-std=c17", "ISO C17.", lang="c"),
    FlagInfo("-fno-exceptions", "Disable C++ exceptions.", lang="cxx", warn="Qt/STL code that throws will fail to compile."),
    FlagInfo("-fno-rtti", "Disable RTTI (dynamic_cast/typeid).", lang="cxx", warn="Breaks dynamic_cast; Qt mostly OK."),
    FlagInfo("-fPIC", "Position-independent code (needed for shared libraries)."),
    FlagInfo("-fpermissive", "Downgrade some C++ conformance errors to warnings.", lang="cxx",
             warn="Hides real bugs; use only as a temporary workaround."),
    # --- Hardening ---
    FlagInfo("-D_FORTIFY_SOURCE=2", "Runtime buffer-overflow checks in libc calls (needs -O1 or higher)."),
    FlagInfo("-D_GLIBCXX_ASSERTIONS", "Bounds checks in libstdc++ containers.", lang="cxx"),
    FlagInfo("-fstack-protector-strong", "Stack smashing protection for vulnerable functions."),
    FlagInfo("-fstack-clash-protection", "Protect against stack-clash attacks."),
    FlagInfo("-fcf-protection", "Intel CET control-flow protection."),
    # --- Sanitizers ---
    FlagInfo("-fsanitize=address", "AddressSanitizer: detect out-of-bounds and use-after-free.",
             warn="Debug only. ~2x slowdown; not supported by MinGW."),
    FlagInfo("-fsanitize=undefined", "UBSan: detect undefined behaviour at runtime.", warn="Debug only."),
    FlagInfo("-fsanitize=thread", "ThreadSanitizer: detect data races.", warn="Cannot combine with address sanitizer."),
    # --- Preprocessor ---
    FlagInfo("-D", "Define a preprocessor macro, e.g. -DQT_NO_DEBUG_OUTPUT."),
    FlagInfo("-DQT_NO_DEBUG_OUTPUT", "Compile out all qDebug() output."),
    FlagInfo("-DQT_NO_INFO_OUTPUT", "Compile out all qInfo() output."),
    FlagInfo("-DNDEBUG", "Disable assert() (already set by Release builds)."),
    FlagInfo("-I", "Add an include search directory, e.g. -I/opt/include."),
    FlagInfo("-pipe", "Use pipes instead of temp files between compile stages."),
    FlagInfo("-fdiagnostics-color=always", "Coloured compiler diagnostics even when piped (e.g. into this GUI log)."),
    # --- MSVC ---
    FlagInfo("/O2", "MSVC: maximise speed.", family="msvc"),
    FlagInfo("/O1", "MSVC: minimise size.", family="msvc"),
    FlagInfo("/Od", "MSVC: disable optimisation (debug).", family="msvc"),
    FlagInfo("/GL", "MSVC: whole program optimisation (pair with /LTCG at link).", family="msvc"),
    FlagInfo("/Zi", "MSVC: generate PDB debug info.", family="msvc"),
    FlagInfo("/W4", "MSVC: high warning level.", family="msvc"),
    FlagInfo("/WX", "MSVC: treat warnings as errors.", family="msvc", warn="Qt headers may break the build."),
    FlagInfo("/permissive-", "MSVC: strict standards conformance.", family="msvc"),
    FlagInfo("/utf-8", "MSVC: treat source and execution charset as UTF-8.", family="msvc"),
    FlagInfo("/MP", "MSVC: compile multiple sources in parallel.", family="msvc"),
    FlagInfo("/arch:AVX2", "MSVC: allow AVX2 instructions.", family="msvc", warn="Will not run on older CPUs."),
    FlagInfo("/EHsc", "MSVC: standard C++ exception handling model.", family="msvc", lang="cxx"),
    FlagInfo("/fsanitize=address", "MSVC: AddressSanitizer.", family="msvc", warn="Debug only."),
    FlagInfo("/D", "MSVC: define a preprocessor macro, e.g. /DQT_NO_DEBUG_OUTPUT.", family="msvc"),
]

CMAKE_ARG_FLAGS: List[FlagInfo] = [
    FlagInfo("-G", "Choose generator, e.g. -G Ninja or -G \"Unix Makefiles\".", family="any"),
    FlagInfo("-GNinja", "Use the Ninja generator (fast incremental builds).", family="any"),
    FlagInfo("-DCMAKE_PREFIX_PATH=", "Where to find Qt, e.g. -DCMAKE_PREFIX_PATH=/opt/Qt/6.8.0/gcc_64.", family="any"),
    FlagInfo("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "Write compile_commands.json for clangd/IDEs.", family="any"),
    FlagInfo("-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON", "Enable LTO/IPO the CMake way (portable across compilers).", family="any"),
    FlagInfo("-DCMAKE_C_COMPILER=", "Override the C compiler, e.g. clang.", family="any"),
    FlagInfo("-DCMAKE_CXX_COMPILER=", "Override the C++ compiler, e.g. clang++.", family="any"),
    FlagInfo("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache", "Cache compilations with ccache for faster rebuilds.", family="any"),
    FlagInfo("-DCMAKE_LINKER_TYPE=MOLD", "Link with mold (CMake >= 3.29).", family="any"),
    FlagInfo("-DCMAKE_EXE_LINKER_FLAGS=", "Extra linker flags, e.g. -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld.", family="any"),
    FlagInfo("-DCMAKE_VERBOSE_MAKEFILE=ON", "Print full compiler command lines during the build.", family="any"),
    FlagInfo("-DCMAKE_INSTALL_PREFIX=", "Install prefix used by cmake --install.", family="any"),
    FlagInfo("-DCMAKE_TOOLCHAIN_FILE=", "Custom toolchain file.", family="any",
             warn="build.py already sets a MinGW toolchain for cross Windows builds."),
    FlagInfo("-DBUILD_TESTING=OFF", "Skip building the unit test targets.", family="any"),
    FlagInfo("--fresh", "Discard the existing CMake cache and reconfigure from scratch (CMake >= 3.24).", family="any"),
    FlagInfo("-Wno-dev", "Silence CMake developer warnings.", family="any"),
    FlagInfo("--trace-expand", "Trace every CMake command (very verbose debugging).", family="any"),
]


def lookup_flag(token: str, catalog: List[FlagInfo]) -> Optional[FlagInfo]:
    """Find the catalog entry describing a fully typed token (exact, then value-prefix)."""
    if not token:
        return None
    for info in catalog:
        if info.flag == token:
            return info
    # Value-taking flags: "-march=znver4" -> "-march=", "-DFOO" -> "-D"
    best: Optional[FlagInfo] = None
    for info in catalog:
        stem = info.flag
        if (stem.endswith("=") or stem in ("-D", "-I", "/D", "-G")) and token.startswith(stem):
            if best is None or len(stem) > len(best.flag):
                best = info
    return best


def lint_flags(flags: str, catalog: List[FlagInfo], lang: str) -> List[str]:
    """Return human-readable notes (warnings / unknown flags) for a flag string."""
    notes: List[str] = []
    for tok in flags.split():
        info = lookup_flag(tok, catalog)
        if info is None:
            notes.append(f"{tok}: not in the guidance catalog (passed through unchanged)")
            continue
        if info.lang not in ("both", lang):
            notes.append(f"{tok}: only meaningful for {'C++' if info.lang == 'cxx' else 'C'} sources")
        if info.warn:
            notes.append(f"{tok}: {info.warn}")
    return notes


@dataclass
class BuildConfig:
    target: str = "linux"             # "linux", "windows", "appimage"
    build_type: str = "Debug"         # "Debug", "Release", "RelWithDebInfo", "MinSizeRel"
    build_dir: str = ""               # default resolved dynamically
    output_dir: str = ""              # default resolved dynamically
    version_id: str = DEFAULT_VERSION
    localversion: str = ""            # e.g., "agnestachyon"
    cflags: str = ""                  # extra C compiler flags
    cxxflags: str = ""                # extra C++ compiler flags
    cmake_args: str = ""              # raw extra CMake arguments
    clean: bool = False
    run_tests: bool = False
    package: bool = False
    jobs: int = os.cpu_count() or 4
    appimagetool_path: str = ""
    download_tools: bool = False
    installer: bool = False           # build an Inno Setup installer (windows target)
    iscc_path: str = ""               # custom path to ISCC.exe (Inno Setup compiler)
    verbose: bool = False

    def full_version(self) -> str:
        base = self.version_id.strip() if self.version_id.strip() else DEFAULT_VERSION
        loc = self.localversion.strip()
        return f"{base}-{loc}" if loc else base

    def resolved_build_dir(self) -> Path:
        if self.build_dir.strip():
            return (REPO_ROOT / self.build_dir.strip()).resolve()
        if self.target == "windows" and platform.system() != "Windows":
            return (REPO_ROOT / "build-win").resolve()
        return (REPO_ROOT / "build").resolve()

    def resolved_output_dir(self) -> Path:
        if self.output_dir.strip():
            return (REPO_ROOT / self.output_dir.strip()).resolve()
        return self.resolved_build_dir()

    def to_cli_args(self) -> List[str]:
        args = ["python3", "build.py", "--target", self.target, "--build-type", self.build_type]
        if self.version_id.strip() and self.version_id.strip() != DEFAULT_VERSION:
            args.extend(["--version-id", self.version_id.strip()])
        if self.localversion.strip():
            args.extend(["--localversion", self.localversion.strip()])
        if self.build_dir.strip():
            args.extend(["--build-dir", self.build_dir.strip()])
        if self.output_dir.strip():
            args.extend(["--output-dir", self.output_dir.strip()])
        if self.cflags.strip():
            args.append(f'--cflags="{self.cflags.strip()}"')
        if self.cxxflags.strip():
            args.append(f'--cxxflags="{self.cxxflags.strip()}"')
        if self.cmake_args.strip():
            args.append(f'--cmake-args="{self.cmake_args.strip()}"')
        if self.clean:
            args.append("--clean")
        if self.run_tests:
            args.append("--test")
        if self.package:
            args.append("--package")
        if self.installer:
            args.append("--installer")
        if self.iscc_path.strip():
            args.extend(["--iscc", f'"{self.iscc_path.strip()}"'])
        if self.jobs != (os.cpu_count() or 4):
            args.extend(["--jobs", str(self.jobs)])
        return args

    def to_ci_command(self) -> str:
        return " ".join(self.to_cli_args())


class EnsembleBuilder:
    def __init__(self, config: BuildConfig, log_cb: Optional[Callable[[str], None]] = None):
        self.config = config
        self.log_cb = log_cb or self._default_log
        self.current_process: Optional[subprocess.Popen] = None
        self._cancelled = False

    def _default_log(self, text: str):
        sys.stdout.write(text)
        sys.stdout.flush()

    def log(self, text: str):
        if not text.endswith("\n"):
            text += "\n"
        self.log_cb(text)

    def cancel(self):
        self._cancelled = True
        if self.current_process and self.current_process.poll() is None:
            self.log("[ABORT] Terminating active build process...")
            try:
                self.current_process.terminate()
                time.sleep(0.5)
                if self.current_process.poll() is None:
                    self.current_process.kill()
            except Exception as e:
                self.log(f"[ABORT] Error stopping process: {e}")

    def run_cmd(self, cmd: List[str], cwd: Optional[Path] = None, env: Optional[dict] = None) -> int:
        if self._cancelled:
            return 130
        display_cmd = " ".join(cmd)
        self.log(f">> {display_cmd}")
        run_env = os.environ.copy()
        if env:
            run_env.update(env)

        proc = subprocess.Popen(
            cmd,
            cwd=str(cwd or REPO_ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env=run_env,
        )
        self.current_process = proc

        if proc.stdout:
            for line in proc.stdout:
                self.log(line.rstrip("\r\n"))
                if self._cancelled:
                    break

        proc.wait()
        self.current_process = None
        return proc.returncode

    def clean(self) -> int:
        bdir = self.config.resolved_build_dir()
        self.log(f"[CLEAN] Removing build directory: {bdir} and running cargo clean...")
        if bdir.exists():
            shutil.rmtree(bdir, ignore_errors=True)
        return self.run_cmd(["cargo", "clean"])

    def _is_release_profile(self) -> bool:
        return self.config.build_type.lower() in ("release", "relwithdebinfo", "minsizerel")

    def configure(self) -> int:
        bdir = self.config.resolved_build_dir()
        bdir.mkdir(parents=True, exist_ok=True)
        out_dir = self.config.resolved_output_dir()
        out_dir.mkdir(parents=True, exist_ok=True)

        self.log(f"[CONFIG] Verifying Cargo workspace for {self.config.target.upper()} ({self.config.build_type})...")
        self.log(f"[VERSION] Full target version: {self.config.full_version()}")
        return self.run_cmd(["cargo", "metadata", "--format-version", "1", "--no-deps"])

    def build(self) -> int:
        bdir = self.config.resolved_build_dir()
        out_dir = self.config.resolved_output_dir()
        bdir.mkdir(parents=True, exist_ok=True)
        out_dir.mkdir(parents=True, exist_ok=True)

        cmd = ["cargo", "build", "--workspace", "--jobs", str(self.config.jobs)]
        if self._is_release_profile():
            cmd.append("--release")

        if self.config.target == "windows" and platform.system() != "Windows":
            cmd.extend(["--target", "x86_64-pc-windows-gnu"])

        self.log(f"[BUILD] Compiling Rust workspace for {self.config.target.upper()} ({self.config.build_type}) with {self.config.jobs} jobs...")
        ret = self.run_cmd(cmd)
        if ret != 0:
            return ret

        profile_dir = "release" if self._is_release_profile() else "debug"
        exe_name = "ensemble.exe" if (self.config.target == "windows" or platform.system() == "Windows") else "ensemble"

        candidates = [
            REPO_ROOT / "target" / "x86_64-pc-windows-gnu" / profile_dir / exe_name,
            REPO_ROOT / "target" / profile_dir / exe_name,
        ]
        built_bin = next((p for p in candidates if p.exists()), None)
        if built_bin:
            dst1 = bdir / exe_name
            try:
                if dst1.exists():
                    dst1.unlink()
            except OSError:
                pass
            shutil.copy2(built_bin, dst1)
            if out_dir != bdir:
                dst2 = out_dir / exe_name
                try:
                    if dst2.exists():
                        dst2.unlink()
                except OSError:
                    pass
                shutil.copy2(built_bin, dst2)
            self.log(f"[STAGE] Staged compiled binary to {bdir / exe_name}")
        return 0

    def test(self) -> int:
        self.log("[TEST] Running Rust workspace unit and integration test suites via cargo test...")
        cmd = ["cargo", "test", "--workspace", "--jobs", str(self.config.jobs)]
        if self._is_release_profile():
            cmd.append("--release")
        return self.run_cmd(cmd)

    def _resolve_icon_png(self) -> Path:
        for p in (REPO_ROOT / "resources" / "icon.png", REPO_ROOT / "src" / "icons" / "icon.png"):
            if p.exists():
                return p
        return REPO_ROOT / "resources" / "icon.png"

    def _ensure_appimagetool(self) -> Optional[Path]:
        if self.config.appimagetool_path.strip():
            custom = Path(self.config.appimagetool_path.strip()).resolve()
            if custom.exists():
                return custom

        tool = shutil.which("appimagetool")
        if tool:
            return Path(tool)

        tools_dir = REPO_ROOT / "tools"
        local_tool = tools_dir / "appimagetool"
        if local_tool.exists():
            return local_tool

        if self.config.download_tools:
            self.log("[APPIMAGE] appimagetool not found. Downloading continuous release...")
            tools_dir.mkdir(parents=True, exist_ok=True)
            try:
                urllib.request.urlretrieve(APPIMAGETOOL_URL, str(local_tool))
                local_tool.chmod(local_tool.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
                self.log(f"[APPIMAGE] Downloaded and configured appimagetool at {local_tool}")
                return local_tool
            except Exception as e:
                self.log(f"[WARN] Failed to download appimagetool: {e}")

        return None

    def package_appimage(self) -> int:
        bdir = self.config.resolved_build_dir()
        out_dir = self.config.resolved_output_dir()
        appdir = bdir / "AppDir"

        self.log(f"[APPIMAGE] Staging AppDir at: {appdir}")
        if appdir.exists():
            shutil.rmtree(appdir, ignore_errors=True)
        (appdir / "usr" / "bin").mkdir(parents=True, exist_ok=True)
        (appdir / "usr" / "share" / "applications").mkdir(parents=True, exist_ok=True)
        (appdir / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps").mkdir(parents=True, exist_ok=True)

        profile_dir = "release" if self._is_release_profile() else "debug"
        candidates = [
            bdir / "ensemble",
            out_dir / "ensemble",
            REPO_ROOT / "target" / profile_dir / "ensemble",
        ]
        exe_src = next((p for p in candidates if p.exists()), None)
        if not exe_src:
            self.log("[ERROR] Compiled ensemble binary not found for AppDir staging.")
            return 1

        shutil.copy2(exe_src, appdir / "usr" / "bin" / "ensemble")

        desktop_src = REPO_ROOT / "resources" / "ensemble.desktop"
        icon_src = self._resolve_icon_png()

        if desktop_src.exists():
            shutil.copy2(desktop_src, appdir / "ensemble.desktop")
            shutil.copy2(desktop_src, appdir / "usr" / "share" / "applications" / "ensemble.desktop")
        if icon_src.exists():
            shutil.copy2(icon_src, appdir / "ensemble.png")
            shutil.copy2(icon_src, appdir / ".DirIcon")
            shutil.copy2(icon_src, appdir / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps" / "ensemble.png")

        for lic_name in ("LICENSE", "LICENSE.LGPLv3", "LICENSE.GPLv3"):
            lic_file = REPO_ROOT / lic_name
            if lic_file.exists():
                shutil.copy2(lic_file, appdir / lic_name)

        apprun_path = appdir / "AppRun"
        apprun_content = (
            "#!/bin/bash\n"
            "set -e\n"
            'APPDIR="$(dirname "$(readlink -f "${0}")")"\n'
            'export PATH="${APPDIR}/usr/bin:${PATH}"\n'
            'export LD_LIBRARY_PATH="${APPDIR}/usr/lib:${APPDIR}/usr/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH}"\n'
            'export XDG_DATA_DIRS="${APPDIR}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"\n'
            'exec "${APPDIR}/usr/bin/ensemble" "$@"\n'
        )
        apprun_path.write_text(apprun_content)
        apprun_path.chmod(apprun_path.stat().st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)

        self.log("[APPIMAGE] AppDir structure staged successfully.")

        tool = self._ensure_appimagetool()
        variant_suffix = "-debug" if self.config.build_type.lower() == "debug" else ""
        target_filename = f"Ensemble-{self.config.full_version()}{variant_suffix}-x86_64.AppImage"
        target_file = out_dir / target_filename

        if tool:
            self.log(f"[APPIMAGE] Generating {target_filename} with {tool}...")
            env = {"ARCH": "x86_64"}
            cmd = [str(tool)]
            if str(tool).endswith(".AppImage"):
                cmd.append("--appimage-extract-and-run")
            cmd.extend([str(appdir), str(target_file)])
            ret = self.run_cmd(cmd, env=env)
            if ret == 0:
                self.log(f"[SUCCESS] AppImage created at: {target_file}")
                return 0
            else:
                self.log("[ERROR] appimagetool execution failed.")
                return ret
        else:
            self.log(f"[INFO] AppDir is fully ready at: {appdir}")
            return 0

    def package_linux(self) -> int:
        bdir = self.config.resolved_build_dir()
        out_dir = self.config.resolved_output_dir()
        dist_dir = bdir / "dist_linux"

        self.log(f"[LINUX-PKG] Packaging Linux standalone bundle into {dist_dir}...")
        if dist_dir.exists():
            shutil.rmtree(dist_dir, ignore_errors=True)
        dist_dir.mkdir(parents=True, exist_ok=True)

        profile_dir = "release" if self._is_release_profile() else "debug"
        candidates = [
            bdir / "ensemble",
            out_dir / "ensemble",
            REPO_ROOT / "target" / profile_dir / "ensemble",
        ]
        exe_src = next((p for p in candidates if p.exists()), None)
        if not exe_src:
            self.log("[ERROR] Could not find compiled ensemble executable for packaging.")
            return 1

        shutil.copy2(exe_src, dist_dir / "ensemble")
        desktop_src = REPO_ROOT / "resources" / "ensemble.desktop"
        icon_src = self._resolve_icon_png()
        if desktop_src.exists():
            shutil.copy2(desktop_src, dist_dir / "ensemble.desktop")
        if icon_src.exists():
            shutil.copy2(icon_src, dist_dir / "ensemble.png")

        variant_suffix = "-debug" if self.config.build_type.lower() == "debug" else ""
        archive_stem = f"Ensemble-{self.config.full_version()}-linux-x64{variant_suffix}"
        tar_path = out_dir / f"{archive_stem}.tar.gz"
        self.log(f"[LINUX-PKG] Compressing to {tar_path}...")
        shutil.make_archive(str(out_dir / archive_stem), "gztar", str(dist_dir))
        self.log(f"[SUCCESS] Linux release archive created: {tar_path}")
        return 0

    def package_windows(self) -> int:
        bdir = self.config.resolved_build_dir()
        out_dir = self.config.resolved_output_dir()
        dist_dir = bdir / "dist_win"

        self.log(f"[WIN-PKG] Packaging Windows build into {dist_dir}...")
        if dist_dir.exists():
            shutil.rmtree(dist_dir, ignore_errors=True)
        dist_dir.mkdir(parents=True, exist_ok=True)

        profile_dir = "release" if self._is_release_profile() else "debug"
        candidates = [
            bdir / "ensemble.exe",
            out_dir / "ensemble.exe",
            REPO_ROOT / "target" / profile_dir / "ensemble.exe",
            REPO_ROOT / "target" / "x86_64-pc-windows-gnu" / profile_dir / "ensemble.exe",
        ]
        exe_src = next((p for p in candidates if p.exists()), None)
        if not exe_src:
            self.log("[ERROR] Could not find compiled ensemble.exe for packaging.")
            return 1

        shutil.copy2(exe_src, dist_dir / "ensemble.exe")

        variant_suffix = "-debug" if self.config.build_type.lower() == "debug" else ""
        zip_stem = f"Ensemble-{self.config.full_version()}-windows-x64{variant_suffix}"
        zip_path = out_dir / f"{zip_stem}.zip"
        self.log(f"[WIN-PKG] Compressing to {zip_path}...")
        shutil.make_archive(str(out_dir / zip_stem), "zip", str(dist_dir))
        self.log(f"[SUCCESS] Windows release archive created: {zip_path}")

        if self.config.installer:
            return self.package_inno(dist_dir)
        return 0

    # ------------------------------------------------------------------
    # Inno Setup
    # ------------------------------------------------------------------

    def _find_iscc(self) -> Optional[List[str]]:
        """Locate ISCC.exe. Returns the command prefix (may be ["wine", ...] on Linux)."""
        is_windows = platform.system() == "Windows"

        def as_cmd(p: Path) -> List[str]:
            if not is_windows and p.suffix.lower() == ".exe":
                wine = shutil.which("wine") or shutil.which("wine64")
                return [wine, str(p)] if wine else []
            return [str(p)]

        # 1. Explicit path
        if self.config.iscc_path.strip():
            custom = Path(self.config.iscc_path.strip()).expanduser().resolve()
            if custom.exists():
                return as_cmd(custom) or None
            self.log(f"[WARN] --iscc path does not exist: {custom}")

        # 2. PATH (native ISCC, or a user-provided Linux wrapper script named iscc)
        for name in ("ISCC", "iscc", "ISCC.exe"):
            found = shutil.which(name)
            if found:
                return as_cmd(Path(found)) or None

        # 3. Default install locations
        candidates: List[Path] = []
        if is_windows:
            for env_key in ("ProgramFiles(x86)", "ProgramFiles"):
                base = os.environ.get(env_key)
                if base:
                    candidates.append(Path(base) / "Inno Setup 6" / "ISCC.exe")
            local = os.environ.get("LOCALAPPDATA")
            if local:
                candidates.append(Path(local) / "Programs" / "Inno Setup 6" / "ISCC.exe")
        else:
            prefix = Path(os.environ.get("WINEPREFIX", Path.home() / ".wine"))
            for pf in ("Program Files (x86)", "Program Files"):
                candidates.append(prefix / "drive_c" / pf / "Inno Setup 6" / "ISCC.exe")
            candidates.append(REPO_ROOT / "tools" / "InnoSetup" / "ISCC.exe")

        for c in candidates:
            if c.exists():
                cmd = as_cmd(c)
                if cmd:
                    return cmd
                self.log(f"[WARN] Found {c} but Wine is not installed to run it.")
        return None

    def _tool_path(self, p: Path, via_wine: bool) -> str:
        """Translate a host path for ISCC (Windows paths when running under Wine)."""
        if not via_wine:
            return str(p)
        winepath = shutil.which("winepath")
        if winepath:
            try:
                out = subprocess.run([winepath, "-w", str(p)], capture_output=True, text=True, timeout=30)
                if out.returncode == 0 and out.stdout.strip():
                    return out.stdout.strip().splitlines()[-1]
            except Exception:
                pass
        return "Z:" + str(p).replace("/", "\\")

    def package_inno(self, dist_dir: Path) -> int:
        out_dir = self.config.resolved_output_dir()
        if not INNO_SCRIPT.exists():
            self.log(f"[ERROR] Inno Setup script missing: {INNO_SCRIPT}")
            return 1

        iscc = self._find_iscc()
        if not iscc:
            self.log("[ERROR] -----------------------------------------------------------------")
            self.log("[ERROR] Inno Setup compiler (ISCC.exe) not found.")
            self.log("[INFO]  Install Inno Setup 6 from https://jrsoftware.org/isdl.php")
            if platform.system() != "Windows":
                self.log("[INFO]  On Linux: install Wine, then run the Inno Setup installer with wine,")
                self.log("[INFO]  or point --iscc at an ISCC.exe / wrapper script.")
            self.log(f"[INFO]  The staged files are ready at: {dist_dir}")
            self.log("[ERROR] -----------------------------------------------------------------")
            return 1

        via_wine = len(iscc) > 1 and Path(iscc[0]).name.startswith("wine")
        full_ver = self.config.full_version()
        m = re.match(r"\d+(\.\d+){0,3}", self.config.version_id.strip() or DEFAULT_VERSION)
        numeric_ver = m.group(0) if m else "0.0.0"
        variant_suffix = "-debug" if self.config.build_type.lower() == "debug" else ""
        base_name = f"Ensemble-{full_ver}-windows-x64-setup{variant_suffix}"

        defines = {
            "SourceDir": self._tool_path(dist_dir, via_wine),
            "OutputDir": self._tool_path(out_dir, via_wine),
            "OutputBaseName": base_name,
            "AppVersion": full_ver,
            "NumericVersion": numeric_ver,
        }
        license_file = REPO_ROOT / "LICENSE"
        if license_file.exists():
            defines["LicenseFile"] = self._tool_path(license_file, via_wine)
        icon_file = REPO_ROOT / "src" / "icons" / "icon.ico"
        if icon_file.exists():
            defines["SetupIcon"] = self._tool_path(icon_file, via_wine)

        cmd = list(iscc) + [f"/D{k}={v}" for k, v in defines.items()]
        cmd.append(self._tool_path(INNO_SCRIPT, via_wine))

        self.log(f"[INNO] Compiling installer {base_name}.exe{' (via Wine)' if via_wine else ''}...")
        ret = self.run_cmd(cmd, env={"WINEDEBUG": "-all"} if via_wine else None)
        if ret != 0:
            self.log(f"[ERROR] ISCC failed with exit code {ret}")
            return ret
        self.log(f"[SUCCESS] Installer created: {out_dir / (base_name + '.exe')}")
        return 0

    def execute_all(self) -> int:
        start_time = time.time()
        self.log("=" * 60)
        self.log(" ENSEMBLE BUILD ORCHESTRATOR")
        self.log(f" Target Platform : {self.config.target.upper()}")
        self.log(f" Build Type      : {self.config.build_type}")
        self.log(f" Version ID      : {self.config.version_id}")
        self.log(f" Codename / Local: {self.config.localversion or '(none)'}")
        self.log(f" Full Version    : {self.config.full_version()}")
        self.log(f" Output Location : {self.config.resolved_output_dir()}")
        self.log(f" Build Directory : {self.config.resolved_build_dir()}")
        if self.config.cxxflags:
            self.log(f" CXXFLAGS        : {self.config.cxxflags}")
        if self.config.cflags:
            self.log(f" CFLAGS          : {self.config.cflags}")
        if self.config.installer:
            self.log(" Installer       : Inno Setup (.exe)")
        self.log("=" * 60)

        for label, flags, lang in (("CXXFLAGS", self.config.cxxflags, "cxx"), ("CFLAGS", self.config.cflags, "c")):
            for note in lint_flags(flags, COMPILER_FLAGS, lang):
                self.log(f"[WARN] {label} {note}")
        if self.config.installer and self.config.target != "windows":
            self.log("[WARN] Inno Setup installer only applies to the windows target; ignoring.")

        if self.config.clean:
            ret = self.clean()
            if ret != 0:
                return ret

        ret = self.configure()
        if ret != 0:
            self.log(f"[FAILED] CMake configure failed with exit code {ret}")
            return ret

        ret = self.build()
        if ret != 0:
            self.log(f"[FAILED] Compilation failed with exit code {ret}")
            return ret

        if self.config.run_tests:
            ret = self.test()
            if ret != 0:
                self.log(f"[FAILED] Unit tests failed with exit code {ret}")
                return ret

        wants_win_installer = self.config.installer and self.config.target == "windows"
        if self.config.package or self.config.target == "appimage" or wants_win_installer:
            if self.config.target == "appimage":
                ret = self.package_appimage()
            elif self.config.target == "linux":
                ret = self.package_linux()
            elif self.config.target == "windows":
                ret = self.package_windows()
            if ret != 0:
                return ret

        elapsed = time.time() - start_time
        self.log("=" * 60)
        self.log(f" [COMPLETE] Build process finished successfully in {elapsed:.2f}s!")
        self.log("=" * 60)
        return 0


# ==============================================================================
# Modern Dark Graphical User Interface (Tkinter)
# ==============================================================================

class HoverTip:
    """Small delayed tooltip shown while the mouse hovers over a widget."""

    def __init__(self, widget, text: str, bg: str = "#313244", fg: str = "#cdd6f4", delay_ms: int = 450):
        self.widget, self.text, self.bg, self.fg, self.delay = widget, text, bg, fg, delay_ms
        self._tip = None
        self._after = None
        widget.bind("<Enter>", self._schedule, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def _schedule(self, _e=None):
        self._cancel()
        self._after = self.widget.after(self.delay, self._show)

    def _cancel(self):
        if self._after:
            self.widget.after_cancel(self._after)
            self._after = None

    def _show(self):
        import tkinter as tk
        if self._tip or not self.text:
            return
        x = self.widget.winfo_rootx() + 12
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 4
        self._tip = tk.Toplevel(self.widget)
        self._tip.wm_overrideredirect(True)
        self._tip.attributes("-topmost", True)
        self._tip.geometry(f"+{x}+{y}")
        tk.Label(self._tip, text=self.text, justify="left", bg=self.bg, fg=self.fg, bd=1, relief="solid",
                 padx=8, pady=5, wraplength=420, font=("Sans", 9)).pack()

    def _hide(self, _e=None):
        self._cancel()
        if self._tip:
            self._tip.destroy()
            self._tip = None


class FlagAssist:
    """
    As-you-type guidance popup for flag entry fields.

    - Explains the flag currently under the cursor (with warnings in yellow).
    - Lists matching flags while typing; Up/Down to select, Tab/Enter to insert, Esc to dismiss.
    - Filters suggestions by the active toolchain (GCC/Clang/MinGW vs MSVC) and language.
    """

    MAX_ROWS = 8

    def __init__(self, gui, entry, catalog: List[FlagInfo], lang: str, title: str, on_change: Callable[[], None]):
        self.gui, self.entry, self.catalog, self.lang, self.title = gui, entry, catalog, lang, title
        self.on_change = on_change
        self.popup = None
        self.matches: List[FlagInfo] = []
        self.sel = 0
        self.detail: Optional[FlagInfo] = None
        self.typed = ""
        entry.bind("<KeyRelease>", self._on_key_release, add="+")
        entry.bind("<FocusIn>", lambda e: self.refresh(), add="+")
        entry.bind("<FocusOut>", lambda e: entry.after(150, self._hide_if_unfocused), add="+")
        entry.bind("<ButtonRelease-1>", lambda e: self.refresh(), add="+")
        entry.bind("<Down>", lambda e: self._move(1))
        entry.bind("<Up>", lambda e: self._move(-1))
        entry.bind("<Tab>", self._accept)
        entry.bind("<Return>", self._accept)
        entry.bind("<Escape>", lambda e: (self.hide(), "break")[1])
        entry.winfo_toplevel().bind("<Configure>", lambda e: self._reposition(), add="+")

    # ---- token helpers -------------------------------------------------
    def _token_bounds(self):
        text = self.entry.get()
        idx = self.entry.index("insert")
        start = text.rfind(" ", 0, idx) + 1
        end = text.find(" ", idx)
        if end == -1:
            end = len(text)
        return text, start, end, text[start:idx]

    def _visible(self, info: FlagInfo) -> bool:
        fam = self.gui.toolchain_family()
        if info.family not in ("any", fam) and fam != "unknown":
            return False
        return info.lang in ("both", self.lang)

    # ---- events --------------------------------------------------------
    def _on_key_release(self, e):
        if e.keysym in ("Up", "Down", "Tab", "Return", "Escape", "Shift_L", "Shift_R"):
            return
        self.refresh()

    def _move(self, delta: int):
        if not self.popup or not self.matches:
            return None
        self.sel = (self.sel + delta) % len(self.matches)
        self._render()
        return "break"

    def _accept(self, _e=None):
        if not self.popup or not self.matches:
            return None  # let Tab do normal focus traversal
        self._insert(self.matches[self.sel])
        return "break"

    def _insert(self, info: FlagInfo):
        text, start, end, _ = self._token_bounds()
        takes_value = info.flag.endswith("=") or info.flag in ("-D", "-I", "/D", "-G")
        insert = info.flag if takes_value else info.flag + " "
        new_text = text[:start] + insert + text[end:].lstrip(" ")
        self.entry.delete(0, "end")
        self.entry.insert(0, new_text)
        self.entry.icursor(start + len(insert))
        self.entry.focus_set()
        self.on_change()
        self.refresh()

    # ---- popup ---------------------------------------------------------
    def refresh(self):
        if self.entry.focus_get() is not self.entry:
            return
        _, start, end, partial = self._token_bounds()
        full_token = self.entry.get()[start:end]
        visible = [f for f in self.catalog if self._visible(f)]

        if partial:
            pl = partial.lower()
            prefix = [f for f in visible if f.flag.lower().startswith(pl)]
            fuzzy = [f for f in visible if f not in prefix and pl.lstrip("-/") and pl.lstrip("-/") in f.flag.lower()]
            self.matches = (prefix + fuzzy)[: self.MAX_ROWS]
            # Hide the list if the only match is exactly what's typed - just show its detail
            if len(self.matches) == 1 and self.matches[0].flag == full_token:
                self.matches = []
        else:
            self.matches = []

        self.detail = lookup_flag(full_token, self.catalog) if full_token else None
        self.typed = full_token
        self.sel = 0
        self._render()

    def _hide_if_unfocused(self):
        try:
            focused = self.entry.focus_get()
        except Exception:
            focused = None
        if focused is not self.entry:
            self.hide()

    def hide(self):
        if self.popup:
            self.popup.destroy()
            self.popup = None

    def _reposition(self):
        if self.popup:
            x = self.entry.winfo_rootx()
            y = self.entry.winfo_rooty() + self.entry.winfo_height() + 2
            self.popup.geometry(f"+{x}+{y}")

    def _render(self):
        import tkinter as tk
        g = self.gui
        if self.popup is None:
            self.popup = tk.Toplevel(self.entry)
            self.popup.wm_overrideredirect(True)
            self.popup.attributes("-topmost", True)
            self.popup.configure(bg=g.accent_blue)
        for child in self.popup.winfo_children():
            child.destroy()
        self._reposition()

        body = tk.Frame(self.popup, bg=g.bg_card, padx=8, pady=6)
        body.pack(fill="both", expand=True, padx=1, pady=1)
        width = max(self.entry.winfo_width(), 420)
        wrap = width - 24

        fam_label = {"gnu": "GCC / Clang / MinGW", "msvc": "MSVC", "unknown": "any compiler"}[g.toolchain_family()]
        hdr = self.title if self.catalog is CMAKE_ARG_FLAGS else f"{self.title}  ·  toolchain: {fam_label}"
        tk.Label(body, text=hdr, bg=g.bg_card, fg=g.accent_blue, font=("Sans", 9, "bold"), anchor="w").pack(fill="x")

        # Detail of the token under the cursor
        if self.detail:
            tk.Label(body, text=f"{self.typed}  —  {self.detail.desc}", bg=g.bg_card, fg=g.fg_primary,
                     font=("Sans", 9), anchor="w", justify="left", wraplength=wrap).pack(fill="x", pady=(4, 0))
            if self.detail.lang not in ("both", self.lang):
                tk.Label(body, text=f"⚠ Only applies to {'C++' if self.detail.lang == 'cxx' else 'C'} sources.",
                         bg=g.bg_card, fg=g.accent_yellow, font=("Sans", 9), anchor="w").pack(fill="x")
            if self.detail.warn:
                tk.Label(body, text=f"⚠ {self.detail.warn}", bg=g.bg_card, fg=g.accent_yellow, font=("Sans", 9),
                         anchor="w", justify="left", wraplength=wrap).pack(fill="x")
        elif self.typed and not self.matches:
            tk.Label(body, text=f"{self.typed}  —  not in the guidance list; it will be passed to the compiler as-is.",
                     bg=g.bg_card, fg=g.fg_muted, font=("Sans", 9), anchor="w", justify="left",
                     wraplength=wrap).pack(fill="x", pady=(4, 0))

        # Suggestions
        if self.matches:
            lst = tk.Frame(body, bg=g.bg_card)
            lst.pack(fill="x", pady=(6, 0))
            for i, info in enumerate(self.matches):
                selected = i == self.sel
                row_bg = "#45475a" if selected else g.bg_card
                row = tk.Frame(lst, bg=row_bg, cursor="hand2")
                row.pack(fill="x")
                fg_flag = g.accent_yellow if info.warn else g.accent_green
                l1 = tk.Label(row, text=info.flag, bg=row_bg, fg=fg_flag, font=("Monospace", 9, "bold"),
                              width=26, anchor="w")
                l1.pack(side="left")
                l2 = tk.Label(row, text=info.desc, bg=row_bg, fg=g.fg_primary if selected else g.fg_muted,
                              font=("Sans", 9), anchor="w")
                l2.pack(side="left", fill="x", expand=True)
                for w in (row, l1, l2):
                    w.bind("<Button-1>", lambda e, f=info: self._insert(f))
            if self.matches[self.sel].warn:
                tk.Label(body, text=f"⚠ {self.matches[self.sel].warn}", bg=g.bg_card, fg=g.accent_yellow,
                         font=("Sans", 9), anchor="w", justify="left", wraplength=wrap).pack(fill="x", pady=(4, 0))
            tk.Label(body, text="↑/↓ select  ·  Tab/Enter insert  ·  Esc close", bg=g.bg_card, fg=g.fg_muted,
                     font=("Sans", 8), anchor="w").pack(fill="x", pady=(4, 0))
        elif not self.typed:
            starter = "-" if g.toolchain_family() != "msvc" else "/"
            if self.catalog is CMAKE_ARG_FLAGS:
                starter = "-D"
            tk.Label(body, text=f"Start typing (e.g. '{starter}') to see suggestions. Separate flags with spaces.",
                     bg=g.bg_card, fg=g.fg_muted, font=("Sans", 9), anchor="w").pack(fill="x", pady=(4, 0))

        # Whole-field warnings summary
        if self.catalog is COMPILER_FLAGS:
            notes = [n for n in lint_flags(self.entry.get(), self.catalog, self.lang) if "not in the guidance" not in n]
            if notes:
                tk.Frame(body, bg=g.fg_muted, height=1).pack(fill="x", pady=(6, 4))
                for n in notes[:4]:
                    tk.Label(body, text=f"• {n}", bg=g.bg_card, fg=g.accent_yellow, font=("Sans", 8), anchor="w",
                             justify="left", wraplength=wrap).pack(fill="x")


class EnsembleBuildGui:
    def __init__(self, root, initial_config: BuildConfig):
        self.root = root
        self.config = initial_config
        self.builder: Optional[EnsembleBuilder] = None
        self.worker_thread: Optional[threading.Thread] = None

        self.root.title("Ensemble Build Orchestrator")
        self.root.geometry("1020x760")
        self.root.minsize(860, 640)

        self._apply_dark_theme()
        self._build_ui()
        self._update_ci_preview()

    def _apply_dark_theme(self):
        import tkinter as tk
        from tkinter import ttk

        self.bg_dark = "#181825"
        self.bg_panel = "#1e1e2e"
        self.bg_card = "#313244"
        self.bg_input = "#11111b"
        self.fg_primary = "#cdd6f4"
        self.fg_muted = "#a6adc8"
        self.accent_blue = "#89b4fa"
        self.accent_green = "#a6e3a1"
        self.accent_red = "#f38ba8"
        self.accent_yellow = "#f9e2af"

        self.root.configure(bg=self.bg_dark)

        style = ttk.Style()
        style.theme_use("clam")

        style.configure(".", background=self.bg_panel, foreground=self.fg_primary, font=("Sans", 10))
        style.configure("TFrame", background=self.bg_panel)
        style.configure("Dark.TFrame", background=self.bg_dark)
        style.configure("Card.TFrame", background=self.bg_card)
        style.configure("TLabel", background=self.bg_panel, foreground=self.fg_primary)
        style.configure("Muted.TLabel", background=self.bg_panel, foreground=self.fg_muted, font=("Sans", 9))
        style.configure("Title.TLabel", background=self.bg_dark, foreground=self.accent_blue, font=("Sans", 15, "bold"))
        style.configure("Sub.TLabel", background=self.bg_dark, foreground=self.fg_muted, font=("Sans", 9))
        style.configure("Section.TLabel", background=self.bg_card, foreground=self.accent_blue, font=("Sans", 10, "bold"))

        style.configure("TCheckbutton", background=self.bg_card, foreground=self.fg_primary)
        style.configure("TRadiobutton", background=self.bg_card, foreground=self.fg_primary)
        style.configure("TCombobox", fieldbackground=self.bg_input, foreground=self.fg_primary)

        style.configure("Primary.TButton", background=self.accent_blue, foreground="#11111b", font=("Sans", 10, "bold"), padding=6)
        style.map("Primary.TButton", background=[("active", "#b4befe")])

        style.configure("Success.TButton", background=self.accent_green, foreground="#11111b", font=("Sans", 10, "bold"), padding=6)
        style.map("Success.TButton", background=[("active", "#94e2d5")])

        style.configure("Secondary.TButton", background=self.bg_card, foreground=self.fg_primary, padding=6)
        style.map("Secondary.TButton", background=[("active", "#45475a")])

        style.configure("Danger.TButton", background=self.accent_red, foreground="#11111b", font=("Sans", 10, "bold"), padding=6)

    def _build_ui(self):
        import tkinter as tk
        from tkinter import ttk, scrolledtext, messagebox, filedialog

        main_box = ttk.Frame(self.root, style="Dark.TFrame", padding=14)
        main_box.pack(fill=tk.BOTH, expand=True)

        # Header
        hdr_frame = ttk.Frame(main_box, style="Dark.TFrame")
        hdr_frame.pack(fill=tk.X, pady=(0, 10))

        title_lbl = ttk.Label(hdr_frame, text="⚡ Ensemble Build Orchestrator", style="Title.TLabel")
        title_lbl.pack(side=tk.LEFT)

        sub_lbl = ttk.Label(hdr_frame, text="Unified compiler configuration, AppImage packaging & CI one-liner runner", style="Sub.TLabel")
        sub_lbl.pack(side=tk.LEFT, padx=(15, 0), pady=(4, 0))

        # Main splitter (Top config cards, Bottom log terminal)
        content_paned = tk.PanedWindow(main_box, orient=tk.VERTICAL, bg=self.bg_dark, bd=0, sashwidth=4)
        content_paned.pack(fill=tk.BOTH, expand=True)

        # Config Panel
        config_container = ttk.Frame(content_paned, style="Dark.TFrame")
        content_paned.add(config_container, minsize=290)

        # Grid layout for settings
        grid_frame = ttk.Frame(config_container, style="Dark.TFrame")
        grid_frame.pack(fill=tk.BOTH, expand=True)
        grid_frame.columnconfigure(0, weight=1)
        grid_frame.columnconfigure(1, weight=1)

        # --- Card 1: Target & Build Type ---
        c1 = ttk.Frame(grid_frame, style="Card.TFrame", padding=10)
        c1.grid(row=0, column=0, sticky="nsew", padx=4, pady=4)

        ttk.Label(c1, text="TARGET & MODE", style="Section.TLabel").pack(anchor=tk.W, pady=(0, 6))

        # Target selector
        self.var_target = tk.StringVar(value=self.config.target)
        t_row = ttk.Frame(c1, style="Card.TFrame")
        t_row.pack(fill=tk.X, pady=2)
        ttk.Label(t_row, text="Target:", background=self.bg_card, width=12).pack(side=tk.LEFT)
        for t_val, t_txt in [("linux", "Linux (ELF)"), ("appimage", "AppImage"), ("windows", "Windows (MinGW/MSVC)")]:
            r = ttk.Radiobutton(t_row, text=t_txt, value=t_val, variable=self.var_target, command=self._update_ci_preview)
            r.pack(side=tk.LEFT, padx=4)

        # Build Type selector
        self.var_btype = tk.StringVar(value=self.config.build_type)
        b_row = ttk.Frame(c1, style="Card.TFrame")
        b_row.pack(fill=tk.X, pady=4)
        ttk.Label(b_row, text="Build Type:", background=self.bg_card, width=12).pack(side=tk.LEFT)
        for b_val in ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"]:
            r = ttk.Radiobutton(b_row, text=b_val, value=b_val, variable=self.var_btype, command=self._update_ci_preview)
            r.pack(side=tk.LEFT, padx=4)

        # Options checkboxes
        opts_row = ttk.Frame(c1, style="Card.TFrame")
        opts_row.pack(fill=tk.X, pady=(6, 2))
        self.var_clean = tk.BooleanVar(value=self.config.clean)
        self.var_tests = tk.BooleanVar(value=self.config.run_tests)
        self.var_download = tk.BooleanVar(value=self.config.download_tools)

        chk1 = ttk.Checkbutton(opts_row, text="Clean First", variable=self.var_clean, command=self._update_ci_preview)
        chk1.pack(side=tk.LEFT, padx=4)
        chk2 = ttk.Checkbutton(opts_row, text="Run Tests", variable=self.var_tests, command=self._update_ci_preview)
        chk2.pack(side=tk.LEFT, padx=4)
        chk3 = ttk.Checkbutton(opts_row, text="Auto-fetch AppImage Tools", variable=self.var_download, command=self._update_ci_preview)
        chk3.pack(side=tk.LEFT, padx=4)

        inst_row = ttk.Frame(c1, style="Card.TFrame")
        inst_row.pack(fill=tk.X, pady=(2, 2))
        self.var_installer = tk.BooleanVar(value=self.config.installer)
        chk4 = ttk.Checkbutton(inst_row, text="Inno Setup Installer (.exe)", variable=self.var_installer,
                               command=self._update_ci_preview)
        chk4.pack(side=tk.LEFT, padx=4)
        HoverTip(chk4, "Windows target only. After packaging the zip, compiles resources/ensemble.iss with "
                       "Inno Setup 6 (ISCC.exe) into Ensemble-<version>-windows-x64-setup.exe.\n"
                       "On Linux, ISCC.exe is run through Wine.", bg=self.bg_input, fg=self.fg_primary)
        self.lbl_installer_hint = ttk.Label(inst_row, text="", background=self.bg_card, foreground=self.accent_yellow,
                                            font=("Sans", 8))
        self.lbl_installer_hint.pack(side=tk.LEFT, padx=6)

        # --- Card 2: Versioning & Codename ---
        c2 = ttk.Frame(grid_frame, style="Card.TFrame", padding=10)
        c2.grid(row=0, column=1, sticky="nsew", padx=4, pady=4)

        ttk.Label(c2, text="VERSIONING & IDENTIFIERS", style="Section.TLabel").pack(anchor=tk.W, pady=(0, 6))

        v_row = ttk.Frame(c2, style="Card.TFrame")
        v_row.pack(fill=tk.X, pady=2)
        ttk.Label(v_row, text="Version ID:", background=self.bg_card, width=15).pack(side=tk.LEFT)
        self.entry_version = tk.Entry(v_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_version.insert(0, self.config.version_id)
        self.entry_version.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.entry_version.bind("<KeyRelease>", lambda e: self._update_ci_preview())

        l_row = ttk.Frame(c2, style="Card.TFrame")
        l_row.pack(fill=tk.X, pady=4)
        ttk.Label(l_row, text="Codename / Local:", background=self.bg_card, width=15).pack(side=tk.LEFT)
        self.entry_localversion = tk.Entry(l_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_localversion.insert(0, self.config.localversion)
        self.entry_localversion.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.entry_localversion.bind("<KeyRelease>", lambda e: self._update_ci_preview())

        syn_row = ttk.Frame(c2, style="Card.TFrame")
        syn_row.pack(fill=tk.X, pady=2)
        ttk.Label(syn_row, text="Synthesized Tag:", background=self.bg_card, width=15).pack(side=tk.LEFT)
        self.lbl_synthesized = ttk.Label(syn_row, text=self.config.full_version(), foreground=self.accent_green, background=self.bg_card, font=("Sans", 10, "bold"))
        self.lbl_synthesized.pack(side=tk.LEFT)

        # --- Card 3: Paths & Build Directory ---
        c3 = ttk.Frame(grid_frame, style="Card.TFrame", padding=10)
        c3.grid(row=1, column=0, sticky="nsew", padx=4, pady=4)

        ttk.Label(c3, text="LOCATIONS & OUTPUT", style="Section.TLabel").pack(anchor=tk.W, pady=(0, 6))

        # Output dir
        out_row = ttk.Frame(c3, style="Card.TFrame")
        out_row.pack(fill=tk.X, pady=2)
        ttk.Label(out_row, text="Output Directory:", background=self.bg_card, width=15).pack(side=tk.LEFT)
        self.entry_output_dir = tk.Entry(out_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_output_dir.insert(0, self.config.output_dir)
        self.entry_output_dir.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        self.entry_output_dir.bind("<KeyRelease>", lambda e: self._update_ci_preview())
        btn_browse_out = ttk.Button(out_row, text="Browse", style="Secondary.TButton", command=lambda: self._browse_dir(self.entry_output_dir))
        btn_browse_out.pack(side=tk.LEFT)

        # Build dir
        bld_row = ttk.Frame(c3, style="Card.TFrame")
        bld_row.pack(fill=tk.X, pady=2)
        ttk.Label(bld_row, text="Build Tree Dir:", background=self.bg_card, width=15).pack(side=tk.LEFT)
        self.entry_build_dir = tk.Entry(bld_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_build_dir.insert(0, self.config.build_dir)
        self.entry_build_dir.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        self.entry_build_dir.bind("<KeyRelease>", lambda e: self._update_ci_preview())
        btn_browse_bld = ttk.Button(bld_row, text="Browse", style="Secondary.TButton", command=lambda: self._browse_dir(self.entry_build_dir))
        btn_browse_bld.pack(side=tk.LEFT)

        # Inno Setup compiler path
        iscc_row = ttk.Frame(c3, style="Card.TFrame")
        iscc_row.pack(fill=tk.X, pady=2)
        lbl_iscc = ttk.Label(iscc_row, text="ISCC.exe Path:", background=self.bg_card, width=15)
        lbl_iscc.pack(side=tk.LEFT)
        self.entry_iscc = tk.Entry(iscc_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_iscc.insert(0, self.config.iscc_path)
        self.entry_iscc.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4))
        self.entry_iscc.bind("<KeyRelease>", lambda e: self._update_ci_preview())
        btn_browse_iscc = ttk.Button(iscc_row, text="Browse", style="Secondary.TButton", command=self._browse_iscc)
        btn_browse_iscc.pack(side=tk.LEFT)
        HoverTip(lbl_iscc, "Optional. Leave empty to auto-detect Inno Setup 6 in PATH, Program Files, "
                           "%LOCALAPPDATA%\\Programs, or (Linux) $WINEPREFIX/drive_c and tools/InnoSetup/.",
                 bg=self.bg_input, fg=self.fg_primary)

        # --- Card 4: Compiler Flags & CMake Arguments ---
        c4 = ttk.Frame(grid_frame, style="Card.TFrame", padding=10)
        c4.grid(row=1, column=1, sticky="nsew", padx=4, pady=4)

        ttk.Label(c4, text="COMPILER & BUILD FLAGS", style="Section.TLabel").pack(anchor=tk.W, pady=(0, 6))

        cxx_row = ttk.Frame(c4, style="Card.TFrame")
        cxx_row.pack(fill=tk.X, pady=2)
        lbl_cxx = ttk.Label(cxx_row, text="Extra CXXFLAGS ⓘ", background=self.bg_card, width=18)
        lbl_cxx.pack(side=tk.LEFT)
        self.entry_cxxflags = tk.Entry(cxx_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_cxxflags.insert(0, self.config.cxxflags)
        self.entry_cxxflags.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.entry_cxxflags.bind("<KeyRelease>", lambda e: self._update_ci_preview())

        c_row = ttk.Frame(c4, style="Card.TFrame")
        c_row.pack(fill=tk.X, pady=2)
        lbl_c = ttk.Label(c_row, text="Extra CFLAGS ⓘ", background=self.bg_card, width=18)
        lbl_c.pack(side=tk.LEFT)
        self.entry_cflags = tk.Entry(c_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_cflags.insert(0, self.config.cflags)
        self.entry_cflags.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.entry_cflags.bind("<KeyRelease>", lambda e: self._update_ci_preview())

        cmake_row = ttk.Frame(c4, style="Card.TFrame")
        cmake_row.pack(fill=tk.X, pady=2)
        lbl_cmake = ttk.Label(cmake_row, text="Extra CMake Args ⓘ", background=self.bg_card, width=18)
        lbl_cmake.pack(side=tk.LEFT)
        self.entry_cmake_args = tk.Entry(cmake_row, bg=self.bg_input, fg=self.fg_primary, insertbackground=self.fg_primary, bd=1, relief=tk.FLAT)
        self.entry_cmake_args.insert(0, self.config.cmake_args)
        self.entry_cmake_args.pack(side=tk.LEFT, fill=tk.X, expand=True)
        self.entry_cmake_args.bind("<KeyRelease>", lambda e: self._update_ci_preview())

        tip_kw = dict(bg=self.bg_input, fg=self.fg_primary)
        HoverTip(lbl_cxx, "Appended to CMAKE_CXX_FLAGS (via ENSEMBLE_EXTRA_CXX_FLAGS) for every C++ file.\n"
                          "Space-separated, e.g.  -O2 -march=x86-64-v2 -Wall\n"
                          "Click the field and start typing for suggestions.", **tip_kw)
        HoverTip(lbl_c, "Appended to CMAKE_C_FLAGS (via ENSEMBLE_EXTRA_C_FLAGS) for C sources only.\n"
                        "Most of Ensemble is C++, so CXXFLAGS is usually what you want.", **tip_kw)
        HoverTip(lbl_cmake, "Raw arguments added to the 'cmake -B ... -S ...' configure step,\n"
                            "e.g.  -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON", **tip_kw)
        self.flag_assists = [
            FlagAssist(self, self.entry_cxxflags, COMPILER_FLAGS, "cxx", "C++ compiler flags", self._update_ci_preview),
            FlagAssist(self, self.entry_cflags, COMPILER_FLAGS, "c", "C compiler flags", self._update_ci_preview),
            FlagAssist(self, self.entry_cmake_args, CMAKE_ARG_FLAGS, "both", "CMake configure arguments", self._update_ci_preview),
        ]

        # --- CI One-Liner Banner & Action Toolbar ---
        action_bar = ttk.Frame(config_container, style="Dark.TFrame", padding=(0, 8, 0, 0))
        action_bar.pack(fill=tk.X)

        ci_box = ttk.Frame(action_bar, style="Dark.TFrame")
        ci_box.pack(fill=tk.X, pady=(0, 8))

        ttk.Label(ci_box, text="CI One-Liner:", style="Sub.TLabel").pack(side=tk.LEFT, padx=(0, 6))
        self.entry_ci = tk.Entry(ci_box, bg=self.bg_input, fg=self.accent_yellow, font=("Monospace", 9), bd=1, relief=tk.FLAT)
        self.entry_ci.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))

        btn_copy = ttk.Button(ci_box, text="📋 Copy One-Liner", style="Secondary.TButton", command=self._copy_ci_command)
        btn_copy.pack(side=tk.LEFT)

        btn_strip = ttk.Frame(action_bar, style="Dark.TFrame")
        btn_strip.pack(fill=tk.X)

        self.btn_build = ttk.Button(btn_strip, text="🔨 Build", style="Primary.TButton", command=self._on_click_build)
        self.btn_build.pack(side=tk.LEFT, padx=(0, 6))

        self.btn_test = ttk.Button(btn_strip, text="🧪 Run Tests", style="Secondary.TButton", command=self._on_click_test)
        self.btn_test.pack(side=tk.LEFT, padx=4)

        self.btn_pkg = ttk.Button(btn_strip, text="📦 Package", style="Success.TButton", command=self._on_click_package)
        self.btn_pkg.pack(side=tk.LEFT, padx=4)

        self.btn_clean = ttk.Button(btn_strip, text="🧹 Clean", style="Secondary.TButton", command=self._on_click_clean)
        self.btn_clean.pack(side=tk.LEFT, padx=4)

        self.btn_cancel = ttk.Button(btn_strip, text="⏹ Stop", style="Danger.TButton", command=self._on_click_cancel, state=tk.DISABLED)
        self.btn_cancel.pack(side=tk.LEFT, padx=8)

        self.lbl_status = ttk.Label(btn_strip, text="Ready", style="Sub.TLabel")
        self.lbl_status.pack(side=tk.RIGHT, padx=6)

        # --- Terminal Log Window ---
        log_container = ttk.Frame(content_paned, style="Dark.TFrame")
        content_paned.add(log_container, minsize=200)

        log_hdr = ttk.Frame(log_container, style="Dark.TFrame")
        log_hdr.pack(fill=tk.X, pady=(6, 2))
        ttk.Label(log_hdr, text="Console Output", style="Sub.TLabel").pack(side=tk.LEFT)

        btn_clear = ttk.Button(log_hdr, text="Clear", style="Secondary.TButton", command=self._clear_logs)
        btn_clear.pack(side=tk.RIGHT)

        self.log_text = scrolledtext.ScrolledText(
            log_container,
            wrap=tk.CHAR,
            bg=self.bg_input,
            fg=self.fg_primary,
            insertbackground=self.fg_primary,
            font=("Monospace", 9),
            bd=0,
            padx=8,
            pady=8,
        )
        self.log_text.pack(fill=tk.BOTH, expand=True)

        self.log_text.tag_config("cyan", foreground=self.accent_blue)
        self.log_text.tag_config("green", foreground=self.accent_green)
        self.log_text.tag_config("red", foreground=self.accent_red)
        self.log_text.tag_config("yellow", foreground=self.accent_yellow)
        self.log_text.tag_config("muted", foreground=self.fg_muted)

    def _browse_dir(self, entry_widget):
        from tkinter import filedialog
        path = filedialog.askdirectory(initialdir=str(REPO_ROOT))
        if path:
            rel = os.path.relpath(path, str(REPO_ROOT))
            entry_widget.delete(0, "end")
            entry_widget.insert(0, rel)
            self._update_ci_preview()

    def _browse_iscc(self):
        from tkinter import filedialog
        path = filedialog.askopenfilename(
            title="Locate Inno Setup compiler (ISCC.exe)",
            filetypes=[("Inno Setup compiler", "ISCC.exe iscc.exe ISCC iscc"), ("All files", "*")],
        )
        if path:
            self.entry_iscc.delete(0, "end")
            self.entry_iscc.insert(0, path)
            self._update_ci_preview()

    def toolchain_family(self) -> str:
        """Best guess of the compiler family for the selected target ('gnu', 'msvc' or 'unknown')."""
        target = self.var_target.get()
        if target in ("linux", "appimage"):
            return "gnu"
        if platform.system() != "Windows":
            return "gnu"  # cross-compiling with MinGW
        if shutil.which("cl"):
            return "msvc"
        if shutil.which("g++") or shutil.which("x86_64-w64-mingw32-g++"):
            return "gnu"
        return "unknown"

    def _update_ci_preview(self):
        cfg = self._extract_config()
        self.lbl_synthesized.configure(text=cfg.full_version())
        ci_cmd = cfg.to_ci_command()
        self.entry_ci.delete(0, "end")
        self.entry_ci.insert(0, ci_cmd)
        if cfg.installer and cfg.target != "windows":
            self.lbl_installer_hint.configure(text="⚠ only used with the Windows target")
        else:
            self.lbl_installer_hint.configure(text="")
        # Toolchain may have changed with the target -> refresh any open flag popup
        for fa in getattr(self, "flag_assists", []):
            if fa.popup:
                fa.refresh()

    def _copy_ci_command(self):
        cmd = self.entry_ci.get()
        self.root.clipboard_clear()
        self.root.clipboard_append(cmd)
        self.lbl_status.configure(text="Copied CI command to clipboard! ✓", foreground=self.accent_green)
        self.root.after(3000, lambda: self.lbl_status.configure(text="Ready", foreground=self.fg_muted))

    def _clear_logs(self):
        self.log_text.delete("1.0", "end")

    def append_log(self, text: str):
        def _insert():
            tag = None
            if text.startswith("[FAILED]") or text.startswith("[ERROR]") or text.startswith("FAILED:"):
                tag = "red"
            elif text.startswith("[SUCCESS]") or text.startswith("[COMPLETE]") or "Passed" in text:
                tag = "green"
            elif text.startswith("==") or text.startswith(">>") or text.startswith("[CONFIG]") or text.startswith("[BUILD]") or text.startswith("[INNO]"):
                tag = "cyan"
            elif text.startswith("[WARN]") or text.startswith("ninja: warning"):
                tag = "yellow"

            if tag:
                self.log_text.insert("end", text, tag)
            else:
                self.log_text.insert("end", text)
            self.log_text.see("end")

        self.root.after(0, _insert)

    def _extract_config(self) -> BuildConfig:
        return BuildConfig(
            target=self.var_target.get(),
            build_type=self.var_btype.get(),
            build_dir=self.entry_build_dir.get().strip(),
            output_dir=self.entry_output_dir.get().strip(),
            version_id=self.entry_version.get().strip(),
            localversion=self.entry_localversion.get().strip(),
            cflags=self.entry_cflags.get().strip(),
            cxxflags=self.entry_cxxflags.get().strip(),
            cmake_args=self.entry_cmake_args.get().strip(),
            clean=self.var_clean.get(),
            run_tests=self.var_tests.get(),
            download_tools=self.var_download.get(),
            installer=self.var_installer.get(),
            iscc_path=self.entry_iscc.get().strip(),
        )

    def _run_worker(self, target_func: Callable[[EnsembleBuilder], int]):
        if self.worker_thread and self.worker_thread.is_alive():
            return

        cfg = self._extract_config()
        self.builder = EnsembleBuilder(cfg, log_cb=self.append_log)

        self.btn_build.configure(state="disabled")
        self.btn_test.configure(state="disabled")
        self.btn_pkg.configure(state="disabled")
        self.btn_clean.configure(state="disabled")
        self.btn_cancel.configure(state="normal")
        self.lbl_status.configure(text="Running...", foreground=self.accent_blue)

        def worker():
            try:
                ret = target_func(self.builder)
                if ret == 0:
                    status_msg = "Completed successfully ✓"
                    color = self.accent_green
                elif ret == 130:
                    status_msg = "Process stopped."
                    color = self.accent_yellow
                else:
                    status_msg = f"Failed (exit code {ret}) ✗"
                    color = self.accent_red
            except Exception as e:
                self.append_log(f"[EXCEPTION] {e}\n")
                status_msg = "Error occurred."
                color = self.accent_red

            def finish():
                self.btn_build.configure(state="normal")
                self.btn_test.configure(state="normal")
                self.btn_pkg.configure(state="normal")
                self.btn_clean.configure(state="normal")
                self.btn_cancel.configure(state="disabled")
                self.lbl_status.configure(text=status_msg, foreground=color)

            self.root.after(0, finish)

        self.worker_thread = threading.Thread(target=worker, daemon=True)
        self.worker_thread.start()

    def _on_click_build(self):
        self._run_worker(lambda b: b.execute_all())

    def _on_click_test(self):
        def run_test_only(b: EnsembleBuilder):
            b.configure()
            b.build()
            return b.test()
        self._run_worker(run_test_only)

    def _on_click_package(self):
        def run_package_only(b: EnsembleBuilder):
            b.configure()
            b.build()
            if b.config.target in ("appimage", "linux"):
                return b.package_appimage()
            elif b.config.target == "windows":
                return b.package_windows()
            return 0
        self._run_worker(run_package_only)

    def _on_click_clean(self):
        self._run_worker(lambda b: b.clean())

    def _on_click_cancel(self):
        if self.builder:
            self.builder.cancel()


# ==============================================================================
# CLI Entrypoint & Parser
# ==============================================================================

def parse_args() -> tuple[argparse.Namespace, List[str]]:
    parser = argparse.ArgumentParser(
        description="Ensemble Build Orchestrator: Unified build & packaging CLI/GUI",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 build.py --target linux --build-type Release
  python3 build.py --target appimage --version-id 1.2.0 --localversion agnestachyon --package
  python3 build.py --target windows --cxxflags="-O3" --build-dir build-win
  python3 build.py --target windows --build-type Release --installer
  python3 build.py --list-flags
  python3 build.py --test
  python3 build.py --gui

Note: flag values beginning with '-' must use the '=' form, e.g. --cxxflags="-O2 -Wall".
        """,
    )

    parser.add_argument("shorthand", nargs="*", help="Optional shorthand commands (e.g. debug, release, test, clean, appimage, win, installer)")
    parser.add_argument("--target", "-t", choices=["linux", "windows", "appimage"], default="linux", help="Target platform / package format")
    parser.add_argument("--build-type", "-b", choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"], default="Debug", help="CMake build configuration")
    parser.add_argument("--build-dir", default="", help="Path to CMake build directory")
    parser.add_argument("--output-dir", "-o", default="", help="Output directory for binaries / packages")
    parser.add_argument("--version-id", default=DEFAULT_VERSION, help="Base version ID override (default: 1.1.1)")
    parser.add_argument("--localversion", default="", help="Localversion / codename tag (e.g. agnestachyon)")
    parser.add_argument("--cflags", default="", help='Custom C compiler flags, e.g. --cflags="-O2" (see --list-flags)')
    parser.add_argument("--cxxflags", default="", help='Custom C++ compiler flags, e.g. --cxxflags="-O2 -Wall" (see --list-flags)')
    parser.add_argument("--cmake-args", default="", help='Extra raw arguments forwarded to CMake, e.g. --cmake-args="-GNinja"')
    parser.add_argument("--clean", action="store_true", help="Clean build directory before configuring")
    parser.add_argument("--test", dest="run_tests", action="store_true", help="Run ctest unit tests after building")
    parser.add_argument("--package", action="store_true", help="Package distribution artifact (AppImage or zip)")
    parser.add_argument("--installer", action="store_true", help="Windows target: also build an Inno Setup installer (.exe)")
    parser.add_argument("--iscc", default="", help="Custom path to the Inno Setup compiler ISCC.exe (run via Wine on Linux)")
    parser.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 4, help="Parallel build concurrency")
    parser.add_argument("--appimagetool", default="", help="Custom path to appimagetool binary")
    parser.add_argument("--download-tools", action="store_true", help="Auto-download appimagetool if not found")
    parser.add_argument("--list-flags", action="store_true", help="Print the compiler/CMake flag guidance list and exit")
    parser.add_argument("--gui", action="store_true", help="Launch interactive graphical interface")
    parser.add_argument("--cli", "--no-gui", dest="no_gui", action="store_true", help="Force command-line mode")

    return parser.parse_known_args()


def print_flag_guide():
    def section(title: str, items: List[FlagInfo]):
        print(f"\n{title}\n" + "-" * len(title))
        for f in items:
            scope = {"cxx": " [C++]", "c": " [C]"}.get(f.lang, "")
            print(f"  {f.flag:<40} {f.desc}{scope}")
            if f.warn:
                print(f"  {'':<40} ! {f.warn}")

    section("GCC / Clang / MinGW  (--cxxflags / --cflags)", [f for f in COMPILER_FLAGS if f.family == "gnu"])
    section("MSVC  (--cxxflags / --cflags)", [f for f in COMPILER_FLAGS if f.family == "msvc"])
    section("CMake configure  (--cmake-args)", CMAKE_ARG_FLAGS)
    print()


def main():
    args, unknown = parse_args()

    if args.list_flags:
        print_flag_guide()
        return 0

    # Process positional shorthand tokens for convenience
    target = args.target
    build_type = args.build_type
    run_clean = args.clean
    run_tests = args.run_tests
    run_package = args.package
    build_installer = args.installer

    for token in args.shorthand:
        low = token.lower()
        if low in ("release", "--release"):
            build_type = "Release"
        elif low in ("debug", "--debug"):
            build_type = "Debug"
        elif low in ("relwithdebinfo",):
            build_type = "RelWithDebInfo"
        elif low in ("minsizerel",):
            build_type = "MinSizeRel"
        elif low in ("win", "windows", "mingw", "--win"):
            target = "windows"
        elif low in ("linux", "--linux"):
            target = "linux"
        elif low in ("appimage", "--appimage"):
            target = "appimage"
            run_package = True
        elif low in ("clean", "--clean"):
            run_clean = True
        elif low in ("test", "tests", "--test"):
            run_tests = True
        elif low in ("package", "--package"):
            run_package = True
        elif low in ("installer", "inno", "setup"):
            target = "windows"
            build_installer = True

    config = BuildConfig(
        target=target,
        build_type=build_type,
        build_dir=args.build_dir,
        output_dir=args.output_dir,
        version_id=args.version_id,
        localversion=args.localversion,
        cflags=args.cflags,
        cxxflags=args.cxxflags,
        cmake_args=args.cmake_args,
        clean=run_clean,
        run_tests=run_tests,
        package=run_package,
        jobs=args.jobs,
        appimagetool_path=args.appimagetool,
        download_tools=args.download_tools,
        installer=build_installer,
        iscc_path=args.iscc,
    )

    # Launch GUI if explicitly requested OR if no CLI arguments and DISPLAY is set and not forced CLI
    wants_gui = args.gui or (not args.no_gui and len(sys.argv) == 1 and bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY") or platform.system() == "Windows"))

    if wants_gui:
        try:
            import tkinter as tk
            root = tk.Tk()
            app = EnsembleBuildGui(root, config)
            root.mainloop()
            return 0
        except Exception as e:
            sys.stderr.write(f"[WARN] Failed to start GUI ({e}), falling back to CLI.\n")

    # CLI Execution
    builder = EnsembleBuilder(config)
    ret = builder.execute_all()
    sys.exit(ret)


if __name__ == "__main__":
    main()

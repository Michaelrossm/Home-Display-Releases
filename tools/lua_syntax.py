"""Parse packages with the pinned firmware Lua, without executing their code."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

class LuaSyntaxChecker:
    def __init__(self, runner="lua_syntax_check.c"):
        # Runtime tests explicitly select the separate host-only runner. The
        # package validator keeps its parse-only default and never executes apps.
        if runner not in ("lua_syntax_check.c", "lua_runtime_check.c"):
            raise ValueError("Unknown Lua checker runner")
        self.runner = runner

    def __enter__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="home-display-lua-syntax-")
        folder = Path(self.temp.name)
        lua = ROOT / "managed_components/espressif__lua/lua"
        sources = [str(p) for p in lua.glob("*.c") if p.name not in ("lua.c", "luac.c", "onelua.c")]
        if not sources or not (lua / "lua.h").is_file():
            self.temp.cleanup()
            raise RuntimeError("Pinned Lua dependency missing; run tools/dev.py build first")
        self.exe = folder / ("check.exe" if os.name == "nt" else "check")
        compiler = shutil.which("gcc") or shutil.which("clang")
        if compiler:
            cmd = [compiler, "-O1", "-DLUA_32BITS=1", "-I", str(lua), str(ROOT / "tools" / self.runner), *sources, "-o", str(self.exe)]
            if os.name != "nt": cmd += ["-lm", "-ldl"]
        else:
            vcvars = Path("C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
            if not vcvars.exists():
                vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
                if vswhere.exists():
                    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
                    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
            if not vcvars.exists():
                self.temp.cleanup()
                raise RuntimeError("Host C compiler required for package syntax validation")
            arguments = " ".join(f'"{p}"' for p in [str(ROOT / "tools" / self.runner), *sources])
            cmd = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /O1 /DLUA_32BITS=1 /I"{lua}" {arguments} /Fe:check.exe"'
        result = subprocess.run(cmd, cwd=folder, capture_output=True, text=True)
        if result.returncode:
            self.temp.cleanup()
            raise RuntimeError(result.stdout[-2000:] + result.stderr[-2000:])
        return self

    def check(self, content, name):
        result = subprocess.run([str(self.exe), "-"], input=content, capture_output=True)
        if result.returncode:
            raise ValueError(f"{name}: {result.stderr.decode('utf-8', errors='replace').strip()}")

    def __exit__(self, *args):
        self.temp.cleanup()

#!/usr/bin/env python3
from __future__ import annotations
import argparse, shutil, subprocess, sys, os, time, configparser
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_ENV="home_display_p4_legacy_dev"
PIO_VERSION="6.1.19"
VALID_ENVS=("home_display_p4_r3_dev","home_display_p4_r3_prod",
            "home_display_p4_legacy_dev","home_display_p4_legacy_prod")

def run(cmd,check=True):
    print("+"," ".join(map(str,cmd)),flush=True)
    r=subprocess.run(cmd,cwd=ROOT)
    if check and r.returncode: raise SystemExit(r.returncode)
    return r.returncode

def tool_python():
    local=ROOT/'.pio/cli'/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    return str(local) if local.is_file() else sys.executable

def pio(bootstrap=False):
    candidates=[]
    local=ROOT/'.pio/cli'/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    if local.is_file():candidates.append([str(local),'-m','platformio'])
    e=shutil.which('pio') or shutil.which('platformio')
    if e:candidates.append([e])
    candidates.append([sys.executable,'-m','platformio'])
    for c in candidates:
        try:
            r=subprocess.run(c+['--version'],capture_output=True,text=True)
            if r.returncode==0 and r.stdout.strip().endswith(' '+PIO_VERSION):return c
        except OSError:pass
    if bootstrap:
        setup()
        return [str(local),'-m','platformio']
    return None

def setup():
    local=ROOT/'.pio/cli'
    python=local/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    if not python.is_file():run([sys.executable,'-m','venv',str(local)])
    run([str(python),'-m','pip','install','--disable-pip-version-check','--quiet','-r',str(ROOT/'requirements-dev.txt')])
    return 0

def doctor():
    print("Home-Display developer doctor")
    print("repo:",ROOT)
    print("python:",sys.version.split()[0])
    print("platformio:"," ".join(pio() or ["MISSING"]))
    print("default env:",DEFAULT_ENV)
    config=configparser.ConfigParser(interpolation=None)
    config.read(ROOT/'platformio.ini',encoding='utf-8')
    print("PlatformIO isolation:",config.get('platformio','core_dir',fallback='GLOBAL (not isolated)'))
    req=["platformio.ini","partitions_32mb.csv","sdkconfig.defaults",
         "boards/waveshare_home_display_p4_r3.json",
         "boards/waveshare_home_display_p4_legacy.json",
         "config/sdkconfig.rev3_x","config/sdkconfig.rev1_3",
         "src/idf_component.yml","CMakeLists.txt","src/CMakeLists.txt",
         "AGENTS.md","CODEX_HANDOFF.md"]
    miss=[x for x in req if not (ROOT/x).is_file()]
    if miss:
        for x in miss: print("MISSING:",x)
        return 2
    print("source layout: OK")
    if not pio():
        print("Pinned PlatformIO",PIO_VERSION,"missing. Run: python tools/dev.py setup")
        return 1
    return 0

def test():
    run([tool_python(),"-m","unittest","discover","-s","tests","-p","test_*.py"])
    d=ROOT/"tests"/"architecture"
    if d.is_dir():
        for f in sorted(d.glob("test_*.py")):
            run([tool_python(),str(f)])
    return 0

def validate(prod=False):
    c=[tool_python(),"tools/release_validator.py","--source-only"]
    if prod:c.append("--production")
    return run(c,False)

def build(env):
    c=pio(bootstrap=True)
    if not c:
        print("PlatformIO missing. Run bootstrap first."); return 3
    result = run(c+["run","-e",env],False)
    if result: return result
    # Do not certify a binary built using an older cached silicon profile.
    header = ROOT / ".pio" / "build" / env / "config" / "sdkconfig.h"
    required = "CONFIG_ESP32P4_REV_MIN_100" if "legacy" in env else "CONFIG_ESP32P4_REV_MIN_300"
    if not header.is_file() or f"#define {required} 1" not in header.read_text():
        print("ERROR: generated silicon revision does not match", env)
        print("Preserve or remove stale generated sdkconfig/build state, then rebuild. Do not flash.")
        return 4
    return 0

def clean(env):
    c=pio()
    return 3 if not c else run(c+["run","-e",env,"-t","clean"],False)

def upload(env,port):
    c=pio()
    if not c:return 3
    a=c+["run","-e",env,"-t","upload"]
    if port:a+=["--upload-port",port]
    return run(a,False)

def monitor(port):
    c=pio()
    if not c:return 3
    a=c+["device","monitor","-b","115200"]
    if port:a+=["-p",port]
    return run(a,False)


def deepclean():
    # Source-safe cleanup for stale PlatformIO / IDF Component Manager state.
    # Do not touch source or user data.
    for rel in (
        ".pio/build",
        ".pio/libdeps",
        "managed_components",
        "sdkconfig.home_display_p4_r3_dev",
        "sdkconfig.home_display_p4_r3_prod",
        "sdkconfig.home_display_p4_legacy_dev",
        "sdkconfig.home_display_p4_legacy_prod",
    ):
        p = ROOT / rel
        if not p.exists():
            continue
        for attempt in range(5):
            try:
                if p.is_dir():
                    shutil.rmtree(p)
                else:
                    p.unlink()
                print("removed:", rel)
                break
            except PermissionError:
                if attempt == 4:
                    print("LOCKED:", rel)
                    print("Close VS Code build terminals / Explorer previews, then run:")
                    print("  powershell -ExecutionPolicy Bypass -File tools/clean_windows_component_locks.ps1")
                    return 4
                time.sleep(0.5)
    return 0

def main():
    ap=argparse.ArgumentParser(description="Home-Display developer entry point")
    sp=ap.add_subparsers(dest="cmd",required=True)
    sp.add_parser("doctor"); sp.add_parser("test"); sp.add_parser("deepclean"); sp.add_parser("setup")
    v=sp.add_parser("validate"); v.add_argument("--production",action="store_true")
    for n in ("build","clean","upload"):
        q=sp.add_parser(n); q.add_argument("--env",choices=VALID_ENVS,default=DEFAULT_ENV)
        if n=="upload":q.add_argument("--port")
    m=sp.add_parser("monitor");m.add_argument("--port")
    a=ap.parse_args()
    return {"doctor":lambda:doctor(),"test":lambda:test(),
            "validate":lambda:validate(a.production),"deepclean":lambda:deepclean(),"setup":lambda:setup(),
            "build":lambda:build(a.env),"clean":lambda:clean(a.env),
            "upload":lambda:upload(a.env,a.port),
            "monitor":lambda:monitor(a.port)}[a.cmd]()

if __name__=="__main__": raise SystemExit(main())

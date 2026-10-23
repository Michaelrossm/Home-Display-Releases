"""Package the printer Lua app without installing or flashing it."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import json
import argparse
from lua_syntax import LuaSyntaxChecker

ROOT = Path(__file__).resolve().parents[1]

def package(source=None, output=None):
    source = Path(source) if source else ROOT / "apps/printer_panel_professional"
    manifest = json.loads((source / "app.json").read_text(encoding="utf-8"))
    assert manifest["id"] == "printer_panel" and manifest["schema"] in (2,3)
    assert manifest["settingsEntry"] == "settings.lua"
    with LuaSyntaxChecker() as checker:
        for name in ("main.lua", "settings.lua"):
            checker.check((source / name).read_bytes(), name)
    assert json.loads((source / "web_settings.json").read_text(encoding="utf-8"))["schema"] == 1
    target = Path(output) if output else ROOT / "dist" / f"3D-Drucker-HomeDisplay-v{manifest['version']}.zip"
    if target.exists():
        raise FileExistsError(f"Existing package is preserved: {target}")
    target.parent.mkdir(exist_ok=True)
    with ZipFile(target, "w", ZIP_DEFLATED) as archive:
        for name in ("app.json", "main.lua", "settings.lua", "web_settings.json"):
            archive.write(source / name, name)
    print(target)
    return target

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    package(args.source,args.output)

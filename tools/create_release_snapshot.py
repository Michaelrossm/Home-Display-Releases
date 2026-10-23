#!/usr/bin/env python3
"""Export the current tracked sources, never Git history, caches or private keys."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
PRIVATE_AUDIT_DOCS={'docs/NATIVE-INTERCOM.md','docs/GOLIATH-PHASE2.md','.vscode/settings.json'}

def export(destination):
    destination=Path(destination).resolve()
    if destination==ROOT or ROOT.is_relative_to(destination):
        raise ValueError('Snapshot must not contain the working repository')
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Snapshot destination must be empty')
    destination.mkdir(parents=True,exist_ok=True)
    files=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf-8').split('\0')
    inventory={}
    for name in filter(None,files):
        # Retain private field reports locally, never publish device/household details.
        if name in PRIVATE_AUDIT_DOCS:continue
        source=ROOT/name
        if not source.is_file():continue
        lower=name.lower()
        if any(part in ('.git','.pio','managed_components','__pycache__') for part in Path(name).parts):
            raise ValueError('Cache/internal file tracked: '+name)
        if lower.startswith(('apps/','release/')) or lower.endswith(('.private.pem','-private.pem','.key')):
            raise ValueError('Private/runtime file tracked: '+name)
        if source.is_symlink() or not source.resolve().is_relative_to(ROOT):
            raise ValueError('External symlink: '+name)
        data=source.read_bytes()
        if re.search(rb'-----BEGIN (?:EC |RSA )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\s]+-----END ',data):
            raise ValueError('Private key material: '+name)
        target=destination/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        inventory[name]=hashlib.sha256(data).hexdigest()
    (destination/'release-source.json').write_text(json.dumps({
        'format':1,'historyIncluded':False,'files':inventory},indent=2,sort_keys=True)+'\n',encoding='utf-8')
    return len(inventory)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();print('Exported',export(a.out),'source files without Git history to',a.out)
if __name__=='__main__':main()

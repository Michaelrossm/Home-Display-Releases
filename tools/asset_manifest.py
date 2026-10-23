#!/usr/bin/env python3
"""Generate/validate Home-Display asset manifests (adapted from IR-Tracker asset_manifest.py)."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
SCHEMA=1

def file_meta(path:Path)->dict:
    data=path.read_bytes();return {"size":len(data),"sha256":hashlib.sha256(data).hexdigest()}

def generate(root:Path,build:str)->dict:
    files={}
    for p in sorted(root.rglob('*')):
        if not p.is_file() or p.name=='manifest.json':continue
        rel=p.relative_to(root).as_posix()
        if '..' in Path(rel).parts:raise ValueError('unsafe path')
        files[rel]=file_meta(p)
    return {"schema":SCHEMA,"product":"Home-Display","build":build,"files":files}

def validate(root:Path,manifest:dict)->list[str]:
    errors=[]
    if manifest.get('schema')!=SCHEMA:errors.append('schema')
    if manifest.get('product')!='Home-Display':errors.append('product')
    files=manifest.get('files')
    if not isinstance(files,dict):return errors+['files']
    for name,meta in files.items():
        p=root/name
        if name.startswith('/') or '..' in Path(name).parts:errors.append(f'path:{name}');continue
        if not p.is_file():errors.append(f'missing:{name}');continue
        actual=file_meta(p)
        if meta.get('size')!=actual['size']:errors.append(f'size:{name}')
        if meta.get('sha256')!=actual['sha256']:errors.append(f'sha256:{name}')
    return errors

def main():
    ap=argparse.ArgumentParser();ap.add_argument('root',type=Path);ap.add_argument('--build',required=True);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    mp=a.root/'manifest.json'
    if a.check:
        errors=validate(a.root,json.loads(mp.read_text('utf-8')));print('VALID' if not errors else '\n'.join(errors));raise SystemExit(bool(errors))
    m=generate(a.root,a.build);mp.write_text(json.dumps(m,indent=2,sort_keys=True)+'\n','utf-8');print(mp)
if __name__=='__main__':main()

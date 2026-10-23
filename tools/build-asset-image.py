#!/usr/bin/env python3
"""Build SystemData/AppFS LittleFS image after generating its manifest."""
from __future__ import annotations
import argparse, csv, shutil, subprocess, sys
from pathlib import Path
from asset_manifest import generate
import json
ROOT=Path(__file__).resolve().parents[1]
def partition_size(label:str)->int:
    with (ROOT/'partitions_32mb.csv').open() as f:
        for row in csv.reader(f):
            if not row or row[0].lstrip().startswith('#'):continue
            if row[0].strip()==label:return int(row[4].strip(),0)
    raise SystemExit(f'partition not found: {label}')

def build_python_image(src:Path,out:Path,size:int):
    try:
        from littlefs import LittleFS
    except ImportError:
        raise SystemExit('Use the pinned .pio/core/penv Python (littlefs-python), or pass --mklittlefs')
    fs=LittleFS(block_size=4096,block_count=size//4096,read_size=1,prog_size=1,
                cache_size=4096,lookahead_size=32,block_cycles=500,name_max=64,
                disk_version=0x00020001,mount=False)
    fs.format();fs.mount()
    for item in sorted(src.rglob('*')):
        if not item.is_file():continue
        relative=item.relative_to(src)
        if relative.parent!=Path('.'):
            fs.makedirs(relative.parent.as_posix(),exist_ok=True)
        with fs.open(relative.as_posix(),'wb') as target:target.write(item.read_bytes())
    fs.unmount();out.write_bytes(bytes(fs.context.buffer))
def main():
    ap=argparse.ArgumentParser();ap.add_argument('area',choices=['systemdata','appfs']);ap.add_argument('--build',required=True);ap.add_argument('--mklittlefs');ap.add_argument('--out',type=Path);a=ap.parse_args()
    src=ROOT/'assets'/a.area; (src/'manifest.json').write_text(json.dumps(generate(src,a.build),indent=2,sort_keys=True)+'\n','utf-8')
    exe=a.mklittlefs or shutil.which('mklittlefs')
    out=a.out or ROOT/'release'/f'{a.area}-{a.build}.bin';out.parent.mkdir(parents=True,exist_ok=True)
    if exe:subprocess.run([exe,'-c',str(src),'-s',str(partition_size(a.area)),str(out)],check=True)
    else:build_python_image(src,out,partition_size(a.area))
    if out.stat().st_size>partition_size(a.area):raise SystemExit('image too large')
    print(out)
if __name__=='__main__':main()

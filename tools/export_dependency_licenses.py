#!/usr/bin/env python3
"""Keep original notices for the actual pinned build, without changing packages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT=Path(__file__).resolve().parents[1]

def collect(roots,destination):
    destination=Path(destination);manifest=[]
    for category,base in roots:
        base=Path(base)
        if not base.exists():continue
        for source in sorted(base.rglob('*')):
            if not source.is_file() or source.is_symlink():continue
            if source.suffix.lower() in ('.py','.yml','.yaml','.h','.c','.cpp'):continue
            if not re.search(r'(?:^|[._ -])(licen[cs]e|copying|notice|copyright|patents|ofl)(?:$|[._ -])',source.name,re.I):continue
            if source.stat().st_size>2*1024*1024:raise ValueError('Unexpected large notice: '+str(source))
            relative=Path(category)/source.relative_to(base)
            target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(source,target)
            manifest.append({'file':relative.as_posix(),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
    destination.mkdir(parents=True,exist_ok=True)
    (destination/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    return len(manifest)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--env',default='home_display_p4_legacy_prod')
    p.add_argument('--out',type=Path,default=ROOT/'licenses/third-party')
    a=p.parse_args()
    roots=[('components',ROOT/'managed_components'),('project-components',ROOT/'components'),('fonts',ROOT/'tools/assets'),
           ('libraries',ROOT/'.pio/libdeps'/a.env),('arduino',ROOT/'.pio/core/packages/framework-arduinoespressif32'),
           ('arduino-libs',ROOT/'.pio/core/packages/framework-arduinoespressif32-libs'),
           ('esp-idf',ROOT/'.pio/core/packages/framework-espidf')]
    print('Exported',collect(roots,a.out),'original license/notice files')
if __name__=='__main__':main()

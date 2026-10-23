#!/usr/bin/env python3
"""Home-Display release gate; architecture adapted from IR-Tracker release/build validators."""
from __future__ import annotations
import argparse,csv,json,re,subprocess,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FAIL=[];WARN=[]
def fail(x):FAIL.append(x)
def warn(x):WARN.append(x)
def buildinfo():
 t=(ROOT/'include/BuildInfo.h').read_text();m=re.search(r'kVersion = "([^"]+)"',t);s=re.search(r'kConfigSchema = (\d+)',t);return (m.group(1) if m else '',int(s.group(1)) if s else 0)
def partitions():
 out=[]
 with (ROOT/'partitions_32mb.csv').open() as f:
  for row in csv.reader(f):
   if not row or row[0].lstrip().startswith('#'):continue
   try:out.append((row[0].strip(),int(row[3].strip(),0),int(row[4].strip(),0)))
   except Exception:fail('invalid partition row: '+str(row))
 return out
def scan_architecture():
 src=list((ROOT/'src').rglob('*.cpp'))+list((ROOT/'src').rglob('*.h'))
 allowed_prefs={'src/services/ConfigService.cpp','src/services/ConfigService.h','src/infrastructure/TransactionJournal.cpp'}
 allowed_sd={'src/services/StorageService.cpp','src/services/UpdateService.cpp'}
 for p in src:
  rel=p.relative_to(ROOT).as_posix();t=p.read_text(errors='ignore')
  if 'Preferences' in t and rel not in allowed_prefs:fail('direct Preferences outside config/recovery: '+rel)
  if 'SD_MMC' in t and rel not in allowed_sd:fail('direct SD_MMC outside storage/update: '+rel)
  if 'ESPmDNS' in t:fail('legacy ESPmDNS implementation remains: '+rel)
 if len(list((ROOT/'src').rglob('StorageManager.*'))):fail('duplicate StorageManager')
 if not (ROOT/'src/ui/RecoveryUI.cpp').is_file():fail('Recovery UI missing')
 if 'BootHealthCheck' not in (ROOT/'src/services/OtaService.cpp').read_text():fail('OTA not health gated')
def scan_secrets(production):
 pats=[re.compile(r'password\s*=\s*"[^"\n]{4,}"',re.I),re.compile(r'Bearer\s+[A-Za-z0-9_.-]{20,}')]
 for base in [ROOT/'src',ROOT/'include']:
  for p in base.rglob('*'):
   if not p.is_file() or p.name.endswith('.example.h'):continue
   t=p.read_text(errors='ignore')
   # Explicit public factory login requested for this product. Only this exact
   # assignment in the shared authentication module is exempt; arbitrary
   # credentials in the same files are still scanned and rejected.
   rel=p.relative_to(ROOT).as_posix()
   if rel == 'src/services/WebAdminAuth.cpp':
    t=re.sub(r'\bpassword\s*=\s*"Passwort"\s*;', '', t)
   for pat in pats:
    if pat.search(t):fail('possible secret: '+rel)
 if production:
  key=ROOT/'signing/home-display-signing-public.pem'
  if not key.is_file():fail('production signing public key missing')
  hdr=(ROOT/'include/UpdateSigningKey.h').read_text()
  if 'kPem[] = ""' in hdr:fail('embedded production signing key missing')
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--production',action='store_true');ap.add_argument('--source-only',action='store_true');ap.add_argument('--firmware',type=Path);ap.add_argument('--systemdata',type=Path);ap.add_argument('--appfs',type=Path);a=ap.parse_args();v,s=buildinfo()
 if not re.fullmatch(r'\d+\.\d+\.\d+',v):fail('BuildInfo requires a numeric release version')
 if not s:fail('config schema missing')
 pm=json.loads((ROOT/'PROJECT_MANIFEST.json').read_text())
 if pm.get('version') not in (v,v+'-dev'):fail('PROJECT_MANIFEST version inconsistent')
 ps=partitions();ps.sort(key=lambda x:x[1]);
 for x,y in zip(ps,ps[1:]):
  if x[1]+x[2]>y[1]:fail('partition overlap '+x[0]+'/'+y[0])
 if ps and ps[-1][1]+ps[-1][2]!=0x2000000:fail('partition table does not end at 32MiB')
 for label in ('ota_0','ota_1','systemdata','appfs'):
  if not any(x[0]==label for x in ps):fail('partition missing '+label)
 scan_architecture();scan_secrets(a.production)
 tests=subprocess.run([sys.executable,'-m','unittest','discover','-s',str(ROOT/'tests'),'-p','test_*.py'],cwd=ROOT)
 if tests.returncode:fail('unit tests failed')
 
 if not a.source_only:
  sizes={name:size for name,off,size in ps}
  if not a.firmware or not a.firmware.is_file():fail('firmware binary missing')
  else:
   n=a.firmware.stat().st_size
   if n>min(sizes.get('ota_0',0),sizes.get('ota_1',0)):fail('firmware does not fit both OTA slots')
   else:print(f'Firmware bytes: {n}; OTA reserve: {min(sizes["ota_0"],sizes["ota_1"])-n}')
  for label,path in [('systemdata',a.systemdata),('appfs',a.appfs)]:
   if path:
    if not path.is_file():fail(label+' image missing')
    elif path.stat().st_size>sizes.get(label,0):fail(label+' image too large')
    else:print(f'{label} bytes: {path.stat().st_size}; reserve: {sizes[label]-path.stat().st_size}')
   elif a.production:fail(label+' image required for production release')
 elif a.production:
  warn('production source-only mode checks signing/source gates but cannot certify binary sizes')
 for x in WARN:print('WARN:',x)
 for x in FAIL:print('FAIL:',x)
 if FAIL:raise SystemExit(1)
 print(f'VALID Home-Display {v}; config schema {s}; production={a.production}')
if __name__=='__main__':main()

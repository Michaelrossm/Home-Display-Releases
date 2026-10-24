#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def meta(p):d=p.read_bytes();return {"file":p.name,"size":len(d),"sha256":hashlib.sha256(d).hexdigest()}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--version',required=True);ap.add_argument('--firmware',type=Path,required=True);ap.add_argument('--systemdata',type=Path);ap.add_argument('--appfs',type=Path)
 ap.add_argument('--profile',choices=['home_display_p4_legacy_prod','home_display_p4_r3_prod'],default='home_display_p4_legacy_prod')
 ap.add_argument('--factory',type=Path);ap.add_argument('--private-key',type=Path)
 ap.add_argument('--out-dir',type=Path,default=ROOT/'release');a=ap.parse_args();rel=a.out_dir;rel.mkdir(parents=True,exist_ok=True)
 version=re.search(r'kVersion = "([^"]+)"',(ROOT/'include/BuildInfo.h').read_text()).group(1)
 if a.version!=version:raise SystemExit('Release version differs from BuildInfo')
 hardware='p4-r3-400mhz' if '_r3_' in a.profile else 'p4-legacy-360mhz'
 base=f'home-display-{a.version}-{hardware}'
 doc={"format":1,"product":"Home-Display","firmwareVersion":a.version,"firmware":meta(a.firmware),"profile":a.profile,"license":"GPL-3.0-only","disabledFeatures":["intercom"]}
 if a.systemdata:doc['systemData']=meta(a.systemdata)
 if a.appfs:doc['appFs']=meta(a.appfs)
 if a.factory:doc['factory']=meta(a.factory)
 mf=rel/f'{base}.json';mf.write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n','utf-8')
 signature=None
 if a.private_key:
  if a.private_key.resolve().is_relative_to(ROOT):raise SystemExit('Private key must remain outside the repository')
  from cryptography.hazmat.primitives import hashes,serialization
  from cryptography.hazmat.primitives.asymmetric import ec
  key=serialization.load_pem_private_key(a.private_key.read_bytes(),password=None)
  public=serialization.load_pem_public_key((ROOT/'signing/home-display-signing-public.pem').read_bytes())
  if not isinstance(key,ec.EllipticCurvePrivateKey) or not isinstance(key.curve,ec.SECP256R1) or key.public_key().public_numbers()!=public.public_numbers():raise SystemExit('Wrong release signing key')
  signature=mf.with_suffix('.json.sig');sig=key.sign(mf.read_bytes(),ec.ECDSA(hashes.SHA256()))
  public.verify(sig,mf.read_bytes(),ec.ECDSA(hashes.SHA256()));signature.write_bytes(sig)
 z=rel/(base+('-signed.zip' if signature else '-unsigned.zip'))
 with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
  out.write(mf,mf.name);out.write(a.firmware,a.firmware.name)
  if signature:out.write(signature,signature.name)
  if a.systemdata:out.write(a.systemdata,a.systemdata.name)
  if a.appfs:out.write(a.appfs,a.appfs.name)
  if a.factory:out.write(a.factory,a.factory.name)
  for name in ['LICENSE.md','THIRD_PARTY_NOTICES.md','signing/home-display-signing-public.pem','docs/RELEASE_0.1.0.md']:
   path=ROOT/name
   if path.is_file():out.write(path,'README.md' if name.startswith('docs/RELEASE_') else name)
  notices=ROOT/'licenses/third-party'
  if notices.exists():
   for path in sorted(notices.rglob('*')):
    if path.is_file():out.write(path,path.relative_to(ROOT).as_posix())
 print(z)
if __name__=='__main__':main()

#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import Prehashed

def verify(manifest:Path,sig:Path,key:Path,base:Path):
 raw=manifest.read_bytes();pub=serialization.load_pem_public_key(key.read_bytes());pub.verify(sig.read_bytes(),hashlib.sha256(raw).digest(),ec.ECDSA(Prehashed(hashes.SHA256())));doc=json.loads(raw);assert doc['format']==1 and doc['product']=='Home-Display'
 for name in ('firmware','systemData','appFs'):
  spec=doc.get(name)
  if not spec:continue
  p=base/spec['file'];data=p.read_bytes();assert len(data)==spec['size'];assert hashlib.sha256(data).hexdigest()==spec['sha256'].lower()
 return doc

def main():
 ap=argparse.ArgumentParser();ap.add_argument('manifest',type=Path);ap.add_argument('signature',type=Path);ap.add_argument('--public-key',type=Path,required=True);ap.add_argument('--base',type=Path);a=ap.parse_args()
 try:doc=verify(a.manifest,a.signature,a.public_key,a.base or a.manifest.parent)
 except (OSError,KeyError,AssertionError,InvalidSignature,ValueError) as e:print(f'INVALID: {e}');raise SystemExit(1)
 print('VALID',doc['firmwareVersion'])
if __name__=='__main__':main()

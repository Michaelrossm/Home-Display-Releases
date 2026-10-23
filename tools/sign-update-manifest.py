#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib
from pathlib import Path
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import Prehashed

def main():
 ap=argparse.ArgumentParser();ap.add_argument('manifest',type=Path);ap.add_argument('--private-key',type=Path,required=True);ap.add_argument('--out',type=Path);a=ap.parse_args();data=a.manifest.read_bytes();key=serialization.load_pem_private_key(a.private_key.read_bytes(),password=None);sig=key.sign(hashlib.sha256(data).digest(),ec.ECDSA(Prehashed(hashes.SHA256())));out=a.out or a.manifest.with_suffix(a.manifest.suffix+'.sig');out.write_bytes(sig);print(out)
if __name__=='__main__':main()

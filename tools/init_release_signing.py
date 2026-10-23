#!/usr/bin/env python3
"""Create/reuse the release key OUTSIDE the project; export only its public half."""
from pathlib import Path
import argparse
import os
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from embed_public_key import embed

ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--private-key', type=Path, required=True)
    a = p.parse_args(); private = a.private_key.resolve()
    if private.is_relative_to(ROOT):
        raise SystemExit('Private key must be outside the project')
    if private.exists():
        key = serialization.load_pem_private_key(private.read_bytes(), password=None)
    else:
        private.parent.mkdir(parents=True, exist_ok=True)
        key = ec.generate_private_key(ec.SECP256R1())
        raw = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption())
        fd = os.open(private, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as f: f.write(raw)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        raise SystemExit('Existing key is not ECDSA P-256; not replaced')
    public = ROOT/'signing/home-display-signing-public.pem'
    raw = key.public_key().public_bytes(serialization.Encoding.PEM,
                                       serialization.PublicFormat.SubjectPublicKeyInfo)
    if public.exists() and public.read_bytes() != raw:
        raise SystemExit('Public key differs; explicit key rotation required')
    public.write_bytes(raw)
    embed(public, ROOT/'include/UpdateSigningKey.h')
    print('Public verification key exported. Back up the private key outside Git.')

if __name__ == '__main__': main()

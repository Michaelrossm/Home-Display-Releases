#!/usr/bin/env python3
"""Embed only a validated public ECDSA P-256 update-verification key."""
from pathlib import Path
import argparse
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parents[1]

def embed(public_key, output):
    key = serialization.load_pem_public_key(Path(public_key).read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError('ECDSA P-256 public key required')
    pem = key.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode('ascii')
    Path(output).write_text('#pragma once\nnamespace hp::update_key {\n'
        '// Public verification key only. The private key must remain outside repositories.\n'
        'inline constexpr const char kPem[] = R"HPKEY(' + pem + ')HPKEY";\n}\n', encoding='utf-8')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('public_key', type=Path)
    p.add_argument('--out', type=Path, default=ROOT/'include/UpdateSigningKey.h')
    a = p.parse_args(); embed(a.public_key, a.out)
    print('Embedded public verification key:', a.out)

if __name__ == '__main__': main()

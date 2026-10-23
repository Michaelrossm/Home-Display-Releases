# Home-Display signing

Production releases require a dedicated ECDSA public key at:
`signing/home-display-signing-public.pem`

Generate the corresponding C++ header with `tools/embed_public_key.py`.
Never commit or package the private signing key.
Development source intentionally contains an empty `UpdateSigningKey.h`; the
production release validator rejects that state.

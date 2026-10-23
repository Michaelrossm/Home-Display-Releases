from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
h=(ROOT/"src/services/NasService.h").read_text()
c=(ROOT/"src/services/NasService.cpp").read_text()
cred=(ROOT/"src/services/CredentialStore.cpp").read_text()
profile=h.split("struct NasProfile",1)[1].split("};",1)[0]
assert "password" not in profile.lower()
assert "CredentialStore::instance().nasPassword" in c
assert "putSecretString" in cred
print("PASS NAS secrets")

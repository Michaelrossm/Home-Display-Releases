from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
c=(ROOT/"src/services/FileTransferService.cpp").read_text()
assert "uint8_t buffer[4096]" in c
assert 'target + ".part"' in c
assert "commitUploadedFile(fs, temp, target)" in c
assert "rename(target, backup)" in c
assert "rename(backup, target)" in c
assert "rename(temp, target)" in c
assert "SD card required" in c
print("PASS file upload")

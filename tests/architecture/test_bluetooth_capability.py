from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
h=(ROOT/"src/services/BluetoothService.h").read_text()
caps=(ROOT/"src/services/HardwareCapabilitiesService.h").read_text()
assert "classicAudioAvailable() const { return false; }" in h
assert "bluetoothClassicAudio = false" in caps
print("PASS bluetooth capability")

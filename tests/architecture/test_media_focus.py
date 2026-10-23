from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
h=(ROOT/"src/services/MediaSessionService.h").read_text()
c=(ROOT/"src/services/MediaSessionService.cpp").read_text()
assert "Stop," in h
assert "requestAudioFocus" in c
assert "previousHandler(MediaCommand::Stop)" in c
assert "did not confirm stop" in c
print("PASS media focus")

#pragma once
#include <cstdint>
namespace hp {
// Diagnostic only, never used to authorize boot or resume a partial upload.
class OtaTrace {
public:
    enum Phase : uint32_t { None, Begin, Writing, Validating, Readback, Selecting, Ready, Failed };
    struct Snapshot { uint32_t phase=0, bytes=0; int32_t error=0; };
    static void record(Phase phase, uint32_t bytes, int32_t error=0);
    static Snapshot snapshot();
};
}

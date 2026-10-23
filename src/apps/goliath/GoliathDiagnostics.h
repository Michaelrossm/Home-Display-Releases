#pragma once
#include <Arduino.h>

namespace hp {

// Volatile and deliberately bounded: 256 records * 176 bytes = 44 KiB.
class GoliathDiagnostics {
public:
    struct Record { uint32_t sequence; uint32_t uptimeSeconds; char code[20]; char message[148]; };
    static GoliathDiagnostics& instance();
    bool begin();
    void add(const char* code, const String& message);
    void clear();
    size_t count() const;
    static constexpr size_t capacity() { return kCapacity; }
    static constexpr size_t bytes() { return kCapacity * sizeof(Record); }
    bool copy(Record* target, size_t capacity, size_t& copied) const;
private:
    static constexpr size_t kCapacity = 256;
    Record* records_ = nullptr;
    size_t writeIndex_ = 0, count_ = 0;
    uint32_t sequence_ = 0;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

}

#include "GoliathDiagnostics.h"
#include <esp_heap_caps.h>

namespace hp {
GoliathDiagnostics& GoliathDiagnostics::instance() { static GoliathDiagnostics diagnostics; return diagnostics; }

bool GoliathDiagnostics::begin() {
    if (records_) return true;
    records_ = static_cast<Record*>(heap_caps_calloc(kCapacity, sizeof(Record), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!records_) records_ = static_cast<Record*>(calloc(kCapacity, sizeof(Record)));
    return records_ != nullptr;
}

void GoliathDiagnostics::add(const char* code, const String& message) {
    if (!records_ && !begin()) return;
    portENTER_CRITICAL(&lock_);
    auto& record = records_[writeIndex_];
    record = {};
    record.sequence = ++sequence_;
    record.uptimeSeconds = millis() / 1000;
    strlcpy(record.code, code ? code : "EVENT", sizeof(record.code));
    strlcpy(record.message, message.c_str(), sizeof(record.message));
    writeIndex_ = (writeIndex_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;
    portEXIT_CRITICAL(&lock_);
}

void GoliathDiagnostics::clear() {
    if (!records_) return;
    portENTER_CRITICAL(&lock_);
    memset(records_, 0, kCapacity * sizeof(Record));
    writeIndex_ = count_ = sequence_ = 0;
    portEXIT_CRITICAL(&lock_);
}

size_t GoliathDiagnostics::count() const {
    portENTER_CRITICAL(&lock_); const size_t value = count_; portEXIT_CRITICAL(&lock_); return value;
}

bool GoliathDiagnostics::copy(Record* target, size_t capacity, size_t& copied) const {
    copied = 0;
    if (!target || !capacity || !records_) return false;
    portENTER_CRITICAL(&lock_);
    const size_t available = min(capacity, count_);
    const size_t first = count_ < kCapacity ? 0 : writeIndex_;
    for (size_t i = 0; i < available; ++i) target[i] = records_[(first + i) % kCapacity];
    copied = available;
    portEXIT_CRITICAL(&lock_);
    return true;
}
}

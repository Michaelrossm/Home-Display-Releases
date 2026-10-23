#pragma once
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_heap_caps.h>
#include <memory>
#include <utility>
#if defined(ESP_PLATFORM)
#include "../../services/UiPerformanceMonitor.h"
#endif

namespace hp {
// CONFIG_SPIRAM_USE_CAPS_ALLOC does not route ordinary String/realloc buffers
// to PSRAM. Script source must not require a large contiguous internal block.
class LuaScriptBuffer {
    struct Free { void operator()(char* p) const { if (p) heap_caps_free(p); } };
    std::unique_ptr<char, Free> bytes_;
    size_t length_ = 0, capacity_ = 0;
public:
    LuaScriptBuffer() = default;
    explicit LuaScriptBuffer(const char* text) {
        if (text && *text) { const size_t size = strlen(text); if (reserve(size)) concat(text, size); }
    }
    LuaScriptBuffer(LuaScriptBuffer&& other) noexcept
        : bytes_(std::move(other.bytes_)), length_(std::exchange(other.length_, 0)), capacity_(std::exchange(other.capacity_, 0)) {}
    LuaScriptBuffer& operator=(LuaScriptBuffer&& other) noexcept {
        if (this != &other) { bytes_ = std::move(other.bytes_); length_ = std::exchange(other.length_, 0); capacity_ = std::exchange(other.capacity_, 0); }
        return *this;
    }
    LuaScriptBuffer(const LuaScriptBuffer&) = delete;
    LuaScriptBuffer& operator=(const LuaScriptBuffer&) = delete;
    LuaScriptBuffer& operator=(const char* text) { *this = LuaScriptBuffer(text); return *this; }
    bool reserve(size_t count) {
        if (count > 196608) return false;
        if (bytes_ && capacity_ >= count) return true;
        auto* replacement = static_cast<char*>(heap_caps_malloc(count + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!replacement) replacement = static_cast<char*>(heap_caps_malloc(count + 1, MALLOC_CAP_8BIT));
        if (!replacement) return false;
        if (length_) memcpy(replacement, bytes_.get(), length_);
        replacement[length_] = 0; bytes_.reset(replacement); capacity_ = count;
        return true;
    }
    bool concat(const char* text, size_t count) {
        if (!bytes_ || count > capacity_ - length_) return false;
        memcpy(bytes_.get() + length_, text, count); length_ += count; bytes_.get()[length_] = 0;
        return true;
    }
    size_t length() const { return length_; }
    const char* c_str() const { return bytes_ ? bytes_.get() : ""; }
    int indexOf(const char* text) const { const char* found = strstr(c_str(), text); return found ? static_cast<int>(found - c_str()) : -1; }
};
// File::readString() may return a partial String after allocation/read failure.
// Never pass a truncated package to the parser or execute a partial script.
template<class FileType, class BufferType>
bool readLuaScript(FileType& file, BufferType& script, const char*& error,const std::atomic<bool>* cancelled=nullptr) {
#if defined(ESP_PLATFORM)
    const uint32_t readBeganUs=micros();
#endif
    script = "";
    error = nullptr;
    const size_t expected = file.size();
    if (!expected || expected > 196608) { error = "lua_script_size"; return false; }
    if (!script.reserve(expected)) { error = "lua_memory:script_buffer"; return false; }
    char chunk[512];
    size_t consumed = 0;
    while (consumed < expected) {
        if(cancelled&&cancelled->load(std::memory_order_acquire)){error="cancelled";break;}
        const size_t wanted = std::min(sizeof(chunk), expected - consumed);
        const size_t got = file.read(reinterpret_cast<uint8_t*>(chunk), wanted);
        if (!got || got > wanted) { error = "lua_script_read_failed"; break; }
        if (memchr(chunk, 0, got)) { error = "lua_script_binary"; break; }
        if (!script.concat(chunk, static_cast<unsigned int>(got))) { error = "lua_memory:script_buffer"; break; }
        consumed += got;
    }
    if (!error && (script.length() != expected || file.size() != expected)) error = "lua_script_read_failed";
    if (error) { script = ""; return false; }
#if defined(ESP_PLATFORM)
    UiPerformanceMonitor::instance().luaRead(micros()-readBeganUs);
#endif
    return true;
}
}

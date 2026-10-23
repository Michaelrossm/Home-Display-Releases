#pragma once
#include "OtaTrace.h"
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <mutex>

namespace hp {
// Shared by HTTP and signed-package updates. No task, storage mount or LVGL call.
// Keep the lease until verification/boot selection has completed, not just end().
class OtaWriteSession {
public:
    static constexpr size_t kChunkSize = 4096;
    static std::mutex& mutex() { static std::mutex value; return value; }
    OtaWriteSession() : lock_(mutex(), std::try_to_lock) {}
    ~OtaWriteSession() { abort(); heap_caps_free(buffer_); }
    OtaWriteSession(const OtaWriteSession&) = delete;
    OtaWriteSession& operator=(const OtaWriteSession&) = delete;
    bool acquired() const { return lock_.owns_lock(); }
    esp_err_t begin(const esp_partition_t* target, size_t bytes) {
        if (!acquired() || handle_ || buffer_) return ESP_ERR_INVALID_STATE;
        const auto* running = esp_ota_get_running_partition();
        if (!target || !running || target->address == running->address ||
            !bytes || bytes > target->size) return ESP_ERR_INVALID_ARG;
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) return ESP_ERR_OTA_ROLLBACK_INVALID_STATE;
        // Allocate BEFORE touching flash; never hand PSRAM buffers to the writer.
        buffer_ = static_cast<uint8_t*>(heap_caps_malloc(kChunkSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!buffer_) return ESP_ERR_NO_MEM;
        expected_ = bytes;
        written_ = 0;
        // IDF erases only the sectors needed for each sequential write. Avoid a
        // multi-megabyte blocking erase before the HTTP body can be consumed.
        OtaTrace::record(OtaTrace::Begin,0);
        const auto result = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
        if(result != ESP_OK) OtaTrace::record(OtaTrace::Failed,0,result);
        return result;
    }
    esp_err_t write(const void* data, size_t bytes) {
        if (!handle_ || !data || bytes > expected_ - written_) return ESP_ERR_INVALID_ARG;
        auto* input = static_cast<const uint8_t*>(data);
        while (bytes) {
            const size_t count = bytes < kChunkSize ? bytes : kChunkSize;
            if (input != buffer_) memcpy(buffer_, input, count);
            OtaTrace::record(OtaTrace::Writing,written_);
            const auto result = esp_ota_write(handle_, buffer_, count);
            if (result != ESP_OK) { OtaTrace::record(OtaTrace::Failed,written_,result); return result; }
            written_ += count; input += count; bytes -= count;
            // delay(0) alone does not give lower-priority idle tasks a timeslice.
            vTaskDelay(1);
        }
        return ESP_OK;
    }
    esp_err_t finish() {
        if (!handle_ || written_ != expected_) return ESP_ERR_INVALID_SIZE;
        const auto handle = handle_; handle_ = 0;
        // IDF frees the handle even if validation fails. Never abort it twice.
        OtaTrace::record(OtaTrace::Validating,written_);
        const auto result = esp_ota_end(handle);
        OtaTrace::record(result == ESP_OK ? OtaTrace::Readback : OtaTrace::Failed,written_,result);
        return result;
    }
    void abort() { if (handle_) { esp_ota_abort(handle_); handle_ = 0;
        if(OtaTrace::snapshot().phase != OtaTrace::Failed) OtaTrace::record(OtaTrace::Failed,written_,ESP_FAIL);
    } }
    uint8_t* buffer() { return buffer_; }
private:
    std::unique_lock<std::mutex> lock_;
    esp_ota_handle_t handle_ = 0;
    uint8_t* buffer_ = nullptr;
    size_t expected_ = 0, written_ = 0;
};
}

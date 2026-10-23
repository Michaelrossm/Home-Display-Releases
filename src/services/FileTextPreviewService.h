#pragma once
#include <Arduino.h>
#include <atomic>
#include <memory>

namespace hp {
struct FileTextPreviewRequest {
    std::atomic<bool> canceled{false};
    std::atomic<bool> ready{false};
    String text;
    String message;
};

// Read-only text preview. I/O always runs outside the LVGL thread.
class FileTextPreviewService {
public:
    static bool supports(const String& path);
    static std::shared_ptr<FileTextPreviewRequest> request(const String& uri);
private:
    struct Job;
    static void run(void* job);
};
}

#pragma once
#include <Arduino.h>
#include <esp_http_server.h>
#include <atomic>

namespace hp {

class WebCsrfService {
public:
    static WebCsrfService& instance();
    const String& token();
    bool validate(httpd_req_t* req);
    uint32_t failures() const { return failures_.load(std::memory_order_relaxed); }

private:
    WebCsrfService() = default;
    void ensureToken();
    String token_;
    std::atomic<uint32_t> failures_{0};
};

} // namespace hp

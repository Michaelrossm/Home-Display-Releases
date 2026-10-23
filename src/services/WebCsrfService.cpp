#include "WebCsrfService.h"
#include <esp_random.h>
#include <cstring>

namespace hp {

WebCsrfService& WebCsrfService::instance() {
    static WebCsrfService service;
    return service;
}

void WebCsrfService::ensureToken() {
    if (token_.length()) return;
    char text[33]{};
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t value = esp_random();
        snprintf(text + i * 8, 9, "%08lx", (unsigned long)value);
    }
    token_ = text;
}

const String& WebCsrfService::token() {
    ensureToken();
    return token_;
}

bool WebCsrfService::validate(httpd_req_t* req) {
    ensureToken();
    char supplied[64]{};
    if (!req ||
        httpd_req_get_hdr_value_str(req, "X-HP-CSRF", supplied, sizeof(supplied)) != ESP_OK) {
        failures_.fetch_add(1, std::memory_order_relaxed);
        if (req) {
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_set_type(req, "text/plain; charset=utf-8");
            httpd_resp_sendstr(req, "CSRF-Schutz: Sicherheits-Token fehlt");
        }
        return false;
    }
    const size_t a = token_.length();
    const size_t b = strlen(supplied);
    unsigned diff = (unsigned)(a ^ b);
    for (size_t i = 0; i < a && i < b; ++i)
        diff |= (unsigned char)token_[i] ^ (unsigned char)supplied[i];
    if (diff != 0) {
        failures_.fetch_add(1, std::memory_order_relaxed);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_sendstr(req, "CSRF-Schutz: Sicherheits-Token ungültig");
        return false;
    }
    return true;
}

} // namespace hp

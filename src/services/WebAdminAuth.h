#pragma once
#include <esp_http_server.h>
#include <Arduino.h>

namespace hp::WebAdminAuth {
// Shared credentials only; each server retains its own routes/CSRF/recovery policy.
bool initialize();
bool verify(const String& username, const String& submitted);
bool authorized(httpd_req_t* request);
}

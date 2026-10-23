#pragma once
#include <Arduino.h>
#include <esp_http_server.h>

namespace hp {

struct LuaAppDataCleanupResult {
    bool ok = false;
    size_t kvRemoved = 0;
    size_t secretsRemoved = 0;
    bool filesRemoved = false;
    String error;
};

class LuaAppDataCleanupService {
public:
    static LuaAppDataCleanupService& instance();

    // Phase 1 for complete deletion: validate both per-app indexes and all
    // embedded owner/key identities before any package file is touched.
    bool preflight(const String& appId, String& error);

    // Removes app-owned KV, secrets and UserData files only.
    LuaAppDataCleanupResult erase(const String& appId);

    // Streams a downloadable JSON backup. Normal KV and app files are included.
    // Secret values are intentionally never exported; only their key names and
    // configured state are listed.
    esp_err_t sendExport(httpd_req_t* req, const String& appId);

private:
    LuaAppDataCleanupService() = default;
};

} // namespace hp

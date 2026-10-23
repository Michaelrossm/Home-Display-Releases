#pragma once
#include <Arduino.h>
#include <esp_http_server.h>
#include <atomic>

namespace hp {

class FileTransferService {
public:
    static FileTransferService& instance();

    bool begin();
    // Normal-mode service access. Starts port 8088 only on demand.
    // A running timed session is never extended by repeated start requests.
    bool startTimed(uint32_t maxMinutes = 30);
    void loop();
    void stop();
    bool running() const { return server_ != nullptr; }
    bool secureRequest(httpd_req_t* req) const { return server_ && req && req->handle == server_; }
    bool timedSession() const { return timedSession_.load(std::memory_order_relaxed); }
    uint32_t remainingSeconds() const;
    size_t activeClients() const;
    size_t freeSockets() const { const size_t n=activeClients(); return n<1?1-n:0; }
    static bool authorize(httpd_req_t* req); // Shared admin policy for modular routes.

    // Reused by WebServerService on port 80. There is intentionally only one
    // implementation of app/file/firmware mutations; both HTTP frontends call
    // these proven handlers instead of duplicating storage or OTA logic.
    static esp_err_t uploadHandler(httpd_req_t* req);
    static esp_err_t filesIndexHandler(httpd_req_t* req);
    static esp_err_t filesDeleteHandler(httpd_req_t* req);
    static esp_err_t filesOperationHandler(httpd_req_t* req);

    static esp_err_t appsIndexHandler(httpd_req_t* req);
    static esp_err_t appsAddHandler(httpd_req_t* req);
    static esp_err_t appsRemoveHandler(httpd_req_t* req);
    static esp_err_t appsNativeHandler(httpd_req_t* req);
    static esp_err_t appsToggleHandler(httpd_req_t* req);
    static esp_err_t appsWebToggleHandler(httpd_req_t* req);
    static esp_err_t appsLuaRemoveHandler(httpd_req_t* req);
    static esp_err_t appsExportHandler(httpd_req_t* req);
    static esp_err_t appsLuaFileHandler(httpd_req_t* req);
    static esp_err_t appsLuaCancelHandler(httpd_req_t* req);
    static esp_err_t appsLuaCommitHandler(httpd_req_t* req);
    static esp_err_t appsUploadHandler(httpd_req_t* req);
    static esp_err_t firmwareInspectHandler(httpd_req_t* req);
    static esp_err_t firmwareUploadHandler(httpd_req_t* req);
    static esp_err_t firmwareC6MediaUploadHandler(httpd_req_t* req);
    static esp_err_t firmwareMediaStatusHandler(httpd_req_t* req);
    static esp_err_t firmwareRebootHandler(httpd_req_t* req);

private:
    static esp_err_t indexHandler(httpd_req_t* req);
    static void stopExpiredSession(void*);
    static bool validUploadName(const String& name);
    static String destinationFor(const String& name);
    static String urlDecode(const String& in);

    httpd_handle_t server_ = nullptr;
    std::atomic<uint32_t> expiresAtMs_{0};
    std::atomic<bool> timedSession_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> expirationQueued_{false};
    uint32_t lastExpirationAttempt_=0;
};

}

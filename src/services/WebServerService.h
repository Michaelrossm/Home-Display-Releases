#pragma once
#include <Arduino.h>
#include <esp_http_server.h>

namespace hp {

class WebServerService {
public:
    static WebServerService& instance();
    bool begin();
    void stop();
    bool running() const { return server_ != nullptr; }
    bool secureRequest(const httpd_req_t* req) const {return req && server_ && req->handle==server_;}
    size_t activeClients() const;
    static constexpr size_t socketCapacity() { return 6; }
    size_t freeSockets() const { const size_t n=activeClients(); return n<socketCapacity()?socketCapacity()-n:0; }

private:
    static esp_err_t rootHandler(httpd_req_t* req);
    static esp_err_t licenseHandler(httpd_req_t* req);
    static esp_err_t appPageHandler(httpd_req_t* req);
    static esp_err_t appWebLifecycleHandler(httpd_req_t* req);
    static esp_err_t accountConnectScriptHandler(httpd_req_t* req);
    static esp_err_t statusHandler(httpd_req_t* req);
    static esp_err_t csrfHandler(httpd_req_t* req);
    static esp_err_t userRevisionHandler(httpd_req_t* req);
    static esp_err_t calendarPageHandler(httpd_req_t* req);
    static esp_err_t calendarJobHandler(httpd_req_t* req);
    static esp_err_t calendarResultHandler(httpd_req_t* req);
    static esp_err_t phonebookPageHandler(httpd_req_t* req);
    static esp_err_t phonebookJobHandler(httpd_req_t* req);
    static esp_err_t phonebookResultHandler(httpd_req_t* req);
    static esp_err_t photosPageHandler(httpd_req_t* req);
    static esp_err_t photosJobHandler(httpd_req_t* req);
    static esp_err_t photosResultHandler(httpd_req_t* req);
    static esp_err_t dataJobHandler(httpd_req_t* req,bool phonebook);
    static esp_err_t dataResultHandler(httpd_req_t* req,bool phonebook);
    static esp_err_t diagnosticsHandler(httpd_req_t* req);
    static esp_err_t intercomDiagnosticsHandler(httpd_req_t* req);
    static esp_err_t intercomPageHandler(httpd_req_t* req);
    static esp_err_t intercomSettingsHandler(httpd_req_t* req);
    static esp_err_t intercomResultHandler(httpd_req_t* req);
    static esp_err_t intercomEventHandler(httpd_req_t* req);
    static esp_err_t appsHandler(httpd_req_t* req);
    static esp_err_t appToggleHandler(httpd_req_t* req);
    static esp_err_t appSettingsOpenHandler(httpd_req_t* req);
    static esp_err_t appWebSettingsGetHandler(httpd_req_t* req);
    static esp_err_t appWebSettingsSaveHandler(httpd_req_t* req);
    static esp_err_t appWebSettingsDiscoveryHandler(httpd_req_t* req);
    static esp_err_t appDataExportHandler(httpd_req_t* req);
    static esp_err_t networkHandler(httpd_req_t* req);
    static esp_err_t wifiScanHandler(httpd_req_t* req);
    static esp_err_t wifiProfileHandler(httpd_req_t* req);
    static esp_err_t wifiEnabledHandler(httpd_req_t* req);
    static esp_err_t wifiPreferredHandler(httpd_req_t* req);
    static esp_err_t wifiForgetHandler(httpd_req_t* req);
    static esp_err_t settingsHandler(httpd_req_t* req);
    static esp_err_t languageHandler(httpd_req_t* req);
    static esp_err_t regionHandler(httpd_req_t* req);
    static esp_err_t themeHandler(httpd_req_t* req);
    static esp_err_t brightnessHandler(httpd_req_t* req);
    static esp_err_t restartHandler(httpd_req_t* req);
    static esp_err_t serviceRedirectHandler(httpd_req_t* req);
    static esp_err_t serviceStatusHandler(httpd_req_t* req);
    static esp_err_t serviceStartHandler(httpd_req_t* req);
    static esp_err_t serviceStopHandler(httpd_req_t* req);

    httpd_handle_t server_ = nullptr;
    httpd_handle_t redirect_ = nullptr;
};

}

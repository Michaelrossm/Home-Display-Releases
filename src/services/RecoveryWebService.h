#pragma once
#include <Arduino.h>
#include <esp_http_server.h>

namespace hp {

class RecoveryWebService {
public:
    static RecoveryWebService& instance();
    bool begin();
    void stop();
    bool running() const { return server_ != nullptr; }

private:
    static esp_err_t rootHandler(httpd_req_t* req);
    static esp_err_t statusHandler(httpd_req_t* req);
    static esp_err_t actionHandler(httpd_req_t* req);
    static esp_err_t diagnosticsHandler(httpd_req_t* req);
    static esp_err_t restartHandler(httpd_req_t* req);

    httpd_handle_t server_ = nullptr;
};

}

#pragma once
#include <esp_http_server.h>
namespace hp {
class WebLoginService {
public:
    static bool authorized(httpd_req_t*);
    static bool browser(httpd_req_t*);
    static esp_err_t deny(httpd_req_t*);
    static esp_err_t page(httpd_req_t*);
    static esp_err_t script(httpd_req_t*);
    static esp_err_t login(httpd_req_t*);
    static esp_err_t logout(httpd_req_t*);
    static esp_err_t csrf(httpd_req_t*);
};
}

#pragma once
#include <esp_http_server.h>
#include <cstddef>
namespace hp {
// One source of truth for registration and server capacity planning.
const httpd_uri_t* oauthWebRoutes(size_t& count);
bool registerOAuthWebRoutes(httpd_handle_t);
}

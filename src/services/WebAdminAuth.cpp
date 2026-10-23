#include "WebAdminAuth.h"
#include "ConfigService.h"
#include "WebLoginService.h"
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <cstring>

namespace hp::WebAdminAuth {
namespace {
bool credential(String& password) {
    auto& config = ConfigService::instance();
    password = config.getSecretString("web_admin_pwd", "");
    if (password.length()) return true; // Existing credentials also work in read-only recovery.
    if (config.hasSecret("web_admin_pwd")) return false; // Never overwrite an unreadable existing credential.
    if (!config.writable() || !config.putSecretString("web_admin_pwd", "Passwort")) return false;
    password = "Passwort";
    return true;
}
void wipe(String& value) {
    if (value.length()) mbedtls_platform_zeroize(&value[0], value.length());
    value = "";
}
}
bool initialize() {
    String password;
    const bool ready = credential(password);
    wipe(password);
    if (!ready) return false;
    auto& config = ConfigService::instance();
    // Preserve the existing migration marker, without making read-only recovery depend on it.
    if (config.writable() && !config.getBool("web_pwd_v2", false))
        return config.putBool("web_pwd_v2", true);
    return true;
}
bool verify(const String& username, const String& submitted) {
    String password;
    if (!credential(password)) return false;
    size_t diff = username.length() ^ size_t(5);
    for (size_t i=0;i<username.length();++i) diff |= username[i] ^ (i<5 ? "admin"[i] : 0);
    diff |= submitted.length() ^ password.length();
    for (size_t i=0;i<submitted.length();++i) diff |= submitted[i] ^ (i<password.length() ? password[i] : 0);
    wipe(password);
    return diff == 0;
}
bool authorized(httpd_req_t* request) {
    if (WebLoginService::authorized(request)) return true;
    if (WebLoginService::browser(request)) return false;
    char encoded[160]{};
    unsigned char decoded[120]{};
    String password;
    bool accepted = false;
    if (httpd_req_get_hdr_value_str(request, "Authorization", encoded, sizeof(encoded)) == ESP_OK &&
        strncmp(encoded, "Basic ", 6) == 0 && credential(password)) {
        size_t length = 0;
        if (mbedtls_base64_decode(decoded, sizeof(decoded), &length,
                reinterpret_cast<const unsigned char*>(encoded + 6), strlen(encoded + 6)) == 0) {
            String expected = "admin:" + password;
            size_t diff = length ^ expected.length();
            // Do not return early on a mismatching credential byte.
            for (size_t i = 0; i < length; ++i)
                diff |= decoded[i] ^ (i < expected.length() ? static_cast<unsigned char>(expected[i]) : 0);
            accepted = diff == 0;
            wipe(expected);
        }
    }
    wipe(password);
    mbedtls_platform_zeroize(decoded, sizeof(decoded));
    mbedtls_platform_zeroize(encoded, sizeof(encoded));
    return accepted;
}
}

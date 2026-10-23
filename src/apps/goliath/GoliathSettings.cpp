#include "GoliathSettings.h"
#include "../../services/ConfigService.h"

namespace hp {
namespace {
String bounded(const String& value, size_t length) { return value.substring(0, length); }
uint16_t port(uint16_t value, uint16_t fallback) { return value ? value : fallback; }
bool validHost(const String& host) {
    if (!host.length() || host.length() > 96) return false;
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host[i];
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == ':')) return false;
    }
    return true;
}
}

GoliathSettings& GoliathSettings::instance() { static GoliathSettings settings; return settings; }

GoliathAppSettings GoliathSettings::load() const {
    auto& config = ConfigService::instance();
    GoliathAppSettings settings;
    settings.enabled = config.getBool("gol_enabled", false);
    settings.host = bounded(config.has("gol_host") ? config.getString("gol_host", "") : "", 96);
    settings.httpPort = port(config.getU16("gol_http", 80), 80);
    settings.rtspPort = port(config.getU16("gol_rtsp", 554), 554);
    settings.username = bounded(config.has("gol_user") ? config.getString("gol_user", "") : "", 48);
    settings.password = bounded(config.hasSecret("gol_pwd") ? config.getSecretString("gol_pwd", "") : "", 192);
    settings.apartment = bounded(config.has("gol_apt") ? config.getString("gol_apt", "") : "", 24);
    settings.indoorStation = bounded(config.has("gol_indoor") ? config.getString("gol_indoor", "") : "", 24);
    settings.useExtraStream = config.getBool("gol_extra", true);
    settings.showCameraOnRing = config.getBool("gol_camera", true);
    settings.diagnosticsEnabled = config.getBool("gol_diag", false);
    return settings;
}

bool GoliathSettings::save(const GoliathAppSettings& input) const {
    auto& config = ConfigService::instance();
    if (!config.writable()) return false;
    GoliathAppSettings settings = input;
    settings.host = bounded(settings.host, 96);
    settings.username = bounded(settings.username, 48);
    settings.password = bounded(settings.password, 192);
    settings.apartment = bounded(settings.apartment, 24);
    settings.indoorStation = bounded(settings.indoorStation, 24);
    if (settings.enabled && !validHost(settings.host)) return false;
    if (!settings.httpPort || !settings.rtspPort) return false;
    return config.putBool("gol_enabled", settings.enabled) &&
        config.putString("gol_host", settings.host) &&
        config.putU16("gol_http", settings.httpPort) &&
        config.putU16("gol_rtsp", settings.rtspPort) &&
        config.putString("gol_user", settings.username) &&
        config.putSecretString("gol_pwd", settings.password) &&
        config.putString("gol_apt", settings.apartment) &&
        config.putString("gol_indoor", settings.indoorStation) &&
        config.putBool("gol_extra", settings.useExtraStream) &&
        config.putBool("gol_camera", settings.showCameraOnRing) &&
        config.putBool("gol_diag", settings.diagnosticsEnabled);
}

}

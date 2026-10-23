#include "RadioSettings.h"
#include "../../services/ConfigService.h"
#include "../../services/SecurityService.h"

namespace hp {
namespace {
String bounded(const String& value, size_t length) {
    return value.substring(0, length);
}
}

RadioSettings& RadioSettings::instance() { static RadioSettings settings; return settings; }

RadioAppSettings RadioSettings::load() const {
    auto& config = ConfigService::instance();
    RadioAppSettings settings;
    // One-time compatible read of the former global keys. New writes only use
    // the app namespace; no other app needs to know Radio's schema.
    settings.host = bounded(config.getString("rad_host", config.getString("radio_host", "").c_str()), 96);
    settings.pin = bounded(config.getSecretString("rad_pin", config.getSecretString("radio_pin", "1234").c_str()), 16);
    settings.parentalEnabled = config.getBool("rad_parent", config.getBool("ar_parent", false));
    settings.volumeLimitEnabled = config.getBool("rad_vlim", config.getBool("ar_vlim", false));
    settings.maxVolumePercent = constrain(config.getU8("rad_vmax", config.getU8("ar_vmax", 60)), 5, 100);
    settings.lockSources = config.getBool("rad_lsrc", config.getBool("ar_lsrc", false));
    settings.lockPowerOff = config.getBool("rad_lpwr", config.getBool("ar_lpwr", false));
    settings.lockDisplayControls = config.getBool("rad_ldsp", config.getBool("ar_ldsp", false));
    return settings;
}

bool RadioSettings::save(const RadioAppSettings& input) const {
    SecurityWriteGuard guard;
    if(SecurityService::instance().tabletLocked())return false;
    auto& config = ConfigService::instance();
    if (!config.writable()) return false;
    RadioAppSettings settings = input;
    if(!SecurityService::instance().settingsAllowed()) {
        const auto old=load();settings.parentalEnabled=old.parentalEnabled;
        settings.volumeLimitEnabled=old.volumeLimitEnabled;settings.maxVolumePercent=old.maxVolumePercent;
        settings.lockSources=old.lockSources;settings.lockPowerOff=old.lockPowerOff;settings.lockDisplayControls=old.lockDisplayControls;
    }
    settings.host.trim();
    if (settings.host.length() > 96 || settings.pin.length() > 16 ||
        settings.host.indexOf('/') >= 0 || settings.host.indexOf('?') >= 0 ||
        settings.host.indexOf('#') >= 0 || settings.host.indexOf('@') >= 0) return false;
    for (size_t i = 0; i < settings.host.length(); ++i) {
        const char c = settings.host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':' || c == '[' || c == ']')) return false;
    }
    for (size_t i = 0; i < settings.pin.length(); ++i)
        if ((uint8_t)settings.pin[i] < 32 || (uint8_t)settings.pin[i] == 127) return false;
    settings.maxVolumePercent = constrain(settings.maxVolumePercent, 5, 100);
    return config.putString("rad_host", settings.host) &&
        config.putSecretString("rad_pin", settings.pin) &&
        config.putBool("rad_parent", settings.parentalEnabled) &&
        config.putBool("rad_vlim", settings.volumeLimitEnabled) &&
        config.putU8("rad_vmax", settings.maxVolumePercent) &&
        config.putBool("rad_lsrc", settings.lockSources) &&
        config.putBool("rad_lpwr", settings.lockPowerOff) &&
        config.putBool("rad_ldsp", settings.lockDisplayControls);
}

}

#include "SettingsService.h"
#include "ConfigService.h"
#include <algorithm>
#include "SecurityService.h"
#include "FileBrowserService.h"

namespace hp {

namespace {
constexpr uint8_t kMaxThemeMode = 13; // 8 built-ins + up to 6 SD themes.
}

bool SettingsService::setWallpaperControls(bool enabled,uint8_t dim){SecurityWriteGuard guard;auto& security=SecurityService::instance();
    if(!ok_||!security.settingsAllowed()||security.tabletLocked())return false;auto& config=ConfigService::instance();
    if(!config.putBool("ui_wallon",enabled)||!config.putU8("ui_walldim",std::min<uint8_t>(dim,100)))return false;
    FileBrowserService::notifyWallpaperChanged();return true;
}
bool SettingsService::setWallpaperMode(const String& mode){SecurityWriteGuard guard;auto& security=SecurityService::instance();
    if(!ok_||!security.settingsAllowed()||security.tabletLocked()||(mode!="cover"&&mode!="contain"&&mode!="center"))return false;
    return ConfigService::instance().putString("ui_wmode",mode);
}

bool SettingsService::setTheme(uint8_t mode, uint8_t accent) {
    if (!ok_ || SecurityService::instance().tabletLocked()) return false;
    if (mode > kMaxThemeMode || accent > 5) return false;

    auto& config = ConfigService::instance();
    if (!config.writable()) return false;
    const uint8_t oldMode = std::min<uint8_t>(kMaxThemeMode, config.getU8("ui_theme", 0));
    const uint8_t oldAccent = std::min<uint8_t>(5, config.getU8("ui_accent", 0));
    if (oldMode == mode && oldAccent == accent) return true;

    if (oldMode != mode && !config.putU8("ui_theme", mode)) return false;
    if (oldAccent != accent && !config.putU8("ui_accent", accent)) return false;
    return true;
}

bool SettingsService::wakeOnSingleTap() const {
    return ok_ && ConfigService::instance().getBool("ui_wake_single", false);
}

bool SettingsService::setWakeOnSingleTap(bool enabled) {
    return ok_ && ConfigService::instance().putBool("ui_wake_single", enabled);
}

bool SettingsService::setBrightness(uint8_t value) {
    if (!ok_) return false;
    auto& config = ConfigService::instance();
    if (!config.writable()) return false;
    value = std::max<uint8_t>(1, value);
    if (config.getU8("brightness", 210) == value) return true;
    return config.putU8("brightness", value);
}

bool SettingsService::setWifiPowerSave(bool enabled) {
    if (!ok_) return false;
    auto& config = ConfigService::instance();
    if (!config.writable()) return false;
    if (config.getBool("net_wps", true) == enabled) return true;
    return config.putBool("net_wps", enabled);
}

bool SettingsService::setWifiOffWhenLan(bool enabled) {
    if (!ok_) return false;
    auto& config = ConfigService::instance();
    if (!config.writable()) return false;
    if (config.getBool("net_wlanoff", false) == enabled) return true;
    return config.putBool("net_wlanoff", enabled);
}

SettingsService& SettingsService::instance() {
    static SettingsService s;
    return s;
}

bool SettingsService::begin() {
    ok_ = ConfigService::instance().begin();
    return ok_;
}

static String bounded(String v, size_t n) {
    v.trim();
    if (v.length() > n) v.remove(n);
    return v;
}

static String sanitizeHostname(String v) {
    v = bounded(v, 32);
    String o;
    o.reserve(v.length());
    for (size_t i = 0; i < v.length(); ++i) {
        const char c = v[i];
        if (isalnum((unsigned char)c) || c == '-') o += c;
    }
    while (o.startsWith("-")) o.remove(0, 1);
    while (o.endsWith("-")) o.remove(o.length() - 1);
    if (!o.length()) o = "home-display";
    return o;
}

static void sanitizeProfile(IdleProfile& x) {
    x.dimBrightness = std::max<uint8_t>(1, x.dimBrightness);
    x.saverBrightness = std::max<uint8_t>(1, x.saverBrightness);
    x.alwaysOnBrightness = std::max<uint8_t>(1, x.alwaysOnBrightness);
    if (x.saverEnabled && x.saverAfter && x.dimAfter && x.saverAfter < x.dimAfter)
        x.saverAfter = x.dimAfter;
    if (x.offAfter) {
        uint16_t m = x.dimAfter;
        if (x.saverEnabled && x.saverAfter > m) m = x.saverAfter;
        if (x.offAfter < m) x.offAfter = m;
    }
}

static void loadProf(ConfigService& c, const char* k, IdleProfile& x) {
    String b = k;
    x.dimBrightness = c.getU8((b + "db").c_str(), x.dimBrightness);
    x.dimAfter = c.getU16((b + "da").c_str(), x.dimAfter);
    x.saverEnabled = c.getBool((b + "se").c_str(), x.saverEnabled);
    x.saverBrightness = c.getU8((b + "sb").c_str(), x.saverBrightness);
    x.saverAfter = c.getU16((b + "sa").c_str(), x.saverAfter);
    x.offAfter = c.getU16((b + "of").c_str(), x.offAfter);
    x.alwaysOnEnabled = c.getBool((b + "ao").c_str(), x.alwaysOnEnabled);
    x.alwaysOnBrightness = c.getU8((b + "ab").c_str(), x.alwaysOnBrightness);
    sanitizeProfile(x);
}

static void saveProf(ConfigService& c, const char* k, IdleProfile x) {
    sanitizeProfile(x);
    String b = k;
    c.putU8((b + "db").c_str(), x.dimBrightness);
    c.putU16((b + "da").c_str(), x.dimAfter);
    c.putBool((b + "se").c_str(), x.saverEnabled);
    c.putU8((b + "sb").c_str(), x.saverBrightness);
    c.putU16((b + "sa").c_str(), x.saverAfter);
    c.putU16((b + "of").c_str(), x.offAfter);
    c.putBool((b + "ao").c_str(), x.alwaysOnEnabled);
    c.putU8((b + "ab").c_str(), x.alwaysOnBrightness);
}

SettingsSnapshot SettingsService::snapshot() {
    SettingsSnapshot s;
    auto& c = ConfigService::instance();
    if (!ok_) return s;
    const uint32_t revision=c.revision();
    const auto cached=std::atomic_load(&cached_);
    if(cached&&cached->revision==revision)return cached->value;

    s.hostname = sanitizeHostname(c.getString("host", "home-display"));
    s.weatherRegion = bounded(c.getString("weather", "Muenchen"), 64);
    s.powerHost = bounded(c.getString("power_host", ""), 96);
    s.powerPassword = bounded(c.getSecretString("power_pwd", ""), 128);
    s.energyDetailHistoryEnabled=c.getBool("ed_enabled",false);
    s.energyDetailHistoryIntervalMinutes=c.getU8("ed_interval",5);
    if(s.energyDetailHistoryIntervalMinutes<1||s.energyDetailHistoryIntervalMinutes>30)s.energyDetailHistoryIntervalMinutes=5;
    s.energyDetailHistoryRetentionDays=std::min<uint16_t>(36500,c.getU16("ed_retention",0));
    s.energyDetailHistoryPreferProviderBackfill=c.getBool("ed_backfill",true);

    s.brightness = c.getU8("brightness", 210);
    if (!s.brightness) s.brightness = 1;
    s.rotation = c.getU8("rotation", 0) & 3;

    s.powerManagement.energySaving = c.getBool("pm_energy", true);
    s.powerManagement.autoLightSleepWhenOff = c.getBool("pm_lsleep", false);

    s.networkEco.wifiOffWhenLan = c.getBool("net_wlanoff", false);
    s.networkEco.wifiPowerSave = c.getBool("net_wps", true);
    s.networkEco.aggressiveWifiSleep = c.getBool("net_wpsmax", false);
    s.networkEco.adaptiveTxPower = c.getBool("net_txauto", true);
    s.networkEco.lanStableBeforeWifiOff =
        std::min<uint16_t>(60, c.getU16("net_lanwait", 5));

    s.networkEco.wifiScheduleEnabled = c.getBool("net_wsched", false);
    s.networkEco.wifiScheduleStartMinute =
        std::min<uint16_t>(1439, c.getU16("net_wsstart", 22 * 60));
    s.networkEco.wifiScheduleEndMinute =
        std::min<uint16_t>(1439, c.getU16("net_wsend", 6 * 60));

    s.ui.wallpaperEnabled = c.getBool("ui_wallon", true);
    s.ui.wallpaperDim = std::min<uint8_t>(100, c.getU8("ui_walldim", 36));
    s.ui.wallpaperMode=c.getString("ui_wmode","cover");
    if(s.ui.wallpaperMode!="cover"&&s.ui.wallpaperMode!="contain"&&s.ui.wallpaperMode!="center")s.ui.wallpaperMode="cover";
    s.ui.themeMode = std::min<uint8_t>(kMaxThemeMode, c.getU8("ui_theme", 0));
    s.ui.accent = std::min<uint8_t>(5, c.getU8("ui_accent", 0));
    s.ui.wallpaperPath =
        bounded(c.getString("ui_wall", "/homepanel/media/wallpapers/home.jpg"), 120);

    s.ambientLight.autoBrightness = c.getBool("als_auto", false);
    s.ambientLight.minBrightness = c.getU8("als_min", 25);
    s.ambientLight.maxBrightness = c.getU8("als_max", 220);
    s.ambientLight.darkLux = c.getU16("als_dark", 5);
    s.ambientLight.brightLux = c.getU16("als_bright", 500);

    s.browser.allowHttp = c.getBool("br_http", true);
    s.browser.allowHttps = c.getBool("br_https", true);
    s.browser.allowWan = c.getBool("br_wan", false);
    s.browser.maxPageBytes = c.getU16("br_maxkb", 128) * 1024u;

    s.music.volume = std::min<uint8_t>(100, c.getU8("music_vol", 55));
    s.music.resumeLast = c.getBool("music_resume", true);
    s.music.shuffle = c.getBool("music_shuffle", false);

    loadProf(c, "n_", s.normal);
    loadProf(c, "m_", s.media);
    loadProf(c, "p_", s.power);
    // Never hold the cache's publication lock across NVS reads. A concurrent
    // change invalidates this result instead of publishing it as current.
    if(c.revision()==revision){
        std::shared_ptr<const CachedSettings> next=std::make_shared<CachedSettings>(CachedSettings{s,revision});
        std::atomic_store(&cached_,std::move(next));
    }
    return s;
}

void SettingsService::save(const SettingsSnapshot& in) {
    auto& c = ConfigService::instance();
    if (!ok_ || !c.writable()) return;

    SettingsSnapshot s = in;
    s.hostname = sanitizeHostname(s.hostname);
    s.weatherRegion = bounded(s.weatherRegion, 64);
    s.powerHost = bounded(s.powerHost, 96);
    s.powerPassword = bounded(s.powerPassword, 128);

    if (!s.brightness) s.brightness = 1;
    s.rotation &= 3;

    c.putString("host", s.hostname);
    c.putString("weather", s.weatherRegion);
    c.putString("power_host", s.powerHost);
    c.putSecretString("power_pwd", s.powerPassword);
    c.putU8("brightness", s.brightness);
    c.putU8("rotation", s.rotation);

    c.putBool("pm_energy", s.powerManagement.energySaving);
    c.putBool("ed_enabled",s.energyDetailHistoryEnabled);
    c.putU8("ed_interval",s.energyDetailHistoryIntervalMinutes>=1&&s.energyDetailHistoryIntervalMinutes<=30?s.energyDetailHistoryIntervalMinutes:5);
    c.putU16("ed_retention",std::min<uint16_t>(36500,s.energyDetailHistoryRetentionDays));
    c.putBool("ed_backfill",s.energyDetailHistoryPreferProviderBackfill);
    c.putBool("pm_lsleep", s.powerManagement.autoLightSleepWhenOff);

    s.networkEco.lanStableBeforeWifiOff =
        std::min<uint16_t>(60, s.networkEco.lanStableBeforeWifiOff);
    s.networkEco.wifiScheduleStartMinute =
        std::min<uint16_t>(1439, s.networkEco.wifiScheduleStartMinute);
    s.networkEco.wifiScheduleEndMinute =
        std::min<uint16_t>(1439, s.networkEco.wifiScheduleEndMinute);

    c.putBool("net_wlanoff", s.networkEco.wifiOffWhenLan);
    c.putBool("net_wps", s.networkEco.wifiPowerSave);
    c.putBool("net_wpsmax", s.networkEco.aggressiveWifiSleep);
    c.putBool("net_txauto", s.networkEco.adaptiveTxPower);
    c.putU16("net_lanwait", s.networkEco.lanStableBeforeWifiOff);

    c.putBool("net_wsched", s.networkEco.wifiScheduleEnabled);
    c.putU16("net_wsstart", s.networkEco.wifiScheduleStartMinute);
    c.putU16("net_wsend", s.networkEco.wifiScheduleEndMinute);

    s.ui.wallpaperDim = std::min<uint8_t>(100, s.ui.wallpaperDim);
    s.ui.themeMode = std::min<uint8_t>(kMaxThemeMode, s.ui.themeMode);
    s.ui.accent = std::min<uint8_t>(5, s.ui.accent);
    s.ui.wallpaperPath = bounded(s.ui.wallpaperPath, 120);
    c.putBool("ui_wallon", s.ui.wallpaperEnabled);
    c.putU8("ui_walldim", s.ui.wallpaperDim);
    c.putU8("ui_theme", s.ui.themeMode);
    c.putU8("ui_accent", s.ui.accent);
    c.putString("ui_wall", s.ui.wallpaperPath);

    if (s.ambientLight.minBrightness > s.ambientLight.maxBrightness)
        std::swap(s.ambientLight.minBrightness, s.ambientLight.maxBrightness);
    c.putBool("als_auto", s.ambientLight.autoBrightness);
    c.putU8("als_min", s.ambientLight.minBrightness);
    c.putU8("als_max", s.ambientLight.maxBrightness);
    c.putU16("als_dark", s.ambientLight.darkLux);
    c.putU16("als_bright", s.ambientLight.brightLux);

    c.putBool("br_http", s.browser.allowHttp);
    c.putBool("br_https", s.browser.allowHttps);
    c.putBool("br_wan", s.browser.allowWan);
    c.putU16(
        "br_maxkb",
        std::min<uint32_t>(
            512,
            std::max<uint32_t>(32, s.browser.maxPageBytes / 1024u)
        )
    );

    s.music.volume = std::min<uint8_t>(100, s.music.volume);
    c.putU8("music_vol", s.music.volume);
    c.putBool("music_resume", s.music.resumeLast);
    c.putBool("music_shuffle", s.music.shuffle);

    saveProf(c, "n_", s.normal);
    saveProf(c, "m_", s.media);
    saveProf(c, "p_", s.power);
}

}

#include "WebAppManager.h"
#include "../../services/StorageService.h"
#include "../../services/SettingsService.h"
#include "../../core/AppManager.h"

namespace hp {

static constexpr const char* kPath = WebAppManager::kInstalledAppsPath;
static constexpr const char* kLegacyPath = WebAppManager::kInstalledAppsPath;
static constexpr const char* kSdPath = "/Apps/32/apps.bin";

WebAppManager& WebAppManager::instance() {
    static WebAppManager w;
    return w;
}

bool WebAppManager::begin() {
    load();
    if (!count_) importFromSd();
    return true;
}

bool WebAppManager::reload() {
    count_ = 0;
    load();
    return true;
}

bool WebAppManager::nativeInstalled(const char* id) const {
    return AppManager::instance().isEnabled(id);
}

bool WebAppManager::setNativeInstalled(const char* id, bool on) {
    return AppManager::instance().setEnabled(id, on);
}

static bool privateIp(const String& host) {
    IPAddress ip;
    if (!ip.fromString(host)) return false;
    uint8_t a = ip[0], b = ip[1];
    return a == 10 || a == 127 ||
           (a == 172 && b >= 16 && b <= 31) ||
           (a == 192 && b == 168) ||
           (a == 169 && b == 254);
}

bool WebAppManager::validateUrl(const char* raw, String* reason) const {
    if (!raw || !raw[0]) {
        if (reason) *reason = "URL fehlt";
        return false;
    }

    String u(raw);
    u.trim();

    auto bs = SettingsService::instance().snapshot().browser;
    bool hs = u.startsWith("https://");
    bool h = u.startsWith("http://");

    if (!h && !hs) {
        if (reason) *reason = "Nur http:// oder https://";
        return false;
    }
    if (h && !bs.allowHttp) {
        if (reason) *reason = "HTTP ist deaktiviert";
        return false;
    }
    if (hs && !bs.allowHttps) {
        if (reason) *reason = "HTTPS ist deaktiviert";
        return false;
    }

    int p = u.indexOf("://") + 3;
    int e = u.indexOf('/', p);
    if (e < 0) e = u.length();
    String host = u.substring(p, e);

    if (host.indexOf('@') >= 0) {
        if (reason) *reason = "Credentials in URL nicht erlaubt";
        return false;
    }

    int c = host.indexOf(':');
    if (c > 0) host.remove(c);
    host.toLowerCase();

    bool local = privateIp(host) || host.endsWith(".local") ||
                 host == "localhost" || host.indexOf('.') < 0;

    if (!bs.allowWan && !local) {
        if (reason) *reason = "Nur lokale Webseiten erlaubt";
        return false;
    }

    return true;
}

bool WebAppManager::validateRecord(const WebAppRecord& r, String* reason) const {
    if (r.schema!=2) {
        if (reason) *reason = "Schema";
        return false;
    }

    if (!r.id || !r.name[0] || strnlen(r.name, sizeof(r.name)) >= sizeof(r.name) ||
        !r.url[0] || strnlen(r.url, sizeof(r.url)) >= sizeof(r.url)) {
        if (reason) *reason = "Felder";
        return false;
    }

    if (strstr(r.icon, "..")) {
        if (reason) *reason = "Icon-Pfad";
        return false;
    }

    if (r.icon[0] &&
        strncmp(r.icon, "user/webapps/", 13) != 0 &&
        strncmp(r.icon, "/homepanel/data/webapps/", 24) != 0) {
        if (reason) *reason = "Icon-Pfad";
        return false;
    }

    if (r.allowAudio && !(r.permissions & perm(AppPermission::Audio))) {
        if (reason) *reason = "Audio-Recht";
        return false;
    }
    if (r.keepAwake && !(r.permissions & perm(AppPermission::KeepScreenOn))) {
        if (reason) *reason = "KeepAwake-Recht";
        return false;
    }

    return validateUrl(r.url, reason);
}

uint32_t WebAppManager::makeId(const char* n, const char* u) const {
    uint32_t h = 2166136261u;
    for (const char* p = n; p && *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    for (const char* p = u; p && *p; ++p) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h ? h : 1;
}

bool WebAppManager::loadLegacy(fs::FS& fs, const String& p, size_t size) {
    if (size % sizeof(LegacyWebAppRecord)) return false;

    File f = fs.open(p, FILE_READ);
    if (!f) return false;

    while (count_ < kMaxApps && f.available() >= (int)sizeof(LegacyWebAppRecord)) {
        LegacyWebAppRecord x{};
        if (f.read((uint8_t*)&x, sizeof(x)) != sizeof(x)) break;

        WebAppRecord r{};
        r.id = x.id;
        r.enabled = x.enabled;
        r.renderMode = x.renderMode;
        r.permissions = perm(AppPermission::Network) | perm(AppPermission::UserStorage);
        strlcpy(r.name, x.name, sizeof(r.name));
        strlcpy(r.url, x.url, sizeof(r.url));

        String reason;
        if (validateRecord(r, &reason)) apps_[count_++] = r;
    }

    f.close();
    return save();
}

bool WebAppManager::load() {
    count_ = 0;
    auto& st = StorageService::instance();
    auto* fs = st.fsFor(StorageArea::AppData);
    String p = st.pathFor(StorageArea::AppData, kPath);

    if (fs) {
        File f = fs->open(p, FILE_READ);
        if (f) {
            size_t size = f.size();
            WebAppFileHeader h{};
            bool header = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h);

            if (!header || h.magic != 0x48445741) {
                f.close();
                return loadLegacy(*fs, p, size);
            }

            if (h.schema != 2 || h.recordSize != sizeof(WebAppRecord) || h.count > kMaxApps) {
                f.close();
                return false;
            }

            for (uint16_t i = 0; i < h.count; ++i) {
                WebAppRecord r{};
                if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) {
                    f.close();
                    return false;
                }

                String why;
                if (!validateRecord(r, &why)) {
                    f.close();
                    return false;
                }

                apps_[count_++] = r;
            }

            f.close();
            return true;
        }
    }

    auto* legacyFs = st.fsFor(StorageArea::UserData);
    String legacyPath = st.pathFor(StorageArea::UserData, kLegacyPath);
    if (!legacyFs) return true;

    File legacy = legacyFs->open(legacyPath, FILE_READ);
    if (!legacy) return true;

    size_t size = legacy.size();
    legacy.close();
    return loadLegacy(*legacyFs, legacyPath, size);
}

bool WebAppManager::importFromSd() {
    auto& st = StorageService::instance();
    if (!st.sdAvailable()) return false;

    File f = st.openSdRead(kSdPath);
    if (!f) return false;

    WebAppFileHeader h{};
    if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) ||
        h.magic != 0x48445741 ||
        h.schema != 2 ||
        h.recordSize != sizeof(WebAppRecord) ||
        h.count > kMaxApps) {
        f.close();
        return false;
    }

    for (uint16_t i = 0; i < h.count && count_ < kMaxApps; ++i) {
        WebAppRecord r{};
        if (f.read((uint8_t*)&r, sizeof(r)) != sizeof(r)) break;

        String why;
        if (!validateRecord(r, &why)) continue;

        bool dup = false;
        for (size_t j = 0; j < count_; ++j) {
            if (apps_[j].id == r.id) {
                dup = true;
                break;
            }
        }

        if (!dup) apps_[count_++] = r;
    }

    f.close();
    return save();
}

bool WebAppManager::save() {
    WebAppFileHeader h;
    h.count = count_;

    const size_t n = sizeof(h) + count_ * sizeof(WebAppRecord);
    uint8_t* buf = (uint8_t*)ps_malloc(n);
    if (!buf) buf = (uint8_t*)malloc(n);
    if (!buf) return false;

    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), apps_, count_ * sizeof(WebAppRecord));

    bool ok = StorageService::instance().writeAtomic(StorageArea::AppData, kPath, buf, n);
    free(buf);
    return ok;
}

bool WebAppManager::add(const char* n, const char* u, WebAppRenderMode m) {
    if (!n || !n[0] || strlen(n) >= 40 ||
        !u || strlen(u) >= 160 ||
        count_ >= kMaxApps) return false;

    WebAppRecord r{};
    r.id = makeId(n, u);
    r.renderMode = (uint8_t)m;
    r.permissions = perm(AppPermission::Network) | perm(AppPermission::UserStorage);
    strlcpy(r.name, n, sizeof(r.name));
    strlcpy(r.url, u, sizeof(r.url));

    String why;
    if (!validateRecord(r, &why)) return false;

    for (size_t i = 0; i < count_; ++i) {
        if (apps_[i].id == r.id) return false;
    }

    apps_[count_++] = r;
    if (!save()) {
        --count_;
        return false;
    }
    return true;
}


bool WebAppManager::setEnabled(size_t index, bool enabled) {
    if (index >= count_) return false;
    const uint8_t old = apps_[index].enabled;
    apps_[index].enabled = enabled ? 1 : 0;
    if (save()) return true;
    apps_[index].enabled = old;
    return false;
}

bool WebAppManager::remove(size_t i) {
    if (i >= count_) return false;
    for (size_t j = i + 1; j < count_; ++j) apps_[j - 1] = apps_[j];
    apps_[--count_] = {};
    return save();
}

}

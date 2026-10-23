#include "FileTransferService.h"
#include "ImageCodec.h"
#include "PhotoLibraryService.h"
#include "WebTlsIdentity.h"
#include <esp_https_server.h>
#include "../core/PerformancePolicy.h"
#include "OAuthWebRoutes.h"
#include "StorageService.h"
#include "UsbStorageService.h"
#include "SecurityService.h"
#include "ConfigService.h"
#include "NetworkWorker.h"
#include "LuaAppDataCleanupService.h"
#include "LuaPackageService.h"
#include "WebCsrfService.h"
#include "WebSettingsSchemaValidator.h"
#include "BuildInfo.h"
#include "PowerManager.h"
#include "OtaService.h"
#include "OtaWriteSession.h"
#include "FirmwareMediaUpdateService.h"
#include "../core/EventLog.h"
#include "../core/AppManager.h"
#include "../core/AppRegistry.h"
#include "../apps/browser/WebAppManager.h"
#include "../apps/lua/LuaApp.h"
#include "../apps/lua/LuaAppRegistry.h"
#include "../apps/lua/LuaPackageInstaller.h"
#include "../apps/lua/LuaAppManifest.h"
#include "../ui/UiShell.h"
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <bootloader_common.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include "WebAdminAuth.h"
#include "WebLoginService.h"
#include "InstallerTransferLease.h"
#include <cstring>
#include <esp_random.h>
#include <mutex>

namespace hp {
namespace {
// Port 80 and 8088 share one installer. Serialize complete HTTP operations,
// including the wait for UI commit; a second server must not reset its target FS.
std::mutex luaUploadMutex;
InstallerTransferLease luaTransferLease;
bool installOwner(httpd_req_t* req,char (&owner)[65]){
    if(httpd_req_get_hdr_value_len(req,"X-Install-Session")==0)strlcpy(owner,"legacy-upload-owner",sizeof(owner));
    else if(httpd_req_get_hdr_value_str(req,"X-Install-Session",owner,sizeof(owner))!=ESP_OK)return false;
    return InstallerTransferLease::validOwner(owner);
}
std::mutex& fileOperationsMutex=StorageService::mutationMutex();
esp_err_t installerBusy(httpd_req_t* req){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"installer_busy");}

bool commitUploadedFile(fs::FS* fs, const String& temp, const String& target) {
    const String backup = target + ".bak";
    // Preserve an interrupted previous commit before touching the old file.
    if (fs->exists(backup)) {
        if (!fs->exists(target)) {
            if (!fs->rename(backup, target)) return false;
        } else if (!fs->remove(backup)) return false;
    }
    const bool hadTarget = fs->exists(target);
    if (hadTarget && !fs->rename(target, backup)) return false;
    if (!fs->rename(temp, target)) {
        if (hadTarget) fs->rename(backup, target);
        return false; // backup remains recoverable if restoration fails
    }
    if (hadTarget) fs->remove(backup);
    return true;
}

// HTTP Basic authentication for local admin actions. HTTP is unencrypted:
// use only on a trusted local network; do not expose port 8088 to the WAN.
bool initializeWebAdminPassword() {
    if (!WebAdminAuth::initialize()) return false;
    // Never disclose the credential in HTTP responses or diagnostic logs.
    Serial.println("[WEB ADMIN] Login: admin");
    Serial.println("[WEB ADMIN] Configured password retained; not printed in logs");
    return true;
}

bool webAdminAuthorized(httpd_req_t* req) {
    // Port 80 reuses these handlers even when the dedicated :8088 server is
    // intentionally stopped. Initialize the shared admin credential lazily.
    if (!WebAdminAuth::initialize()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Web-Administration konnte nicht initialisiert werden");
        return false;
    }

    // Prevent a different website from using cached browser Basic credentials
    // to trigger an admin operation via an embedded form or cross-site fetch.
    // Command-line clients without Sec-Fetch-Site remain supported.
    char fetchSite[32]{};
    if (httpd_req_get_hdr_value_str(req, "Sec-Fetch-Site", fetchSite,
                                   sizeof(fetchSite)) == ESP_OK &&
        strcmp(fetchSite, "same-origin") != 0 &&
        strcmp(fetchSite, "none") != 0) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Cross-origin administration denied");
        return false;
    }
    if (WebAdminAuth::authorized(req)) return true;
    WebLoginService::deny(req);
    return false;
}

bool writeAuthorized(httpd_req_t* req) {
    return webAdminAuthorized(req) && WebCsrfService::instance().validate(req);
}
esp_err_t fileConflict(httpd_req_t* req,const char* message){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,message);}
esp_err_t fileBusyResponse(httpd_req_t* req){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Dateiverwaltung beschaeftigt");}

String installerErrorText(const String& raw) {
    if(raw=="manifest_schema_v3_required")return "Lua-App Schema 3 erforderlich; alte Pakete und App-Daten bleiben erhalten";
    if (raw.startsWith("web_settings_json:"))
        return "web_settings.json ist kein gültiges JSON: " + raw.substring(strlen("web_settings_json:"));
    if (raw.startsWith("lua_syntax:"))
        return "Lua-Syntaxfehler: " + raw.substring(strlen("lua_syntax:"));
    if (raw == "lua_script_read_failed") return "Lua-Datei unvollständig gelesen; Speicher/USB/SD prüfen und Installation wiederholen";
    if (raw == "lua_script_binary") return "Lua-Datei enthält Binärdaten statt gültigem Quelltext";
    if (raw == "lua_memory:script_buffer") return "Lua-Skriptpuffer konnte nicht angelegt werden (PSRAM/RAM)";
    if (raw == "web_settings_open") return "web_settings.json konnte nicht geöffnet werden";
    if (raw == "web_settings_size") return "web_settings.json ist leer oder größer als 16 KiB";
    if (raw.startsWith("web_settings_")) return WebSettingsSchemaValidator::message(raw);
    if (raw == "settings_file_missing") return "settings.lua fehlt, obwohl die App Einstellungen deklariert";
    if (raw == "settings_file_unexpected") return "settings.lua ist laut app.json nicht vorgesehen";
    if (raw == "settings_callback_missing") return "settings.lua enthält keinen gültigen Einstellungs-Einstiegspunkt";
    if (raw == "invalid_package_assets") return "Mindestens ein App-Bild/Asset ist ungültig";
    if (raw == "downgrade_confirmation_required") return "Downgrade muss ausdrücklich bestätigt werden";
    if (raw == "installer_busy") return "Der App-Installer ist gerade beschäftigt";
    if (raw == "installer_ui_timeout") return "Die App-Aktivierung hat zu lange gedauert";
    if (raw == "package_scan_busy") return "Der App-Scan läuft noch. Bitte erneut versuchen.";
    if (raw == "duplicate_package_locations") return "Die App ist mehrfach im Speicher vorhanden; bitte Duplikat bereinigen";
    if (raw == "appfs_unavailable") return "App-Speicher ist nicht verfügbar";
    return raw;
}

String htmlEscape(const String& input) {
    String out;
    out.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        switch (input[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += input[i]; break;
        }
    }
    return out;
}

bool safeBrowserPath(const String& input) {
    if (!input.length() || input.length() > 180 || input[0] != '/') return false;
    if (input.indexOf('\\') >= 0) return false;
    if (input.indexOf("../") >= 0 || input.endsWith("/..") || input.endsWith("/.") ||
        input.indexOf("/./") >= 0 || input.indexOf("//") >= 0 || input.indexOf(':') >= 0) return false;
    for(size_t i=0;i<input.length();++i)if((unsigned char)input[i]<32)return false;
    return true;
}

String normalizeBrowserPath(String path) {
    if (!path.length()) return "/";
    if (!path.startsWith("/")) path = "/" + path;
    while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
    return path;
}

String browserParent(const String& path) {
    if (path == "/") return "/";
    const int slash = path.lastIndexOf('/');
    if (slash <= 0) return "/";
    return path.substring(0, slash);
}

String browserJoin(const String& parent, const String& childName) {
    if (parent == "/") return "/" + childName;
    return parent + "/" + childName;
}

String fileBaseName(const String& path) {
    int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.substring(slash + 1) : path;
}

bool directoryEmpty(fs::FS& fs, const String& path) {
    File dir = fs.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }
    File child = dir.openNextFile();
    const bool empty = !child;
    if (child) child.close();
    dir.close();
    return empty;
}

bool browserMount(
    const String& areaName,
    fs::FS*& fs,
    const char*& label
) {
    auto& storage = StorageService::instance();
    fs = nullptr;

    if (areaName == "flash") {
        fs = storage.fsFor(StorageArea::SystemData);
        label = "Interner Flash";
        return fs != nullptr;
    }

    if (areaName == "app") {
        fs = storage.fsFor(StorageArea::AppData);
        label = "AppFS";
        return fs != nullptr;
    }

    if (areaName == "sd") {
        if (!storage.sdAvailable()) return false;
        fs = storage.fsFor(StorageArea::UserData);
        label = "SD-Karte";
        return fs != nullptr;
    }
    if (areaName == "usb") {
        fs = storage.fsFor(StorageClass::UsbAppPackage);
        label = "USB-Speicher";
        return fs != nullptr;
    }

    return false;
}

bool protectedFlashPath(const String& rel) {
    return rel == "/systemdata" || rel.startsWith("/systemdata/") ||
           rel == "/config" || rel.startsWith("/config/") ||
           rel == "/recovery" || rel.startsWith("/recovery/");
}

String kindName(const IApp* app) {
    if (!app) return "";
    switch (app->kind()) {
        case AppKind::Lua: return "Lua";
        case AppKind::Native: return "Native";
        case AppKind::System: return "System";
    }
    return "";
}

bool safeAppId(const String& id) {
    if (!id.length() || id.length() > 31) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char c = id[i];
        if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    }
    return true;
}

bool eraseTree(fs::FS& fs, const String& path) {
    File node = fs.open(path);
    if (!node) return true;

    if (!node.isDirectory()) {
        node.close();
        return fs.remove(path);
    }

    File child = node.openNextFile();
    while (child) {
        String childPath = child.path();
        bool isDir = child.isDirectory();
        child.close();

        bool ok = isDir ? eraseTree(fs, childPath) : fs.remove(childPath);
        if (!ok) {
            node.close();
            return false;
        }
        child = node.openNextFile();
    }

    node.close();
    return fs.rmdir(path);
}

bool luaPackageExists(const char* id) {
    if (!id || !id[0]) return false;
    auto& st = StorageService::instance();
    if (st.appFsAvailable()) {
        auto* fs = st.fsFor(StorageClass::AppPackage);
        if (fs) {
            const String paths[] = {
                String("/") + id + "/main.lua",
                String("/apps/") + id + "/main.lua",
                String("/Apps/") + id + "/main.lua",
                String("/Apps/32/") + id + "/main.lua"
            };
            for (const auto& p : paths) if (fs->exists(p)) return true;
        }
    }
    if(auto* fs=st.fsFor(StorageClass::UsbAppPackage)){
        for(const auto& root:{"/Apps/32/","/apps/"})if(fs->exists(String(root)+id+"/main.lua"))return true;
    }
    if (st.sdAvailable()) {
        const String paths[] = {
            String("/Apps/32/") + id + "/main.lua",
            String("/apps/32/") + id + "/main.lua",
            String("/apps/") + id + "/main.lua"
        };
        for (const auto& p : paths) if (st.sdAbsoluteExists(p)) return true;
    }
    return false;
}

String actionButton(const String& href, const String& text, bool danger = false) {
    return "<button type='button' class='btn " + String(danger ? "danger" : "") +
           "' data-url='" + htmlEscape(href) +
           "' onclick=\"postAction(this.dataset.url)\">" + htmlEscape(text) + "</button>";
}

void redirectApps(httpd_req_t* req) {
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/apps");
    httpd_resp_send(req, nullptr, 0);
}


struct FirmwareImageInfo {
    bool valid = false;
    bool productCompatible = false;
    bool chipCompatible = false;
    String project;
    String version;
    uint16_t chipId = 0;
    String error;
};

int compareFirmwareVersions(const String& a, const String& b) {
    size_t pa = 0, pb = 0;
    for (int part = 0; part < 4; ++part) {
        while (pa < a.length() && !isDigit(a[pa])) ++pa;
        while (pb < b.length() && !isDigit(b[pb])) ++pb;

        int va = 0, vb = 0;
        while (pa < a.length() && isDigit(a[pa])) {
            va = va * 10 + (a[pa++] - '0');
        }
        while (pb < b.length() && isDigit(b[pb])) {
            vb = vb * 10 + (b[pb++] - '0');
        }

        if (va < vb) return -1;
        if (va > vb) return 1;

        while (pa < a.length() && a[pa] != '.') ++pa;
        while (pb < b.length() && b[pb] != '.') ++pb;
        if (pa < a.length()) ++pa;
        if (pb < b.length()) ++pb;
    }
    return 0;
}

bool parseFirmwareInfo(const uint8_t* data, size_t size, FirmwareImageInfo& out) {
    out = {};

    constexpr size_t kInfoOffset =
        sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);

    if (!data || size < kInfoOffset + sizeof(esp_app_desc_t)) {
        out.error = "firmware_header_too_small";
        return false;
    }

    const auto* image = reinterpret_cast<const esp_image_header_t*>(data);
    if (image->magic != ESP_IMAGE_HEADER_MAGIC) {
        out.error = "invalid_esp_image";
        return false;
    }

    esp_app_desc_t app{};
    memcpy(&app, data + kInfoOffset, sizeof(app));

    if (app.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        out.error = "invalid_app_description";
        return false;
    }
    if (!memchr(app.project_name, 0, sizeof(app.project_name)) ||
        !memchr(app.version, 0, sizeof(app.version))) {
        out.error = "invalid_app_description";
        return false;
    }

    const esp_app_desc_t* runningDesc = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();

    esp_image_header_t runningHeader{};
    if (!running ||
        esp_partition_read(running, 0, &runningHeader, sizeof(runningHeader)) != ESP_OK) {
        out.error = "running_image_unavailable";
        return false;
    }

    out.project = String(app.project_name);
    out.version = String(app.version);
    out.chipId = image->chip_id;
    out.productCompatible =
        runningDesc &&
        strncmp(app.project_name, runningDesc->project_name, sizeof(app.project_name)) == 0;
    out.chipCompatible = image->chip_id == runningHeader.chip_id;
    out.valid = out.productCompatible && out.chipCompatible;

    if (!out.productCompatible) {
        out.error = "wrong_product";
        return false;
    }
    if (!out.chipCompatible) {
        out.error = "wrong_chip";
        return false;
    }
    if (!bootloader_common_check_chip_revision_validity(image, true)) {
        out.valid = false;
        out.error = "wrong_chip_revision";
        return false;
    }

    return true;
}

String bytesToHex(const uint8_t* bytes, size_t count) {
    static constexpr char kHex[] = "0123456789abcdef";
    String result;
    result.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
        result += kHex[(bytes[i] >> 4) & 0x0f];
        result += kHex[bytes[i] & 0x0f];
    }
    return result;
}

bool parseSha256Header(const char* text, uint8_t out[32]) {
    if (!text || strlen(text) != 64) return false;

    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    for (size_t i = 0; i < 32; ++i) {
        const int hi = nibble(text[i * 2]);
        const int lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

bool partitionSha256Exact(
    const esp_partition_t* partition,
    size_t size,
    uint8_t out[32],
    uint8_t* buffer
) {
    if (!partition || !buffer || size == 0 || size > partition->size) return false;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;

    size_t offset = 0;
    while (ok && offset < size) {
        const size_t chunk = min<size_t>(4096, size - offset);
        ok =
            esp_partition_read(partition, offset, buffer, chunk) == ESP_OK &&
            mbedtls_sha256_update(&ctx, buffer, chunk) == 0;
        offset += chunk;
        vTaskDelay(1);
    }

    if (ok) ok = mbedtls_sha256_finish(&ctx, out) == 0;

    mbedtls_sha256_free(&ctx);
    return ok;
}

}

FileTransferService& FileTransferService::instance() {
    static FileTransferService f;
    return f;
}

bool FileTransferService::validUploadName(const String& name) {
    if (!name.length() || name.length() > 96 || name.indexOf("..") >= 0 ||
        name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        if ((unsigned char)name[i] < 32) return false;
    }
    return true;
}

String FileTransferService::destinationFor(const String& name) {
    String lower = name;
    lower.toLowerCase();

    if (lower.endsWith(".mp3") || lower.endsWith(".wav") ||
        lower.endsWith(".flac") || lower.endsWith(".aac") ||
        lower.endsWith(".m4a")) return "/imports/music/" + name;

    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg") ||
        lower.endsWith(".png") || lower.endsWith(".webp"))
        return "/imports/images/" + name;

    if (lower.endsWith(".ics")) return "/imports/calendar/" + name;
    return "/imports/files/" + name;
}

esp_err_t FileTransferService::indexHandler(httpd_req_t* req) {
    if (!webAdminAuthorized(req)) return ESP_OK;
    // Keep the root handler tiny. The previous implementation called the
    // complete /apps handler recursively in the same HTTP server task and
    // performed Basic Auth twice. Redirecting gives the large page a fresh
    // request/stack frame.
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/apps");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "");
}

esp_err_t FileTransferService::uploadHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char encoded[128]{};
    if (httpd_req_get_hdr_value_str(req, "X-Filename", encoded, sizeof(encoded)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing filename");

    String name = encoded;
    name.replace("%20", " ");
    name.replace("%28", "(");
    name.replace("%29", ")");

    if (!validUploadName(name))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid filename");

    auto& st = StorageService::instance();
    if (!st.sdAvailable()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "SD card required");
    }

    const String rel = destinationFor(name);
    if (!st.ensureParentDirs(StorageClass::UserData, rel))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "storage");

    fs::FS* fs = st.fsFor(StorageClass::UserData);
    if (!fs) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "storage");

    const String target = st.pathFor(StorageClass::UserData, rel);
    const String temp = target + ".part";
    fs->remove(temp);

    File f = fs->open(temp, FILE_WRITE);
    if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open");

    PerformanceGuard boost;
    int remaining = req->content_len;
    uint8_t buffer[4096];

    while (remaining > 0) {
        const int want = min<int>(remaining, sizeof(buffer));
        const int got = httpd_req_recv(req, (char*)buffer, want);
        if (got <= 0 || f.write(buffer, got) != (size_t)got) {
            f.close();
            fs->remove(temp);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload failed");
        }
        remaining -= got;
        delay(0);
    }

    f.flush();
    f.close();
    if (!commitUploadedFile(fs, temp, target)) {
        fs->remove(temp);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "commit failed");
    }

    EventLog::instance().add("INFO", "FILE_UPLOAD", "Imported " + name);
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t FileTransferService::filesIndexHandler(httpd_req_t* req) {
    if (!webAdminAuthorized(req)) return ESP_OK;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");

    char query[256]{};
    char rawArea[12]{};
    char rawPath[192]{};
    char rawOffset[12]{};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "area", rawArea, sizeof(rawArea));
        httpd_query_key_value(query, "path", rawPath, sizeof(rawPath));
        httpd_query_key_value(query, "offset", rawOffset, sizeof(rawOffset));
    }

    String areaName = rawArea[0] ? urlDecode(rawArea) : "flash";
    String rel = rawPath[0] ? urlDecode(rawPath) : "/";
    rel = normalizeBrowserPath(rel);
    char* offsetEnd=nullptr;const unsigned long offset=rawOffset[0]?strtoul(rawOffset,&offsetEnd,10):0;
    if(offset>16384||(rawOffset[0]&&(!offsetEnd||*offsetEnd)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ungueltige Seite");

    if (!safeBrowserPath(rel))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ungueltiger Pfad");

    fs::FS* fs = nullptr;
    const char* areaLabel = nullptr;
    if (!browserMount(areaName, fs, areaLabel))
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                                   areaName == "sd" ? "SD-Karte nicht vorhanden"
                                                    : "Speicher nicht verfuegbar");

    const String absolute = rel;
    File dir = fs->open(absolute, FILE_READ);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                                   "Ordner nicht gefunden");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    const char* head = R"HTML(
<!doctype html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="utf-8">
<title>Home-Display Dateimanager</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#101923;color:#edf3f9;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:1080px;margin:24px auto;padding:0 16px 36px}
h1{font-size:27px;margin:0 0 4px}.sub{color:#b8c8d5;margin:0 0 18px}
.card{background:#1c2a38;border:1px solid #334858;border-radius:16px;padding:20px;margin:16px 0}
.btn{display:inline-block;text-decoration:none;color:#edf3f9;background:#263c4c;border:1px solid #607c90;border-radius:8px;padding:10px 12px;margin:4px;font-size:14px;cursor:pointer}
.danger{color:#662b2b;background:#cdb8b8}
.active{background:#aeb9c6}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:10px;border-bottom:1px solid #bababa;vertical-align:middle}
th{font-size:12px;color:#555}
.path{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;background:#c5c5c5;padding:9px;border-radius:4px;overflow-wrap:anywhere}
.hint{font-size:13px;color:#b8c8d5;margin-top:8px}
.folder{font-weight:600}
.size{white-space:nowrap}
@media(max-width:700px){th:nth-child(2),td:nth-child(2){display:none}.wrap{margin-top:14px}}
</style>
</head>
<body><div class="wrap">
<h1>Dateien und Ordner</h1>
<p class="sub">AppFS und Benutzerdaten anzeigen und bei Bedarf manuell bereinigen.</p>
<a class="btn" href="/">Zurück zur Übersicht</a>
)HTML";
    httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);

    String nav;
    nav.reserve(1200);
    nav += "<div class='card'><div>";
    nav += "<button class='btn";
    if (areaName == "flash") nav += " active";
    nav += "' onclick=\"goArea('flash')\">Interner Flash</button>";
    nav += "<button class='btn";
    if (areaName == "app") nav += " active";
    nav += "' onclick=\"goArea('app')\">AppFS</button>";
    if (StorageService::instance().sdAvailable()) {
        nav += "<button class='btn";
        if (areaName == "sd") nav += " active";
        nav += "' onclick=\"goArea('sd')\">SD-Karte</button>";
    }
    nav += "</div>";
    if(UsbStorageService::instance().available())nav += "<button class='btn' onclick=\"goArea('usb')\">USB-Speicher</button>";
    nav += "<p class='path'>";
    nav += htmlEscape(String(areaLabel) + ": " + rel);
    nav += "</p>";
    if (rel != "/") {
        nav += "<button class='btn' onclick=\"goPath(this.dataset.path)\" data-path='";
        nav += htmlEscape(browserParent(rel));
        nav += "'>Eine Ebene hoch</button>";
    }
    nav += "<p class='hint'>Interner Flash und AppFS werden vollstaendig angezeigt. "
           "Kritische Flash-Bereiche /systemdata, /config und /recovery sind sichtbar, aber vor manuellem Loeschen geschuetzt. "
           "Eine SD-Karte erscheint automatisch, wenn sie gemountet ist. Loeschen ist endgueltig.</p></div>";
    nav += "<div class='card'><table><thead><tr><th>Name</th><th>Groesse</th><th>Aktionen</th></tr></thead><tbody>";
    httpd_resp_send_chunk(req, nav.c_str(), nav.length());

    size_t shown = 0;
    File child = dir.openNextFile();
    for(size_t skipped=0;child&&skipped<offset;++skipped){child.close();child=dir.openNextFile();}
    while (child && shown < 200) {
        const bool isDir = child.isDirectory();
        String name = fileBaseName(String(child.path()));
        if (!name.length()) name = fileBaseName(String(child.name()));
        const String childRel = browserJoin(rel, name);
        const size_t size = isDir ? 0 : child.size();
        child.close();

        String row;
        row.reserve(800 + name.length() + childRel.length());
        row += "<tr><td";
        if (isDir) row += " class='folder'";
        row += ">";
        row += isDir ? "[ORDNER] " : "[DATEI] ";
        row += htmlEscape(name);
        row += "</td><td class='size'>";
        if (isDir) row += "-";
        else {
            row += String((unsigned long)size);
            row += " B";
        }
        row += "</td><td>";
        if (isDir) {
            row += "<button class='btn' data-path='";
            row += htmlEscape(childRel);
            row += "' onclick=\"goPath(this.dataset.path)\">Oeffnen</button>";
        }
        if(!isDir && areaName!="app" && !(areaName=="flash"&&protectedFlashPath(childRel))){
            row += "<a class='btn' href='/files/download?area="+areaName+"&amp;path=";
            // Percent-encode UTF-8 and punctuation; never put a filename into JavaScript.
            for(size_t i=0;i<childRel.length();++i){char encoded[4];snprintf(encoded,sizeof(encoded),"%%%02X",(unsigned char)childRel[i]);row+=encoded;}
            row += "'>Herunterladen</a>";
        }
        if(areaName!="app"&&!(areaName=="flash"&&protectedFlashPath(childRel))){
            row += "<button class='btn' data-path='"+htmlEscape(childRel)+"' onclick=\"renameEntry(this.dataset.path)\">Umbenennen</button>";
        }
        const bool deleteProtected = areaName=="app" ||
            (areaName == "flash" && protectedFlashPath(childRel));
        if (!deleteProtected) {
            row += "<button class='btn danger' data-path='";
            row += htmlEscape(childRel);
            row += "' data-name='";
            row += htmlEscape(name);
            row += "' onclick=\"removeEntry(this.dataset.path,this.dataset.name)\">Loeschen</button>";
        } else {
            row += "<span class='hint'>geschuetzt</span>";
        }
        row += "</td></tr>";
        httpd_resp_send_chunk(req, row.c_str(), row.length());

        ++shown;
        child = dir.openNextFile();
    }
    const bool more=bool(child);if (child) child.close();
    dir.close();

    if (shown == 0) {
        const char* empty = "<tr><td colspan='3'>Ordner ist leer.</td></tr>";
        httpd_resp_send_chunk(req, empty, HTTPD_RESP_USE_STRLEN);
    } else if (more) {
        const char* limited =
            "<tr><td colspan='3'>Anzeige auf 200 Eintraege begrenzt.</td></tr>";
        httpd_resp_send_chunk(req, limited, HTTPD_RESP_USE_STRLEN);
    }

    String tail = R"HTML(
</tbody></table></div>
<div class="card"><h2>Dateien verwalten</h2><button class="btn" onclick="createFolder()">Neuer Ordner</button><label>Datei in diesen Ordner hochladen <input id="localFile" type="file"></label><button class="btn" onclick="uploadFile()">Hochladen</button><p id="fileStatus" role="status"></p><p class="hint">Neue Dateien überschreiben keine vorhandenen Dateien. App-Pakete bitte über den App-Installer verwalten. Geschützte Systembereiche sind nicht herunterladbar oder beschreibbar.</p></div>
<script>
const AREA=')HTML";
    tail += areaName;
    tail += R"HTML(';
const CSRF='__CSRF_FILES__';
const CURRENT=new URL(location.href).searchParams.get('path')||'/';
let fileBusy=false;
async function fileOperation(op,path,body=null,destination=''){
 if(fileBusy)return;fileBusy=true;document.getElementById('fileStatus').textContent='Wird verarbeitet …';
 try{const r=await fetch('/files/'+op,{method:'POST',headers:{'X-HP-CSRF':CSRF,'X-Storage-Area':AREA,'X-Path':encodeURIComponent(path),'X-Destination':encodeURIComponent(destination)},body});const text=await r.text();if(!r.ok)throw Error(text);location.reload()}catch(e){document.getElementById('fileStatus').textContent=e.message}finally{fileBusy=false}
}
function joined(name){if(!name||name==='.'||name==='..'||/[\\/\x00-\x1f:]/.test(name))throw Error('Ungültiger Name');return CURRENT.replace(/\/$/,'')+'/'+name}
function createFolder(){const name=prompt('Name des neuen Ordners');if(name===null)return;try{fileOperation('mkdir',joined(name))}catch(e){alert(e.message)}}
function renameEntry(path){const name=prompt('Neuer Name',path.split('/').pop());if(name===null)return;try{fileOperation('rename',path,null,joined(name))}catch(e){alert(e.message)}}
function uploadFile(){const file=document.getElementById('localFile').files[0];if(!file)return;try{fileOperation('upload',joined(file.name),file)}catch(e){alert(e.message)}}
function goArea(a){
  location.href='/files?area='+encodeURIComponent(a)+'&path=%2F';
}
function goPath(p){
  location.href='/files?area='+encodeURIComponent(AREA)+'&path='+encodeURIComponent(p);
}
async function removeEntry(p,n){
  if(!confirm('Wirklich loeschen?\\n\\n'+n+'\\n'+p))return;
  try{
    let r=await fetch('/files/delete',{
      method:'POST',
      headers:{
        'X-HP-CSRF':CSRF,
        'X-Storage-Area':AREA,
        'X-Path':encodeURIComponent(p)
      }
    });
    let t=await r.text();
    if(!r.ok)throw new Error(t||('HTTP '+r.status));
    if(t)alert(t);
    location.reload();
  }catch(e){
    alert('Loeschen fehlgeschlagen: '+e.message);
  }
}
</script>
</div></body></html>
)HTML";
    tail.replace("__CSRF_FILES__", htmlEscape(WebCsrfService::instance().token()));
    if(more)tail.replace("</tbody></table></div>","</tbody></table><button class='btn' onclick=\"const u=new URL(location.href);u.searchParams.set('offset','"+String(offset+shown)+"');location.href=u\">Nächste Seite</button></div>");
    httpd_resp_send_chunk(req, tail.c_str(), tail.length());
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t FileTransferService::filesOperationHandler(httpd_req_t* req) {
    if (!webAdminAuthorized(req)) return ESP_OK;
    const bool download=req->method==HTTP_GET;
    if(!download&&!writeAuthorized(req))return ESP_OK;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed())
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");
    char area[12]{},path[768]{},destination[768]{};
    if(download){char query[850]{};if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||
       httpd_query_key_value(query,"area",area,sizeof(area))!=ESP_OK||httpd_query_key_value(query,"path",path,sizeof(path))!=ESP_OK)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"storage/path fehlt");
    }else if(httpd_req_get_hdr_value_str(req,"X-Storage-Area",area,sizeof(area))!=ESP_OK||
        httpd_req_get_hdr_value_str(req,"X-Path",path,sizeof(path))!=ESP_OK)
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"storage/path fehlt");
    const String name=urlDecode(area),rel=urlDecode(path);
    if(!safeBrowserPath(rel)||rel=="/")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ungueltiger Pfad");
    const bool photoUpload=!strcmp(req->uri,"/photos/upload");
    if(photoUpload&&((name!="sd"&&name!="usb")||rel.length()>235||!rel.startsWith("/Bilder/")||!ImageCodec::supported(rel.c_str())||req->content_len<=0||req->content_len>ImageCodec::maxFileBytes))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fotos: SD/USB, Bilder-Ordner, Bildformat und 4-MiB-Grenze pruefen");
    if((name=="flash"&&protectedFlashPath(rel))||name=="app")
        return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"geschuetzter System-/App-Bereich");
    fs::FS* fs=nullptr;const char* label=nullptr;
    if(!browserMount(name,fs,label))return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Speicher nicht verfuegbar");
    // Both server frontends serialize complete file mutations. No UI/LVGL lock.
    std::unique_lock<std::mutex> lock(fileOperationsMutex,std::try_to_lock);
    if(!lock.owns_lock())return fileBusyResponse(req);
    if(download){
        File file=fs->open(rel,FILE_READ);
        if(!file||file.isDirectory())return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"Datei nicht gefunden");
        String disposition="attachment; filename*=UTF-8''";
        const String filename=fileBaseName(rel);for(size_t i=0;i<filename.length();++i){char encoded[4];snprintf(encoded,sizeof(encoded),"%%%02X",(unsigned char)filename[i]);disposition+=encoded;}
        httpd_resp_set_type(req,"application/octet-stream");httpd_resp_set_hdr(req,"Content-Disposition",disposition.c_str());
        httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(req,"Cache-Control","no-store");
        uint8_t bytes[4096];const uint32_t started=millis();
        while(file.available()){
            if(SecurityService::instance().tabletLocked()||uint32_t(millis()-started)>120000){file.close();return ESP_FAIL;}
            const size_t n=file.read(bytes,sizeof(bytes));if(!n||httpd_resp_send_chunk(req,(const char*)bytes,n)!=ESP_OK){file.close();return ESP_FAIL;}delay(0);
        }
        file.close();return httpd_resp_send_chunk(req,nullptr,0);
    }
    bool ok=false;
    if(!strcmp(req->uri,"/files/mkdir")){if(fs->exists(rel))return fileConflict(req,"Ziel existiert bereits");ok=fs->mkdir(rel);}
    else if(!strcmp(req->uri,"/files/rename")){
        if(httpd_req_get_hdr_value_str(req,"X-Destination",destination,sizeof(destination))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ziel fehlt");
        const String target=urlDecode(destination);
        if(!safeBrowserPath(target)||target=="/"||target.startsWith(rel+"/")||(name=="flash"&&protectedFlashPath(target)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"ungueltiges Ziel");
        if(fs->exists(target))return fileConflict(req,"Ziel existiert bereits");
        ok=fs->rename(rel,target);
    }else if(!strcmp(req->uri,"/files/upload")||photoUpload){
        if(req->content_len<0||req->content_len>32*1024*1024)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Dateilimit: 32 MiB");
        if(fs->exists(rel))return fileConflict(req,"Ziel existiert bereits");
        if(photoUpload){uint64_t free=0,total=0,used=0;bool known=false;
            if(name=="sd"){free=StorageService::instance().sdFreeBytes();known=true;}
            else if(UsbStorageService::instance().usage(total,used)){free=total>used?total-used:0;known=true;}
            if(known&&free<uint64_t(req->content_len)+65536){httpd_resp_set_status(req,"507 Insufficient Storage");return httpd_resp_sendstr(req,"Nicht genug freier Speicher; vorhandene Bilder bleiben erhalten.");}}
        // Unique sibling temp preserves all unrelated files and interrupted uploads.
        const String temp=rel+".upload-"+String(esp_random(),HEX);
        if(fs->exists(temp))return fileConflict(req,"Temporaeres Ziel existiert");
        File file=fs->open(temp,FILE_WRITE);if(!file)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Datei nicht beschreibbar");
        uint8_t bytes[4096];int remaining=req->content_len;const uint32_t started=millis();ok=true;
        while(remaining>0){
            if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()||uint32_t(millis()-started)>120000){ok=false;break;}
            const int n=httpd_req_recv(req,(char*)bytes,std::min<int>(remaining,sizeof(bytes)));
            if(n<=0||file.write(bytes,n)!=size_t(n)){ok=false;break;}remaining-=n;delay(0);
        }
        file.flush();file.close();
        if(ok){SecurityWriteGuard guard;ok=!SecurityService::instance().tabletLocked()&&SecurityService::instance().settingsAllowed()&&!fs->exists(rel)&&fs->rename(temp,rel);}
        if(!ok)fs->remove(temp);
    }else return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"unbekannte Dateioperation");
    if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Dateioperation fehlgeschlagen; Speicher pruefen");
    StorageService::instance().notifyMutation();if(photoUpload)PhotoLibraryService::instance().notifyMediaChanged();return httpd_resp_sendstr(req,"OK");
}

esp_err_t FileTransferService::filesDeleteHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");
    std::unique_lock<std::mutex> lock(fileOperationsMutex,std::try_to_lock);
    if(!lock.owns_lock())return fileBusyResponse(req);

    char rawArea[12]{};
    char rawPath[256]{};

    if (httpd_req_get_hdr_value_str(req, "X-Storage-Area",
                                    rawArea, sizeof(rawArea)) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "X-Path",
                                    rawPath, sizeof(rawPath)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "storage/path fehlt");
    }

    const String areaName = urlDecode(rawArea);
    const String rel = normalizeBrowserPath(urlDecode(rawPath));

    if (!safeBrowserPath(rel) || rel == "/")
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "ungueltiges Loeschziel");

    if (areaName=="app" || (areaName == "flash" && protectedFlashPath(rel)))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN,
                                   "kritischer Flash-Bereich ist geschuetzt");

    fs::FS* fs = nullptr;
    const char* areaLabel = nullptr;
    if (!browserMount(areaName, fs, areaLabel))
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                                   areaName == "sd" ? "SD-Karte nicht vorhanden"
                                                    : "Speicher nicht verfuegbar");

    const String absolute = rel;
    File target = fs->open(absolute, FILE_READ);
    if (!target)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                                   "Pfad nicht gefunden");

    const bool isDir = target.isDirectory();
    target.close();

    bool ok = false;
    bool contentsOnly = false;

    if (isDir) {
        ok = eraseTree(*fs, absolute);
        if (!ok && fs->exists(absolute) && directoryEmpty(*fs, absolute)) {
            ok = true;
            contentsOnly = true;
        }
    } else {
        ok = fs->remove(absolute);
    }

    if (!ok)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Loeschen fehlgeschlagen");

    StorageService::instance().notifyMutation();
    EventLog::instance().add(
        "WARN", "WEB_FILE_DELETE",
        String(areaLabel) + ":" + rel
    );

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (contentsOnly)
        return httpd_resp_sendstr(req,
            "Inhalt geloescht. LittleFS hat nur den leeren Ordner beibehalten.");
    return httpd_resp_sendstr(req, "Geloescht.");
}


bool FileTransferService::begin() {
    if (server_) return true;
    if (stopping_.load(std::memory_order_acquire)) return false;
    if (!initializeWebAdminPassword()) {
        Serial.println("[WEB ADMIN] Cannot persist credential; web admin disabled");
        return false;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 8088;
    cfg.core_id = performance::kIoCore<0 ? tskNO_AFFINITY : performance::kIoCore;
    cfg.max_open_sockets = 1;
    // OTA writes may pause TLS reads while flash sectors are programmed.
    // Defaults (5 s) are too short for slow clients and busy flash.
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.max_uri_handlers = 40;
    // The admin page needs more than the original 7 KiB, but 16 KiB plus
    // extra background workers consumed too much internal task-stack RAM.
    // 12 KiB keeps useful headroom while reducing pressure.
    cfg.stack_size = 12288;
    cfg.lru_purge_enable = true;

    auto& identity=WebTlsIdentity::instance();
    if(!identity.load())return false;
    httpd_ssl_config_t tls=HTTPD_SSL_CONFIG_DEFAULT();tls.httpd=cfg;tls.port_secure=8088;
    tls.servercert=identity.certificate();tls.servercert_len=identity.certificateSize();
    tls.prvtkey_pem=identity.key();tls.prvtkey_len=identity.keySize();tls.tls_handshake_timeout_ms=5000;
    if (httpd_ssl_start(&server_, &tls) != ESP_OK) {
        server_ = nullptr;
        return false;
    }

    httpd_uri_t root{};
    const httpd_uri_t loginRoutes[]={
        {"/login",HTTP_GET,WebLoginService::page,nullptr},
        {"/login.js",HTTP_GET,WebLoginService::script,nullptr},
        {"/api/auth/login",HTTP_POST,WebLoginService::login,nullptr},
        {"/api/auth/logout",HTTP_POST,WebLoginService::logout,nullptr},
        {"/api/csrf",HTTP_GET,WebLoginService::csrf,nullptr}};
    for(const auto& route:loginRoutes)if(httpd_register_uri_handler(server_,&route)!=ESP_OK){stop();return false;}
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = indexHandler;
    httpd_register_uri_handler(server_, &root);

    httpd_uri_t upload{};
    upload.uri = "/upload";
    upload.method = HTTP_POST;
    upload.handler = uploadHandler;
    httpd_register_uri_handler(server_, &upload);

    httpd_uri_t filesIndex{};
    filesIndex.uri = "/files";
    filesIndex.method = HTTP_GET;
    filesIndex.handler = filesIndexHandler;
    httpd_register_uri_handler(server_, &filesIndex);

    httpd_uri_t filesDelete{};
    filesDelete.uri = "/files/delete";
    filesDelete.method = HTTP_POST;
    filesDelete.handler = filesDeleteHandler;
    httpd_register_uri_handler(server_, &filesDelete);
    for(const auto& operation: {std::pair<const char*,httpd_method_t>{"/files/download",HTTP_GET},
        {"/files/mkdir",HTTP_POST},{"/files/rename",HTTP_POST},{"/files/upload",HTTP_POST}}){
        httpd_uri_t route{};route.uri=operation.first;route.method=operation.second;route.handler=filesOperationHandler;
        httpd_register_uri_handler(server_,&route);
    }

    httpd_uri_t appsIndex{};
    appsIndex.uri = "/apps";
    appsIndex.method = HTTP_GET;
    appsIndex.handler = appsIndexHandler;
    httpd_register_uri_handler(server_, &appsIndex);

    httpd_uri_t appsAdd{};
    appsAdd.uri = "/apps/add";
    appsAdd.method = HTTP_POST;
    appsAdd.handler = appsAddHandler;
    httpd_register_uri_handler(server_, &appsAdd);

    httpd_uri_t appsRemove{};
    appsRemove.uri = "/apps/remove";
    appsRemove.method = HTTP_POST;
    appsRemove.handler = appsRemoveHandler;
    httpd_register_uri_handler(server_, &appsRemove);

    httpd_uri_t appsToggle{};
    appsToggle.uri = "/apps/toggle";
    appsToggle.method = HTTP_POST;
    appsToggle.handler = appsToggleHandler;
    httpd_register_uri_handler(server_, &appsToggle);

    httpd_uri_t appsNative{};
    appsNative.uri = "/apps/native";
    appsNative.method = HTTP_POST;
    appsNative.handler = appsNativeHandler;
    httpd_register_uri_handler(server_, &appsNative);

    httpd_uri_t appsWebToggle{};
    appsWebToggle.uri = "/apps/web/toggle";
    appsWebToggle.method = HTTP_POST;
    appsWebToggle.handler = appsWebToggleHandler;
    httpd_register_uri_handler(server_, &appsWebToggle);

    httpd_uri_t appsLuaRemove{};
    appsLuaRemove.uri = "/apps/lua/remove";
    appsLuaRemove.method = HTTP_POST;
    appsLuaRemove.handler = appsLuaRemoveHandler;
    httpd_register_uri_handler(server_, &appsLuaRemove);

    httpd_uri_t appsExport{};
    appsExport.uri = "/apps/export";
    appsExport.method = HTTP_GET;
    appsExport.handler = appsExportHandler;
    httpd_register_uri_handler(server_, &appsExport);

    httpd_uri_t appsLuaFile{};
    appsLuaFile.uri = "/apps/lua/file";
    appsLuaFile.method = HTTP_POST;
    appsLuaFile.handler = appsLuaFileHandler;
    httpd_register_uri_handler(server_, &appsLuaFile);

    httpd_uri_t appsLuaCommit{};
    appsLuaCommit.uri = "/apps/lua/commit";
    appsLuaCommit.method = HTTP_POST;
    appsLuaCommit.handler = appsLuaCommitHandler;
    httpd_register_uri_handler(server_, &appsLuaCommit);

    httpd_uri_t appsLuaCancel{};
    appsLuaCancel.uri = "/apps/lua/cancel";
    appsLuaCancel.method = HTTP_POST;
    appsLuaCancel.handler = appsLuaCancelHandler;
    httpd_register_uri_handler(server_, &appsLuaCancel);

    httpd_uri_t appsUpload{};
    appsUpload.uri = "/apps/upload";
    appsUpload.method = HTTP_POST;
    appsUpload.handler = appsUploadHandler;
    httpd_register_uri_handler(server_, &appsUpload);

    httpd_uri_t firmwareInspect{};
    firmwareInspect.uri = "/firmware/inspect";
    firmwareInspect.method = HTTP_POST;
    firmwareInspect.handler = firmwareInspectHandler;
    httpd_register_uri_handler(server_, &firmwareInspect);

    httpd_uri_t firmwareUpload{};
    firmwareUpload.uri = "/firmware/upload";
    firmwareUpload.method = HTTP_POST;
    firmwareUpload.handler = firmwareUploadHandler;
    httpd_register_uri_handler(server_, &firmwareUpload);

    httpd_uri_t firmwareReboot{};
    firmwareReboot.uri = "/firmware/reboot";
    firmwareReboot.method = HTTP_POST;
    firmwareReboot.handler = firmwareRebootHandler;
    httpd_register_uri_handler(server_, &firmwareReboot);

    if(!registerOAuthWebRoutes(server_)){stop();return false;}
    return true;
}

bool FileTransferService::authorize(httpd_req_t* req){return webAdminAuthorized(req);}

bool FileTransferService::startTimed(uint32_t maxMinutes) {
    if (maxMinutes == 0 || maxMinutes > 30) maxMinutes = 30;

    // Do not extend an already active manual session. This makes "max. 30 min"
    // a hard upper bound from the first successful start.
    if (running() && timedSession_.load(std::memory_order_acquire))
        return true;

    if (!running() && !begin())
        return false;

    const uint32_t now = millis();
    expiresAtMs_.store(now + maxMinutes * 60UL * 1000UL,
                       std::memory_order_release);
    timedSession_.store(true, std::memory_order_release);

    EventLog::instance().add(
        "INFO", "SERVICE_WEB_START",
        "Port 8088 fuer maximal " + String(maxMinutes) + " Minuten gestartet");
    return true;
}

uint32_t FileTransferService::remainingSeconds() const {
    if (!running() || !timedSession_.load(std::memory_order_acquire))
        return 0;

    const uint32_t deadline = expiresAtMs_.load(std::memory_order_acquire);
    const int32_t remaining = static_cast<int32_t>(deadline - millis());
    if (remaining <= 0) return 0;
    return static_cast<uint32_t>((remaining + 999) / 1000);
}

void FileTransferService::loop() {
    if (!running() || !timedSession_.load(std::memory_order_acquire))
        return;

    const uint32_t deadline = expiresAtMs_.load(std::memory_order_acquire);
    if (static_cast<int32_t>(millis() - deadline) < 0)
        return;

    // httpd_stop can wait for clients. Never run it on the main/UI loop.
    const uint32_t now=millis();
    if(lastExpirationAttempt_&&uint32_t(now-lastExpirationAttempt_)<250)return;
    lastExpirationAttempt_=now;
    bool queued=false;
    if(!expirationQueued_.compare_exchange_strong(queued,true))return;
    if(!NetworkWorker::instance().submit(stopExpiredSession,this,"web-expire"))expirationQueued_=false;
}

void FileTransferService::stopExpiredSession(void* context){
    auto& self=*static_cast<FileTransferService*>(context);
    self.expirationQueued_=false;
    // A session may have been extended/replaced while this job was queued.
    if(!self.running()||!self.timedSession_||static_cast<int32_t>(millis()-self.expiresAtMs_.load())<0)return;

    // Claim expiration once so concurrent loop calls cannot stop twice.
    bool expected = true;
    if (!self.timedSession_.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel))
        return;

    self.expiresAtMs_.store(0, std::memory_order_release);
    EventLog::instance().add(
        "INFO", "SERVICE_WEB_TIMEOUT",
        "Port 8088 nach maximaler Laufzeit automatisch beendet");
    self.stop();
}

size_t FileTransferService::activeClients() const {
    if (!server_) return 0;
    int fds[3]{};
    size_t count = 3;
    if (httpd_get_client_list(server_, &count, fds) != ESP_OK) return 0;
    return count;
}

void FileTransferService::stop() {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    timedSession_.store(false, std::memory_order_release);
    expiresAtMs_.store(0, std::memory_order_release);

    // Publish "not running" before the potentially slower httpd_stop().
    // A concurrent second stop therefore cannot target the same handle.
    httpd_handle_t handle = server_;
    server_ = nullptr;
    if (handle) httpd_ssl_stop(handle);

    stopping_.store(false, std::memory_order_release);
}

String FileTransferService::urlDecode(const String& in) {
    String out;
    out.reserve(in.length());

    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < in.length()) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }

    return out;
}

esp_err_t FileTransferService::appsIndexHandler(httpd_req_t* req) {
    if (!webAdminAuthorized(req)) return ESP_OK;
    auto& registry = AppRegistry::instance();

    // AppRegistry is already refreshed by the UI / install paths. Rebuilding
    // it here can touch Lua metadata/storage concurrently with the display task.
    // The web page only consumes the current snapshot.
    String page;
    page.reserve(30000);

    page += R"HTML(
<!doctype html>
<html lang="de">
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta charset="utf-8">
<title>Home-Display</title>
<style>
*{box-sizing:border-box}
body{margin:0;background:#b9b9b9;color:#202020;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:1080px;margin:24px auto;padding:0 16px 36px}
h1{font-size:27px;margin:0 0 4px}.sub{color:#555;margin:0 0 18px}
.card{background:#d3d3d3;border:1px solid #a8a8a8;border-radius:7px;padding:17px;margin:12px 0}
.card h2{font-size:18px;margin:0 0 12px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:10px;border-bottom:1px solid #bababa;vertical-align:middle}
th{font-size:12px;color:#555}tr:last-child td{border-bottom:0}
.badge{display:inline-block;padding:3px 7px;border-radius:4px;background:#bebebe;font-size:12px;color:#444}
.on{background:#b3c6b3;color:#244724}.off{background:#c5c5c5;color:#5d5d5d}
.btn{display:inline-block;text-decoration:none;color:#222;background:#bfbfbf;border:1px solid #9f9f9f;border-radius:4px;padding:6px 9px;margin:2px 3px 2px 0;font-size:13px}
.danger{color:#662b2b;background:#cdb8b8}
label{display:block;font-size:13px;color:#555;margin:8px 0 5px}
input[type=text],input[type=url],input[type=file]{width:100%;padding:9px;border:1px solid #999;border-radius:4px;background:#e3e3e3}
button{margin-top:10px;border:1px solid #929292;background:#b9b9b9;color:#202020;border-radius:4px;padding:9px 13px;font-weight:600;cursor:pointer}
button:disabled{opacity:.45;cursor:not-allowed}
.drop{padding:22px;border:1px dashed #777;border-radius:5px;background:#c7c7c7;text-align:center;color:#444}
.drop.drag{background:#bdbdbd}
.hint{font-size:12px;color:#5b5b5b;margin-top:7px}
.status{font-size:13px;margin-top:9px;min-height:20px}
.meta{background:#c5c5c5;border:1px solid #aaa;border-radius:4px;padding:10px;margin-top:10px;font-size:13px;line-height:1.5;overflow-wrap:anywhere}
.progress{height:12px;background:#aaa;border:1px solid #929292;border-radius:3px;overflow:hidden;margin-top:10px}
.bar{height:100%;width:0;background:#666;transition:width .12s linear}
.ok{color:#245124}.err{color:#6a2020}
.firmware-card{grid-column:1/-1;scroll-margin-top:20px}.fw-steps{display:flex;flex-wrap:wrap;gap:8px;padding:0;list-style:none;counter-reset:step}.fw-steps li{padding:8px 12px;border:1px solid #999;border-radius:6px;font-size:13px}.fw-steps li:before{counter-increment:step;content:counter(step) '. ';font-weight:700}.fw-steps li.active{background:#c0d6e9;border-color:#497999}.firmware-card button{min-height:44px}.firmware-card .primary{background:#245e85;color:#fff}.firmware-card .meta{white-space:pre-wrap}.firmware-card .status{line-height:1.6}.firmware-card progress{width:100%;height:16px;accent-color:#245e85}
@media(max-width:760px){.grid{grid-template-columns:1fr}th:nth-child(2),td:nth-child(2){display:none}.wrap{margin-top:14px}}
</style>
</head>
<body>
<div class="wrap">
<h1>Home-Display</h1>
<p class="sub">Firmware und Apps verwalten.</p>
<p><a class="btn" href="/files">Dateien und Ordner verwalten</a></p>
<p><a class="btn" href="/accounts">E-Mail-Konten und Kalender verbinden</a></p>

<script>
const CSRF='__CSRF_TOKEN__';
const luaSession=[...crypto.getRandomValues(new Uint8Array(16))].map(x=>x.toString(16).padStart(2,'0')).join('');
async function postAction(url){
  let r=await fetch(url,{method:'POST',headers:{'X-HP-CSRF':CSRF},cache:'no-store'});
  let t=await r.text();
  if(!r.ok){alert(t||('HTTP '+r.status));return}
  location.reload();
}
function uiP(bar,status,v,t,c=''){
  bar.style.width=Math.max(0,Math.min(100,v))+'%';
  status.className='status '+c;
  status.textContent=t;
}
function bindDrop(drop,input,cb){
  drop.ondragover=e=>{e.preventDefault();drop.classList.add('drag')};
  drop.ondragleave=()=>drop.classList.remove('drag');
  drop.ondrop=e=>{
    e.preventDefault();
    drop.classList.remove('drag');
    if(e.dataTransfer.files.length) cb(e.dataTransfer.files[0]);
  };
  input.onchange=()=>cb(input.files[0]);
}
function xhrUpload(url,data,headers,onProgress){
  return new Promise((resolve,reject)=>{
    let x=new XMLHttpRequest();
    x.open('POST',url);
    if(url==='/firmware/upload'){
      x.timeout=660000;
      x.ontimeout=()=>reject(new Error('Update-Zeitlimit erreicht. Geraetestatus pruefen; nicht automatisch erneut senden.'));
    }
    x.setRequestHeader('X-HP-CSRF',CSRF);
    if(url.startsWith('/apps/lua/'))x.setRequestHeader('X-Install-Session',luaSession);
    Object.entries(headers||{}).forEach(([k,v])=>x.setRequestHeader(k,v));
    x.upload.onprogress=e=>{
      if(e.lengthComputable && onProgress) onProgress(e.loaded/e.total);
    };
    x.onload=()=>x.status>=200&&x.status<300
      ? resolve(x.responseText)
      : reject(new Error(x.responseText||('HTTP '+x.status)));
    x.onerror=()=>reject(new Error('Netzwerkfehler'));
    x.send(data);
  });
}
</script>
)HTML";

    page.replace("__CSRF_TOKEN__", htmlEscape(WebCsrfService::instance().token()));
    page += "<script>const installedLua={";
    bool firstLuaMeta = true;
    for (size_t i = 0; i < registry.count(); ++i) {
        const auto* e = registry.at(i);
        if (!e || e->kind != AppKind::Lua || !e->userInstallable) continue;
        if (!firstLuaMeta) page += ",";
        firstLuaMeta = false;
        page += "'" + e->id + "':'" + e->version + "'";
    }
    page += "};</script>";

    // The two upload cards are intentionally emitted as an explicit fixed grid.
    // Shared upload/progress helpers above are used by both cards.
    page += R"HTML(
<div class="grid">
  <div class="card firmware-card" id="firmware">
    <h2>Firmware aktualisieren</h2>
    <ol class="fw-steps" aria-label="Update-Ablauf"><li id="fwStep1" class="active">Datei prüfen</li><li id="fwStep2">Übertragen und verifizieren</li><li id="fwStep3">Neu starten und prüfen</li></ol>
)HTML";
    page += "<div class='hint'>Installiert: " + String(build::kVersion) +
            " - OTA: " +
            String(OtaService::instance().pendingValidation()
                ? "Health-Check laeuft"
                : "bereit / bestaetigt") +
            "</div>";
    page += R"HTML(
    <div id="fwDrop" class="drop">Firmware .bin hier ablegen oder auswählen</div>
    <input type="file" id="fwFile" accept=".bin,application/octet-stream">
    <div id="fwMeta" class="meta" style="display:none"></div>
    <label id="fwConfirmLabel" style="display:none">
      <input type="checkbox" id="fwConfirm"> Gleiche oder ältere Version trotzdem installieren
    </label>
    <button id="fwUpload" class="primary" disabled>Update übertragen</button>
    <button id="fwReboot" disabled>Update jetzt starten</button>
    <a class="btn" href="/">Zur Übersicht</a>
    <div class="progress"><div id="fwBar" class="bar"></div></div>
    <div id="fwStatus" class="status" role="status" aria-live="polite">Noch keine Datei ausgewählt.</div>
    <p class="hint">Nur <b>firmware.bin</b> verwenden, kein Factory-/Komplettabbild. Apps und persönliche Daten bleiben erhalten. Während des Updates die Stromversorgung nicht trennen. Ein Neustart erfolgt erst nach deiner Bestätigung.</p>
  </div>

  <div class="card">
    <h2>Lua-App installieren / aktualisieren</h2>
    <div id="luaDrop" class="drop">ZIP mit app.json + main.lua (optional settings.lua + web_settings.json) hier ablegen</div>
    <input type="file" id="luaFile" accept=".zip,application/zip">
    <div id="luaMeta" class="meta" style="display:none"></div>
    <label id="luaDownLabel" style="display:none">
      <input type="checkbox" id="luaDown"> Downgrade ausdrücklich erlauben
    </label>
    <button id="luaUpload" disabled>Lua-App installieren</button>
    <div class="progress"><div id="luaBar" class="bar"></div></div>
    <div id="luaStatus" class="status">Noch keine Datei ausgewählt.</div>
    <p class="hint">Lua-App Manifest-Schema 3. Die bestehende App bleibt bis zum erfolgreichen Commit erhalten.</p>
  </div>
</div>

<script>
const ff=document.getElementById('fwFile'),
      fd=document.getElementById('fwDrop'),
      fu=document.getElementById('fwUpload'),
      fr=document.getElementById('fwReboot'),
      fs=document.getElementById('fwStatus'),
      fb=document.getElementById('fwBar'),
      fm=document.getElementById('fwMeta'),
      fc=document.getElementById('fwConfirm'),
      fcl=document.getElementById('fwConfirmLabel');
let firmware=null,fwInfo=null,fwHash='',fwSequence=0,fwBusy=false,fwReady=false;
function fwStep(step){for(let i=1;i<=3;i++)document.getElementById('fwStep'+i).classList.toggle('active',i===step)}
function fwError(error){return ({wrong_product:'Die Datei gehört nicht zum Home-Display.',wrong_chip:'Die Datei ist nicht für diesen Prozessor geeignet.',wrong_chip_revision:'Falsche Hardware-Revision. Den zum Gerät passenden Legacy- oder v3.x-Build wählen.',invalid_esp_image:'Kein gültiges Firmware-Update. Bitte firmware.bin auswählen.',invalid_app_description:'Kein gültiges App-Abbild. Factory-/Bootloader-Dateien sind keine OTA-Updates.',firmware_header_too_small:'Die Firmware-Datei ist unvollständig.'})[error]||error}
window.addEventListener('beforeunload',e=>{if(fwBusy){e.preventDefault();e.returnValue=''}});
async function fwFetch(url,options={}){
  const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),15000);
  try{return await fetch(url,{credentials:'same-origin',cache:'no-store',...options,signal:controller.signal})}
  finally{clearTimeout(timer)}
}

async function sha256hex(blob){
  let b=await blob.arrayBuffer();
  let h=await crypto.subtle.digest('SHA-256',b);
  return [...new Uint8Array(h)].map(x=>x.toString(16).padStart(2,'0')).join('');
}
async function fwInspect(file){
  if(fwBusy)return;
  const sequence=++fwSequence;
  ff.disabled=false;fc.disabled=false;
  firmware=null;fwInfo=null;fwHash='';
  fwReady=false;fwStep(1);
  fu.disabled=true;fr.disabled=true;fm.style.display='none';
  fcl.style.display='none';fc.checked=false;
  if(!file)return;
  try{
    if(!/\.bin$/i.test(file.name)||file.size<1024)throw new Error('Bitte eine gültige firmware.bin auswählen');
    if(/(?:factory|merged|bootloader|partition|boot_app0)/i.test(file.name))throw new Error('Kein Factory-, Bootloader- oder Partitionsabbild verwenden. Bitte firmware.bin wählen.');
    if(!globalThis.crypto?.subtle)throw new Error('Für die sichere SHA-256-Prüfung bitte diese Seite über HTTPS öffnen.');
    uiP(fb,fs,5,'Image-Header wird geprüft ...');
    let r=await fwFetch('/firmware/inspect',{method:'POST',headers:{'X-HP-CSRF':CSRF},body:file.slice(0,1024)});
    if(r.status===401||r.status===403)throw new Error('Anmeldung oder Freigabe fehlt. Bitte erneut anmelden.');
    let j=await r.json();
    if(sequence!==fwSequence)return;
    if(!r.ok||!j.ok)throw new Error(fwError(j.error)||('HTTP '+r.status));
    if(j.pendingValidation)throw new Error('Der Boot-Health-Check läuft noch. Bitte später erneut prüfen.');
    if(!j.maxBytes||file.size>j.maxBytes)throw new Error('Firmware passt nicht in den OTA-Slot. Kein vollständiges Flash-Abbild verwenden.');
    uiP(fb,fs,12,'SHA-256 wird lokal berechnet ...');
    const hash=await sha256hex(file);
    if(sequence!==fwSequence)return;
    fwHash=hash;
    firmware=file;fwInfo=j;
    let warning=j.relation==='same'?'Gleiche Version':
                (j.relation==='older'?'Ältere Version / Downgrade':'Neue Version');
    if(j.relation==='same'||j.relation==='older')fcl.style.display='block';
    fm.textContent=warning+'\nDatei: '+file.name+'\nProdukt: '+j.project+
      '\nVersion: '+j.version+' · installiert: '+j.runningVersion+
      '\nGröße: '+Math.round(file.size/1024)+' KiB von '+Math.round(j.maxBytes/1024)+
      ' KiB\nSHA-256: '+fwHash;
    fm.style.display='block';
    fu.disabled=false;
    uiP(fb,fs,18,'Prüfung abgeschlossen. Noch nichts hochgeladen.','ok');
  }catch(e){
    if(sequence!==fwSequence)return;
    firmware=null;
    ff.value='';
    fu.disabled=true;
    uiP(fb,fs,0,'Fehler: '+e.message,'err');
  }
}
bindDrop(fd,ff,fwInspect);

fu.onclick=async()=>{
  if(fwBusy||fwReady||!firmware||!fwInfo)return;
  if((fwInfo.relation==='same'||fwInfo.relation==='older')&&!fc.checked){
    uiP(fb,fs,18,'Gleiche/ältere Version muss ausdrücklich bestätigt werden.','err');
    return;
  }
  if(!window.confirm('Firmware '+fwInfo.version+' übertragen? Apps und Daten werden nicht gelöscht. Stromversorgung angeschlossen lassen.'))return;
  fwBusy=true;fwStep(2);
  fu.disabled=true;ff.disabled=true;fc.disabled=true;fr.disabled=true;
  try{
    let result=await xhrUpload('/firmware/upload',firmware,{
      'X-Filename':encodeURIComponent(firmware.name),
      'X-SHA256':fwHash,
      'X-Allow-Same':fc.checked?'1':'0',
      'X-Allow-Downgrade':fc.checked?'1':'0'
    },p=>uiP(fb,fs,20+p*60,p>=1?'Uebertragung abgeschlossen; Firmware wird auf dem Display geprueft ...':'Firmware wird übertragen ...'));
    let reply={};try{reply=JSON.parse(result)}catch(e){throw new Error('Ungueltige Update-Bestaetigung; Geraetestatus pruefen.')}
    if(reply.ok!==true||!reply.slot||reply.sha256!==fwHash)throw new Error('Update nicht bestaetigt oder SHA-256 Rueckmeldung stimmt nicht');
    uiP(fb,fs,100,'Firmware geprüft und im OTA-Slot bereit.','ok');
    fwReady=true;fwStep(3);
    fr.disabled=false;
  }catch(e){
    uiP(fb,fs,0,'Update nicht bestätigt: '+e.message+' Nicht automatisch erneut senden. Geräteübersicht bzw. Diagnose prüfen und die Datei bei Bedarf neu auswählen.','err');
    firmware=null;fwInfo=null;fwHash='';
    ff.value='';
  }
  fwBusy=false;ff.disabled=fwReady;fc.disabled=fwReady;
};
fr.onclick=async()=>{
  if(fwBusy||!fwReady)return;
  if(!window.confirm('Jetzt mit der geprüften Firmware neu starten? Die Verbindung wird kurz unterbrochen.'))return;
  fwBusy=true;
  fr.disabled=true;
  uiP(fb,fs,100,'Neustart – anschließend läuft der Health-Check ...','ok');
  try{
    const response=await fwFetch('/firmware/reboot',{method:'POST',headers:{'X-HP-CSRF':CSRF}});
    if(!response.ok)throw new Error('Neustart abgelehnt (HTTP '+response.status+').');
    uiP(fb,fs,100,'Neustart angefordert. Nach etwa 15 Sekunden die Übersicht öffnen und den Gerätestatus prüfen.','ok');
    setTimeout(()=>{fwBusy=false;location.href='/'},15000);
  }catch(e){
    fwBusy=false;fr.disabled=false;
    uiP(fb,fs,100,'Neustart nicht bestätigt: '+e.message+' Geräteübersicht prüfen, bevor du erneut startest.','err');
  }
};

const lf=document.getElementById('luaFile'),
      ld=document.getElementById('luaDrop'),
      lu=document.getElementById('luaUpload'),
      ls=document.getElementById('luaStatus'),
      lb=document.getElementById('luaBar'),
      lm=document.getElementById('luaMeta'),
      lcheck=document.getElementById('luaDown'),
      lcheckLabel=document.getElementById('luaDownLabel');
let prepared=null;

function u16(v,o){return v.getUint16(o,true)}
function u32(v,o){return v.getUint32(o,true)}
async function inflateRaw(b){
  if(!('DecompressionStream'in window))throw new Error('Browser kann ZIP nicht entpacken');
  let d=new DecompressionStream('deflate-raw');
  return new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(d)).arrayBuffer());
}
async function readZip(f){
  let b=await f.arrayBuffer(),v=new DataView(b),u=new Uint8Array(b),e=-1;
  for(let i=b.byteLength-22;i>=Math.max(0,b.byteLength-65557);--i){
    if(u32(v,i)===0x06054b50){e=i;break}
  }
  if(e<0)throw new Error('Ungültige ZIP-Datei');
  let n=u16(v,e+10),o=u32(v,e+16),out={};
  for(let k=0;k<n;k++){
    if(u32(v,o)!==0x02014b50)throw new Error('ZIP-Verzeichnis ungültig');
    let m=u16(v,o+10),cs=u32(v,o+20),nl=u16(v,o+28),xl=u16(v,o+30),cl=u16(v,o+32),
        lo=u32(v,o+42),name=new TextDecoder().decode(u.slice(o+46,o+46+nl)).replace(/\\/g,'/'),
        ln=u16(v,lo+26),lx=u16(v,lo+28),st=lo+30+ln+lx,data=u.slice(st,st+cs),
        plain=m===0?data:(m===8?await inflateRaw(data):null);
    if(!plain)throw new Error('ZIP-Kompression '+m+' nicht unterstützt');
    let base=name.split('/').pop().toLowerCase();
    if(/^assets\/[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(png|jpe?g|bmp|gif|webp|tga)$/.test(name)){
      if(name.length>63||name.includes('..')||out[name]||plain.byteLength>131072)throw new Error('Ungültiges PNG-Asset');
      const assets=Object.keys(out).filter(x=>x.startsWith('assets/'));
      if(assets.length>=16||assets.reduce((n,x)=>n+out[x].byteLength,0)+plain.byteLength>524288)throw new Error('Zu viele/große Bilddateien');
      out[name]=plain;
    }else if(base==='main.lua'||base==='settings.lua'||base==='web_settings.json'||base==='app.json'||base==='manifest.json'){
      if(name!==base||out[base])throw new Error('Paketdateien müssen eindeutig im ZIP-Hauptverzeichnis liegen');
      out[base]=plain;
    }
    o+=46+nl+xl+cl;
  }
  return out;
}
function vc(a,b){
  let A=(a||'0').match(/\d+/g)||['0'],B=(b||'0').match(/\d+/g)||['0'];
  for(let i=0;i<4;i++){let x=+(A[i]||0),y=+(B[i]||0);if(x<y)return-1;if(x>y)return 1}
  return 0;
}
function validId(x){return /^[a-z0-9_-]{1,31}$/.test(x)}
function checkManifest(o){
  if(o.schema!==3)throw new Error('app.json: Schema 3 erforderlich. Alte App-Pakete bleiben gespeichert, werden aber nicht ausgefuehrt.');
  if(o.schema===3){
    const allowed=new Set(['network.local','network.internet','storage','secrets','discovery','mqtt','websocket','home_assistant','energy','media','storage.read','storage.writeAppData','storage.nasRead','system.wallpaper','power.idleIntegration','calendar.read','calendar.write']);
    if(!Array.isArray(o.permissions)||o.permissions.length>16||o.permissions.some(p=>!allowed.has(p))||new Set(o.permissions).size!==o.permissions.length||(o.permissions.includes('storage')&&o.permissions.includes('storage.writeAppData')))throw new Error('app.json: permissions ungueltig');
    if(!o.settings&&o.settingsEntry!==undefined)throw new Error('app.json: settingsEntry ohne settings');
  }
  for(const k of ['id','name','version','type','entry'])
    if(typeof o[k]!=='string'||!o[k])throw new Error('app.json: '+k+' fehlt');
  if(!validId(o.id))throw new Error('app.json: ungültige id');
  if(o.name.length>48)throw new Error('app.json: name zu lang');
  if(o.type!=='lua')throw new Error('app.json: type muss lua sein');
  if(o.entry!=='main.lua')throw new Error('app.json: entry muss main.lua sein');
  if(typeof o.settings!=='boolean')throw new Error('app.json: settings muss true/false sein');
  if(o.settingsEntry!==undefined&&o.settingsEntry!=='settings.lua')throw new Error('app.json: settingsEntry muss settings.lua sein');
  if(o.settings&&o.settingsEntry!=='settings.lua')throw new Error('app.json: settingsEntry muss settings.lua sein');
}
function validateWebSettingsBytes(bytes){
  if(!bytes)return;
  if(bytes.byteLength===0||bytes.byteLength>16384)throw new Error('web_settings.json muss 1 Byte bis 16 KiB groß sein');
  let w;try{w=JSON.parse(new TextDecoder().decode(bytes))}catch(e){throw new Error('web_settings.json ist kein gültiges JSON: '+e.message)}
  if(w.schema!==1)throw new Error('web_settings.json: schema muss 1 sein');
  if(!Array.isArray(w.tabs)||w.tabs.length<1||w.tabs.length>12)throw new Error('web_settings.json: 1 bis 12 Tabs erforderlich');
  const idOk=x=>typeof x==='string'&&/^[A-Za-z0-9_-]{1,48}$/.test(x), textOk=(x,n)=>typeof x==='string'&&x.length>0&&x.length<=n;
  const simple=new Set(['text','password','bool','integer','number','select','host','url']),tabs=new Set(),keys=new Set();let top=0,total=0;
  function opts(f){if(!Array.isArray(f.options)||f.options.length<1||f.options.length>24)throw new Error('web_settings.json: Auswahlfeld benötigt 1 bis 24 Optionen');let seen=new Set();for(const o of f.options){let v,l;if(typeof o==='string'){v=o;l=o}else if(o&&typeof o==='object'&&typeof o.value==='string'&&typeof o.label==='string'){v=o.value;l=o.label}else throw new Error('web_settings.json: Auswahloption ist ungültig');if(!textOk(v,96)||!textOk(l,96))throw new Error('web_settings.json: Auswahloption ist zu lang');if(seen.has(v))throw new Error('web_settings.json: Auswahlwert ist doppelt');seen.add(v)}}
  function sf(f,nested){if(!f||!idOk(f.key)||!textOk(f.label,72)||!simple.has(f.type))throw new Error('web_settings.json: Feld-Key, Beschriftung oder Typ ist ungültig');if(nested&&f.type==='password')throw new Error('web_settings.json: Passwort im Repeater nicht erlaubt');if(f.help!==undefined&&(!textOk(f.help,220)))throw new Error('web_settings.json: Hilfetext ist ungültig');if(f.placeholder!==undefined&&(typeof f.placeholder!=='string'||f.placeholder.length>120))throw new Error('web_settings.json: Platzhalter ist zu lang');if(f.maxLength!==undefined&&(!Number.isInteger(f.maxLength)||f.maxLength<1||f.maxLength>2048))throw new Error('web_settings.json: maxLength muss 1 bis 2048 sein');if(f.type==='select')opts(f)}
  for(const t of w.tabs){if(!t||!idOk(t.id)||!textOk(t.label,48)||!Array.isArray(t.fields))throw new Error('web_settings.json: Tab ist ungültig');if(tabs.has(t.id))throw new Error('web_settings.json: Tab-ID ist doppelt');tabs.add(t.id);if(top+t.fields.length>48)throw new Error('web_settings.json: maximal 48 Hauptfelder erlaubt');for(const f of t.fields){top++;total++;if(total>96)throw new Error('web_settings.json: maximal 96 Felder inklusive Repeater');if(!f||!idOk(f.key))throw new Error('web_settings.json: Feld-Key ist ungültig');if(keys.has(f.key))throw new Error('web_settings.json: Einstellungs-Key '+f.key+' ist doppelt');keys.add(f.key);if(f.type==='repeater'){if(!textOk(f.label,72)||!Array.isArray(f.fields)||f.fields.length<1||f.fields.length>16)throw new Error('web_settings.json: Repeater ist ungültig');let mi=f.maxItems===undefined?8:f.maxItems;if(!Number.isInteger(mi)||mi<1||mi>16)throw new Error('web_settings.json: Repeater maxItems muss 1 bis 16 sein');let ck=new Set();for(const c of f.fields){sf(c,true);if(c.key===f.key)throw new Error('web_settings.json: Repeater-Key kollidiert mit Unterfeld');if(ck.has(c.key))throw new Error('web_settings.json: Repeater-Unterfeld '+c.key+' ist doppelt');ck.add(c.key);if(++total>96)throw new Error('web_settings.json: maximal 96 Felder inklusive Repeater')}}else sf(f,false)}}
}
async function luaChoose(file){
  prepared=null;lu.disabled=true;lm.style.display='none';
  lcheckLabel.style.display='none';lcheck.checked=false;
  if(!file)return;
  try{
    uiP(lb,ls,5,'ZIP wird geprüft ...');
    let z=await readZip(file);
    if(!z['main.lua'])throw new Error('main.lua fehlt');
    if(z['web_settings.json'])validateWebSettingsBytes(z['web_settings.json']);
    let m=z['app.json']||z['manifest.json'];
    if(!m)throw new Error('app.json fehlt');
    let obj=JSON.parse(new TextDecoder().decode(m));
    checkManifest(obj);
    if(obj.settings&&!z['settings.lua'])throw new Error('settings_file_missing: settings.lua fehlt');
    if(obj.schema===3&&!obj.settings&&z['settings.lua'])throw new Error('settings_file_unexpected');
    let current=installedLua[obj.id],mode='Neuinstallation';
    if(current!==undefined){
      let c=vc(obj.version,current);
      mode=c>0?'Update':(c===0?'Neuinstallation gleicher Version':'Downgrade');
      if(c<0)lcheckLabel.style.display='block';
    }
    prepared={
      id:obj.id,name:obj.name,version:obj.version,current:current,
      main:z['main.lua'],manifest:m,
      settings:z['settings.lua'],
      webSettings:z['web_settings.json'],
      assets:Object.keys(z).filter(x=>x.startsWith('assets/')).map(x=>({name:x,data:z[x]})),
      manifestName:z['app.json']?'app.json':'manifest.json',mode:mode
    };
    lm.innerHTML='<b>'+mode+'</b><br>App: '+obj.name+'<br>ID: '+obj.id+
      '<br>Neue Version: '+obj.version+
      (current!==undefined?'<br>Installiert: '+(current||'(ohne Version)'):'')+
      '<br>main.lua: '+prepared.main.byteLength+' Byte';
    lm.style.display='block';lu.disabled=false;
    uiP(lb,ls,15,'Paket geprüft. Noch nichts verändert.','ok');
  }catch(e){
    uiP(lb,ls,0,'Fehler: '+e.message,'err');
  }
}
bindDrop(ld,lf,luaChoose);

lu.onclick=async()=>{
  if(!prepared)return;
  if(prepared.mode==='Downgrade'&&!lcheck.checked){
    uiP(lb,ls,15,'Downgrade muss bestätigt werden.','err');return;
  }
  lu.disabled=true;lf.disabled=true;
  try{
    await xhrUpload('/apps/lua/cancel',new Uint8Array(),{'X-App-Id':prepared.id},()=>{});
    await xhrUpload('/apps/lua/file',prepared.manifest,
      {'X-App-Id':prepared.id,'X-App-File':prepared.manifestName},
      p=>uiP(lb,ls,20+p*20,'Manifest wird ins Staging übertragen ...'));
    await xhrUpload('/apps/lua/file',prepared.main,
      {'X-App-Id':prepared.id,'X-App-File':'main.lua'},
      p=>uiP(lb,ls,40+p*35,'main.lua wird ins Staging übertragen ...'));
    if(prepared.settings)await xhrUpload('/apps/lua/file',prepared.settings,
      {'X-App-Id':prepared.id,'X-App-File':'settings.lua'},
      p=>uiP(lb,ls,65+p*8,'settings.lua wird ins Staging übertragen ...'));
    if(prepared.webSettings)await xhrUpload('/apps/lua/file',prepared.webSettings,
      {'X-App-Id':prepared.id,'X-App-File':'web_settings.json'},
      p=>uiP(lb,ls,73+p*5,'web_settings.json wird ins Staging übertragen ...'));
    for(const asset of prepared.assets)await xhrUpload('/apps/lua/file',asset.data,
      {'X-App-Id':prepared.id,'X-App-File':asset.name},()=>uiP(lb,ls,78,'Bilddatei wird übertragen ...'));
    let result=await xhrUpload('/apps/lua/commit',new Uint8Array(),
      {'X-App-Id':prepared.id,'X-Allow-Downgrade':lcheck.checked?'1':'0'},
      p=>uiP(lb,ls,80+p*15,'Paket wird geprüft und aktiviert ...'));
    uiP(lb,ls,100,result||'Lua-App erfolgreich installiert.','ok');
    setTimeout(()=>location.reload(),1000);
  }catch(e){
    const tail=prepared&&prepared.current!==undefined
      ? ' – bisherige App bleibt erhalten.'
      : ' – Installation abgebrochen; es wurde keine App installiert.';
    uiP(lb,ls,0,'Fehler: '+e.message+tail,'err');
    lu.disabled=false;
  }
  lf.disabled=false;
};
</script>
)HTML";

    page += "<div class='card'><h2>Installierte Apps</h2><table><thead><tr><th>App</th><th>Typ</th><th>Status</th><th>Aktionen</th></tr></thead><tbody>";
    for (size_t i = 0; i < registry.count(); ++i) {
        const auto* e = registry.at(i);
        if (!e || !e->userInstallable) continue;
        const bool enabled = e->state == AppRegistryState::Enabled;
        page += "<tr><td><strong>" + htmlEscape(e->name) + "</strong>";
        if (e->version.length()) page += "<div class='hint'>Version " + htmlEscape(e->version) + "</div>";
        if (e->detail.length()) page += "<div class='hint'>" + htmlEscape(e->detail) + "</div>";
        page += "</td><td><span class='badge'>" + String(AppRegistry::kindName(e->kind)) + "</span></td>";
        page += "<td><span class='badge " + String(enabled?"on":"off") + "'>" + String(AppRegistry::stateName(e->state)) + "</span></td><td>";
        page += actionButton("/apps/toggle?id=" + e->id + "&on=" + String(enabled?"0":"1"), enabled?"Deaktivieren":"Aktivieren");
        if (e->hasSettings) page += "<span class='badge'>Einstellungen am Display</span>";
        if (e->removable && luaPackageExists(e->id.c_str())) {
            if (e->kind == AppKind::Lua) {
                page += "<a class='btn' href='/apps/export?id=" +
                        htmlEscape(e->id) + "'>Daten sichern</a>";
                page += actionButton(
                    String("/apps/lua/remove?id=") + e->id + "&purge=0",
                    "App loeschen", true);
                page += actionButton(
                    String("/apps/lua/remove?id=") + e->id + "&purge=1",
                    "Komplett loeschen", true);
            } else {
                page += actionButton(
                    String("/apps/remove?id=") + e->id,
                    "Loeschen", true);
            }
        }
        page += "</td></tr>";
    }
    page += "</tbody></table></div>";

    page +=
        "<div class='card'><h2>Web-App hinzufuegen</h2><form id='webAppForm' onsubmit='addWebApp(event)'>"
        "<label>Name</label><input type='text' name='name' maxlength='39' placeholder='z.B. IR Tracker'>"
        "<label>URL</label><input type='url' name='url' maxlength='159' placeholder='http://192.168.x.x/'>"
        "<button type='submit'>Web-App speichern</button></form><script>async function addWebApp(e){e.preventDefault();let f=e.target,b=new URLSearchParams(new FormData(f));let r=await fetch('/apps/add',{method:'POST',headers:{'X-HP-CSRF':CSRF,'Content-Type':'application/x-www-form-urlencoded'},body:b});let t=await r.text();if(!r.ok){alert(t||('HTTP '+r.status));return}location.reload()}</script><p class='hint'>Wird danach ebenfalls in der gemeinsamen AppRegistry gefuehrt.</p></div>"
        "<details class='card'><summary>Erweitert / apps.bin</summary><p class='hint'>Legacy-Import fuer Web-Apps.</p>"
        "<div id='au' class='drop'>apps.bin hier ablegen</div><input type='file' id='af' accept='.bin'>"
        "<script>function sendApp(f){xhrUpload('/apps/upload',f,{'X-Filename':encodeURIComponent(f.name)},()=>{}).then(()=>location.reload()).catch(e=>alert(e.message))}bindDrop(document.getElementById('au'),document.getElementById('af'),sendApp);</script></details>"
        "</div></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t FileTransferService::appsAddHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    if (req->content_len <= 0 || req->content_len > 512)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");

    char buf[513]{};
    const int received = httpd_req_recv(
        req,
        buf,
        min<int>(req->content_len, sizeof(buf) - 1)
    );
    if (received <= 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body");

    buf[received] = 0;

    String name, url;
    const String body(buf);
    int start = 0;

    while (start < (int)body.length()) {
        int amp = body.indexOf('&', start);
        if (amp < 0) amp = body.length();

        const String pair = body.substring(start, amp);
        const int eq = pair.indexOf('=');

        if (eq > 0) {
            const String key = pair.substring(0, eq);
            const String value = urlDecode(pair.substring(eq + 1));
            if (key == "name") name = value;
            else if (key == "url") url = value;
        }

        start = amp + 1;
    }

    String reason;
    if (!WebAppManager::instance().validateUrl(url.c_str(), &reason))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, reason.c_str());

    if (!AppRegistry::instance().addWeb(name.c_str(), url.c_str()))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "install failed");

    EventLog::instance().add("INFO", "APP_INSTALL", "Installed Web-App " + name);
    UiShell::instance().requestAppsRefresh();
    redirectApps(req);
    return ESP_OK;
}

esp_err_t FileTransferService::appsRemoveHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char query[96]{}, rawId[48]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "id", rawId, sizeof(rawId));
    String id = urlDecode(rawId);

    auto& registry = AppRegistry::instance();
    AppRegistryEntry entryCopy;
    if (!registry.lookup(id.c_str(), entryCopy) || !entryCopy.removable) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "app not removable");
    }

    if (entryCopy.kind == AppKind::Web) {
        registry.removeWeb(id.c_str());
        UiShell::instance().requestAppsRefresh();
        EventLog::instance().add("INFO", "APP_REMOVE", id);
        redirectApps(req); return ESP_OK;
    }

    if (entryCopy.kind == AppKind::Lua) {
        const auto result = LuaPackageService::instance().remove(id, false);
        if (!result.ok)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, result.error.c_str());
        redirectApps(req);
        return ESP_OK;
    }

    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unsupported app type");
}

esp_err_t FileTransferService::appsToggleHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char query[128]{}, rawId[48]{}, on[4]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", rawId, sizeof(rawId));
        httpd_query_key_value(query, "on", on, sizeof(on));
    }
    const String id=urlDecode(rawId); const bool enabled=atoi(on)!=0;
    if (!AppRegistry::instance().setEnabled(id.c_str(), enabled))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "could not change app state");
    UiShell::instance().requestAppsRefresh(); redirectApps(req); return ESP_OK;
}

esp_err_t FileTransferService::appsNativeHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char query[96]{};
    char id[40]{};
    char on[4]{};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", id, sizeof(id));
        httpd_query_key_value(query, "on", on, sizeof(on));
    }

    if (id[0]) {
        const bool enabled = atoi(on) != 0;
        AppManager::instance().setEnabled(id, enabled);
        UiShell::instance().requestAppsRefresh();
        EventLog::instance().add(
            "INFO",
            enabled ? "APP_ENABLE" : "APP_DISABLE",
            String(id)
        );
    }

    redirectApps(req);
    return ESP_OK;
}

esp_err_t FileTransferService::appsWebToggleHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char query[64]{};
    int index = -1;
    char on[4]{};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[16]{};
        if (httpd_query_key_value(query, "id", value, sizeof(value)) == ESP_OK)
            index = atoi(value);
        httpd_query_key_value(query, "on", on, sizeof(on));
    }

    if (index >= 0) {
        WebAppManager::instance().setEnabled((size_t)index, atoi(on) != 0);
        UiShell::instance().requestAppsRefresh();
    }

    redirectApps(req);
    return ESP_OK;
}

esp_err_t FileTransferService::appsExportHandler(httpd_req_t* req) {
    if (!webAdminAuthorized(req)) return ESP_OK;
    char query[96]{}, rawId[40]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
        httpd_query_key_value(query, "id", rawId, sizeof(rawId));
    const String id = urlDecode(rawId);
    if (!safeAppId(id))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid app id");
    return LuaAppDataCleanupService::instance().sendExport(req, id);
}

esp_err_t FileTransferService::appsLuaRemoveHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char query[128]{}, rawId[40]{}, rawPurge[4]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "id", rawId, sizeof(rawId));
        httpd_query_key_value(query, "purge", rawPurge, sizeof(rawPurge));
    }
    const String id = urlDecode(rawId);
    const bool purgeData = atoi(rawPurge) != 0;
    const auto result = LuaPackageService::instance().remove(id, purgeData);
    if (!result.ok) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        return httpd_resp_send(req, result.error.c_str(), result.error.length());
    }
    redirectApps(req);
    return ESP_OK;
}


esp_err_t FileTransferService::appsLuaFileHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    std::unique_lock<std::mutex> upload(luaUploadMutex,std::try_to_lock);
    if(!upload.owns_lock())return installerBusy(req);
    char appId[40]{};
    char fileName[64]{};

    if (httpd_req_get_hdr_value_str(req, "X-App-Id", appId, sizeof(appId)) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "X-App-File", fileName, sizeof(fileName)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing headers");
    }

    String rel;
    String error;
    char owner[65]{};if(!installOwner(req,owner)||!luaTransferLease.claim(appId,owner,millis()))return installerBusy(req);
    if (!LuaPackageInstaller::instance().stagePath(appId, fileName, rel, error)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error.c_str());
    }

    const String uploadedName = fileName;
    const size_t maxSize = uploadedName.startsWith("assets/") ? 131072 :
        (uploadedName == "main.lua" || uploadedName == "settings.lua") ? 196608 :
        uploadedName == "web_settings.json" ? 16384 : 8192;
    if (req->content_len <= 0 || (size_t)req->content_len > maxSize) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid size");
    }

    auto& storage = StorageService::instance();
    auto* fs = LuaPackageInstaller::instance().stageFs(appId);
    if (!fs) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Installationsziel nicht verfügbar oder Upload nicht vorbereitet; SD/AppFS und Staging prüfen");
    }
    String stageError;
    const String stageFolder = String("/installer_stage/") + appId;

    // resetStage() is the single authority for preparing the staging directory.
    // Do not run generic ensureParentDirs() on the final file path afterwards:
    // on AppFS/LittleFS this was the source of false "Staging-Verzeichnis nicht
    // anlegbar" errors even though /installer_stage/<id> was already writable.
    bool stageReady = false;
    if (fs->exists(stageFolder)) {
        File stageDir = fs->open(stageFolder, FILE_READ);
        stageReady = stageDir && stageDir.isDirectory();
        if (stageDir) stageDir.close();
    }

    if (!stageReady &&
        !LuaPackageInstaller::instance().resetStage(appId, stageError)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   stageError.c_str());
    }

    // Verify the parent once, directly on AppFS.
    File stageDir = fs->open(stageFolder, FILE_READ);
    const bool stageIsDir = stageDir && stageDir.isDirectory();
    if (stageDir) stageDir.close();
    if (!stageIsDir) {
        const String diagnostic =
            "Staging-Ordner fehlt oder ist kein Ordner: " + stageFolder;
        EventLog::instance().add("ERROR", "LUA_STAGE_PARENT", diagnostic);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   diagnostic.c_str());
    }

    const String target = rel;
    if(String(fileName).startsWith("assets/")&&!fs->exists(stageFolder+"/assets")&&!fs->mkdir(stageFolder+"/assets"))
        return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"asset_parent_mkdir");
    const String temp = target + ".part";
    fs->remove(temp);

    File out = fs->open(temp, FILE_WRITE);
    if (!out) {
        const String diagnostic =
            "Staging-Datei nicht beschreibbar: " + temp +
            " (AppFS voll oder Dateisystemfehler)";
        EventLog::instance().add("ERROR", "LUA_STAGE_OPEN", diagnostic);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   diagnostic.c_str());
    }

    int remaining = req->content_len;
    uint8_t buffer[2048];

    while (remaining > 0) {
        const int want = min<int>(remaining, sizeof(buffer));
        const int got = httpd_req_recv(req, (char*)buffer, want);
        if (got <= 0 || out.write(buffer, got) != (size_t)got) {
            out.close();
            fs->remove(temp);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "stage upload");
        }
        remaining -= got;
        delay(0);
    }

    out.flush();
    out.close();
    if (!commitUploadedFile(fs, temp, target)) {
        fs->remove(temp);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "stage commit");
    }

    EventLog::instance().add("INFO", "LUA_STAGE_FILE", String(appId) + "/" + fileName);
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t FileTransferService::appsLuaCommitHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    std::unique_lock<std::mutex> upload(luaUploadMutex,std::try_to_lock);
    if(!upload.owns_lock())return installerBusy(req);
    char appId[40]{};
    char allow[4]{};

    if (httpd_req_get_hdr_value_str(req, "X-App-Id", appId, sizeof(appId)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing app id");
    }
    httpd_req_get_hdr_value_str(req, "X-Allow-Downgrade", allow, sizeof(allow));
    char owner[65]{};if(!installOwner(req,owner)||!luaTransferLease.claim(appId,owner,millis()))return installerBusy(req);

    LuaInstallPlan plan;
    String error;
    if (!LuaPackageInstaller::instance().commitFromHttp(appId, atoi(allow) != 0, plan, error)) {
        luaTransferLease.release(appId,owner);
        EventLog::instance().add("WARN", "LUA_INSTALL_FAIL", String(appId) + " " + error);
        const String friendly = installerErrorText(error);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, friendly.c_str());
    }

    // AppRegistry and launcher refresh were already requested by the UI task.
    luaTransferLease.release(appId,owner);
    String result = String(LuaPackageInstaller::modeName(plan.mode)) + ": ";
    if (plan.currentVersion.length()) {
        result += plan.currentVersion + " -> ";
    }
    result += plan.manifest.version;

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, result.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t FileTransferService::appsLuaCancelHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    std::unique_lock<std::mutex> upload(luaUploadMutex,std::try_to_lock);
    if(!upload.owns_lock())return installerBusy(req);
    char appId[40]{};
    if (httpd_req_get_hdr_value_str(req, "X-App-Id", appId, sizeof(appId)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing app id");
    }

    String error;
    char owner[65]{},release[4]{};if(!installOwner(req,owner)||!luaTransferLease.claim(appId,owner,millis()))return installerBusy(req);
    httpd_req_get_hdr_value_str(req,"X-Install-Release",release,sizeof(release));
    if(!strcmp(release,"1")){LuaPackageInstaller::instance().cancel(appId);luaTransferLease.release(appId,owner);return httpd_resp_sendstr(req,"OK");}
    if (!LuaPackageInstaller::instance().resetStage(appId, error)) {
        luaTransferLease.release(appId,owner);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   error.c_str());
    }
    return httpd_resp_sendstr(req, "OK");
}


esp_err_t FileTransferService::firmwareInspectHandler(httpd_req_t* req) {
    if (!webAdminAuthorized(req)) return ESP_OK;
    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid_header_size\"}");
    }

    uint8_t buffer[4096];
    int total = 0;
    while (total < req->content_len) {
        const int got = httpd_req_recv(
            req,
            (char*)buffer + total,
            req->content_len - total
        );
        if (got <= 0) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"header_upload_failed\"}");
        }
        total += got;
    }

    FirmwareImageInfo info;
    if (!parseFirmwareInfo(buffer, total, info)) {
        String body = "{\"ok\":false,\"error\":\"" + info.error + "\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
    }

    const esp_app_desc_t* running = esp_app_get_description();
    const String runningVersion = running ? String(running->version) : String(build::kVersion);
    const int cmp = compareFirmwareVersions(info.version, runningVersion);
    const char* relation = cmp > 0 ? "newer" : (cmp == 0 ? "same" : "older");

    String body =
        "{\"ok\":true,\"project\":\"" + htmlEscape(info.project) +
        "\",\"version\":\"" + htmlEscape(info.version) +
        "\",\"runningVersion\":\"" + htmlEscape(runningVersion) +
        "\",\"chipId\":" + String(info.chipId) +
        ",\"relation\":\"" + relation + "\",\"maxBytes\":" +
        String(esp_ota_get_next_update_partition(nullptr) ? esp_ota_get_next_update_partition(nullptr)->size : 0) +
        ",\"pendingValidation\":" + (OtaService::instance().pendingValidation() ? "true" : "false") + "}";

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t FileTransferService::firmwareUploadHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    OtaWriteSession writer;
    if (!writer.acquired()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "OTA already in progress");
    }
    // Reject ambiguous framing. This endpoint accepts one bounded raw image.
    if (httpd_req_get_hdr_value_len(req, "Transfer-Encoding"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Length required; chunked OTA unsupported");
    const uint32_t uploadStarted = millis();
    uint32_t lastReceived = uploadStarted;
    // A 6+ MiB image can take several minutes over HTTPS. Keep a bounded
    // overall deadline, but do not reject a healthy slow transfer after 3 min.
    constexpr uint32_t uploadDeadlineMs = 600000;
    auto receive = [&](uint8_t* data, size_t wanted) -> int {
        while (millis() - uploadStarted < uploadDeadlineMs && millis() - lastReceived < 45000U) {
            const int got = httpd_req_recv(req, reinterpret_cast<char*>(data), wanted);
            if (got > 0) lastReceived = millis();
            if (got != HTTPD_SOCK_ERR_TIMEOUT) return got;
            vTaskDelay(1);
        }
        return HTTPD_SOCK_ERR_TIMEOUT;
    };
    if (req->content_len <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty firmware");
    }

    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        EventLog::instance().add("ERROR", "WEB_OTA", "No OTA target partition");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition");
    }

    if ((size_t)req->content_len > target->size) {
        EventLog::instance().add("ERROR", "WEB_OTA", "Firmware too large");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware too large");
    }

    char expectedShaText[65]{};
    uint8_t expectedSha[32]{};
    if (httpd_req_get_hdr_value_str(req, "X-SHA256", expectedShaText, sizeof(expectedShaText)) != ESP_OK ||
        !parseSha256Header(expectedShaText, expectedSha)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing or invalid SHA-256");
    }

    char allowSame[4]{};
    char allowDowngrade[4]{};
    httpd_req_get_hdr_value_str(req, "X-Allow-Same", allowSame, sizeof(allowSame));
    httpd_req_get_hdr_value_str(req, "X-Allow-Downgrade", allowDowngrade, sizeof(allowDowngrade));

    // Read enough data to validate metadata before erasing/writing the OTA slot.
    uint8_t firstChunk[1024];
    const int firstWant = min<int>(req->content_len, sizeof(firstChunk));
    int firstGot = 0;
    while (firstGot < firstWant) {
        const int got = receive(firstChunk + firstGot, firstWant - firstGot);
        if (got <= 0) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload interrupted or timed out");
        }
        firstGot += got;
    }

    FirmwareImageInfo info;
    if (!parseFirmwareInfo(firstChunk, firstGot, info)) {
        EventLog::instance().add("WARN", "WEB_OTA_REJECT", info.error);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, info.error.c_str());
    }

    const esp_app_desc_t* running = esp_app_get_description();
    const String runningVersion = running ? String(running->version) : String(build::kVersion);
    const int versionCmp = compareFirmwareVersions(info.version, runningVersion);

    if (versionCmp == 0 && atoi(allowSame) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "same version requires confirmation");
    }
    if (versionCmp < 0 && atoi(allowDowngrade) == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "downgrade requires confirmation");
    }

    mbedtls_sha256_context incomingHash;
    mbedtls_sha256_init(&incomingHash);
    if (mbedtls_sha256_starts(&incomingHash, 0) != 0 ||
        mbedtls_sha256_update(&incomingHash, firstChunk, firstGot) != 0) {
        mbedtls_sha256_free(&incomingHash);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SHA-256 init failed");
    }

    // Start the existing full-screen LVGL overlay only after preflight validation.
    // No LVGL call is made from this HTTPS request handler.
    auto& updateUi = FirmwareMediaUpdateService::instance();
    if (!updateUi.beginWebUpload(runningVersion, info.version)) {
        mbedtls_sha256_free(&incomingHash);
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "another firmware update is active");
    }
    auto failUi = [&](const char* reason) { updateUi.endWebUpload(false, reason); };
    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Checking, 2,
        "Firmware geprueft. Flash-Slot wird vorbereitet ...");
    PerformanceGuard boost;
    esp_err_t err = writer.begin(target, req->content_len);
    if (err != ESP_OK) {
        mbedtls_sha256_free(&incomingHash);
        failUi("OTA-Slot konnte nicht vorbereitet werden.");
        EventLog::instance().add("ERROR", "WEB_OTA", String("begin: ") + esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    }

    err = writer.write(firstChunk, firstGot);
    bool ok = err == ESP_OK;
    int remaining = req->content_len - firstGot;
    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Writing, 3,
        "Firmware wird ueber HTTPS empfangen und geschrieben ...");

    uint8_t* buffer = writer.buffer();

    while (ok && remaining > 0) {
        const int want = min<int>(remaining, OtaWriteSession::kChunkSize);
        const int got = receive(buffer, want);
        if (got <= 0) {
            err = got == HTTPD_SOCK_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : ESP_FAIL;
            ok = false;
            break;
        }

        ok =
            mbedtls_sha256_update(&incomingHash, buffer, got) == 0 &&
            (err = writer.write(buffer, got)) == ESP_OK;

        remaining -= got;
        if (ok) updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Writing,
            uint8_t(3 + 77ULL * uint64_t(req->content_len - remaining) / uint64_t(req->content_len)),
            "Firmware wird ueber HTTPS empfangen und geschrieben ...");
    }

    uint8_t incomingSha[32]{};
    if (ok) ok = mbedtls_sha256_finish(&incomingHash, incomingSha) == 0;
    mbedtls_sha256_free(&incomingHash);

    if (!ok) {
        writer.abort();
        failUi("Upload unterbrochen oder Flash-Schreibfehler.");
        EventLog::instance().add("ERROR", "WEB_OTA", String("transfer/write: ") + esp_err_to_name(err) + " remaining=" + String(remaining));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
    }

    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Verifying, 82,
        "Upload-SHA-256 wird kontrolliert ...");
    if (memcmp(incomingSha, expectedSha, sizeof(incomingSha)) != 0) {
        writer.abort();
        failUi("Datei-SHA-256 ist ungueltig.");
        EventLog::instance().add("ERROR", "WEB_OTA_HASH", "Upload SHA-256 mismatch");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload SHA-256 mismatch");
    }

    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Verifying, 85,
        "Firmware wird auf Bootfaehigkeit geprueft ...");
    err = writer.finish();
    if (err != ESP_OK) {
        failUi("Firmware-Abbild ungueltig.");
        EventLog::instance().add("ERROR", "WEB_OTA", "Firmware image validation failed");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "firmware image validation failed");
    }

    // Read back the exact written image and hash it again before selecting it.
    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Verifying, 90,
        "Flash wird vollstaendig zur SHA-256-Pruefung gelesen ...");
    uint8_t partitionSha[32]{};
    if (!partitionSha256Exact(target, req->content_len, partitionSha, writer.buffer())) {
        OtaTrace::record(OtaTrace::Failed,req->content_len,ESP_FAIL);
        failUi("Flash konnte nicht gelesen werden.");
        EventLog::instance().add("ERROR", "WEB_OTA_HASH", "Partition SHA-256 readback failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "partition SHA-256 failed");
    }

    if (memcmp(partitionSha, expectedSha, sizeof(partitionSha)) != 0) {
        OtaTrace::record(OtaTrace::Failed,req->content_len,ESP_FAIL);
        failUi("Flash-SHA-256 stimmt nicht.");
        EventLog::instance().add("ERROR", "WEB_OTA_HASH", "Partition SHA-256 mismatch");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "partition SHA-256 mismatch");
    }

    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Verifying, 96,
        "Firmware-Version im Flash wird kontrolliert ...");
    // Re-read metadata from the target partition as a final compatibility check.
    uint8_t verifyHeader[1024]{};
    if (esp_partition_read(target, 0, verifyHeader, sizeof(verifyHeader)) != ESP_OK) {
        failUi("Versionspruefung im Flash fehlgeschlagen.");
        OtaTrace::record(OtaTrace::Failed,req->content_len,ESP_FAIL);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "partition metadata read failed");
    }

    FirmwareImageInfo writtenInfo;
    if (!parseFirmwareInfo(verifyHeader, sizeof(verifyHeader), writtenInfo) ||
        writtenInfo.project != info.project ||
        writtenInfo.version != info.version) {
        OtaTrace::record(OtaTrace::Failed,req->content_len,ESP_FAIL);
        failUi("Firmware-Version im Flash stimmt nicht.");
        EventLog::instance().add("ERROR", "WEB_OTA_VERIFY", "Written metadata mismatch");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "written firmware metadata mismatch");
    }

    updateUi.webUploadProgress(FirmwareMediaUpdateService::Phase::Verifying, 99,
        "Verifizierten OTA-Boot-Slot aktivieren ...");
    OtaTrace::record(OtaTrace::Selecting,req->content_len);
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        OtaTrace::record(OtaTrace::Failed,req->content_len,err);
        failUi("OTA-Boot-Slot konnte nicht aktiviert werden.");
        EventLog::instance().add("ERROR", "WEB_OTA", "Could not select OTA boot slot");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "boot slot failed");
    }

    updateUi.endWebUpload(true, "Update geprueft. Jetzt neu starten.");
    OtaTrace::record(OtaTrace::Ready,req->content_len);
    const String hashText = bytesToHex(partitionSha, sizeof(partitionSha));

    EventLog::instance().add(
        "INFO",
        "WEB_OTA_READY",
        info.project + " " + info.version +
        " slot=" + target->label +
        " sha256=" + hashText
    );

    String body =
        "{\"ok\":true,\"project\":\"" + htmlEscape(info.project) +
        "\",\"version\":\"" + htmlEscape(info.version) +
        "\",\"sha256\":\"" + hashText +
        "\",\"slot\":\"" + String(target->label) +
        "\",\"healthCheckAfterReboot\":true}";

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t FileTransferService::firmwareRebootHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    std::unique_lock<std::mutex> updateLock(OtaWriteSession::mutex(), std::try_to_lock);
    if (!updateLock.owns_lock()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "OTA still in progress");
    }
    FirmwareMediaUpdateService::instance().webRebooting();
    EventLog::instance().add("INFO", "WEB_OTA_REBOOT", "Reboot requested from web UI");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "rebooting");
    delay(250);
    esp_restart();
    return ESP_OK;
}

esp_err_t FileTransferService::appsUploadHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    if (req->content_len <= 0 || req->content_len > 8192)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "size");

    auto& st = StorageService::instance();
    if (!st.canStore(StorageClass::AppPackage))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "storage");

    uint8_t* buf = (uint8_t*)malloc(req->content_len);
    if (!buf)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "memory");

    int total = 0;
    while (total < req->content_len) {
        const int got = httpd_req_recv(
            req,
            (char*)buf + total,
            req->content_len - total
        );
        if (got <= 0) {
            free(buf);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "upload failed");
        }
        total += got;
    }

    const bool ok = st.writeAtomic(
        StorageArea::AppData,
        WebAppManager::kInstalledAppsPath,
        buf,
        total
    );
    free(buf);

    if (!ok)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");

    WebAppManager::instance().reload();
    EventLog::instance().add("INFO", "APP_DB_UPLOAD", "apps.bin replaced via web upload");

    redirectApps(req);
    return ESP_OK;
}

}

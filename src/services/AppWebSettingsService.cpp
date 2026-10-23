#include "AppWebSettingsService.h"
#include "MinimalMdns.h"
#include "NetworkWorker.h"
#include "NetworkService.h"
#include "PrinterService.h"
#include "printer/PrinterWebProfiles.h"
#include <mutex>
#include "ConfigService.h"
#include "StorageService.h"
#include "UsbAppFileSystem.h"
#include "WebSettingsSchemaValidator.h"
#include "../core/AppRegistry.h"
#include "../apps/lua/LuaApp.h"
#include "../apps/lua/LuaAppRegistry.h"
#include "../apps/lua/LuaStorageBridge.h"
#include <ArduinoJson.h>
#include <FS.h>
#include <mbedtls/sha256.h>
#include <cmath>

namespace hp {
namespace {

constexpr size_t kMaxSchemaBytes = WebSettingsSchemaValidator::kMaxBytes;
struct PackageReadGuard {
    bool acquired=LuaAppRegistry::instance().beginPackageRead();
    ~PackageReadGuard(){if(acquired)LuaAppRegistry::instance().endPackageRead();}
};
static_assert(kPrinterWebProfiles==PrinterService::kProfiles);

bool validName(const String& name) {
    if (!name.length() || name.length() > 48) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    }
    return true;
}

String keyFor(const String& owner, const String& name, char prefix) {
    const String identity = owner + ":" + name;
    unsigned char digest[32]{};
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(identity.c_str()),
                       identity.length(), digest, 0) != 0) return "";
    char key[16]{};
    key[0] = prefix;
    for (unsigned i = 0; i < 7; ++i)
        snprintf(key + 1 + i * 2, 3, "%02x", digest[i]);
    return String(key);
}

String jsonEsc(const String& input) {
    String out;
    out.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        const unsigned char c = input[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c >= 0x20) out += static_cast<char>(c);
                break;
        }
    }
    return out;
}

String humanizeKey(String key) {
    key.replace("_", " ");
    key.replace("-", " ");
    key.trim();
    if (!key.length()) return key;

    // Common technical suffixes/words.
    key.replace("ip ", "IP ");
    key.replace(" ip", " IP");
    key.replace("url ", "URL ");
    key.replace(" url", " URL");
    key.replace("ssid", "SSID");
    key.replace("wifi", "WLAN");
    key.replace("poll seconds", "Abfrageintervall");
    key.replace("summary seconds", "Zusammenfassung");
    key.replace("refresh minutes", "Aktualisierungsintervall");
    key.replace("max volume percent", "Maximale Lautstärke");
    key.replace("enabled", "Aktiviert");

    bool cap = true;
    for (size_t i = 0; i < key.length(); ++i) {
        if (key[i] == ' ') cap = true;
        else if (cap) {
            key.setCharAt(i, static_cast<char>(toupper(static_cast<unsigned char>(key[i]))));
            cap = false;
        }
    }
    return key;
}

bool readAppEntry(const String& appId, AppRegistryEntry& entry,
                  LuaApp*& app, String& error) {
    if (!AppRegistry::instance().lookup(appId.c_str(), entry) ||
        entry.kind != AppKind::Lua || !entry.app) {
        error = "Lua-App nicht gefunden";
        return false;
    }
    // Caller owns packageIoMutex. Registry metadata can lag a just-completed
    // activation; never dereference the potentially freed snapshot pointer.
    auto* live=AppManager::instance().find(appId.c_str());
    if(!live||live->kind()!=AppKind::Lua||!LuaAppRegistry::instance().owns(appId.c_str())){
        error="App wird gerade aktualisiert. Bitte erneut laden.";return false;
    }
    app = static_cast<LuaApp*>(live);
    if (!app->assetPackageFolder().length()) {
        error = "App besitzt kein installiertes Paket";
        return false;
    }
    return true;
}

bool openPackageFile(LuaApp* app, const String& name, File& file) {
    if (!app || !name.length()) return false;
    auto& storage = StorageService::instance();
    const String rel = app->assetPackageFolder() + "/" + name;
    if (app->assetPackageOnUsb()) {
        if(auto fs=usbAppFileSystemFor(app->assetUsbGeneration()))file=fs->open(rel,FILE_READ);
    } else if (app->assetPackageOnAppFs()) {
        auto* fs = storage.fsFor(StorageClass::AppPackage);
        if (!fs) return false;
        file = fs->open(storage.pathFor(StorageClass::AppPackage, rel), FILE_READ);
    } else {
        file = storage.openSdRead(rel);
    }
    return static_cast<bool>(file);
}

bool loadWebSchema(LuaApp* app, JsonDocument& schema, bool& exists, String& error) {
    exists = false;
    File f;
    if (!openPackageFile(app, "web_settings.json", f)) return true;
    exists = true;
    if (f.isDirectory() || f.size() == 0 || f.size() > kMaxSchemaBytes) {
        f.close();
        error = "web_settings.json ist leer oder zu groß";
        return false;
    }
    const auto parse = deserializeJson(schema, f, DeserializationOption::NestingLimit(12));
    f.close();
    if (parse) {
        error = String("web_settings.json: ") + parse.c_str();
        return false;
    }
    return true;
}

bool readKvRecord(const String& owner, const String& name,
                  int& type, JsonVariant value, bool& exists) {
    exists = false;
    if (!validName(name)) return false;

    auto& config = ConfigService::instance();
    const String raw = config.getString(keyFor(owner, name, 'k').c_str(), "");
    if (!raw.length()) return true;

    JsonDocument outer;
    if (deserializeJson(outer, raw) ||
        outer["owner"].as<String>() != owner ||
        outer["name"].as<String>() != name ||
        !outer["value"].is<const char*>()) return false;

    JsonDocument inner;
    if (deserializeJson(inner, outer["value"].as<String>()) ||
        !inner["type"].is<int>() || inner["type"].as<int>() < 0 ||
        inner["type"].as<int>() > 3 || inner["data"].isUnbound()) return false;

    type = inner["type"].as<int>();
    value.set(inner["data"]);
    exists = true;
    return true;
}

bool secretExists(const String& owner, const String& name) {
    if (!validName(name)) return false;
    const String raw = ConfigService::instance().getSecretString(
        keyFor(owner, name, 's').c_str(), "");
    if (!raw.length()) return false;
    JsonDocument doc;
    return !deserializeJson(doc, raw) &&
           doc["owner"].as<String>() == owner &&
           doc["name"].as<String>() == name;
}

bool readIndex(const String& owner, bool secret, JsonArray output) {
    auto& config = ConfigService::instance();
    const String key = keyFor(owner, "$index", secret ? 's' : 'k');
    const String raw = secret ? config.getSecretString(key.c_str(), "")
                              : config.getString(key.c_str(), "");
    if (!raw.length()) return true;

    JsonDocument doc;
    if (deserializeJson(doc, raw) || doc["owner"].as<String>() != owner ||
        !doc["names"].is<JsonArray>()) return false;

    for (JsonVariant v : doc["names"].as<JsonArray>()) {
        if (v.is<const char*>() && validName(v.as<String>()))
            output.add(v.as<String>());
    }
    return true;
}

bool looksStructuredJson(const String& value) {
    String t = value;
    t.trim();
    if (!(t.startsWith("{") || t.startsWith("["))) return false;
    JsonDocument doc;
    if (deserializeJson(doc, t, DeserializationOption::NestingLimit(12))) return false;
    return doc.is<JsonObject>() || doc.is<JsonArray>();
}

bool validSimpleType(const String& type) {
    return type == "text" || type == "password" || type == "bool" ||
           type == "integer" || type == "number" || type == "select" ||
           type == "host" || type == "url";
}

int storageTypeFor(const String& type) {
    if (type == "integer") return 1;
    if (type == "bool") return 2;
    if (type == "number") return 3;
    return 0;
}

bool validateSchema(JsonDocument& schema, String& error, size_t& fieldCount) {
    fieldCount = 0;
    String code;
    if (!WebSettingsSchemaValidator::validate(schema, code)) {
        error = WebSettingsSchemaValidator::message(code);
        return false;
    }
    for (JsonObjectConst tab : schema["tabs"].as<JsonArrayConst>()) {
        for (JsonObjectConst field : tab["fields"].as<JsonArrayConst>()) {
            ++fieldCount;
            if (field["type"].as<String>() == "repeater")
                fieldCount += field["fields"].size();
        }
    }
    return true;
}

void appendOptionJson(String& out, JsonVariantConst option) {
    if (option.is<const char*>()) {
        const String value = option.as<String>();
        out += "{\"value\":\"" + jsonEsc(value) + "\",\"label\":\"" +
               jsonEsc(value) + "\"}";
    } else {
        out += "{\"value\":\"" + jsonEsc(option["value"].as<String>()) +
               "\",\"label\":\"" + jsonEsc(option["label"].as<String>()) + "\"}";
    }
}

void appendSimpleFieldMeta(String& out, JsonObjectConst field, const String& owner) {
    const String key = field["key"].as<String>();
    const String type = field["type"].as<String>();

    out += "{\"key\":\"" + jsonEsc(key) +
           "\",\"label\":\"" + jsonEsc(field["label"].as<String>()) +
           "\",\"type\":\"" + jsonEsc(type) + "\"";

    if (field["help"].is<const char*>())
        out += ",\"help\":\"" + jsonEsc(field["help"].as<String>()) + "\"";
    if (field["placeholder"].is<const char*>())
        out += ",\"placeholder\":\"" + jsonEsc(field["placeholder"].as<String>()) + "\"";
    if (field["min"].is<double>()) out += ",\"min\":" + String(field["min"].as<double>(), 6);
    if (field["max"].is<double>()) out += ",\"max\":" + String(field["max"].as<double>(), 6);
    if (field["step"].is<double>()) out += ",\"step\":" + String(field["step"].as<double>(), 6);
    if (field["maxLength"].is<uint16_t>())
        out += ",\"maxLength\":" + String(field["maxLength"].as<uint16_t>());

    if (type == "select") {
        out += ",\"options\":[";
        bool first = true;
        for (JsonVariantConst option : field["options"].as<JsonArrayConst>()) {
            if (!first) out += ',';
            first = false;
            appendOptionJson(out, option);
        }
        out += ']';
    }

    if (type == "password") {
        out += ",\"value\":\"\",\"configured\":";
        out += secretExists(owner, key) ? "true" : "false";
        out += '}';
        return;
    }

    JsonDocument holder;
    JsonVariant value = holder["v"];
    int storedType = -1;
    bool exists = false;
    const bool readable = readKvRecord(owner, key, storedType, value, exists);
    const int wanted = storageTypeFor(type);
    if (!readable || (exists && storedType != wanted)) exists = false;

    out += ",\"value\":";
    if (exists) {
        String encoded;
        serializeJson(value, encoded);
        out += encoded;
    } else if (!field["default"].isUnbound()) {
        String encoded;
        serializeJson(field["default"], encoded);
        out += encoded;
    } else if (type == "bool") out += "false";
    else if (type == "integer" || type == "number") out += "0";
    else out += "\"\"";
    out += ",\"configured\":";
    out += exists ? "true" : "false";
    out += '}';
}

void appendRepeater(String& out, JsonObjectConst field, const String& owner) {
    const String key = field["key"].as<String>();
    const uint8_t maxItems = field["maxItems"] | 8;

    out += "{\"key\":\"" + jsonEsc(key) +
           "\",\"label\":\"" + jsonEsc(field["label"].as<String>()) +
           "\",\"type\":\"repeater\",\"maxItems\":" + String(maxItems);
    if (field["itemLabel"].is<const char*>())
        out += ",\"itemLabel\":\"" + jsonEsc(field["itemLabel"].as<String>()) + "\"";
    if (field["help"].is<const char*>())
        out += ",\"help\":\"" + jsonEsc(field["help"].as<String>()) + "\"";
    if(field["discovery"].is<JsonObjectConst>()){
        String metadata;serializeJson(field["discovery"],metadata);out+=",\"discovery\":"+metadata;
    }

    out += ",\"fields\":[";
    bool firstChild = true;
    for (JsonObjectConst child : field["fields"].as<JsonArrayConst>()) {
        if (!firstChild) out += ',';
        firstChild = false;
        // Metadata only. Nested values come from the JSON array below.
        out += "{\"key\":\"" + jsonEsc(child["key"].as<String>()) +
               "\",\"label\":\"" + jsonEsc(child["label"].as<String>()) +
               "\",\"type\":\"" + jsonEsc(child["type"].as<String>()) + "\"";
        if (child["placeholder"].is<const char*>())
            out += ",\"placeholder\":\"" + jsonEsc(child["placeholder"].as<String>()) + "\"";
        if (child["min"].is<double>()) out += ",\"min\":" + String(child["min"].as<double>(), 6);
        if (child["max"].is<double>()) out += ",\"max\":" + String(child["max"].as<double>(), 6);
        if (child["step"].is<double>()) out += ",\"step\":" + String(child["step"].as<double>(), 6);
        if (child["maxLength"].is<uint16_t>())
            out += ",\"maxLength\":" + String(child["maxLength"].as<uint16_t>());
        if(!child["default"].isUnbound()){
            String value;serializeJson(child["default"],value);out+=",\"default\":"+value;
        }
        if (child["type"].as<String>() == "select") {
            out += ",\"options\":[";
            bool fo = true;
            for (JsonVariantConst option : child["options"].as<JsonArrayConst>()) {
                if (!fo) out += ',';
                fo = false;
                appendOptionJson(out, option);
            }
            out += ']';
        }
        out += '}';
    }
    out += ']';

    JsonDocument holder;
    JsonVariant raw = holder["v"];
    int type = -1;
    bool exists = false;
    JsonDocument parsed;
    bool valid = false;
    if (readKvRecord(owner, key, type, raw, exists) && exists && type == 0 &&
        raw.is<const char*>()) {
        const String text = raw.as<String>();
        if (!deserializeJson(parsed, text, DeserializationOption::NestingLimit(12)) &&
            parsed.is<JsonArray>() && parsed.size() <= maxItems) valid = true;
    }

    out += ",\"items\":";
    if (valid) {
        String encoded;
        serializeJson(parsed, encoded);
        out += encoded;
    } else {
        out += "[]";
    }
    if(key=="printer_profiles"){
        JsonDocument info;auto states=info["states"].to<JsonObject>();unsigned online=0;
        for(JsonObjectConst item:parsed.as<JsonArrayConst>()){
            PrinterProfile profile;profile.owner=owner;profile.id=item["id"]|"";
            profile.host=item["host"]|"";profile.protocol=item["protocol"]|"reprap";profile.port=item["port"]|printerDefaultPort(profile.protocol.c_str());
            profile.mode=item["mode"]|"auto";profile.passwordSecret=item["password_secret"]|"";
            profile.enabled=item["enabled"]|true;
            if(!printerIdentity(profile.id.c_str(),36))continue;
            JsonDocument snap;const String cached=PrinterService::instance().cachedSnapshot(profile);
            if(!cached.length()||deserializeJson(snap,cached)||!snap["revision"].as<uint32_t>())continue;
            auto state=states[profile.id].to<JsonObject>();
            const bool stale=(snap["stale"]|false)||uint32_t(millis()-snap["timestamp"].as<uint32_t>())>60000;
            const bool connected=(snap["online"]|false)&&!stale;
            state["online"]=connected;state["stale"]=stale;state["state"]=snap["state"]|"";
            state["host"]=profile.host;state["port"]=profile.port;state["protocol"]=profile.protocol;state["mode"]=profile.mode;
            state["enabled"]=profile.enabled;
            state["message"]=snap["message"]|"";if(connected)++online;
            state["schema_version"]=snap["schema_version"]|1u;state["firmware"]=snap["firmware"]|"";
            state["sensor_count"]=snap["temperatures"].size();state["truncated"]=snap["truncated"]|false;
            state["updated_at"]=snap["updated_at"]|0u;state["age_ms"]=uint32_t(millis()-state["updated_at"].as<uint32_t>());
        }
        String encoded;serializeJson(states,encoded);
        out+=",\"printer_status\":"+encoded+",\"printer_online\":"+String(online);
    }
    out += '}';
}

JsonObjectConst findField(JsonDocument& schema, const String& key) {
    if (!schema["tabs"].is<JsonArray>()) return JsonObjectConst();
    for (JsonObjectConst tab : schema["tabs"].as<JsonArrayConst>())
        for (JsonObjectConst field : tab["fields"].as<JsonArrayConst>())
            if (field["key"].as<String>() == key) return field;
    return JsonObjectConst();
}

bool valueMatchesSimple(JsonVariantConst value, const String& type,
                        JsonObjectConst field, String& error) {
    if (type == "password" || type == "text" || type == "select" ||
        type == "host" || type == "url") {
        if (!value.is<const char*>()) { error = "Textwert erwartet"; return false; }
        const String text = value.as<String>();
        const uint16_t maxLength = field["maxLength"] | 256;
        if (text.length() > maxLength || text.length() > 2048) {
            error = "Text zu lang"; return false;
        }
        if (type == "select") {
            bool found = false;
            for (JsonVariantConst option : field["options"].as<JsonArrayConst>()) {
                const String v = option.is<const char*>()
                    ? option.as<String>() : option["value"].as<String>();
                if (v == text) { found = true; break; }
            }
            if (!found) { error = "Auswahlwert ungültig"; return false; }
        }
        return true;
    }
    if (type == "bool") {
        if (!value.is<bool>()) { error = "Boolean erwartet"; return false; }
        return true;
    }
    if (type == "integer") {
        if (!value.is<int32_t>() && !value.is<int>()) {
            error = "Ganzzahl erwartet"; return false;
        }
        const int32_t n = value.as<int32_t>();
        if (!field["min"].isUnbound() && n < field["min"].as<int32_t>()) {
            error = "Wert zu klein"; return false;
        }
        if (!field["max"].isUnbound() && n > field["max"].as<int32_t>()) {
            error = "Wert zu groß"; return false;
        }
        return true;
    }
    if (type == "number") {
        if (!value.is<float>() && !value.is<double>() &&
            !value.is<int>() && !value.is<int32_t>()) {
            error = "Zahl erwartet"; return false;
        }
        const double n = value.as<double>();
        if (!std::isfinite(n)) { error = "Zahl ungültig"; return false; }
        if (!field["min"].isUnbound() && n < field["min"].as<double>()) {
            error = "Wert zu klein"; return false;
        }
        if (!field["max"].isUnbound() && n > field["max"].as<double>()) {
            error = "Wert zu groß"; return false;
        }
        return true;
    }
    error = "Feldtyp unbekannt";
    return false;
}

bool validateRepeaterValue(JsonVariantConst value, JsonObjectConst field,
                           String& encoded, String& error) {
    if (!value.is<JsonArrayConst>()) { error = "Liste erwartet"; return false; }
    const uint8_t maxItems = field["maxItems"] | 8;
    JsonArrayConst items = value.as<JsonArrayConst>();
    if (items.size() > maxItems) { error = "Zu viele Einträge"; return false; }

    for (JsonVariantConst item : items) {
        if (!item.is<JsonObjectConst>()) { error = "Listeneintrag ungültig"; return false; }
        JsonObjectConst object = item.as<JsonObjectConst>();

        // Reject undeclared members.
        for (JsonPairConst pair : object) {
            bool declared = false;
            for (JsonObjectConst child : field["fields"].as<JsonArrayConst>()) {
                if (child["key"].as<String>() == String(pair.key().c_str())) {
                    declared = true; break;
                }
            }
            if (!declared && !(field["key"].as<String>()=="printer_profiles"&&printerRuntimeField(String(pair.key().c_str())))) {
                error = "Unbekanntes Listenfeld"; return false;
            }
        }

        for (JsonObjectConst child : field["fields"].as<JsonArrayConst>()) {
            const String childKey = child["key"].as<String>();
            if (object[childKey].isUnbound()) continue;
            if (!valueMatchesSimple(object[childKey],
                                    child["type"].as<String>(), child, error))
                return false;
        }
    }

    if (measureJson(value) > 8192) { error = "Liste zu groß"; return false; }
    serializeJson(value, encoded);
    return true;
}

bool legacyTypes(const String& appId, JsonObject out, String& error) {
    JsonDocument indexes;
    JsonArray normal = indexes["normal"].to<JsonArray>();
    JsonArray secrets = indexes["secrets"].to<JsonArray>();
    if (!readIndex(appId, false, normal) || !readIndex(appId, true, secrets)) {
        error = "App-Speicherindex beschädigt";
        return false;
    }
    for (JsonVariant n : normal) {
        JsonDocument holder;
        JsonVariant v = holder["v"];
        int t = 0; bool exists = false;
        if (readKvRecord(appId, n.as<String>(), t, v, exists) && exists)
            out[n.as<String>()] = t;
    }
    for (JsonVariant n : secrets) out[n.as<String>()] = 4;
    return true;
}

struct WebDiscoveryRun {
    std::mutex mutex;
    String owner,field,results="[]",message;
    uint32_t id=0,queued=0,started=0,finished=0;
    bool busy=false;
} webDiscovery;

void runWebDiscovery(void*) {
    {
        std::lock_guard<std::mutex> guard(webDiscovery.mutex);
        webDiscovery.started=millis();
        if(static_cast<uint32_t>(millis()-webDiscovery.queued)>15000){
            webDiscovery.busy=false;webDiscovery.finished=millis();
            webDiscovery.message="Suche in der Warteschlange abgelaufen. Bitte erneut versuchen.";return;
        }
    }
    JsonDocument doc;auto devices=doc.to<JsonArray>();
    String message="Keine auswertbare mDNS-Ankündigung empfangen. Drucker können trotzdem vorhanden sein: manuelle IP-Eingabe verwenden oder mDNS am Gerät prüfen.";
    if(NetworkService::instance().online()){
        const String services[]={"_moonraker._tcp.local","_octoprint._tcp.local","_http._tcp.local"};
        int queryError=0;MdnsDiscoveryResult found[8];const int count=queryMdnsServices(services,3,found,8,1500,&queryError);
        for(int i=0;i<count;++i){
            auto& item=found[i];String service=item.service,product=item.product;service.toLowerCase();product.toLowerCase();
            String protocol="unknown";
            if(service=="_moonraker._tcp.local")protocol="moonraker";
            else if(service=="_octoprint._tcp.local")protocol="octoprint";
            else if(product=="reprapfirmware"||product=="duet")protocol="rrf";
            else if(product=="octoprint")protocol="octoprint";
            bool duplicate=false;
            for(JsonObject old:devices)if(old["host"].as<String>()==item.host&&old["port"].as<uint16_t>()==item.port){
                if(old["protocol"].as<String>()=="unknown"&&protocol!="unknown")old["protocol"]=protocol;
                duplicate=true;break;
            }
            if(duplicate)continue;
            auto device=devices.add<JsonObject>();device["host"]=item.host;device["hostname"]=item.hostname;
            device["port"]=item.port;device["protocol"]=protocol;device["service"]=item.service;
        }
        if(queryError)message=queryError==1?"Suche konnte wegen Speichermangel nicht vollständig ausgeführt werden. Bitte später versuchen.":"mDNS-Netzwerksuche konnte nicht vollständig ausgeführt werden. Verbindung prüfen und manuelle IP-Eingabe verwenden.";
        else if(devices.size())message="mDNS-Kandidaten gefunden (Angaben vom Gerät, nicht verifiziert). Profil nur bewusst übernehmen; weitere Drucker können ohne mDNS vorhanden sein.";
    }else message="Netzwerk offline. Manuelle IP-Eingabe bleibt möglich.";
    String output;serializeJson(doc,output);
    std::lock_guard<std::mutex> guard(webDiscovery.mutex);
    webDiscovery.results=output;webDiscovery.message=message;webDiscovery.finished=millis();webDiscovery.busy=false;
}

} // namespace

bool AppWebSettingsService::discover(const String& appId,const String& field,uint32_t run,String& json,String& error){
    PackageReadGuard packageGuard;if(!packageGuard.acquired){error="App-Pakete beschäftigt. Bitte erneut versuchen.";return false;}
    if(!validName(appId)||!validName(field)){error="Ungültige App oder Profilgruppe";return false;}
    AppRegistryEntry entry;LuaApp* app=nullptr;
    if(!readAppEntry(appId,entry,app,error))return false;
    if(!app->allows(AppPermission::Discovery)||!app->allows(AppPermission::Network)){error="App benötigt discovery und network.local";return false;}
    JsonDocument schema;bool exists=false;size_t fields=0;
    if(!loadWebSchema(app,schema,exists,error)||!exists||!validateSchema(schema,error,fields)){if(!error.length())error="Keine gültigen Web-Einstellungen";return false;}
    bool allowed=false;
    for(JsonObjectConst tab:schema["tabs"].as<JsonArrayConst>())for(JsonObjectConst f:tab["fields"].as<JsonArrayConst>())
        if(f["key"].as<String>()==field&&f["type"].as<String>()=="repeater"&&f["discovery"].is<JsonObjectConst>())allowed=true;
    if(!allowed){error="Für diese Profilgruppe ist keine Suche freigegeben";return false;}
    std::lock_guard<std::mutex> guard(webDiscovery.mutex);
    const uint32_t now=millis();
    if(!run){
        if(webDiscovery.busy||(webDiscovery.id&&static_cast<uint32_t>(now-webDiscovery.queued)<5000)){error="Suche bereits aktiv oder kurze Suchpause. Bitte später erneut versuchen.";return false;}
        if(!NetworkService::instance().online()){error="Netzwerk offline; IP weiterhin manuell eintragen";return false;}
        webDiscovery.id++;if(!webDiscovery.id)webDiscovery.id=1;
        webDiscovery.owner=appId;webDiscovery.field=field;webDiscovery.queued=now;webDiscovery.started=0;webDiscovery.finished=0;
        webDiscovery.results="[]";webDiscovery.message="Suche eingereiht …";webDiscovery.busy=true;
        if(!NetworkWorker::instance().submit(runWebDiscovery,nullptr,"web-discovery")){webDiscovery.busy=false;error="Netzwerk-Warteschlange belegt. Bitte erneut versuchen.";return false;}
        run=webDiscovery.id;
    }
    if(run!=webDiscovery.id||webDiscovery.owner!=appId||webDiscovery.field!=field||(!webDiscovery.busy&&static_cast<uint32_t>(now-webDiscovery.finished)>30000)){error="Suchlauf abgelaufen oder gehört zu einer anderen App";return false;}
    json="{\"ok\":true,\"run\":"+String(run)+",\"done\":"+String(webDiscovery.busy?"false":"true")+
        ",\"status\":\""+(webDiscovery.busy?(webDiscovery.started?String("searching"):String("queued")):String("done"))+"\",\"message\":\""+
        jsonEsc(webDiscovery.busy?(webDiscovery.started?String("Suche nach mDNS-Ankündigungen …"):webDiscovery.message):webDiscovery.message)+"\",\"devices\":"+webDiscovery.results+"}";
    return true;
}

AppWebSettingsService& AppWebSettingsService::instance() {
    static AppWebSettingsService service;
    return service;
}

bool AppWebSettingsService::describe(const String& appId,
                                     String& json, String& error) {
    PackageReadGuard packageGuard;if(!packageGuard.acquired){error="App-Pakete beschäftigt. Bitte erneut versuchen.";return false;}
    if (!validName(appId)) { error = "App-ID ungültig"; return false; }

    AppRegistryEntry entry;
    LuaApp* app = nullptr;
    if (!readAppEntry(appId, entry, app, error)) return false;

    JsonDocument schema;
    bool schemaExists = false;
    if (!loadWebSchema(app, schema, schemaExists, error)) return false;

    size_t fieldCount = 0;
    if (schemaExists && !validateSchema(schema, error, fieldCount)) return false;

    json = "{\"ok\":true,\"id\":\"" + jsonEsc(appId) +
           "\",\"name\":\"" + jsonEsc(entry.name) +
           "\",\"declared\":" + String(schemaExists ? "true" : "false");

    if (schemaExists) {
        json += ",\"tabs\":[";
        bool firstTab = true;
        for (JsonObjectConst tab : schema["tabs"].as<JsonArrayConst>()) {
            if (!firstTab) json += ',';
            firstTab = false;
            json += "{\"id\":\"" + jsonEsc(tab["id"].as<String>()) +
                    "\",\"label\":\"" + jsonEsc(tab["label"].as<String>()) +
                    "\",\"fields\":[";
            bool firstField = true;
            for (JsonObjectConst field : tab["fields"].as<JsonArrayConst>()) {
                if (!firstField) json += ',';
                firstField = false;
                if (field["type"].as<String>() == "repeater")
                    appendRepeater(json, field, appId);
                else
                    appendSimpleFieldMeta(json, field, appId);
            }
            json += "]}";
        }
        json += "]}";
        return true;
    }

    // Backward-compatible primitive fallback. Structured JSON is intentionally
    // not offered as an editable text box because that can corrupt an app's
    // internal data model.
    JsonDocument indexes;
    JsonArray normal = indexes["normal"].to<JsonArray>();
    JsonArray secrets = indexes["secrets"].to<JsonArray>();
    if (!readIndex(appId, false, normal) || !readIndex(appId, true, secrets)) {
        error = "App-Speicherindex beschädigt";
        return false;
    }

    json += ",\"tabs\":[{\"id\":\"legacy\",\"label\":\"Allgemein\",\"fields\":[";
    bool first = true;
    size_t complexCount = 0;

    for (JsonVariant nameValue : normal) {
        const String key = nameValue.as<String>();
        JsonDocument holder;
        JsonVariant value = holder["v"];
        int type = 0; bool exists = false;
        if (!readKvRecord(appId, key, type, value, exists) || !exists) continue;

        if (type == 0 && value.is<const char*>() &&
            (key.endsWith("_json") || looksStructuredJson(value.as<String>()))) {
            ++complexCount;
            continue;
        }

        if (!first) json += ',';
        first = false;
        const char* typeName = type == 1 ? "integer" :
                               type == 2 ? "bool" :
                               type == 3 ? "number" : "text";
        json += "{\"key\":\"" + jsonEsc(key) + "\",\"label\":\"" +
                jsonEsc(humanizeKey(key)) + "\",\"type\":\"" + typeName +
                "\",\"generated\":true,\"configured\":true,\"value\":";
        String encoded;
        serializeJson(value, encoded);
        json += encoded + "}";
    }

    for (JsonVariant nameValue : secrets) {
        const String key = nameValue.as<String>();
        if (!first) json += ',';
        first = false;
        json += "{\"key\":\"" + jsonEsc(key) + "\",\"label\":\"" +
                jsonEsc(humanizeKey(key)) +
                "\",\"type\":\"password\",\"generated\":true,\"configured\":" +
                String(secretExists(appId, key) ? "true" : "false") +
                ",\"value\":\"\"}";
    }

    json += "]}],\"complexHidden\":" + String(complexCount) + "}";
    return true;
}

bool AppWebSettingsService::apply(const String& body,
                                  String& json, String& error) {
    PackageReadGuard packageGuard;if(!packageGuard.acquired){error="App-Pakete beschäftigt. Bitte erneut versuchen.";return false;}
    if (!body.length() || body.length() > 16384) {
        error = "Anfrage zu groß";
        return false;
    }

    JsonDocument request;
    if (deserializeJson(request, body, DeserializationOption::NestingLimit(12)) ||
        !request["id"].is<const char*>() || !request["values"].is<JsonObject>()) {
        error = "Ungültige Anfrage";
        return false;
    }

    const String appId = request["id"].as<String>();
    if (!validName(appId)) { error = "App-ID ungültig"; return false; }

    AppRegistryEntry entry;
    LuaApp* app = nullptr;
    if (!readAppEntry(appId, entry, app, error)) return false;

    JsonDocument schema;
    bool schemaExists = false;
    if (!loadWebSchema(app, schema, schemaExists, error)) return false;
    size_t fieldCount = 0;
    if (schemaExists && !validateSchema(schema, error, fieldCount)) return false;

    JsonDocument legacyDoc;
    JsonObject legacy = legacyDoc.to<JsonObject>();
    if (!schemaExists && !legacyTypes(appId, legacy, error)) return false;

    // Reject/normalize the complete printer list before any setting is written.
    if(schemaExists&&!request["values"]["printer_profiles"].isUnbound()){
        const auto field=findField(schema,"printer_profiles");
        if(field.isNull()||field["type"].as<String>()!="repeater"){
            error="Druckerprofile benoetigen ein deklariertes Repeater-Feld";return false;
        }
        auto profiles=request["values"]["printer_profiles"].as<JsonArray>();
        String encoded;
        if(profiles.isNull()||!normalizePrinterWebProfiles(profiles,error)||!validateRepeaterValue(profiles,field,encoded,error)){
            if(!error.length())error="Druckerprofilliste erwartet";return false;
        }
    }

    size_t changed = 0;
    for (JsonPairConst pair : request["values"].as<JsonObjectConst>()) {
        const String key = pair.key().c_str();
        if (!validName(key)) { error = "Ungültiger Schlüssel"; return false; }

        int storageType = -1;
        String typeName;
        JsonObjectConst field;

        if (schemaExists) {
            field = findField(schema, key);
            if (field.isNull()) { error = "Unbekanntes Einstellungsfeld"; return false; }
            typeName = field["type"].as<String>();

            if (typeName == "repeater") {
                String encoded;
                if (!validateRepeaterValue(request["values"][key], field, encoded, error)) return false;
                JsonDocument inner;
                inner["type"] = 0;
                inner["data"] = encoded;
                String wrapped;
                serializeJson(inner, wrapped);
                const auto result = executeLuaStorage(appId, 1, key, wrapped);
                if (!result.ok) {
                    error = "Liste konnte nicht gespeichert werden: " + result.error;
                    return false;
                }
                ++changed;
                continue;
            }

            if (!valueMatchesSimple(pair.value(), typeName, field, error)) return false;
            storageType = typeName == "password" ? 4 : storageTypeFor(typeName);
        } else {
            if (!legacy[key].is<int>()) { error = "Unbekannter Legacy-Schlüssel"; return false; }
            storageType = legacy[key].as<int>();
            if (storageType == 4) typeName = "password";
            else if (storageType == 1) typeName = "integer";
            else if (storageType == 2) typeName = "bool";
            else if (storageType == 3) typeName = "number";
            else typeName = "text";

            // Never allow a structured JSON legacy value to be overwritten
            // through the generic browser fallback.
            if (storageType == 0) {
                JsonDocument holder;
                JsonVariant oldValue = holder["v"];
                int oldType = 0; bool exists = false;
                if (readKvRecord(appId, key, oldType, oldValue, exists) && exists &&
                    oldValue.is<const char*>() &&
                    (key.endsWith("_json") || looksStructuredJson(oldValue.as<String>()))) {
                    error = "Komplexe Einstellung benötigt web_settings.json";
                    return false;
                }
            }

            if ((storageType == 0 || storageType == 4) && !pair.value().is<const char*>()) {
                error = "Textwert erwartet"; return false;
            }
            if (storageType == 1 && !pair.value().is<int32_t>() && !pair.value().is<int>()) {
                error = "Ganzzahl erwartet"; return false;
            }
            if (storageType == 2 && !pair.value().is<bool>()) {
                error = "Boolean erwartet"; return false;
            }
            if (storageType == 3 && !pair.value().is<double>() &&
                !pair.value().is<float>() && !pair.value().is<int>()) {
                error = "Zahl erwartet"; return false;
            }
        }

        if (storageType == 4) {
            const String secret = pair.value().as<String>();
            if (!secret.length()) continue; // preserve existing
            const auto result = executeLuaStorage(appId, 4, key, secret);
            if (!result.ok) {
                error = "Secret konnte nicht gespeichert werden: " + result.error;
                return false;
            }
        } else {
            JsonDocument inner;
            inner["type"] = storageType;
            inner["data"].set(pair.value());
            String encoded;
            serializeJson(inner, encoded);
            const auto result = executeLuaStorage(appId, 1, key, encoded);
            if (!result.ok) {
                error = "Einstellung konnte nicht gespeichert werden: " + result.error;
                return false;
            }
        }
        ++changed;
    }

    if (request["clearSecrets"].is<JsonArray>()) {
        for (JsonVariantConst item : request["clearSecrets"].as<JsonArrayConst>()) {
            if (!item.is<const char*>()) { error = "clearSecrets ungültig"; return false; }
            const String key = item.as<String>();
            bool allowed = false;
            if (schemaExists) {
                const auto field = findField(schema, key);
                allowed = !field.isNull() && field["type"].as<String>() == "password";
            } else {
                allowed = legacy[key].is<int>() && legacy[key].as<int>() == 4;
            }
            if (!allowed) { error = "Secret-Schlüssel unbekannt"; return false; }
            const auto result = executeLuaStorage(appId, 5, key, "");
            if (!result.ok) { error = "Secret konnte nicht gelöscht werden"; return false; }
            ++changed;
        }
    }

    json = "{\"ok\":true,\"changed\":" + String(changed) + "}";
    return true;
}

} // namespace hp

#include "PowerMeterClient.h"
#include <NetworkClient.h>
#include <HTTPClient.h>
#include <stdlib.h>
#include <ArduinoJson.h>

bool PowerMeterClient::fetchDetailHistory(const String& raw,const String& password,int64_t from,int64_t to,const hp::EnergyHistoryProvider::Visitor& visitor){
    String host;if(!visitor||from<1577836800LL||to<=from||to-from>2400||!normalizeHost(raw,host))return false;
    for(unsigned request=0;request<2;++request){String body,error;const int64_t anchor=request?to-1:from;
        if(!requestJson("http://"+host+"/api/v1/history?range=hour&anchor="+String(static_cast<unsigned long>(anchor)),body,error,password,110000,4500))return false;
        JsonDocument doc;if(deserializeJson(doc,body))return false;
        const uint16_t step=doc["step"]|0;if(!step||step>3600||!doc["values"].is<JsonArray>())return false;
        for(JsonObject item:doc["values"].as<JsonArray>()){
            const int64_t timestamp=item["ts"].as<int64_t>();if(timestamp<from||timestamp>=to)continue;
            hp::EnergyHistoryPoint point;point.timestamp=timestamp;point.resolutionSeconds=step;
            // Tracker history exposes measured grid power; interval import/export are
            // not cumulative meter counters and must not be stored as such.
            if(item["avg"].is<float>()){point.values[0]=item["avg"].as<float>();point.quality=2;}
            if(point.quality&&!visitor(point))return true;
        }
        if(doc["to"].as<int64_t>()>=to)return true;
    }return true;
}

bool PowerMeterClient::jsonNumber(const String& json,
                                  const char* key,
                                  float& value) {
    String token = "\"" + String(key) + "\"";
    int p = json.indexOf(token);
    if (p < 0)
        return false;

    p = json.indexOf(':', p + token.length());
    if (p < 0)
        return false;

    ++p;
    while (p < (int)json.length() &&
           (json[p] == ' ' || json[p] == '\t' ||
            json[p] == '\r' || json[p] == '\n'))
        ++p;

    const char* start = json.c_str() + p;
    char* end = nullptr;
    double parsed = strtod(start, &end);
    if (end == start)
        return false;

    value = (float)parsed;
    return isfinite(value);
}

bool PowerMeterClient::firstNumber(const String& json,
                                   const char* const* keys,
                                   size_t keyCount,
                                   float& value) {
    for (size_t i = 0; i < keyCount; ++i)
        if (jsonNumber(json, keys[i], value))
            return true;
    return false;
}

bool PowerMeterClient::taggedObject(const String& json, const char* key,
                                    const char* value, String& object) {
    const String token = "\"" + String(key) + "\":\"" + String(value) + "\"";
    const int tag = json.indexOf(token);
    if (tag < 0) return false;
    const int begin = json.lastIndexOf('{', tag);
    const int end = json.indexOf('}', tag + token.length());
    if (begin < 0 || end < 0 || end <= begin || end - begin > 768) return false;
    object = json.substring(begin, end + 1);
    return true;
}

void PowerMeterClient::sanitizeData(PowerMeterData& data) {
    auto validRange = [](float& value, float low, float high) {
        if (!isfinite(value) || value < low || value > high) value = NAN;
    };
    for (int i = 0; i < 3; ++i) {
        validRange(data.voltage[i], 0.0f, 300.0f);
        validRange(data.current[i], 0.0f, 1000.0f);
        validRange(data.power[i], -1000000.0f, 1000000.0f);
    }
    validRange(data.totalPower, -3000000.0f, 3000000.0f);
    validRange(data.totalImportKwh, 0.0f, 1000000000.0f);
    validRange(data.totalExportKwh, 0.0f, 1000000000.0f);
}

bool PowerMeterClient::normalizeHost(const String& raw, String& normalized) {
    normalized = raw;
    normalized.trim();

    if (!normalized.length() || normalized.length() > 96)
        return false;

    if (normalized.startsWith("https://"))
        return false; // IR Tracker is intentionally local HTTP only.

    if (normalized.startsWith("http://"))
        normalized.remove(0, 7);

    while (normalized.endsWith("/"))
        normalized.remove(normalized.length() - 1);

    if (!normalized.length())
        return false;

    // Reject paths, queries, credentials and control characters.
    for (size_t i = 0; i < normalized.length(); ++i) {
        char c = normalized[i];
        if (c <= 0x20 || c == '/' || c == '?' || c == '#' || c == '@')
            return false;
    }

    return true;
}

bool PowerMeterClient::requestJson(const String& url,
                                   String& body,
                                   String& errorOut,
                                   const String& password,
                                   size_t maxBody,
                                   uint16_t timeoutMs) {
    NetworkClient client;
    HTTPClient http;

    http.setConnectTimeout(350);
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    http.useHTTP10(true);

    if (!http.begin(client, url)) {
        errorOut = "HTTP Start fehlgeschlagen";
        return false;
    }

    http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    if (password.length())
        http.setAuthorization("admin", password.c_str());

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        errorOut = "HTTP " + String(code);
        http.end();
        return false;
    }

    const int announcedSize = http.getSize();
    if (announcedSize > 0 && (size_t)announcedSize > maxBody) {
        errorOut = "Antwort zu gross";
        http.end();
        return false;
    }

    NetworkClient* stream = http.getStreamPtr();
    if (!stream) {
        errorOut = "Kein Datenstrom";
        http.end();
        return false;
    }

    static constexpr uint32_t READ_IDLE_TIMEOUT_MS = 700;

    body = "";
    body.reserve(1536);
    uint32_t lastProgress = millis();

    while (millis() - lastProgress < READ_IDLE_TIMEOUT_MS &&
           body.length() <= maxBody) {
        bool progressed = false;
        while (stream->available() && body.length() <= maxBody) {
            body += (char)stream->read();
            progressed = true;
        }

        if (progressed)
            lastProgress = millis();

        if (!http.connected() && !stream->available())
            break;

        delay(1);
    }

    http.end();

    if (body.length() > maxBody) {
        body = "";
        errorOut = "Antwort zu gross";
        return false;
    }

    if (!body.length()) {
        errorOut = "Leere Antwort";
        return false;
    }

    return true;
}

bool PowerMeterClient::loadHistory(const String& rawHost,
                                   const String& adminPassword,
                                   PowerMeterHistoryView& out,
                                   String& errorOut) {
    if (!adminPassword.length()) {
        errorOut = "IR-Tracker Admin-Passwort fehlt";
        return false;
    }
    String hostOnly;
    if (!normalizeHost(rawHost, hostOnly)) {
        errorOut = "Stromzaehler-Adresse ungueltig";
        return false;
    }
    const String base = "http://" + hostOnly;
    memset(&out, 0, sizeof(out));
    out.averageImportKwhPerDay = NAN;
    out.averageExportKwhPerDay = NAN;
    PowerMeterHistoryView& loaded = out;

    String summary;
    if (requestJson(base + "/api/v1/dashboard-summary", summary, errorOut,
                    adminPassword, 4096, 2500)) {
        jsonNumber(summary, "year_daily_average_import_kwh",
                   loaded.averageImportKwhPerDay);
        jsonNumber(summary, "year_daily_average_export_kwh",
                   loaded.averageExportKwhPerDay);
    }

    auto parseRange = [&](const char* range, bool hourly) -> bool {
        String body;
        String requestError;
        if (!requestJson(base + "/api/v1/history?range=" + String(range),
                         body, requestError, adminPassword, 110000, 4500)) {
            errorOut = requestError;
            return false;
        }
        float previousImport = NAN, previousExport = NAN;
        uint32_t previousBucket = 0;
        float bucketImportWh = 0, bucketExportWh = 0;
        bool haveBucket = false;
        auto appendBucket = [&](float importWh, float exportWh) {
            float* imports = hourly ? loaded.hourlyImportWh : loaded.dailyImportWh;
            float* exports = hourly ? loaded.hourlyExportWh : loaded.dailyExportWh;
            uint16_t& count = hourly ? loaded.hourlyCount : loaded.dailyCount;
            const uint16_t capacity = hourly
                ? PowerMeterHistoryView::HOURLY_CAPACITY
                : PowerMeterHistoryView::DAILY_CAPACITY;
            if (count >= capacity) {
                memmove(imports, imports + 1, (capacity - 1) * sizeof(float));
                memmove(exports, exports + 1, (capacity - 1) * sizeof(float));
                count = capacity - 1;
            }
            imports[count] = max(0.0f, importWh);
            exports[count] = max(0.0f, exportWh);
            ++count;
        };

        int cursor = body.indexOf("\"values\"");
        while (cursor >= 0) {
            int begin = body.indexOf('{', cursor);
            int end = begin >= 0 ? body.indexOf('}', begin + 1) : -1;
            if (begin < 0 || end < 0) break;
            String item = body.substring(begin, end + 1);
            float timestamp = NAN, importKwh = NAN, exportKwh = NAN;
            jsonNumber(item, "ts", timestamp);
            jsonNumber(item, "import", importKwh);
            jsonNumber(item, "export", exportKwh);
            if (isfinite(timestamp) && isfinite(importKwh) && isfinite(exportKwh)) {
                uint32_t bucket = (uint32_t)timestamp / (hourly ? 3600UL : 86400UL);
                if (haveBucket && bucket != previousBucket) {
                    appendBucket(bucketImportWh, bucketExportWh);
                    bucketImportWh = bucketExportWh = 0;
                }
                if (isfinite(previousImport) && importKwh >= previousImport)
                    bucketImportWh += (importKwh - previousImport) * 1000.0f;
                if (isfinite(previousExport) && exportKwh >= previousExport)
                    bucketExportWh += (exportKwh - previousExport) * 1000.0f;
                previousImport = importKwh;
                previousExport = exportKwh;
                previousBucket = bucket;
                haveBucket = true;
            }
            cursor = end + 1;
        }
        // The final API bucket is the still-running local hour/day. Keep it
        // separate so the display can anchor day at 00:00 and week at Monday
        // 00:00 without counting the current bucket twice.
        if (haveBucket) {
            if (hourly) {
                loaded.currentHourImportWh = max(0.0f, bucketImportWh);
                loaded.currentHourExportWh = max(0.0f, bucketExportWh);
            } else {
                loaded.currentDayImportWh = max(0.0f, bucketImportWh);
                loaded.currentDayExportWh = max(0.0f, bucketExportWh);
            }
        }
        return hourly ? loaded.hourlyCount > 0 : loaded.dailyCount > 0;
    };

    bool weekOk = parseRange("week", true);
    bool yearOk = parseRange("year", false);
    if (!weekOk && !yearOk) return false;
    loaded.loadedFromTracker = true;
    errorOut = "";
    return true;
}

bool PowerMeterClient::poll(const String& rawHost,
                            PowerMeterData& out,const String& password) {
    PowerMeterData next;

    String hostOnly;
    if (!normalizeHost(rawHost, hostOnly)) {
        next.error = "Stromzaehler-Adresse ungueltig";
        out = next;
        return false;
    }

    const String host = "http://" + hostOnly;

    String body;
    String err;

    // Prefer the IR-Tracker's native status schema. It carries live phase
    // values and both cumulative meter readings in one request. The Shelly
    // compatibility endpoint intentionally does not always expose counters.
    bool gotNative = requestJson(host + "/api/v1/status", body, err, password, 12288, 1200);
    if (gotNative) {
        next.nativeStatus = true;
        const char* pt[] = {"power_w", "total_power", "total_act_power"};
        const char* ei[] = {"import_kwh", "total_import_kwh", "energy_import_kwh"};
        const char* ee[] = {"export_kwh", "total_export_kwh", "energy_export_kwh"};
        firstNumber(body, pt, 3, next.totalPower);
        firstNumber(body, ei, 3, next.totalImportKwh);
        firstNumber(body, ee, 3, next.totalExportKwh);

        for (int phase = 0; phase < 3; ++phase) {
            String item;
            const String phaseName = "L" + String(phase + 1);
            if (!taggedObject(body, "phase", phaseName.c_str(), item)) continue;
            jsonNumber(item, "voltage_v", next.voltage[phase]);
            jsonNumber(item, "current_a", next.current[phase]);
            jsonNumber(item, "power_w", next.power[phase]);
        }
    }

    bool phasesComplete = true;
    for (int i = 0; i < 3; ++i)
        phasesComplete = phasesComplete && isfinite(next.voltage[i]) &&
                         isfinite(next.current[i]) && isfinite(next.power[i]);

    // Shelly RPC is a compatibility fallback and phase-data supplement only.
    // Never let a successful RPC response hide missing native meter counters.
    bool gotRpc = false;
    if (!gotNative || !phasesComplete) {
        String rpcBody;
        String rpcError;
        gotRpc = requestJson(host + "/rpc/EM.GetStatus?id=0", rpcBody, rpcError,password);
        if (gotRpc) body = rpcBody;
        else if (!gotNative) err += " / " + rpcError;
    }

    if (gotRpc) {
        jsonNumber(body, "a_voltage", next.voltage[0]);
        jsonNumber(body, "a_current", next.current[0]);
        jsonNumber(body, "a_act_power", next.power[0]);
        jsonNumber(body, "b_voltage", next.voltage[1]);
        jsonNumber(body, "b_current", next.current[1]);
        jsonNumber(body, "b_act_power", next.power[1]);
        jsonNumber(body, "c_voltage", next.voltage[2]);
        jsonNumber(body, "c_current", next.current[2]);
        jsonNumber(body, "c_act_power", next.power[2]);

        const char* totalKeys[] = {
            "total_act_power", "total_power", "act_power"
        };
        firstNumber(body, totalKeys,
                    sizeof(totalKeys) / sizeof(totalKeys[0]),
                    next.totalPower);
        float importWh = NAN;
        float exportWh = NAN;
        if (jsonNumber(body, "total_act_energy", importWh))
            next.totalImportKwh = importWh / 1000.0f;
        if (jsonNumber(body, "total_act_ret_energy", exportWh))
            next.totalExportKwh = exportWh / 1000.0f;
    }

    sanitizeData(next);
    next.countersValid = isfinite(next.totalImportKwh) &&
                         isfinite(next.totalExportKwh);

    if (!isfinite(next.totalPower)) {
        float phaseSum = 0.0f;
        bool hasPhasePower = false;
        for (int i = 0; i < 3; ++i) {
            if (isfinite(next.power[i])) {
                phaseSum += next.power[i];
                hasPhasePower = true;
            }
        }
        if (hasPhasePower)
            next.totalPower = phaseSum;
    }

    bool any = isfinite(next.totalPower);
    for (int i = 0; i < 3; ++i)
        any = any || isfinite(next.voltage[i]) || isfinite(next.current[i]) ||
              isfinite(next.power[i]);

    const bool got = gotNative || gotRpc;
    next.online = got && any;
    next.updatedMs = millis();

    if (!next.online)
        next.error = got ? "Keine L1-L3 Messwerte" : err;

    out = next;
    return next.online;
}

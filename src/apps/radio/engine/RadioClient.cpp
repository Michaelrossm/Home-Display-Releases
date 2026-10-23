#include "RadioClient.h"
#include <HTTPClient.h>
#include <NetworkClient.h>

void RadioClient::configure(String host, String pin) {
    host.trim();
    pin.trim();

    if (host.startsWith("http://")) host.remove(0, 7);
    if (host.startsWith("https://")) host.remove(0, 8);

    int slash = host.indexOf('/');
    if (slash >= 0) host = host.substring(0, slash);

    int q = host.indexOf('?');
    if (q >= 0) host = host.substring(0, q);

    host.trim();

    // Keep only a plausible IPv4/hostname/port string.
    // This also prevents accidental paths from being injected into FSAPI URLs.
    if (host.length() > 253)
        host = "";

    _host = host;
    _pin = pin.length() ? pin : "1234";
}

bool RadioClient::request(const String& path, String& response, uint16_t timeoutMs,bool replaySafe) {
    response = "";
    if (!_host.length()) {
        _lastError = "Radio nicht konfiguriert";
        return false;
    }

    NetworkClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(timeoutMs);
    http.setTimeout(timeoutMs);

    String url = "http://" + _host + path;
    if (!http.begin(client, url)) {
        _lastError = "HTTP Initialisierung";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();

        bool transient =
            code <= 0 || code == 408 || code == 429 || code >= 500;

        if (!transient || !replaySafe) {
            _lastError = "Radio HTTP " + String(code);
            return false;
        }

        delay(45);

        NetworkClient retryClient;
        HTTPClient retry;
        retry.setReuse(false);
        retry.setConnectTimeout(timeoutMs);
        retry.setTimeout(timeoutMs);

        if (!retry.begin(retryClient, url)) {
            _lastError = "Radio HTTP Retry Init";
            return false;
        }

        code = retry.GET();
        if (code != HTTP_CODE_OK) {
            _lastError = "Radio HTTP " + String(code);
            retry.end();
            return false;
        }

        int retryLength = retry.getSize();
        if (retryLength > 65536) {
            _lastError = "Radio Antwort zu gross";
            retry.end();
            return false;
        }

        if (retryLength > 0)
            response.reserve(min(retryLength + 1, 65536));

        response = retry.getString();
        retry.end();

        if (!isOk(response)) {
            _lastError = "FSAPI Fehler";
            return false;
        }

        _lastError = "";
        return true;
    }

    int contentLength = http.getSize();
    if (contentLength > 65536) {
        _lastError = "Radio Antwort zu gross";
        http.end();
        return false;
    }

    if (contentLength > 0)
        response.reserve(min(contentLength + 1, 65536));

    response = http.getString();
    http.end();

    if (!isOk(response)) {
        const int start=response.indexOf("<status>"),end=response.indexOf("</status>");
        String status = start>=0&&end>start?response.substring(start+8,end):String();
        _lastError = status.length() ? status : "FSAPI Fehler";
        return false;
    }

    _lastError = "";
    return true;
}

bool RadioClient::apiGet(const String& node, String& response) {
    return request("/fsapi/GET/" + node + "?pin=" + urlEncode(_pin), response);
}

bool RadioClient::apiSet(const String& node, const String& value) {
    String response;
    return request("/fsapi/SET/" + node + "?pin=" + urlEncode(_pin) +
                   "&value=" + urlEncode(value), response,1800,false);
}

bool RadioClient::listGetNext(const String& node, String& response, int maxItems, int offset) {
    int key = offset < 0 ? -1 : offset;
    return request("/fsapi/LIST_GET_NEXT/" + node + "/" + String(key) +
                   "?pin=" + urlEncode(_pin) + "&maxItems=" + String(maxItems),
                   response, 1900);
}

bool RadioClient::ping() {
    String xml;
    return apiGet("netRemote.sys.info.friendlyName", xml);
}

bool RadioClient::pollCore(RadioState& out) {
    String xml;

    if (!apiGet("netRemote.sys.power", xml)) {
        // Offline debouncing is handled by the owner task. One lost packet
        // must never flip the visible state immediately.
        out.error = _lastError;
        return false;
    }

    out.online = true;
    out.power = xmlValue(xml).toInt() != 0;

    if (apiGet("netRemote.sys.audio.volume", xml))
        out.volume = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.caps.volumeSteps", xml)) {
        int steps = xmlValue(xml).toInt();
        if (steps >= 2 && steps <= 255) out.volumeSteps = steps;
    }

    if (apiGet("netRemote.sys.audio.mute", xml))
        out.mute = xmlValue(xml).toInt() != 0;

    if (apiGet("netRemote.sys.mode", xml))
        out.mode = xmlValue(xml).toInt();

    if (!out.friendlyName.length() && apiGet("netRemote.sys.info.friendlyName", xml))
        out.friendlyName = xmlValue(xml);

    out.error = "";
    return true;
}

bool RadioClient::pollNowPlaying(RadioState& out) {
    if (!out.online) return false;
    const String previousStation=out.station,previousArtist=out.artist,previousAlbum=out.album;
    String xml;
    if (apiGet("netRemote.play.status", xml))
        out.playStatus = xmlValue(xml).toInt();

    if (apiGet("netRemote.play.info.name", xml)) out.station = xmlValue(xml);
    if (apiGet("netRemote.play.info.text", xml)) out.text = xmlValue(xml);
    if (apiGet("netRemote.play.info.artist", xml)) out.artist = xmlValue(xml);
    if (apiGet("netRemote.play.info.album", xml)) out.album = xmlValue(xml);
    if (apiGet("netRemote.play.info.graphicUri", xml)) out.graphicUri = xmlValue(xml);
    else if(out.station!=previousStation||out.artist!=previousArtist||out.album!=previousAlbum)out.graphicUri="";

    return true;
}

bool RadioClient::setPower(bool on) {
    return apiSet("netRemote.sys.power", on ? "1" : "0");
}

bool RadioClient::setMute(bool on) {
    return apiSet("netRemote.sys.audio.mute", on ? "1" : "0");
}

bool RadioClient::setVolume(int value) {
    value = constrain(value, 0, 255);
    return apiSet("netRemote.sys.audio.volume", String(value));
}

bool RadioClient::setMode(int modeKey) {
    return apiSet("netRemote.sys.mode", String(modeKey));
}

bool RadioClient::selectPreset(int presetKey) {
    if (!apiSet("netRemote.nav.state", "1")) return false;
    delay(90);
    return apiSet("netRemote.nav.action.selectPreset", String(presetKey));
}

bool RadioClient::getFmFrequency(uint32_t& frequencyKhz) {
    String xml;
    if (!apiGet("netRemote.play.frequency", xml)) return false;
    frequencyKhz = (uint32_t)xmlValue(xml).toInt();
    return frequencyKhz >= 50000UL && frequencyKhz <= 120000UL;
}

bool RadioClient::getFmRange(uint32_t& lowerKhz, uint32_t& upperKhz,
                             uint32_t& stepKhz) {
    String xml;
    if (!apiGet("netRemote.sys.caps.fmFreqRange.lower", xml)) return false;
    lowerKhz = (uint32_t)xmlValue(xml).toInt();
    if (!apiGet("netRemote.sys.caps.fmFreqRange.upper", xml)) return false;
    upperKhz = (uint32_t)xmlValue(xml).toInt();
    if (!apiGet("netRemote.sys.caps.fmFreqRange.stepSize", xml)) return false;
    stepKhz = (uint32_t)xmlValue(xml).toInt();
    return lowerKhz >= 50000UL && upperKhz > lowerKhz && stepKhz > 0;
}

bool RadioClient::tuneFm(uint32_t frequencyKhz) {
    return apiSet("netRemote.play.frequency", String(frequencyKhz));
}

bool RadioClient::getStationName(String& name) {
    String xml;
    if (!apiGet("netRemote.play.info.name", xml)) return false;
    name = xmlValue(xml);
    name.trim();
    return true;
}

int RadioClient::loadModes(RadioMode* target, int maxItems) {
    if (!target || maxItems < 1) return 0;

    String xml;
    if (!listGetNext("netRemote.sys.caps.validModes", xml, maxItems)) return 0;

    int count = 0;
    int pos = 0;

    while (count < maxItems) {
        int start = xml.indexOf("<item", pos);
        if (start < 0) break;

        int end = xml.indexOf("</item>", start);
        if (end < 0) break;

        String item = xml.substring(start, end + 7);

        int key = -1;
        int kp = item.indexOf("key=\"");
        if (kp >= 0) {
            kp += 5;
            int ke = item.indexOf('"', kp);
            if (ke > kp) key = item.substring(kp, ke).toInt();
        }

        String selectable = fieldValue(item, "selectable");
        if (selectable.toInt() != 0) {
            target[count].key = key;
            target[count].id = fieldValue(item, "id");
            target[count].label = fieldValue(item, "label");
            if (!target[count].label.length())
                target[count].label = target[count].id;
            target[count].selectable = true;
            ++count;
        }

        pos = end + 7;
    }

    return count;
}

int RadioClient::loadPresets(RadioPreset* target, int maxItems, int offset) {
    if (!target || maxItems < 1) return 0;

    apiSet("netRemote.nav.state", "1");
    delay(70);

    String xml;
    if (!listGetNext("netRemote.nav.presets", xml, maxItems, offset)) return 0;

    int count = 0;
    int pos = 0;

    while (count < maxItems) {
        int start = xml.indexOf("<item", pos);
        if (start < 0) break;

        int end = xml.indexOf("</item>", start);
        if (end < 0) break;

        String item = xml.substring(start, end + 7);

        int key = -1;
        int kp = item.indexOf("key=\"");
        if (kp >= 0) {
            kp += 5;
            int ke = item.indexOf('"', kp);
            if (ke > kp) key = item.substring(kp, ke).toInt();
        }

        String name = fieldValue(item, "name");
        if (name.length()) {
            target[count].key = key;
            target[count].name = name;
            ++count;
        }

        pos = end + 7;
    }

    return count;
}

String RadioClient::urlEncode(const String& value) {
    const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(value.length() * 3);

    for (size_t i = 0; i < value.length(); ++i) {
        uint8_t c = (uint8_t)value[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[(c >> 4) & 0x0F];
            out += hex[c & 0x0F];
        }
    }

    return out;
}

bool RadioClient::isOk(const String& xml) {
    return xml.indexOf("<status>FS_OK</status>") >= 0;
}

String RadioClient::xmlValue(const String& xml) {
    const char* tags[] = {
        "c8_array", "u8", "u16", "u32",
        "s8", "s16", "s32", "boolean"
    };

    for (auto tag : tags) {
        String open = "<" + String(tag) + ">";
        String close = "</" + String(tag) + ">";

        int a = xml.indexOf(open);
        if (a >= 0) {
            a += open.length();
            int b = xml.indexOf(close, a);
            if (b >= a) return decodeXml(xml.substring(a, b));
        }
    }

    int a = xml.indexOf("<status>");
    if (a >= 0) {
        a += 8;
        int b = xml.indexOf("</status>", a);
        if (b > a) return xml.substring(a, b);
    }

    return "";
}

String RadioClient::fieldValue(const String& itemXml, const String& fieldName) {
    String marker = "<field name=\"" + fieldName + "\">";
    int a = itemXml.indexOf(marker);
    if (a < 0) return "";

    a += marker.length();
    int b = itemXml.indexOf("</field>", a);
    if (b < 0) return "";

    return xmlValue(itemXml.substring(a, b));
}

String RadioClient::decodeXml(String s) {
    s.replace("&amp;", "&");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&quot;", "\"");
    s.replace("&apos;", "'");
    return s;
}


bool RadioClient::playControl(int command) {
    command = constrain(command, 1, 4);
    return apiSet("netRemote.play.control", String(command));
}

bool RadioClient::pollSettings(RadioState& out) {
    if (!out.online) return false;

    String xml;

    if (apiGet("netRemote.sys.sleep", xml))
        out.sleepSeconds = (uint32_t)xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.audio.eqPreset", xml))
        out.eqPreset = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.audio.eqCustom.param0", xml))
        out.eqBass = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.audio.eqCustom.param1", xml))
        out.eqTreble = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.audio.eqLoudness", xml))
        out.eqLoudness = xmlValue(xml).toInt() != 0;

    if (apiGet("netRemote.sys.clock.mode", xml))
        out.clockMode = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.clock.source", xml))
        out.clockSource = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.clock.utcOffset", xml))
        out.utcOffset = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.lang", xml))
        out.language = xmlValue(xml).toInt();

    if (apiGet("netRemote.sys.clock.localDate", xml))
        out.localDate = xmlValue(xml);

    if (apiGet("netRemote.sys.clock.localTime", xml))
        out.localTime = xmlValue(xml);

    if (apiGet("netRemote.sys.info.version", xml))
        out.softwareVersion = xmlValue(xml);

    return true;
}

bool RadioClient::setSleep(uint32_t seconds) {
    return apiSet("netRemote.sys.sleep", String(seconds));
}

bool RadioClient::setEqPreset(int key) {
    return apiSet("netRemote.sys.audio.eqPreset", String(key));
}

bool RadioClient::setEqBass(int value) {
    value = constrain(value, -7, 7);
    return apiSet("netRemote.sys.audio.eqCustom.param0", String(value));
}

bool RadioClient::setEqTreble(int value) {
    value = constrain(value, -7, 7);
    return apiSet("netRemote.sys.audio.eqCustom.param1", String(value));
}

bool RadioClient::setEqLoudness(bool on) {
    return apiSet("netRemote.sys.audio.eqLoudness", on ? "1" : "0");
}

bool RadioClient::setClockMode(int mode) {
    return apiSet("netRemote.sys.clock.mode", mode ? "1" : "0");
}

bool RadioClient::setClockSource(int source) {
    return apiSet("netRemote.sys.clock.source", String(source));
}

bool RadioClient::setLanguage(int language) {
    return apiSet("netRemote.sys.lang", String(language));
}

static int parseChoiceList(const String& xml, RadioChoice* target,
                           int maxItems, const String& preferredField) {
    if (!target || maxItems <= 0) return 0;

    int count = 0;
    int pos = 0;

    while (count < maxItems) {
        int start = xml.indexOf("<item", pos);
        if (start < 0) break;

        int end = xml.indexOf("</item>", start);
        if (end < 0) break;

        String item = xml.substring(start, end + 7);

        int key = count;
        int kp = item.indexOf("key=\"");
        if (kp >= 0) {
            kp += 5;
            int ke = item.indexOf('"', kp);
            if (ke > kp) key = item.substring(kp, ke).toInt();
        }

        String label;
        String marker = "name=\"" + preferredField + "\"";
        int fp = item.indexOf(marker);

        if (fp >= 0) {
            int valueStart = item.indexOf("<c8_array>", fp);
            if (valueStart >= 0) {
                valueStart += 10;
                int valueEnd = item.indexOf("</c8_array>", valueStart);
                if (valueEnd > valueStart)
                    label = item.substring(valueStart, valueEnd);
            }
        }

        if (!label.length()) {
            int valueStart = item.indexOf("<c8_array>");
            if (valueStart >= 0) {
                valueStart += 10;
                int valueEnd = item.indexOf("</c8_array>", valueStart);
                if (valueEnd > valueStart)
                    label = item.substring(valueStart, valueEnd);
            }
        }

        label.replace("&amp;", "&");
        label.replace("&quot;", "\"");
        label.replace("&apos;", "'");
        label.replace("&lt;", "<");
        label.replace("&gt;", ">");
        label.trim();

        target[count].key = key;
        target[count].label = label.length() ? label : ("Eintrag " + String(key));
        ++count;
        pos = end + 7;
    }

    return count;
}

int RadioClient::loadEqPresets(RadioChoice* target, int maxItems) {
    String xml;
    if (!listGetNext("netRemote.sys.caps.eqPresets", xml, maxItems))
        return 0;
    return parseChoiceList(xml, target, maxItems, "label");
}

int RadioClient::loadLanguages(RadioChoice* target, int maxItems) {
    String xml;
    if (!listGetNext("netRemote.sys.caps.validLang", xml, maxItems))
        return 0;
    return parseChoiceList(xml, target, maxItems, "langlabel");
}

int RadioClient::loadClockSources(RadioChoice* target, int maxItems) {
    String xml;
    if (!listGetNext("netRemote.sys.caps.clockSourceList", xml, maxItems))
        return 0;
    return parseChoiceList(xml, target, maxItems, "source");
}

bool RadioClient::waitNavReady(uint32_t timeoutMs) {
    uint32_t start = millis();
    String xml;

    while (millis() - start < timeoutMs) {
        if (apiGet("netRemote.nav.status", xml)) {
            int status = xmlValue(xml).toInt();
            if (status != 0)
                return true;
        }
        delay(90);
    }

    _lastError = "Radio-Menue Timeout";
    return false;
}

bool RadioClient::navBegin() {
    // Generic DAB/FM/UNDOK browse path: do not force a 0 -> 1 reset.
    // This preserves the radio's normal navigation state and avoids DAB
    // regressions introduced by the Internet-Radio-specific reset.
    return apiSet("netRemote.nav.state", "1");
}

bool RadioClient::navRestartForInternet() {
    // Internet-Radio search gets its own clean Frontier navigation session.
    apiSet("netRemote.nav.state", "0");
    delay(60);

    if (!apiSet("netRemote.nav.state", "1"))
        return false;

    return waitNavReady(4000);
}

int RadioClient::loadNav(RadioNavItem* target, int maxItems, int offset) {
    if (!target || maxItems < 1) return 0;

    String xml;
    if (!listGetNext("netRemote.nav.list", xml, maxItems, offset))
        return 0;

    int count = 0;
    int pos = 0;

    while (count < maxItems) {
        int start = xml.indexOf("<item", pos);
        if (start < 0) break;

        int end = xml.indexOf("</item>", start);
        if (end < 0) break;

        String item = xml.substring(start, end + 7);

        int key = -1;
        int kp = item.indexOf("key=\"");
        if (kp >= 0) {
            kp += 5;
            int ke = item.indexOf('"', kp);
            if (ke > kp) key = item.substring(kp, ke).toInt();
        }

        target[count].key = key;
        target[count].name = fieldValue(item, "name");
        target[count].type = fieldValue(item, "type").toInt();
        target[count].subtype = fieldValue(item, "subtype").toInt();

        if (!target[count].name.length())
            target[count].name = "Eintrag " + String(key);

        ++count;
        pos = end + 7;
    }

    return count;
}

int RadioClient::loadNavAll(RadioNavItem* target, int maxItems) {
    if (!target || maxItems < 1) return 0;

    int total = 0;
    int lastKey = -1;
    // Frontier accepts 32 list items per request on this radio. Doubling the
    // page size halves the HTTP round trips for long genre/station lists.
    const int chunk = 32;

    while (total < maxItems) {
        int want = min(chunk, maxItems - total);
        RadioNavItem page[32];

        int n = loadNav(page, want, lastKey);
        if (n <= 0) break;

        int appended = 0;
        for (int i = 0; i < n && total < maxItems; ++i) {
            if (total > 0 &&
                page[i].key == target[total - 1].key &&
                page[i].name == target[total - 1].name) {
                continue;
            }

            target[total++] = page[i];
            appended++;
            lastKey = page[i].key;
        }

        if (appended == 0 || n < want)
            break;

        delay(10);
    }

    return total;
}

static int stationMatchScore(const String& wantedRaw,
                             const String& candidateRaw);

int RadioClient::searchStations(const String& searchTerm,
                                RadioNavItem* target, int maxItems) {
    if (!target || maxItems < 1) return 0;

    String term = searchTerm;
    term.trim();
    if (!term.length() || term.length() > 96) {
        _lastError = "Suchbegriff ungueltig";
        return 0;
    }

    if (!navRestartForInternet()) return 0;

    // This Fitzwilliam firmware accepts searchTerm directly on a freshly
    // restarted Internet-Radio root. Prefer that path: navigating the visible
    // type-2 "Search stations" node can acknowledge the command while keeping
    // the previous result list active.
    if (apiSet("netRemote.nav.searchTerm", term)) {
        waitNavReady(4000);
        int directCount = loadNavAll(target, maxItems);
        bool hasPlayableResult = false;
        for (int i = 0; i < directCount; ++i) {
            if (target[i].type != 0 &&
                stationMatchScore(term, target[i].name) >= 68) {
                hasPlayableResult = true;
                break;
            }
        }
        if (hasPlayableResult) {
            _lastError = "";
            return directCount;
        }
    }

    if (!navRestartForInternet()) return 0;

    RadioNavItem menu[16];
    int n = loadNavAll(menu, 16);
    int stationsFolder = -1;
    for (int i = 0; i < n; ++i) {
        String name = menu[i].name;
        name.toLowerCase();
        if (menu[i].type == 0 &&
            (name == "stations" || name == "sender" ||
             name == "stationen" || name == "senderliste")) {
            stationsFolder = i;
            break;
        }
    }

    if (stationsFolder >= 0) {
        if (!navSelect(menu[stationsFolder])) return 0;
        n = loadNavAll(menu, 16);

        int searchNode = -1;
        for (int i = 0; i < n; ++i) {
            String name = menu[i].name;
            name.toLowerCase();
            if (menu[i].type == 2 || name.indexOf("search") >= 0 ||
                name.indexOf("such") >= 0) {
                searchNode = i;
                break;
            }
        }

        if (searchNode >= 0) {
            if (!apiSet("netRemote.nav.action.navigate",
                        String(menu[searchNode].key)))
                return 0;
            delay(120);
        }
    }

    if (!apiSet("netRemote.nav.searchTerm", term)) return 0;
    waitNavReady(4000);
    int loaded = loadNavAll(target, maxItems);
    int count = 0;
    for (int i = 0; i < loaded; ++i) {
        if (stationMatchScore(term, target[i].name) >= 68)
            target[count++] = target[i];
    }
    if (count <= 0) _lastError = "Keine Suchergebnisse";
    else _lastError = "";
    return count;
}

static String normalizeStationName(String s) {
    s.toLowerCase();
    s.replace("!", "");
    s.replace("-", " ");
    s.replace("_", " ");
    while (s.indexOf("  ") >= 0) s.replace("  ", " ");
    s.trim();
    return s;
}

static String stationKey(String s) {
    s = normalizeStationName(s);

    // Frontier catalogues often decorate station names with region,
    // codec/quality or punctuation. Strip separators and whitespace so
    // "BAYERN 3", "BAYERN3", "BAYERN 3 (BR)" etc. still match.
    String out;
    out.reserve(s.length());

    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            out += c;
        }
    }

    return out;
}

static int stationMatchScore(const String& wantedRaw,
                             const String& candidateRaw) {
    String wanted = normalizeStationName(wantedRaw);
    String candidate = normalizeStationName(candidateRaw);
    String wantedKey = stationKey(wantedRaw);
    String candidateKey = stationKey(candidateRaw);

    if (!wanted.length() || !candidate.length())
        return -1;

    if (candidate == wanted)
        return 120;

    if (candidateKey == wantedKey && wantedKey.length())
        return 115;

    if (candidate.startsWith(wanted))
        return 100;

    if (candidate.indexOf(wanted) >= 0)
        return 92;

    if (wanted.indexOf(candidate) >= 0)
        return 84;

    if (candidateKey.startsWith(wantedKey) && wantedKey.length() >= 4)
        return 80;

    if (candidateKey.indexOf(wantedKey) >= 0 && wantedKey.length() >= 4)
        return 74;

    if (wantedKey.indexOf(candidateKey) >= 0 && candidateKey.length() >= 4)
        return 68;

    return -1;
}

// v0.38.8.1:
// These buffers are intentionally static. RadioClient is serialized by the
// radio worker mutex, so only one Internet-Radio search uses them at a time.
// Keeping 48 RadioNavItem objects (each containing Arduino String members) on
// the FreeRTOS task stack was too expensive and could reboot the ESP32.
static RadioNavItem g_irSearchResults[48];
static RadioNavItem g_irMenuItems[48];


bool RadioClient::searchAndPlayStation(const String& searchTerm) {
    _lastInternetResultCount = 0;
    String term = searchTerm;
    term.trim();

    if (!term.length()) {
        _lastError = "IR00 Leerer Suchbegriff";
        return false;
    }

    const uint32_t overallStart = millis();
    const uint32_t overallBudgetMs = 14000UL;

    auto timeLeft = [&]() -> bool {
        return millis() - overallStart < overallBudgetMs;
    };

    auto searchCurrent = [&](const String& stage,
                             RadioNavItem* result,
                             int& count) -> bool {
        count = 0;

        if (!timeLeft()) {
            _lastError = "IR40 Zeitlimit";
            return false;
        }

        if (!apiSet("netRemote.nav.searchTerm", term)) {
            String xml;
            String navErr;
            if (apiGet("netRemote.nav.errorStr", xml))
                navErr = xmlValue(xml);

            _lastError = stage + " searchTerm";
            if (navErr.length())
                _lastError += ": " + navErr;
            return false;
        }

        waitNavReady(2500);
        count = loadNavAll(result, 48);

        if (result == g_irSearchResults)
            _lastInternetResultCount = max(0, count);

        if (count <= 0) {
            String xml;
            String navErr;
            if (apiGet("netRemote.nav.errorStr", xml))
                navErr = xmlValue(xml);

            _lastError = stage + " keine Ergebnisse";
            if (navErr.length())
                _lastError += ": " + navErr;
            return false;
        }

        return true;
    };

    auto bestPlayable = [&](RadioNavItem* items,
                            int n,
                            int& best,
                            int& bestScore) {
        best = -1;
        bestScore = -1;

        for (int i = 0; i < n; ++i) {
            if (items[i].type == 0)
                continue;

            int score = stationMatchScore(term, items[i].name);
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
    };

    auto verifyPlayback = [&](const String& selectedName,
                              const String& previousName,
                              const String& stage) -> bool {
        String xml;
        uint32_t started = millis();

        while (millis() - started < 4500 && timeLeft()) {
            String nowName;

            if (apiGet("netRemote.play.info.name", xml)) {
                nowName = xmlValue(xml);
                nowName.trim();
            }

            bool changed =
                nowName.length() &&
                (!previousName.length() || nowName != previousName);

            bool matches =
                nowName.length() &&
                (stationMatchScore(selectedName, nowName) >= 68 ||
                 stationMatchScore(term, nowName) >= 68);

            if (changed || matches) {
                _lastError = "";
                return true;
            }

            delay(180);
        }

        String playErr;
        if (apiGet("netRemote.play.errorStr", xml))
            playErr = xmlValue(xml);

        _lastError = stage + " Wiedergabe";
        if (playErr.length())
            _lastError += ": " + playErr;
        return false;
    };

    auto selectPlayable = [&](RadioNavItem* items,
                              int n,
                              const String& stage) -> bool {
        int best = -1;
        int bestScore = -1;
        bestPlayable(items, n, best, bestScore);

        if (best < 0 || bestScore < 68)
            return false;

        String xml;
        String previousName;
        if (apiGet("netRemote.play.info.name", xml)) {
            previousName = xmlValue(xml);
            previousName.trim();
        }

        String selectedName = items[best].name;

        if (!navSelect(items[best])) {
            String navErr;
            if (apiGet("netRemote.nav.errorStr", xml))
                navErr = xmlValue(xml);

            _lastError = stage + " selectItem";
            if (navErr.length())
                _lastError += ": " + navErr;
            return false;
        }

        return verifyPlayback(selectedName, previousName, stage);
    };

    auto descendSearchResultFolders = [&](RadioNavItem* items,
                                          int n,
                                          const String& stage) -> bool {
        for (int folderDepth = 0; folderDepth < 2 && timeLeft(); ++folderDepth) {
            int bestFolder = -1;
            int bestFolderScore = -1;
            int folderCount = 0;
            int onlyFolder = -1;

            for (int i = 0; i < n; ++i) {
                if (items[i].type != 0)
                    continue;

                folderCount++;
                onlyFolder = i;

                int score = stationMatchScore(term, items[i].name);
                if (score > bestFolderScore) {
                    bestFolderScore = score;
                    bestFolder = i;
                }
            }

            int chosen = -1;
            if (bestFolder >= 0 && bestFolderScore >= 68)
                chosen = bestFolder;
            else if (folderCount == 1)
                chosen = onlyFolder;

            if (chosen < 0)
                return false;

            String folderName = items[chosen].name;

            if (!navSelect(items[chosen])) {
                _lastError = stage + " Ordner: " + folderName;
                return false;
            }

            if (!waitNavReady(2500)) {
                _lastError = stage + " Ordner Timeout: " + folderName;
                return false;
            }

            n = loadNavAll(g_irMenuItems, 48);
            items = g_irMenuItems;

            if (n <= 0) {
                _lastError = stage + " Ordner leer: " + folderName;
                return false;
            }

            if (selectPlayable(items, n, stage + " RESULT"))
                return true;
        }

        return false;
    };

    if (!navRestartForInternet()) {
        _lastError = "IR41 Navigation Start: " + _lastError;
        return false;
    }

    RadioNavItem* results = g_irSearchResults;
    int resultCount = 0;

    if (searchCurrent("IR42 ROOT", results, resultCount)) {
        if (selectPlayable(results, resultCount, "IR43 ROOT"))
            return true;

        if (descendSearchResultFolders(results, resultCount, "IR44 ROOT"))
            return true;
    }

    if (!timeLeft()) {
        _lastError = "IR40 Zeitlimit";
        return false;
    }

    if (!navRestartForInternet()) {
        _lastError = "IR45 Navigation Neustart: " + _lastError;
        return false;
    }

    RadioNavItem* menu = g_irMenuItems;
    int menuCount = loadNavAll(menu, 48);

    if (menuCount <= 0) {
        _lastError = "IR46 Radio-Menue leer";
        return false;
    }

    int bestFolder = -1;
    int bestPriority = -1;

    for (int i = 0; i < menuCount; ++i) {
        if (menu[i].type != 0)
            continue;

        String name = normalizeStationName(menu[i].name);
        int priority = -1;

        if (name == "search" || name == "suche" ||
            name.indexOf("sender suchen") >= 0 ||
            name.indexOf("station suchen") >= 0)
            priority = 150;
        else if (name == "senderliste" || name == "station list")
            priority = 140;
        else if (name == "sender" || name == "stations" ||
                 name == "stationen")
            priority = 130;

        if (priority > bestPriority) {
            bestPriority = priority;
            bestFolder = i;
        }
    }

    if (bestFolder >= 0 && timeLeft()) {
        String folderName = menu[bestFolder].name;

        if (navSelect(menu[bestFolder]) && waitNavReady(2500)) {
            // Some Frontier firmwares expose a second, type-2 search node
            // below "Stations". searchTerm is accepted only after that node
            // has been opened with nav.action.navigate. Treating type 2 like
            // a playable item leaves the radio in the Stations menu and the
            // requested station never changes.
            int searchNode = -1;
            int stationMenuCount = loadNavAll(menu, 48);

            for (int i = 0; i < stationMenuCount; ++i) {
                String name = normalizeStationName(menu[i].name);
                bool namedSearch =
                    name == "search" || name == "suche" ||
                    name.indexOf("sender suchen") >= 0 ||
                    name.indexOf("station suchen") >= 0 ||
                    name.indexOf("search station") >= 0;

                if (menu[i].type == 2 || namedSearch) {
                    searchNode = i;
                    break;
                }
            }

            if (searchNode >= 0) {
                if (!apiSet("netRemote.nav.action.navigate",
                            String(menu[searchNode].key))) {
                    _lastError = "IR47 Suchknoten: " + _lastError;
                    return false;
                }
                // A search-input node may report FS_NODE_BLOCKED until the
                // term is supplied, so do not wait for a normal list here.
                delay(120);
            }

            if (searchCurrent("IR47 " + folderName, results, resultCount)) {
                if (selectPlayable(results, resultCount, "IR48 SEARCH"))
                    return true;

                if (descendSearchResultFolders(results,
                                               resultCount,
                                               "IR49 SEARCH"))
                    return true;
            }
        }
    }

    if (!timeLeft()) {
        _lastError = "IR40 Zeitlimit";
        return false;
    }

    if (!navRestartForInternet()) {
        _lastError = "IR50 Final Navigation";
        return false;
    }

    if (searchCurrent("IR51 FINAL", results, resultCount)) {
        String sample;

        for (int i = 0; i < resultCount && sample.length() < 110; ++i) {
            if (sample.length())
                sample += " | ";

            sample += results[i].type == 0 ? "[O] " : "[S] ";
            sample += results[i].name;
        }

        _lastError = "IR52 Kein Start";
        if (sample.length())
            _lastError += ": " + sample;
        return false;
    }

    if (!_lastError.length())
        _lastError = "IR53 Internet-Suche fehlgeschlagen";

    return false;
}


int RadioClient::copyLastInternetResults(RadioNavItem* target,
                                         int maxItems) const {
    if (!target || maxItems <= 0)
        return 0;

    int n = min(_lastInternetResultCount, maxItems);

    for (int i = 0; i < n; ++i)
        target[i] = g_irSearchResults[i];

    return n;
}


bool RadioClient::navSelect(const RadioNavItem& item) {
    if (item.type == 0) {
        if (!apiSet("netRemote.nav.action.navigate", String(item.key)))
            return false;
        return waitNavReady(1800);
    }

    return apiSet("netRemote.nav.action.selectItem", String(item.key));
}

bool RadioClient::navBack() {
    if (!apiSet("netRemote.nav.action.navigate", "-1"))
        return false;
    return waitNavReady(1800);
}

#include "WeatherClient.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

namespace {
static const char ISRG_ROOT_X1[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAwTzELMAkGA1UE
BhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdyb3VwMRUwEwYDVQQD
EwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQG
EwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMT
DElTUkcgUm9vdCBYMTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54r
Vygch77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+0TM8ukj1
3Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6UA5/TR5d8mUgjU+g4rk8K
b4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sWT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCN
Aymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyHB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ
4Q7e2RCOFvu396j3x+UCB5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf
1b0SHzUvKBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWnOlFu
hjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTnjh8BCNAw1FtxNrQH
usEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbwqHyGO0aoSCqI3Haadr8faqU9GY/r
OPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CIrU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4G
A1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY
9umbbjANBgkqhkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ3BebYhtF8GaV
0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KKNFtY2PwByVS5uCbMiogziUwt
hDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJw
TdwJx4nLCgdNbOhdjsnvzqvHu7UrTkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nx
e5AW0wdeRlN8NwdCjNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZA
JzVcoyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq4RgqsahD
YVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPAmRGunUHBcnWEvgJBQl9n
JEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57demyPxgcYxn/eR44/KJ4EBs+lVDR3veyJ
m+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

bool fetch(const String& url, String& body) {
    NetworkClientSecure client;
    client.setCACert(ISRG_ROOT_X1);
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    http.setUserAgent("Fitz32-Touch/1.1.4");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, url)) return false;
    int status = http.GET();
    if (status != HTTP_CODE_OK) { http.end(); return false; }
    int length = http.getSize();
    if (length > 24576) { http.end(); return false; }
    body = http.getString();
    http.end();
    return body.length() > 20 && body.length() <= 24576;
}

String encoded(String s) {
    String out;
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < s.length(); ++i) {
        uint8_t c = s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') out += char(c);
        else if (c == ' ') out += "%20";
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

String value(const String& json, const char* key, size_t from = 0) {
    String needle = "\"" + String(key) + "\":";
    int p = json.indexOf(needle, from);
    if (p < 0) return "";
    p += needle.length();
    while (p < (int)json.length() && json[p] == ' ') ++p;
    if (p >= (int)json.length()) return "";
    if (json[p] == '"') {
        int e = json.indexOf('"', p + 1);
        return e > p ? json.substring(p + 1, e) : String();
    }
    int e = p;
    while (e < (int)json.length() && json[e] != ',' && json[e] != '}' && json[e] != ']') ++e;
    return json.substring(p, e);
}

bool validCoordinate(const String& raw, float minimum, float maximum) {
    if (!raw.length() || raw.length() > 16) return false;
    for (size_t i = 0; i < raw.length(); ++i)
        if (!isDigit(raw[i]) && raw[i] != '-' && raw[i] != '.' && raw[i] != '+')
            return false;
    float number = raw.toFloat();
    return isfinite(number) && number >= minimum && number <= maximum;
}

String arrayValue(const String& json, const char* key, int index) {
    String needle = "\"" + String(key) + "\":[";
    int p = json.indexOf(needle);
    if (p < 0) return "";
    p += needle.length();
    for (int i = 0; i < index; ++i) {
        p = json.indexOf(',', p);
        if (p < 0) return "";
        ++p;
    }
    while (p < (int)json.length() && json[p] == ' ') ++p;
    bool quoted = p < (int)json.length() && json[p] == '"';
    if (quoted) ++p;
    int e = p;
    while (e < (int)json.length() && json[e] != (quoted ? '"' : ',') &&
           json[e] != ']') ++e;
    return json.substring(p, e);
}
}

bool WeatherClient::update(const String& rawRegion, WeatherData& out) {
    String region = rawRegion;
    region.trim();
    if (region.length() < 2 || region.length() > 80) {
        out.error = "Region ungueltig";
        return false;
    }
    String geo;
    if (!fetch("https://geocoding-api.open-meteo.com/v1/search?name=" +
               encoded(region) + "&count=1&language=de&format=json", geo)) {
        out.error = "Ortssuche nicht erreichbar";
        return false;
    }
    String latitude = value(geo, "latitude");
    String longitude = value(geo, "longitude");
    String location = value(geo, "name");
    if (!validCoordinate(latitude, -90.0f, 90.0f) ||
        !validCoordinate(longitude, -180.0f, 180.0f) || !location.length()) {
        out.error = "Region nicht gefunden";
        return false;
    }
    String forecast;
    String parameters = "?latitude=" + latitude +
        "&longitude=" + longitude +
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&timezone=auto&forecast_days=4";
    bool dwdIcon = fetch("https://api.open-meteo.com/v1/dwd-icon" +
                         parameters, forecast);
    // Availability fallback only: Germany normally uses the dedicated DWD
    // seamless endpoint. The generic best-match endpoint prevents a single
    // model outage from making the display useless.
    if (!dwdIcon && !fetch("https://api.open-meteo.com/v1/forecast" +
                           parameters, forecast)) {
        out.error = "Wetterdienst nicht erreichbar";
        return false;
    }
    WeatherData fresh;
    fresh.online = true;
    fresh.region = region;
    fresh.location = location;
    fresh.provider = dwdIcon ? "DWD ICON" : "Open-Meteo Best Match";
    int currentAt = forecast.indexOf("\"current\":{");
    if (currentAt < 0) { out.error = "Wetterantwort unvollstaendig"; return false; }
    String temperature = value(forecast, "temperature_2m", currentAt);
    String apparent = value(forecast, "apparent_temperature", currentAt);
    String humidity = value(forecast, "relative_humidity_2m", currentAt);
    String weatherCode = value(forecast, "weather_code", currentAt);
    String wind = value(forecast, "wind_speed_10m", currentAt);
    fresh.temperature = temperature.toFloat();
    fresh.apparentTemperature = apparent.toFloat();
    fresh.humidity = constrain(humidity.toInt(), 0, 100);
    fresh.isDay = value(forecast, "is_day", currentAt).toInt() != 0;
    fresh.code = weatherCode.toInt();
    fresh.windKmh = wind.toFloat();
    if (!temperature.length() || !apparent.length() || !humidity.length() ||
        !weatherCode.length() || !wind.length() ||
        !isfinite(fresh.temperature) || fresh.temperature < -100.0f ||
        fresh.temperature > 70.0f || !isfinite(fresh.apparentTemperature) ||
        fresh.apparentTemperature < -120.0f || fresh.apparentTemperature > 80.0f ||
        fresh.code < 0 || fresh.code > 99 || !isfinite(fresh.windKmh) ||
        fresh.windKmh < 0.0f || fresh.windKmh > 500.0f) {
        out.error = "Wetterwerte ungueltig";
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        String date = arrayValue(forecast, "time", i);
        String code = arrayValue(forecast, "weather_code", i);
        String maximum = arrayValue(forecast, "temperature_2m_max", i);
        String minimum = arrayValue(forecast, "temperature_2m_min", i);
        String rain = arrayValue(forecast, "precipitation_probability_max", i);
        if (date.length() != 10 || !code.length() || !maximum.length() ||
            !minimum.length() || !rain.length()) {
            out.error = "Vorhersage unvollstaendig";
            return false;
        }
        fresh.days[i].date = date;
        fresh.days[i].code = code.toInt();
        fresh.days[i].maximum = maximum.toFloat();
        fresh.days[i].minimum = minimum.toFloat();
        fresh.days[i].rainProbability = constrain(
            rain.toInt(), 0, 100);
        if (fresh.days[i].code < 0 || fresh.days[i].code > 99 ||
            !isfinite(fresh.days[i].minimum) || !isfinite(fresh.days[i].maximum) ||
            fresh.days[i].minimum < -100.0f || fresh.days[i].maximum > 70.0f ||
            fresh.days[i].minimum > fresh.days[i].maximum) {
            out.error = "Vorhersage ungueltig";
            return false;
        }
    }
    fresh.updatedAt = millis();
    out = fresh;
    return true;
}

const char* WeatherClient::conditionDe(int code) {
    if (code == 0) return "Klar";
    if (code <= 3) return "Bewoelkt";
    if (code == 45 || code == 48) return "Nebel";
    if (code >= 51 && code <= 57) return "Nieselregen";
    if (code >= 61 && code <= 67) return "Regen";
    if (code >= 71 && code <= 77) return "Schnee";
    if (code >= 80 && code <= 82) return "Regenschauer";
    if (code >= 85 && code <= 86) return "Schneeschauer";
    if (code >= 95) return "Gewitter";
    return "Unbekannt";
}

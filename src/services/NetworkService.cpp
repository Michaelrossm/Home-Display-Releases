#include "NetworkService.h"
#include "SettingsService.h"
#include "ConfigService.h"
#include "MdnsService.h"
#include "PowerManager.h"
#include "IdleService.h"
#include "TimeService.h"
#include "NetworkWorker.h"
#include "BoardConfig.h"
#include "../core/EventBus.h"
#include "../core/SystemSupervisor.h"
#include "../core/EventLog.h"
#include <WiFi.h>
#include <ETH.h>
#include <esp_wifi.h>
#include <esp32-hal-hosted.h>
#include <algorithm>
#include <atomic>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define HP_WIFI_SSID ""
#define HP_WIFI_PASSWORD ""
#endif

namespace hp {
namespace {
// Written by the Arduino WiFi event task; polled by the normal network loop.
std::atomic<uint32_t> sWifiLinkGeneration{0};
bool sWifiRadioStopped = false;

void stopWifiRadio() {
    // Queued lwIP pbufs retain their esp_netif until the RX buffer is freed.
    // WIFI_OFF deinitializes/destroys it; stopping the radio preserves it.
    WiFi.disconnect(false, false);
    if (esp_wifi_stop() == ESP_OK) sWifiRadioStopped = true;
}

bool resumeWifiRadio() {
    if (!sWifiRadioStopped) return true;
    if (esp_wifi_start() != ESP_OK) return false;
    sWifiRadioStopped = false;
    return true;
}

void signalWifiLinkEvent(WiFiEvent_t) {
    sWifiLinkGeneration.fetch_add(1, std::memory_order_relaxed);
}

String ssidKey(uint8_t slot) { return "wifi_ssid_" + String(slot); }
String passwordKey(uint8_t slot) { return "wifi_pwd_" + String(slot); }
String ipKey(uint8_t slot) { return "wifi_ip_" + String(slot); }
String gatewayKey(uint8_t slot) { return "wifi_gw_" + String(slot); }
String subnetKey(uint8_t slot) { return "wifi_mask_" + String(slot); }
String dns1Key(uint8_t slot) { return "wifi_dns1_" + String(slot); }
String dns2Key(uint8_t slot) { return "wifi_dns2_" + String(slot); }

bool parseIpv4(const String& value, IPAddress& out) {
    if (!value.length()) return false;
    return out.fromString(value);
}
}

NetworkService& NetworkService::instance() {
    static NetworkService n;
    return n;
}

bool NetworkService::initHostedWifi() {
    if (!board::kC6HostedRuntimeEnabled) return false;

    if (!hostedIsInitialized()) {
        if (!hostedSetPins(
                board::kC6SdioClk, board::kC6SdioCmd,
                board::kC6SdioD0, board::kC6SdioD1,
                board::kC6SdioD2, board::kC6SdioD3,
                board::kC6Reset)) {
            EventLog::instance().add("ERROR","C6_PINS","ESP-Hosted SDIO pins could not be configured");
            return false;
        }
    }

    if (!hostedInitWiFi()) {
        EventLog::instance().add("ERROR","C6_WIFI_INIT","ESP-Hosted WiFi initialization failed");
        return false;
    }

    uint32_t hm=0,hn=0,hp=0,sm=0,sn=0,sp=0;
    hostedGetHostVersion(&hm,&hn,&hp);
    hostedGetSlaveVersion(&sm,&sn,&sp);
    EventLog::instance().add(
        "INFO","C6_WIFI_READY",
        "host="+String(hm)+"."+String(hn)+"."+String(hp)+
        " slave="+String(sm)+"."+String(sn)+"."+String(sp)
    );
    return true;
}

void NetworkService::loadWifiProfiles() {
    auto& config = ConfigService::instance();

    // Legacy single-profile migration is strictly one-shot. Otherwise a deleted
    // WLAN 1 reappears from the old wifi_ssid key on the next reboot.
    if (!config.getBool("wifi_prof_v2", false)) {
        const String oldSsid = config.getString("wifi_ssid", HP_WIFI_SSID);
        if (!config.has(ssidKey(0).c_str()) && oldSsid.length()) {
            if (config.putString(ssidKey(0).c_str(), oldSsid)) {
                config.putSecretString(passwordKey(0).c_str(),
                                       config.getSecretString("wifi_pwd", HP_WIFI_PASSWORD));
            }
        }
        config.putBool("wifi_prof_v2", true);
    }

    for (uint8_t i = 0; i < kMaxWifiProfiles; ++i) {
        const String sk = ssidKey(i);
        const String pk = passwordKey(i);
        wifiSsids_[i] = config.getString(sk.c_str(), "");
        wifiPasswords_[i] = config.getSecretString(pk.c_str(), "");
        wifiAddressing_[i].ip = config.getString(ipKey(i).c_str(), "");
        wifiAddressing_[i].gateway = config.getString(gatewayKey(i).c_str(), "");
        wifiAddressing_[i].subnet = config.getString(subnetKey(i).c_str(), "");
        wifiAddressing_[i].dns1 = config.getString(dns1Key(i).c_str(), "");
        wifiAddressing_[i].dns2 = config.getString(dns2Key(i).c_str(), "");
    }

    activeProfile_ = std::min<uint8_t>(
        config.getU8("wifi_active", 0),
        kMaxWifiProfiles - 1
    );

    if (!wifiSsids_[activeProfile_].length()) {
        const int first = nextConfiguredProfile(0);
        if (first >= 0) activeProfile_ = static_cast<uint8_t>(first);
    }

    preferredProfile_ = std::min<uint8_t>(
        config.getU8("wifi_preferred", activeProfile_), kMaxWifiProfiles - 1);
    if (!wifiSsids_[preferredProfile_].length()) {
        const int first = nextConfiguredProfile(0);
        if (first >= 0) preferredProfile_ = static_cast<uint8_t>(first);
    }
    retryProfile_ = preferredProfile_;
    if (wifiSsids_[preferredProfile_].length()) {
        activeProfile_ = preferredProfile_;
        selectRuntimeProfile(preferredProfile_);
    }
    else if (wifiSsids_[activeProfile_].length()) selectRuntimeProfile(activeProfile_);
    else {
        wifiSsid_ = "";
        wifiPassword_ = "";
        wifiConfigured_ = false;
    }
}

int NetworkService::nextConfiguredProfile(uint8_t start) const {
    for (uint8_t off = 0; off < kMaxWifiProfiles; ++off) {
        const uint8_t slot = (start + off) % kMaxWifiProfiles;
        if (wifiSsids_[slot].length()) return slot;
    }
    return -1;
}

void NetworkService::selectRuntimeProfile(uint8_t slot) {
    if (slot >= kMaxWifiProfiles) return;
    wifiSsid_ = wifiSsids_[slot];
    wifiPassword_ = wifiPasswords_[slot];
    wifiConfigured_ = wifiEnabled_ && wifiSsid_.length() > 0;
}


bool NetworkService::validateAddressing(const WifiProfileAddressing& addressing) const {
    if (!addressing.ip.length()) {
        // Empty IP means DHCP. DNS is supplied by DHCP too.
        return true;
    }

    IPAddress ip, gateway, subnet, dns1, dns2;
    if (!parseIpv4(addressing.ip, ip)) return false;
    if (!parseIpv4(addressing.gateway, gateway)) return false;
    if (!parseIpv4(addressing.subnet, subnet)) return false;
    if (addressing.dns1.length() && !parseIpv4(addressing.dns1, dns1)) return false;
    if (addressing.dns2.length() && !parseIpv4(addressing.dns2, dns2)) return false;
    return true;
}

bool NetworkService::applyProfileAddressing(uint8_t slot) {
    if (slot >= kMaxWifiProfiles) return false;
    const auto& a = wifiAddressing_[slot];

    if (!a.ip.length()) {
        const bool ok = WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        if (!ok) {
            connectionState_.text = "DHCP konnte nicht aktiviert werden.";
        }
        return ok;
    }

    IPAddress ip, gateway, subnet, dns1, dns2;
    if (!parseIpv4(a.ip, ip) ||
        !parseIpv4(a.gateway, gateway) ||
        !parseIpv4(a.subnet, subnet)) {
        connectionState_.text = "Statische IP/Gateway/Subnetz ist ungueltig.";
        return false;
    }

    if (a.dns1.length()) {
        if (!parseIpv4(a.dns1, dns1)) {
            connectionState_.text = "DNS 1 ist ungueltig.";
            return false;
        }
    } else {
        dns1 = gateway;
    }

    if (a.dns2.length() && !parseIpv4(a.dns2, dns2)) {
        connectionState_.text = "DNS 2 ist ungueltig.";
        return false;
    }

    if (!WiFi.config(ip, gateway, subnet, dns1, dns2)) {
        connectionState_.text = "Statische IP-Konfiguration wurde abgelehnt.";
        return false;
    }
    return true;
}

WifiPolicyReason NetworkService::wifiPolicyReason() const {
    if (!hostedReady_) return WifiPolicyReason::C6Unavailable;
    if (!wifiEnabled_) return WifiPolicyReason::UserDisabled;
    if (c6Maintenance_) return WifiPolicyReason::Maintenance;
    if (eco_.wifiOffBecauseSchedule) return WifiPolicyReason::Schedule;
    if (eco_.wifiOffBecauseLan) return WifiPolicyReason::EthernetPreferred;
    if (!wifiConfigured_) return WifiPolicyReason::NotConfigured;
    return WifiPolicyReason::None;
}

const char* NetworkService::wifiPolicyReasonName(WifiPolicyReason reason) {
    switch (reason) {
        case WifiPolicyReason::None: return "aktiv";
        case WifiPolicyReason::UserDisabled: return "vom Benutzer ausgeschaltet";
        case WifiPolicyReason::C6Unavailable: return "C6/ESP-Hosted nicht bereit";
        case WifiPolicyReason::Maintenance: return "C6-Wartung aktiv";
        case WifiPolicyReason::Schedule: return "Energiespar-Zeitplan";
        case WifiPolicyReason::EthernetPreferred: return "LAN bevorzugt";
        case WifiPolicyReason::NotConfigured: return "kein Profil eingerichtet";
    }
    return "?";
}

NetworkOverviewState NetworkService::overview() const {
    NetworkOverviewState result;
    result.path = path_;
    result.wifiPolicyReason = wifiPolicyReason();
    result.eco = eco_;
    result.ethernet = ethernetState_;
    result.wifi = connectionState_;
    return result;
}

void NetworkService::updateConnectionState(int wifiStatus, bool refreshDetails) {
    connectionState_.c6Ready = hostedReady_;
    connectionState_.enabled = wifiEnabled_;
    connectionState_.configured = wifiConfigured_;
    connectionState_.profile =
        currentAttemptProfile_ >= 0
            ? static_cast<uint8_t>(currentAttemptProfile_)
            : activeProfile_;
    connectionState_.ssid = wifiSsid_;
    connectionState_.statusCode = wifiStatus;
    connectionState_.staticIp =
        connectionState_.profile < kMaxWifiProfiles &&
        wifiAddressing_[connectionState_.profile].ip.length();

    const auto reason = wifiPolicyReason();
    if (reason != WifiPolicyReason::None) {
        connectionState_.connected = false;
        connectionState_.rssi = -127;
        connectionState_.text = wifiPolicyReasonName(reason);
        return;
    }

    const bool previouslyConnected = connectionState_.connected;
    connectionState_.connected = wifiStatus == WL_CONNECTED;

    switch (wifiStatus) {
        case WL_CONNECTED:
            // These values are ESP-Hosted RPCs. No need to retrieve them every
            // UI tick; update immediately on association and every 5 seconds.
            if (!refreshDetails && previouslyConnected &&
                connectionState_.ip.length()) break;
            wifiIpCache_ = WiFi.localIP();
            connectionState_.ip = wifiIpCache_.toString();
            connectionState_.gateway = WiFi.gatewayIP().toString();
            connectionState_.subnet = WiFi.subnetMask().toString();
            connectionState_.dns1 = WiFi.dnsIP(0).toString();
            connectionState_.dns2 = WiFi.dnsIP(1).toString();
            connectionState_.mac = WiFi.macAddress();
            connectionState_.bssid = WiFi.BSSIDstr();
            connectionState_.channel = WiFi.channel();
            connectionState_.rssi = WiFi.RSSI();
            eco_.wifiRssi = connectionState_.rssi;
            connectionState_.text =
                "Verbunden mit " + connectionState_.ssid +
                " | IP " + connectionState_.ip +
                " | " + String(connectionState_.rssi) + " dBm";
            break;
        case WL_NO_SSID_AVAIL: {
            connectionState_.rssi = -127;

            bool seenInLastScan = false;
            if (scanLock_ &&
                xSemaphoreTake(scanLock_, pdMS_TO_TICKS(5)) == pdTRUE) {
                for (uint8_t i = 0; i < scanCount_; ++i) {
                    if (scanResults_[i].ssid == connectionState_.ssid) {
                        seenInLastScan = true;
                        break;
                    }
                }
                xSemaphoreGive(scanLock_);
            }

            connectionState_.text = seenInLastScan
                ? "SSID wurde beim Scan gefunden. C6-Verbindung wird erneut versucht."
                : "WLAN/SSID aktuell nicht sichtbar.";
            break;
        }
        case WL_CONNECT_FAILED:
            connectionState_.rssi = -127;
            connectionState_.text =
                "Verbindung fehlgeschlagen: Passwort oder Anmeldung abgelehnt.";
            break;
        case WL_CONNECTION_LOST:
            connectionState_.rssi = -127;
            connectionState_.text = "WLAN-Verbindung verloren.";
            break;
        case WL_DISCONNECTED:
            connectionState_.rssi = -127;
            connectionState_.text =
                "Nicht verbunden - Profil " +
                String(connectionState_.profile + 1) + " wird versucht.";
            break;
        case WL_IDLE_STATUS:
        default:
            connectionState_.rssi = -127;
            connectionState_.text =
                "WLAN verbindet - Profil " +
                String(connectionState_.profile + 1) + ".";
            break;
    }
}

bool NetworkService::begin() {
    if (!scanLock_) scanLock_ = xSemaphoreCreateMutex();
    if (!scanLock_) return false;

    hostedReady_ = initHostedWifi();

    // Register only after Hosted initialization. Callback never touches the
    // Hosted transport: it simply requests a refresh on our main loop.
    if (hostedReady_) {
        WiFi.onEvent(signalWifiLinkEvent, ARDUINO_EVENT_WIFI_STA_GOT_IP);
        WiFi.onEvent(signalWifiLinkEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        WiFi.onEvent(signalWifiLinkEvent, ARDUINO_EVENT_WIFI_STA_LOST_IP);
    }
    settingsChanged();

    if (wifiAvailable() && wifiConfigured_) ensureWifiOn();
    refreshPath();
    return true;
}

void NetworkService::settingsChanged() {
    auto& config = ConfigService::instance();
    wifiEnabled_ = config.getBool("wifi_enabled", true);
    loadWifiProfiles();

    wifiConfigured_ =
        wifiAvailable() && wifiEnabled_ && wifiSsid_.length() > 0;

    if (!wifiAvailable()) {
        eco_.wifiRadioOn = false;
        eco_.wifiOffBecauseLan = false;
        return;
    }

    if (!wifiEnabled_ && WiFi.getMode() != WIFI_MODE_NULL) {
        stopWifiRadio();
        eco_.wifiRadioOn = false;
    }
    applyWifiEcoPolicy();
}

bool NetworkService::wifiAvailable() const {
    return board::kC6HostedRuntimeEnabled && hostedReady_;
}

void NetworkService::reportEthernet(bool linkUp, const IPAddress& address) {
    if (linkUp && !ethernetUp_) {
        ethernetUpSince_ = millis();
        ++ethernetReconnects_;
        EventLog::instance().add("INFO","ETH_UP",address.toString());
    }
    if (!linkUp) ethernetUpSince_ = 0;

    ethernetUp_ = linkUp;
    ethernetIp_ = linkUp ? address : IPAddress();
    ethernetState_.connected = linkUp && address != IPAddress();
    if (ethernetState_.connected) {
        ethernetState_.ip = address.toString();
        ethernetState_.gateway = ETH.gatewayIP().toString();
        ethernetState_.subnet = ETH.subnetMask().toString();
        ethernetState_.dns1 = ETH.dnsIP(0).toString();
        ethernetState_.dns2 = ETH.dnsIP(1).toString();
        ethernetState_.mac = ETH.macAddress();
        ethernetState_.linkSpeedMbps = ETH.linkSpeed();
    } else {
        ethernetState_ = {};
    }

    if (!ethernetUp_) {
        wifiRetry_.failure();
        ensureWifiOn();
    }
    refreshPath();
}

void NetworkService::ensureWifiOn() {
    bool scanning = false;
    if (scanLock_) {
        if (xSemaphoreTake(scanLock_, 0) != pdTRUE) return;
        scanning = scanActive_;
        xSemaphoreGive(scanLock_);
    }

    if (!wifiAvailable() || !wifiEnabled_ || scanning ||
        c6Maintenance_ || eco_.wifiOffBecauseSchedule || !wifiRetry_.due()) return;

    // The last periodic/event-driven status refresh is authoritative here.
    // Avoid another Hosted status RPC on every policy tick while connected.
    if (connectionState_.connected && lastWifiStatus_ == WL_CONNECTED) return;

    // Re-entry after a previously established link: try the chosen WLAN first.
    if (lastWifiStatus_ == WL_CONNECTED) retryProfile_ = preferredProfile_;
    const int slot = nextConfiguredProfile(retryProfile_);
    if (slot < 0) {
        wifiConfigured_ = false;
        return;
    }

    const uint8_t selected = static_cast<uint8_t>(slot);
    selectRuntimeProfile(selected);
    currentAttemptProfile_ = static_cast<int8_t>(selected);
    retryProfile_ = (selected + 1) % kMaxWifiProfiles;

    if (WiFi.getMode() == WIFI_MODE_NULL) {
        appliedWifiPs_ = -1;
        appliedWifiTxPower_ = -1;
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
    }

    if (!resumeWifiRadio() || !applyProfileAddressing(selected)) {
        wifiRetry_.failure();
        return;
    }

    WiFi.begin(wifiSsid_.c_str(), wifiPassword_.c_str());
    ++wifiReconnects_;
    wifiRetry_.failure();

    eco_.wifiRadioOn = true;
    eco_.wifiOffBecauseLan = false;
}

int NetworkService::scanNetworks(WifiNetwork* results, size_t capacity) {
    if (!results || !capacity) return 0;
    if (!scanLock_ || xSemaphoreTake(scanLock_, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    const size_t count = std::min<size_t>(capacity, scanCount_);
    for (size_t i = 0; i < count; ++i) results[i] = scanResults_[i];
    xSemaphoreGive(scanLock_);
    return static_cast<int>(count);
}

bool NetworkService::requestWifiScan() {
    if (!wifiAvailable() || !board::kC6HostedScanQualified) {
        if (scanLock_ && xSemaphoreTake(scanLock_, pdMS_TO_TICKS(20)) == pdTRUE) {
            scanStatus_ = hostedReady_ ? "WLAN-Scan ist deaktiviert." :
                                         "ESP32-C6/ESP-Hosted ist nicht bereit.";
            xSemaphoreGive(scanLock_);
        }
        return false;
    }

    if (!scanLock_ || xSemaphoreTake(scanLock_, pdMS_TO_TICKS(20)) != pdTRUE) return false;

    const bool accepted = wifiEnabled_ && !scanActive_ && !c6Maintenance_;
    if (accepted) {
        scanCount_ = 0;
        scanActive_ = true;
        scanStatus_ = "Suche nach WLAN-Netzen ...";
    } else {
        scanStatus_ = c6Maintenance_ ? "C6-Update laeuft." :
                      (wifiEnabled_ ? "WLAN-Suche laeuft bereits." : "WLAN ist ausgeschaltet.");
    }
    xSemaphoreGive(scanLock_);

    if (!accepted) return false;

    if (NetworkWorker::instance().submit(wifiScanJob, this, "wifi-scan")) return true;

    xSemaphoreTake(scanLock_, portMAX_DELAY);
    scanActive_ = false;
    scanStatus_ = "Netzwerk-Worker ist ausgelastet.";
    xSemaphoreGive(scanLock_);
    return false;
}

bool NetworkService::wifiScanInProgress() const {
    if (!scanLock_ || xSemaphoreTake(scanLock_, pdMS_TO_TICKS(5)) != pdTRUE) return true;
    const bool active = scanActive_;
    xSemaphoreGive(scanLock_);
    return active;
}

String NetworkService::wifiScanStatus() const {
    if (!scanLock_ || xSemaphoreTake(scanLock_, pdMS_TO_TICKS(20)) != pdTRUE)
        return "WLAN-Status nicht verfuegbar.";
    const String status = scanStatus_;
    xSemaphoreGive(scanLock_);
    return status;
}

void NetworkService::collectWifiScan(const WifiNetwork* networks, size_t count, const char* status) {
    count = std::min<size_t>(count, sizeof(scanResults_) / sizeof(scanResults_[0]));
    if (!scanLock_) return;

    xSemaphoreTake(scanLock_, portMAX_DELAY);
    for (size_t i = 0; i < count; ++i) scanResults_[i] = networks[i];
    scanCount_ = static_cast<uint8_t>(count);
    scanActive_ = false;
    scanStatus_ = status ? status :
        (count ? "WLAN-Netze gefunden." : "Keine WLAN-Netze gefunden.");
    xSemaphoreGive(scanLock_);
}

void NetworkService::wifiScanJob(void* context) {
    static_cast<NetworkService*>(context)->executeWifiScan();
}

void NetworkService::executeWifiScan() {
    if (!wifiAvailable()) {
        collectWifiScan(nullptr, 0, "ESP32-C6/ESP-Hosted ist nicht bereit.");
        return;
    }

    // IMPORTANT: while scanActive_ is true, the main NetworkService::loop()
    // performs no WiFi/ESP-Hosted RPC. Only this worker talks to the C6.
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
    }

    WiFi.scanDelete();
    if (!resumeWifiRadio()) {
        collectWifiScan(nullptr, 0, "WLAN-Funk konnte nicht gestartet werden.");
        return;
    }
    WiFi.setScanTimeout(6500);
    WiFi.setScanActiveMinTime(30);

    const int16_t started = WiFi.scanNetworks(true, false, false, 80);
    if (started != WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        collectWifiScan(nullptr, 0, "C6-Scan konnte nicht gestartet werden.");
        return;
    }

    const uint32_t deadline = millis() + 8000UL;
    int16_t found = WIFI_SCAN_RUNNING;

    while ((int32_t)(deadline - millis()) > 0) {
        found = WiFi.scanComplete();
        if (found != WIFI_SCAN_RUNNING) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (found == WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
        collectWifiScan(nullptr, 0, "C6-Scan Zeitueberschreitung.");
        return;
    }

    if (found == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        collectWifiScan(nullptr, 0, "C6-Scan fehlgeschlagen.");
        return;
    }

    if (found <= 0) {
        WiFi.scanDelete();
        collectWifiScan(nullptr, 0, "Keine WLAN-Netze gefunden.");
        return;
    }

    // Mesh systems (for example FRITZ!Mesh) advertise the same SSID from
    // several APs/repeaters. Show every SSID only once and keep the strongest
    // currently visible BSSID/RSSI entry.
    WifiNetwork collected[12];
    uint16_t uniqueCount = 0;

    for (int16_t i = 0; i < found; ++i) {
        const String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;

        const int32_t rssi = WiFi.RSSI(i);
        const bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        int existing = -1;
        for (uint16_t j = 0; j < uniqueCount; ++j) {
            if (collected[j].ssid == ssid) {
                existing = static_cast<int>(j);
                break;
            }
        }

        if (existing >= 0) {
            // Same mesh SSID: retain the strongest visible access point.
            if (rssi > collected[existing].rssi) {
                collected[existing].rssi = rssi;
                collected[existing].secure = secure;
            }
            continue;
        }

        if (uniqueCount >= 12) continue;
        collected[uniqueCount].ssid = ssid;
        collected[uniqueCount].rssi = rssi;
        collected[uniqueCount].secure = secure;
        ++uniqueCount;
    }

    // Strongest networks first.
    for (uint16_t i = 0; i < uniqueCount; ++i) {
        for (uint16_t j = i + 1; j < uniqueCount; ++j) {
            if (collected[j].rssi > collected[i].rssi) {
                const WifiNetwork tmp = collected[i];
                collected[i] = collected[j];
                collected[j] = tmp;
            }
        }
    }

    WiFi.scanDelete();
    collectWifiScan(
        collected,
        uniqueCount,
        uniqueCount
            ? "WLAN-Netze gefunden. Mesh-Netze wurden zusammengefasst."
            : "Keine WLAN-Netze gefunden."
    );
}

bool NetworkService::connectWifi(const String& ssid, const String& password) {
    return connectWifiProfile(activeProfile_, ssid, password);
}

bool NetworkService::connectWifiProfile(uint8_t slot, const String& ssid, const String& password) {
    WifiProfileAddressing addressing;
    if (slot < kMaxWifiProfiles) addressing = wifiAddressing_[slot];
    return connectWifiProfile(slot, ssid, password, addressing);
}

bool NetworkService::connectWifiProfile(uint8_t slot, const String& ssid, const String& password,
                                        const WifiProfileAddressing& addressing) {
    if (!wifiAvailable() || slot >= kMaxWifiProfiles || !ssid.length()) return false;
    if (!validateAddressing(addressing)) {
        connectionState_.text =
            "IP-Konfiguration ungueltig. Bei statischer IP muessen IP, Gateway und Subnetz gueltig sein.";
        return false;
    }

    auto& config = ConfigService::instance();
    const String sk = ssidKey(slot);
    const String pk = passwordKey(slot);

    if (!config.putString(sk.c_str(), ssid) ||
        !config.putSecretString(pk.c_str(), password) ||
        !config.putString(ipKey(slot).c_str(), addressing.ip) ||
        !config.putString(gatewayKey(slot).c_str(), addressing.gateway) ||
        !config.putString(subnetKey(slot).c_str(), addressing.subnet) ||
        !config.putString(dns1Key(slot).c_str(), addressing.dns1) ||
        !config.putString(dns2Key(slot).c_str(), addressing.dns2) ||
        !config.putBool("wifi_enabled", true) ||
        !config.putU8("wifi_active", slot)) return false;

    const bool firstProfile = nextConfiguredProfile(0) < 0;
    wifiSsids_[slot] = ssid;
    wifiPasswords_[slot] = password;
    wifiAddressing_[slot] = addressing;
    activeProfile_ = slot;
    retryProfile_ = slot;
    currentAttemptProfile_ = slot;
    wifiEnabled_ = true;
    if (firstProfile) {
        preferredProfile_ = slot;
        config.putU8("wifi_preferred", slot);
    }
    selectRuntimeProfile(slot);

    // Leave any previous association/scan state cleanly before starting a
    // fresh connection. This avoids transient WL_NO_SSID_AVAIL reports from
    // ESP-Hosted immediately after a scan or profile switch.
    WiFi.disconnect(false, false);
    connectionState_.connected = false;
    lastWifiStatus_ = WL_DISCONNECTED;
    vTaskDelay(pdMS_TO_TICKS(150));

    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    WiFi.setAutoReconnect(false);

    if (!applyProfileAddressing(slot)) return false;

    WiFi.begin(wifiSsid_.c_str(), wifiPassword_.c_str());

    eco_.wifiRadioOn = true;
    eco_.wifiOffBecauseLan = false;
    eco_.wifiOffBecauseSchedule = false;
    ++wifiReconnects_;
    wifiRetry_.success();
    connectionState_.text = "Verbindung wird hergestellt ...";
    return true;
}

bool NetworkService::getWifiProfile(uint8_t slot, String& ssid, String& password) const {
    WifiProfileAddressing addressing;
    return getWifiProfile(slot, ssid, password, addressing);
}

bool NetworkService::getWifiProfile(uint8_t slot, String& ssid, String& password,
                                    WifiProfileAddressing& addressing) const {
    if (slot >= kMaxWifiProfiles) return false;
    ssid = wifiSsids_[slot];
    password = wifiPasswords_[slot];
    addressing = wifiAddressing_[slot];
    return ssid.length() > 0;
}

String NetworkService::wifiProfileSsid(uint8_t slot) const {
    if (slot >= kMaxWifiProfiles) return {};
    return wifiSsids_[slot];
}

bool NetworkService::wifiProfileConfigured(uint8_t slot) const {
    return slot < kMaxWifiProfiles && wifiSsids_[slot].length() > 0;
}

bool NetworkService::setPreferredWifiProfile(uint8_t slot) {
    if (slot >= kMaxWifiProfiles || !wifiSsids_[slot].length()) return false;
    if (!ConfigService::instance().putU8("wifi_preferred", slot)) return false;
    preferredProfile_ = slot;
    retryProfile_ = slot;
    // Changing preference is explicit user intent: try it immediately. On
    // failure, normal fallback will still consider the other saved profiles.
    if (wifiAvailable() && wifiEnabled_ && !c6Maintenance_ &&
        !wifiScanInProgress() && !eco_.wifiOffBecauseLan &&
        !eco_.wifiOffBecauseSchedule) {
        if (currentAttemptProfile_ != static_cast<int8_t>(slot) ||
            WiFi.status() != WL_CONNECTED) {
            WiFi.disconnect(false, false);
            currentAttemptProfile_ = -1;
            wifiRetry_.success();
            ensureWifiOn();
        }
    }
    return true;
}

bool NetworkService::forgetWifiProfile(uint8_t slot) {
    if (slot >= kMaxWifiProfiles || !wifiSsids_[slot].length()) return false;
    auto& config = ConfigService::instance();
    // Persist the migration marker BEFORE removing slot 0, so a reboot
    // cannot silently restore the legacy single-profile credentials.
    if (!config.putBool("wifi_prof_v2", true)) return false;
    bool ok = true;
    ok &= config.remove(ssidKey(slot).c_str());
    ok &= config.removeSecret(passwordKey(slot).c_str());
    ok &= config.remove(ipKey(slot).c_str());
    ok &= config.remove(gatewayKey(slot).c_str());
    ok &= config.remove(subnetKey(slot).c_str());
    ok &= config.remove(dns1Key(slot).c_str());
    ok &= config.remove(dns2Key(slot).c_str());
    // Legacy aliases are never used after migration, but remove them too.
    if (slot == 0) {
        ok &= config.remove("wifi_ssid");
        ok &= config.removeSecret("wifi_pwd");
    }
    if (!ok) {
        loadWifiProfiles(); // Reload actual persisted state on partial errors.
        return false;
    }

    const bool removedConnection = currentAttemptProfile_ == static_cast<int8_t>(slot);
    const bool removedActive = activeProfile_ == slot;
    wifiSsids_[slot] = "";
    wifiPasswords_[slot] = "";
    wifiAddressing_[slot] = WifiProfileAddressing{};
    if (removedConnection) {
        currentAttemptProfile_ = -1;
        if (wifiAvailable() && !wifiScanInProgress() && !c6Maintenance_)
            WiFi.disconnect(false, true);
    }

    if (preferredProfile_ == slot || !wifiSsids_[preferredProfile_].length()) {
        const int next = nextConfiguredProfile(0);
        preferredProfile_ = next >= 0 ? static_cast<uint8_t>(next) : 0;
        config.putU8("wifi_preferred", preferredProfile_);
    }
    if (removedActive || !wifiSsids_[activeProfile_].length()) {
        activeProfile_ = preferredProfile_;
        config.putU8("wifi_active", activeProfile_);
    }
    retryProfile_ = preferredProfile_;
    if (nextConfiguredProfile(0) < 0) {
        wifiConfigured_ = false;
        wifiSsid_ = "";
        wifiPassword_ = "";
        connectionState_.connected = false;
        connectionState_.text = "Keine WLAN-Profile gespeichert.";
        if (wifiAvailable() && !wifiScanInProgress() && !c6Maintenance_)
            WiFi.disconnect(false, true);
        refreshPath();
        return true;
    }
    if (removedConnection || removedActive) {
        selectRuntimeProfile(activeProfile_);
        wifiRetry_.success();
        if (!wifiScanInProgress() && !c6Maintenance_) ensureWifiOn();
    }
    return true;
}


void NetworkService::setWifiEnabled(bool enabled) {
    if (!board::kC6HostedRuntimeEnabled) return;

    if (!ConfigService::instance().putBool("wifi_enabled", enabled)) {
        EventLog::instance().add("WARN", "WIFI_SETTING_WRITE", "WLAN switch was not saved");
        return;
    }
    wifiEnabled_ = enabled;

    if (!enabled) {
        if (wifiAvailable()) {
            stopWifiRadio();
        }
        eco_.wifiRadioOn = false;
        appliedWifiPs_ = -1;
        appliedWifiTxPower_ = -1;
        connectionState_.connected = false;
        lastWifiStatus_ = WL_DISCONNECTED;
        refreshPath();
        return;
    }

    if (!hostedReady_) hostedReady_ = initHostedWifi();
    eco_.wifiOffBecauseSchedule = false;
    eco_.wifiOffBecauseLan = false;
    lastWifiStatus_ = -999;
    connectionState_.connected = false;
    wifiRetry_.success();
    wifiConfigured_ = hostedReady_ && nextConfiguredProfile(0) >= 0;

    if (wifiConfigured_) {
        retryProfile_ = activeProfile_;
        wifiRetry_.success();
        ensureWifiOn();
    }
}

void NetworkService::setC6Maintenance(bool active) {
    c6Maintenance_ = active;
}

void NetworkService::switchWifiOffForLan() {
    if (!wifiAvailable()) return;
    if (!eco_.wifiRadioOn && eco_.wifiOffBecauseLan) return;

    stopWifiRadio();

    appliedWifiPs_ = -1;
    appliedWifiTxPower_ = -1;
    eco_.wifiRadioOn = false;
    eco_.wifiSleeping = false;
    eco_.wifiOffBecauseLan = true;
    eco_.wifiOffBecauseSchedule = false;
    eco_.wifiRssi = -127;
}

void NetworkService::switchWifiOffForSchedule() {
    if (!wifiAvailable()) return;
    if (!eco_.wifiRadioOn && eco_.wifiOffBecauseSchedule) return;

    stopWifiRadio();

    appliedWifiPs_ = -1;
    appliedWifiTxPower_ = -1;
    eco_.wifiRadioOn = false;
    eco_.wifiSleeping = false;
    eco_.wifiOffBecauseSchedule = true;
    eco_.wifiOffBecauseLan = false;
    eco_.wifiRssi = -127;
}

bool NetworkService::shouldScheduleWifiOff(
    bool enabled,
    uint16_t startMinute,
    uint16_t endMinute
) const {
    if (!enabled) return false;
    // An idle/dimmed screen can still be in active use (audio, touch, NAS).
    // Automatic radio shutdown is permitted only after full screen-off.
    if (IdleService::instance().stage() != IdleStage::Off) return false;

    // Keep WLAN available until NTP has provided a valid clock.
    if (!TimeService::instance().valid()) return false;

    time_t now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);

    const uint16_t minute =
        static_cast<uint16_t>(local.tm_hour * 60 + local.tm_min);
    const uint16_t start = std::min<uint16_t>(1439, startMinute);
    const uint16_t end = std::min<uint16_t>(1439, endMinute);

    // Equal start/end = whole day whenever the display is inactive.
    if (start == end) return true;

    // Normal daytime window.
    if (start < end) return minute >= start && minute < end;

    // Crosses midnight, e.g. 22:00 -> 06:00.
    return minute >= start || minute < end;
}

void NetworkService::applyWifiEcoPolicy() {
    if (!wifiAvailable() || !eco_.wifiRadioOn) return;

    const auto s = SettingsService::instance().snapshot();

    wifi_ps_type_t ps = WIFI_PS_NONE;
    // WiFi.setSleep is routed by ESP-Hosted to the C6 radio. Keep transport
    // responsive during bounded bursts, then restore the user's eco policy.
    // Only this existing network loop performs the RPC, never a worker/UI callback.
    if (s.networkEco.wifiPowerSave && !NetworkWorker::instance().radioBurstActive()) {
        ps = (s.networkEco.aggressiveWifiSleep &&
              IdleService::instance().stage() == IdleStage::Off)
            ? WIFI_PS_MAX_MODEM
            : WIFI_PS_MIN_MODEM;
    }

    const int8_t nextPs = static_cast<int8_t>(ps);
    if (appliedWifiPs_ != nextPs) {
        // getMode is a Hosted RPC too. Query only when changing policy, not
        // on every 500-ms main-loop pass with an unchanged sleep mode.
        if(WiFi.getMode()==WIFI_MODE_NULL)return;
        if(WiFi.setSleep(ps))appliedWifiPs_ = nextPs;
    }
    eco_.wifiSleeping = appliedWifiPs_ > static_cast<int8_t>(WIFI_PS_NONE);
    eco_.adaptiveTxPower = s.networkEco.adaptiveTxPower;
}

void NetworkService::updateAdaptiveTxPower() {
    if (!wifiAvailable() || !eco_.wifiRadioOn ||
        WiFi.status() != WL_CONNECTED) return;

    const auto s = SettingsService::instance().snapshot();

    const int32_t rssi = WiFi.RSSI();
    eco_.wifiRssi = rssi;

    int raw = 80;
    float dbm = 20.0f;

    if (s.networkEco.adaptiveTxPower && rssi >= -55) {
        raw = 44;
        dbm = 11.0f;
    } else if (s.networkEco.adaptiveTxPower && rssi >= -67) {
        raw = 60;
        dbm = 15.0f;
    }

    if (appliedWifiTxPower_ != raw && WiFi.setTxPower((wifi_power_t)raw)) {
        appliedWifiTxPower_ = raw;
        eco_.requestedTxDbm = dbm;
    }
}

void NetworkService::refreshPath() {
    const NetworkPath old = path_;

    if (ethernetUp_) {
        path_ = NetworkPath::Ethernet;
    } else if (wifiAvailable() && connectionState_.connected) {
        path_ = NetworkPath::Wifi;
    } else {
        path_ = NetworkPath::None;
    }

    if (old == path_) return;

    if (mdns_) {
        MdnsService::instance().stop();
        mdns_ = false;
    }

    PowerManager::instance().requestBurst(1200);

    EventBus::instance().publish({
        path_ == NetworkPath::None ? EventType::NetworkDown : EventType::NetworkUp,
        nullptr
    });
}

void NetworkService::loop() {
    SystemSupervisor::instance().heartbeat(ServiceId::Network);

    // Link changes can wake the policy early; keep a periodic fallback for
    // ESP-Hosted versions which do not forward every WiFi event reliably.
    const uint32_t nowMs = millis();
    static uint32_t handledWifiGeneration = 0;
    const uint32_t generation = sWifiLinkGeneration.load(std::memory_order_relaxed);
    const bool linkEvent = generation != handledWifiGeneration;
    if (!linkEvent && (uint32_t)(nowMs - last_) < 500UL) return;
    last_ = nowMs;
    if (linkEvent) handledWifiGeneration = generation;

    // The scan worker owns ESP-Hosted exclusively while a scan is active.
    if (wifiScanInProgress()) {
        connectionState_.c6Ready = hostedReady_;
        connectionState_.enabled = wifiEnabled_;
        connectionState_.configured = wifiConfigured_;
        connectionState_.text = "WLAN-Netzwerksuche laeuft ...";
        return;
    }

    MdnsService::instance().loop();

    // SettingsSnapshot reads multiple NVS keys. It is not necessary to read
    // them on every network status tick. Newly saved settings take effect in
    // at most two seconds; the connection state itself remains live.
    static SettingsSnapshot cachedSettings;
    static uint32_t lastSettingsRead = 0;
    const uint32_t settingsNow = millis();
    if (!lastSettingsRead ||
        (uint32_t)(settingsNow - lastSettingsRead) >= 2000UL) {
        cachedSettings = SettingsService::instance().snapshot();
        lastSettingsRead = settingsNow;
    }
    const auto& settings = cachedSettings;

    const bool scheduledOff =
        settings.powerManagement.energySaving &&
        shouldScheduleWifiOff(
            settings.networkEco.wifiScheduleEnabled,
            settings.networkEco.wifiScheduleStartMinute,
            settings.networkEco.wifiScheduleEndMinute
        );

    if (scheduledOff && wifiAvailable() && wifiEnabled_ && !c6Maintenance_) {
        switchWifiOffForSchedule();
    } else {
        // Wake-up or leaving the configured time window restores WLAN
        // immediately without changing the user's permanent WLAN switch.
        if (eco_.wifiOffBecauseSchedule) {
            eco_.wifiOffBecauseSchedule = false;
            wifiRetry_.success();
        }

        if (ethernetUp_ && wifiAvailable()) {
            if (!c6Maintenance_ &&
                settings.networkEco.wifiOffWhenLan &&
                ethernetUp_ && ethernetIp_ != IPAddress() &&
                ethernetUpSince_ &&
                millis() - ethernetUpSince_ >=
                    (uint32_t)settings.networkEco.lanStableBeforeWifiOff * 1000UL) {
                switchWifiOffForLan();
            } else if (!settings.networkEco.wifiOffWhenLan) {
                eco_.wifiOffBecauseLan = false;
                ensureWifiOn();
            }
        } else if (wifiAvailable() && !c6Maintenance_) {
            eco_.wifiOffBecauseLan = false;
            ensureWifiOn();
        }
    }

    const uint32_t now = millis();
    const bool radioReady = wifiAvailable() &&
        !eco_.wifiOffBecauseSchedule && !eco_.wifiOffBecauseLan;
    // Track the last actual link query separately from the 15-second
    // connection-detail refresh. Otherwise we'd query every 500 ms after
    // the ten-second mark until the next details update.
    static uint32_t lastWifiLinkPoll = 0;
    const bool fallbackDue = !lastWifiLinkPoll ||
        (uint32_t)(now - lastWifiLinkPoll) >= 10000UL;
    const bool mustQueryLink = linkEvent || fallbackDue ||
        !connectionState_.connected || lastWifiStatus_ == -999;

    // WiFi.status() itself is an ESP-Hosted RPC on this board. A connected
    // link is queried on events and with a slow fallback, not every 500 ms.
    const int wifiStatus = !radioReady ? static_cast<int>(WL_DISCONNECTED) :
        mustQueryLink ? static_cast<int>(WiFi.status()) : lastWifiStatus_;
    if (radioReady && mustQueryLink) lastWifiLinkPoll = now;

    const bool statusChanged = wifiStatus != lastWifiStatus_;
    const bool refreshDetails = statusChanged || !lastWifiDetails_ ||
        (uint32_t)(now - lastWifiDetails_) >= 15000UL;
    if (refreshDetails) lastWifiDetails_ = now;
    lastWifiStatus_ = wifiStatus;
    if (refreshDetails || statusChanged || linkEvent || !radioReady) {
        updateConnectionState(wifiStatus, refreshDetails);
    }
    refreshPath();

    if (usingWifi()) {
        wifiRetry_.success();

        if (currentAttemptProfile_ >= 0 &&
            activeProfile_ != static_cast<uint8_t>(currentAttemptProfile_)) {
            activeProfile_ = static_cast<uint8_t>(currentAttemptProfile_);
            ConfigService::instance().putU8("wifi_active", activeProfile_);
        }

        applyWifiEcoPolicy();

        if (millis() - lastTxTune_ >= 30000UL) {
            lastTxTune_ = millis();
            updateAdaptiveTxPower();
        }
    }

    if(mdns_&&!MdnsService::instance().active())mdns_=false;
    if (online() && !mdns_) {
        auto s = SettingsService::instance().snapshot();
        mdns_ = MdnsService::instance().start(s.hostname.c_str());
    } else if (!online() && mdns_) {
        MdnsService::instance().stop();
        mdns_ = false;
    }
}

String NetworkService::ip() const {
    if (path_ == NetworkPath::Ethernet) return ethernetIp_.toString();
    if (path_ == NetworkPath::Wifi && wifiAvailable()) return wifiIpCache_.toString();
    return {};
}

const char* NetworkService::pathName() const {
    switch (path_) {
        case NetworkPath::Ethernet: return "ethernet";
        case NetworkPath::Wifi: return "wifi";
        default: return "offline";
    }
}

}

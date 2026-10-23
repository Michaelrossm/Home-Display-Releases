#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <freertos/semphr.h>
#include "RetryPolicy.h"

namespace hp {

enum class NetworkPath : uint8_t { None, Ethernet, Wifi };

struct NetworkEcoStatus {
    bool wifiRadioOn = false;
    bool wifiSleeping = false;
    bool wifiOffBecauseLan = false;
    bool wifiOffBecauseSchedule = false;
    bool adaptiveTxPower = false;
    int32_t wifiRssi = -127;
    float requestedTxDbm = 20.0f;
};

struct WifiNetwork {
    String ssid;
    int32_t rssi = -127;
    bool secure = true;
};

struct WifiProfileAddressing {
    String ip;
    String gateway;
    String subnet;
    String dns1;
    String dns2;
};

enum class WifiPolicyReason : uint8_t {
    None,
    UserDisabled,
    C6Unavailable,
    Maintenance,
    Schedule,
    EthernetPreferred,
    NotConfigured
};

struct EthernetConnectionState {
    bool connected = false;
    String ip;
    String gateway;
    String subnet;
    String dns1;
    String dns2;
    String mac;
    uint32_t linkSpeedMbps = 0;
};

struct WifiConnectionState {
    bool c6Ready = false;
    bool enabled = false;
    bool configured = false;
    bool connected = false;
    bool staticIp = false;
    uint8_t profile = 0;
    int statusCode = 0;
    int32_t rssi = -127;
    String ssid;
    String ip;
    String gateway;
    String subnet;
    String dns1;
    String dns2;
    String mac;
    String bssid;
    int32_t channel = 0;
    String text;
};

struct NetworkOverviewState {
    NetworkPath path = NetworkPath::None;
    WifiPolicyReason wifiPolicyReason = WifiPolicyReason::None;
    NetworkEcoStatus eco;
    EthernetConnectionState ethernet;
    WifiConnectionState wifi;
};

class NetworkService {
public:
    static constexpr uint8_t kMaxWifiProfiles = 3;
    static NetworkService& instance();

    bool begin();
    void loop();

    bool online() const { return path_ != NetworkPath::None; }
    NetworkPath path() const { return path_; }
    bool usingEthernet() const { return path_ == NetworkPath::Ethernet; }
    bool usingWifi() const { return path_ == NetworkPath::Wifi; }

    String ip() const;
    const char* pathName() const;
    NetworkEcoStatus ecoStatus() const { return eco_; }

    void reportEthernet(bool linkUp, const IPAddress& address = IPAddress());
    void settingsChanged();

    int scanNetworks(WifiNetwork* results, size_t capacity);
    bool requestWifiScan();
    bool wifiScanInProgress() const;
    String wifiScanStatus() const;

    bool connectWifi(const String& ssid, const String& password);
    bool connectWifiProfile(uint8_t slot, const String& ssid, const String& password);
    bool connectWifiProfile(uint8_t slot, const String& ssid, const String& password,
                            const WifiProfileAddressing& addressing);
    bool getWifiProfile(uint8_t slot, String& ssid, String& password) const;
    bool getWifiProfile(uint8_t slot, String& ssid, String& password,
                        WifiProfileAddressing& addressing) const;
    String wifiProfileSsid(uint8_t slot) const;
    bool wifiProfileConfigured(uint8_t slot) const;
    bool forgetWifiProfile(uint8_t slot);
    bool setPreferredWifiProfile(uint8_t slot);
    uint8_t preferredWifiProfile() const { return preferredProfile_; }
    uint8_t activeWifiProfile() const { return activeProfile_; }

    void setWifiEnabled(bool enabled);
    bool wifiEnabled() const { return wifiEnabled_; }
    bool wifiAvailable() const;
    bool hostedReady() const { return hostedReady_; }
    WifiConnectionState wifiConnectionState() const { return connectionState_; }
    EthernetConnectionState ethernetConnectionState() const { return ethernetState_; }
    NetworkOverviewState overview() const;
    WifiPolicyReason wifiPolicyReason() const;
    static const char* wifiPolicyReasonName(WifiPolicyReason reason);
    void setC6Maintenance(bool active);
    const String& configuredSsid() const { return wifiSsid_; }

private:
    bool initHostedWifi();
    void refreshPath();
    void ensureWifiOn();
    void switchWifiOffForLan();
    void switchWifiOffForSchedule();
    bool shouldScheduleWifiOff(bool enabled, uint16_t startMinute, uint16_t endMinute) const;
    void applyWifiEcoPolicy();
    void updateAdaptiveTxPower();
    void collectWifiScan(const WifiNetwork* networks, size_t count, const char* status = nullptr);
    static void wifiScanJob(void* context);
    void executeWifiScan();
    void loadWifiProfiles();
    int nextConfiguredProfile(uint8_t start) const;
    void selectRuntimeProfile(uint8_t slot);
    bool applyProfileAddressing(uint8_t slot);
    bool validateAddressing(const WifiProfileAddressing& addressing) const;
    void updateConnectionState(int wifiStatus, bool refreshDetails);

    NetworkPath path_ = NetworkPath::None;
    bool ethernetUp_ = false;
    IPAddress ethernetIp_;
    EthernetConnectionState ethernetState_;
    bool mdns_ = false;
    uint32_t last_ = 0;
    uint32_t ethernetUpSince_ = 0;
    uint32_t lastTxTune_ = 0;
    uint32_t lastWifiDetails_ = 0;
    int lastWifiStatus_ = -999;
    int8_t appliedWifiPs_ = -1;
    int appliedWifiTxPower_ = -1;

    bool wifiConfigured_ = false;
    bool wifiEnabled_ = true;
    bool hostedReady_ = false;
    String wifiSsid_;
    String wifiPassword_;
    String wifiSsids_[kMaxWifiProfiles];
    String wifiPasswords_[kMaxWifiProfiles];
    WifiProfileAddressing wifiAddressing_[kMaxWifiProfiles];
    uint8_t activeProfile_ = 0;
    uint8_t preferredProfile_ = 0;
    uint8_t retryProfile_ = 0;
    int8_t currentAttemptProfile_ = -1;

    NetworkEcoStatus eco_;
    WifiConnectionState connectionState_;
    IPAddress wifiIpCache_;
    RetryPolicy wifiRetry_{1000,60000};
    uint32_t wifiReconnects_=0, ethernetReconnects_=0;

    WifiNetwork scanResults_[12];
    uint8_t scanCount_ = 0;
    bool scanActive_ = false;
    bool c6Maintenance_ = false;
    String scanStatus_;
    mutable SemaphoreHandle_t scanLock_ = nullptr;

public:
    uint32_t wifiReconnects() const { return wifiReconnects_; }
    uint32_t ethernetReconnects() const { return ethernetReconnects_; }
};

}

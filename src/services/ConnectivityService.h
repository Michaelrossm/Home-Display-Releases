#pragma once
#include "NetworkService.h"
#include "BluetoothService.h"

namespace hp {

struct ConnectivityState {
    NetworkOverviewState network;
    BluetoothState bluetooth;
    bool c6Ready = false;
    bool wifiAvailable = false;
    bool bleAvailable = false;
};

class ConnectivityService {
public:
    static ConnectivityState snapshot() {
        ConnectivityState s;
        s.network = NetworkService::instance().overview();
        s.bluetooth = BluetoothService::instance().snapshot();
        s.c6Ready = s.network.wifi.c6Ready;
        s.wifiAvailable =
            s.c6Ready &&
            s.network.wifiPolicyReason != WifiPolicyReason::C6Unavailable;
        s.bleAvailable = s.bluetooth.available;
        return s;
    }
};

} // namespace hp

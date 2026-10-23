#include "BluetoothService.h"
#include "BoardConfig.h"
#include "../core/EventLog.h"
#include "PowerManager.h"
#include "ConfigService.h"

#include <esp32-hal-hosted.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

namespace hp {
namespace {

class HostedBleScanCallbacks final : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        const String address = advertisedDevice.getAddress().toString();
        const String name = advertisedDevice.haveName()
            ? advertisedDevice.getName()
            : String();
        BluetoothService::instance().recordAdvertisement(
            address.c_str(),
            name.c_str(),
            advertisedDevice.getRSSI()
        );
    }
};

HostedBleScanCallbacks gScanCallbacks;

}

BluetoothService& BluetoothService::instance() {
    static BluetoothService b;
    return b;
}

bool BluetoothService::begin() {
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!lock_) return false;

    enabled_ = ConfigService::instance().getBool("ble_enabled", true);
    if (!enabled_) {
        initialized_ = false;
        status_ = "Bluetooth ausgeschaltet.";
        return true;
    }

    if (!board::kC6HostedRuntimeEnabled) {
        status_ = "ESP32-C6 ist in BoardConfig deaktiviert.";
        return true;
    }

    if (!hostedIsInitialized()) {
        BLEDevice::setPins(
            board::kC6SdioClk, board::kC6SdioCmd,
            board::kC6SdioD0, board::kC6SdioD1,
            board::kC6SdioD2, board::kC6SdioD3,
            board::kC6Reset
        );
    }

    if (!BLEDevice::init("Home-Display")) {
        status_ = "BLE/NimBLE konnte über ESP32-C6 nicht initialisiert werden.";
        EventLog::instance().add("ERROR","BLE_INIT",status_);
        return true;
    }

    initialized_ = true;
    status_ = "Bluetooth LE bereit.";
    EventLog::instance().add("INFO","BLE_READY","ESP32-C6 Hosted BLE initialized");
    return true;
}

bool BluetoothService::setEnabled(bool enabled) {
    if (enabled == enabled_) return true;
    if (!lock_) return false;
    if (scanning()) {
        // A scan owns the BLE stack in its FreeRTOS worker. Ask the user to
        // switch off after it completes instead of deinitializing mid-scan.
        return false;
    }
    if (!ConfigService::instance().putBool("ble_enabled", enabled)) return false;

    if (!enabled) {
        enabled_ = false;
        if (initialized_) {
            BLEDevice::deinit(false); // BLE only; do not reset ESP-Hosted/WLAN.
            initialized_ = false;
        }
        xSemaphoreTake(lock_, portMAX_DELAY);
        count_ = 0;
        status_ = "Bluetooth ausgeschaltet.";
        xSemaphoreGive(lock_);
        EventLog::instance().add("INFO", "BLE_OFF", "Bluetooth LE disabled by user");
        return true;
    }

    enabled_ = true;
    initialized_ = false;
    begin();
    return initialized_;
}

bool BluetoothService::scanning() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(10)) != pdTRUE) return true;
    const bool value = scanning_;
    xSemaphoreGive(lock_);
    return value;
}

String BluetoothService::status() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE)
        return "BLE-Status nicht verfügbar.";
    const String value = status_;
    xSemaphoreGive(lock_);
    return value;
}

BluetoothState BluetoothService::snapshot() const {
    BluetoothState out;
    out.enabled = enabled_;
    out.initialized = initialized_;
    out.available = enabled_ && initialized_;
    out.capability = BluetoothCapability::BleOnly;

    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) {
        out.status = "BLE-Status nicht verfuegbar.";
        return out;
    }

    out.scanning = scanning_;
    out.deviceCount = count_;
    out.status = status_;
    xSemaphoreGive(lock_);
    return out;
}

size_t BluetoothService::deviceCount() const {
    if (!lock_ || xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    const size_t value = count_;
    xSemaphoreGive(lock_);
    return value;
}

bool BluetoothService::startScan(uint32_t seconds) {
    if (!initialized_) return false;
    seconds = constrain(seconds, 2u, 30u);

    if (xSemaphoreTake(lock_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (scanning_) {
        xSemaphoreGive(lock_);
        return false;
    }

    count_ = 0;
    scanning_ = true;
    scanSeconds_ = seconds;
    status_ = "BLE-Suche läuft ...";
    xSemaphoreGive(lock_);

    PowerManager::instance().requestBurst(1000);

    if (xTaskCreate(scanTask, "ble-scan", 6144, this, 4, nullptr) != pdPASS) {
        finishScan("BLE-Scan-Task konnte nicht gestartet werden.");
        return false;
    }

    EventLog::instance().add("INFO","BLE_SCAN","BLE scan started");
    return true;
}

void BluetoothService::scanTask(void* context) {
    auto* self = static_cast<BluetoothService*>(context);

    BLEScan* scan = BLEDevice::getScan();
    if (!scan) {
        self->finishScan("BLE-Scanner nicht verfügbar.");
        vTaskDelete(nullptr);
        return;
    }

    scan->stop();
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(&gScanCallbacks, true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);

    scan->start(self->scanSeconds_, false);
    scan->clearResults();

    self->finishScan("BLE-Suche abgeschlossen.");
    vTaskDelete(nullptr);
}

bool BluetoothService::stopScan() {
    if (!initialized_) return false;
    BLEScan* scan = BLEDevice::getScan();
    if (scan) scan->stop();
    finishScan("BLE-Suche gestoppt.");
    return true;
}

void BluetoothService::finishScan(const char* result) {
    if (!lock_) return;
    xSemaphoreTake(lock_, portMAX_DELAY);
    scanning_ = false;
    status_ = result ? result : "BLE bereit.";
    xSemaphoreGive(lock_);
}

void BluetoothService::recordAdvertisement(const char* address, const char* name, int rssi) {
    if (!lock_ || !address || !*address) return;
    if (xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) != pdTRUE) return;

    size_t index = count_;
    for (size_t i = 0; i < count_; ++i) {
        if (strcmp(devices_[i].address, address) == 0) {
            index = i;
            break;
        }
    }

    if (index == count_) {
        if (count_ >= kMaxDevices) {
            xSemaphoreGive(lock_);
            return;
        }
        ++count_;
    }

    auto& d = devices_[index];
    strlcpy(d.address, address, sizeof(d.address));
    strlcpy(d.name, (name && *name) ? name : "Unbekanntes BLE-Geraet", sizeof(d.name));
    d.rssi = static_cast<int8_t>(constrain(rssi, -127, 20));

    xSemaphoreGive(lock_);
}

bool BluetoothService::connect(const char* address) {
    if (!initialized_ || !address || !*address) return false;
    EventLog::instance().add(
        "WARN","BLE_CONNECT",
        String("Generic GATT connect needs an app-specific profile: ") + address
    );
    return false;
}

bool BluetoothService::disconnect(const char* address) {
    return false;
}

bool BluetoothService::forget(const char* address) {
    return false;
}

BluetoothDeviceInfo BluetoothService::device(size_t i) const {
    BluetoothDeviceInfo out;
    if (!lock_) return out;
    if (xSemaphoreTake(lock_, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (i < count_) out = devices_[i];
        xSemaphoreGive(lock_);
    }
    return out;
}

void BluetoothService::loop() {
}

}

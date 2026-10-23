#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace hp {

enum class BluetoothCapability : uint8_t { BleOnly, ClassicAudioUnavailable };

struct BluetoothDeviceInfo {
    char address[24]{};
    char name[48]{};
    int8_t rssi = -127;
    bool bonded = false;
    bool connected = false;
};

struct BluetoothState {
    bool enabled = false;
    bool initialized = false;
    bool available = false;
    bool scanning = false;
    size_t deviceCount = 0;
    BluetoothCapability capability = BluetoothCapability::BleOnly;
    String status;
};

class BluetoothService {
public:
    static BluetoothService& instance();

    bool begin();
    void loop();

    bool available() const { return enabled_ && initialized_; }
    bool enabled() const { return enabled_; }
    // Returns false if a scan is still running or if the new preference could not be saved.
    bool setEnabled(bool enabled);
    bool startScan(uint32_t seconds = 8);
    bool stopScan();
    bool scanning() const;
    String status() const;
    BluetoothState snapshot() const;

    bool connect(const char* address);
    bool disconnect(const char* address);
    bool forget(const char* address);

    size_t deviceCount() const;
    BluetoothDeviceInfo device(size_t i) const;

    BluetoothCapability capability() const { return BluetoothCapability::BleOnly; }
    bool classicAudioAvailable() const { return false; }

    void recordAdvertisement(const char* address, const char* name, int rssi);

private:
    static void scanTask(void*);
    void finishScan(const char* result);

    static constexpr size_t kMaxDevices = 24;
    BluetoothDeviceInfo devices_[kMaxDevices]{};
    size_t count_ = 0;
    mutable SemaphoreHandle_t lock_ = nullptr;
    bool initialized_ = false;
    bool enabled_ = true;
    bool scanning_ = false;
    uint32_t scanSeconds_ = 8;
    String status_ = "BLE nicht initialisiert.";
};

}

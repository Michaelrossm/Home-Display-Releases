#pragma once
#include <stdint.h>
namespace hp {
struct StorageVolumeUsage { uint64_t total=0, used=0; bool available=false; };
struct StorageAppUsage { char id[48]{}; uint64_t bytes=0; bool sd=false,usb=false; };
struct StorageUsageSnapshot {
    // Firmware slot, internal apps, system data, SD, internal RAM, PSRAM, USB.
    StorageVolumeUsage volumes[7]{};
    StorageAppUsage apps[32]{};
    uint64_t categories[8]{}, flashTotal=0, otaReserve=0;
    uint32_t generation=0, capturedMs=0;
    uint8_t appCount=0;
    bool incomplete=false;
};
}

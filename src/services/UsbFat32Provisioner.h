#pragma once
#include <stdint.h>
#include <atomic>
#include <usb/msc_host.h>
namespace hp {
// Destructive! A caller must exclusively own the chosen physical USB MSC handle.
// Never accepts FatFs drive numbers, SD handles, or a filesystem path.
class UsbFat32Provisioner {
public:
 static bool run(msc_host_device_handle_t device, const msc_host_device_info_t& info,
                 const std::atomic<bool>& disconnected, std::atomic<uint8_t>& percent);
};
}

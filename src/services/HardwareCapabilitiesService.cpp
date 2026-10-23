#include "HardwareCapabilitiesService.h"

namespace hp {
HardwareCapabilitiesService& HardwareCapabilitiesService::instance() {
    static HardwareCapabilitiesService h;
    return h;
}
}

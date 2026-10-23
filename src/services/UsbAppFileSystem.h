#pragma once
#include <FS.h>
#include <memory>
namespace hp {
// Uses the existing USB volume; never initializes or mounts another driver.
fs::FS& usbAppFileSystem();
std::unique_ptr<fs::FS> usbAppFileSystemFor(uint32_t generation);
}

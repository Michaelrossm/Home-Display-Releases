#pragma once
#include <stdint.h>
namespace hp::board {

inline constexpr const char* kName = "Waveshare ESP32-P4-WIFI6-POE-ETH-D";
inline constexpr const char* kScreen = "10.1-DSI-TOUCH-A";
inline constexpr int kPanelNativeW = 800;
inline constexpr int kPanelNativeH = 1280;
inline constexpr int kLandscapeW = 1280;
inline constexpr int kLandscapeH = 800;

inline constexpr uint32_t kDsiHsyncPulse = 20;
inline constexpr uint32_t kDsiHsyncBackPorch = 20;
inline constexpr uint32_t kDsiHsyncFrontPorch = 40;
inline constexpr uint32_t kDsiVsyncPulse = 4;
inline constexpr uint32_t kDsiVsyncBackPorch = 10;
inline constexpr uint32_t kDsiVsyncFrontPorch = 30;
inline constexpr uint32_t kDsiPreferSpeedHz = 80000000;
inline constexpr uint32_t kDsiLaneBitRateMbps = 1500;

inline constexpr int kPlatformI2cSda = 7;
inline constexpr int kPlatformI2cScl = 8;
inline constexpr uint32_t kPlatformI2cHz = 100000;
inline constexpr uint8_t kDisplayControlI2cAddress = 0x45;

inline constexpr uint8_t kEs8311I2cAddress = 0x18;
inline constexpr int kAudioPaEnable = 53;
inline constexpr int kI2sMclk = 13;
inline constexpr int kI2sBclk = 12;
inline constexpr int kI2sLrck = 10;
inline constexpr int kI2sDout = 9;

inline constexpr int kSdD0 = 39;
inline constexpr int kSdD1 = 40;
inline constexpr int kSdD2 = 41;
inline constexpr int kSdD3 = 42;
inline constexpr int kSdClk = 43;
inline constexpr int kSdCmd = 44;

inline constexpr const char* kEthernetPhy = "IP101";
inline constexpr int kEthernetMdc = 31;
inline constexpr int kEthernetMdio = 52;
inline constexpr int kEthernetReset = 51;
inline constexpr int kEthernetPhyAddress = 1;
inline constexpr int kEthernetRmiiClock = 50;

inline constexpr bool kExactEthernetPortPending = false;
inline constexpr bool kExactSdmmcPortPending = false;

inline constexpr int kC6SdioClk = 18;
inline constexpr int kC6SdioCmd = 19;
inline constexpr int kC6SdioD0 = 14;
inline constexpr int kC6SdioD1 = 15;
inline constexpr int kC6SdioD2 = 16;
inline constexpr int kC6SdioD3 = 17;
inline constexpr int kC6Reset = 54;

inline constexpr bool kExactC6HostedPortPending = false;
inline constexpr bool kC6HostedRuntimeEnabled = true;
inline constexpr bool kC6HostedScanQualified = true;

}

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


class StorageSettingsTests(unittest.TestCase):
    def test_report_uses_actual_filesystem_and_firmware_sizes(self):
        text = source("src/services/StorageService.cpp")
        for value in ("ESP.getSketchSize()", "ESP.getFlashChipSize()",
                      "esp_ota_get_running_partition()", "esp_ota_get_next_update_partition",
                      "self.appFs_.totalBytes()", "self.appFs_.usedBytes()",
                      "LittleFS.totalBytes()", "LittleFS.usedBytes()",
                      "SD_MMC.totalBytes()", "SD_MMC.usedBytes()",
                      "node.size()", "MALLOC_CAP_SPIRAM"):
            self.assertIn(value, text)
        self.assertIn("budget = 4096", text)
        self.assertIn("depth > 8", text)
        self.assertIn("usagePending_.compare_exchange_strong", text)
        self.assertIn("submit(collectUsage", text)

    def test_ui_only_reads_report_and_cleans_timer(self):
        ui = source("src/ui/StorageUsageUi.cpp")
        self.assertIn("usageSnapshot(page.snapshot)", ui)
        self.assertIn("page->timer=lv_timer_create", ui)
        self.assertIn("lv_timer_delete(page->timer)", ui)
        for operation in ("openNextFile", "SD_MMC.usedBytes", "LittleFS.usedBytes"):
            self.assertNotIn(operation, ui)
        self.assertIn('storage.setNewAppStorage(', ui)
        self.assertIn('SD-Karte\\nUSB-Speicher', ui)

    def test_installer_pins_target_and_preserves_update_location(self):
        installer = source("src/apps/lua/LuaPackageInstaller.cpp")
        self.assertIn('storage.newAppStorage()', installer)
        self.assertIn('targetSd_ = sd; targetUsb_=usb;targetLive_ = folder;', installer)
        self.assertIn('duplicate_package_locations', installer)
        self.assertIn('if (!copies && (targetSd_||targetUsb_))', installer)
        self.assertIn('targetId_ != id', installer)
        self.assertIn('targetSd_ ? StorageClass::Media : StorageClass::AppPackage', installer)
        self.assertNotIn('storage.fsFor(StorageArea::AppData)', installer)
        self.assertIn('stageFs(appId)', source("src/services/FileTransferService.cpp"))
        self.assertIn('item.appFs = appFs;', source("src/apps/lua/LuaAppRegistry.cpp"))

    def test_preference_does_not_redirect_existing_app_database_or_runtime(self):
        self.assertNotIn('newAppStorage()', source("src/apps/lua/LuaApp.cpp"))
        self.assertIn('getBool("app_store_sd",false)', source("src/services/StorageService.cpp"))
        self.assertIn('"app_store_sd"', source("src/services/ConfigService.cpp"))

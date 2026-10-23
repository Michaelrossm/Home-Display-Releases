from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text(encoding='utf-8')

class FirmwareMediaUpdateTests(unittest.TestCase):
    def test_shared_firmware_folder_on_external_media(self):
        self.assertIn('"/Firmware"', read('src/services/ExternalStorageLayout.h'))
        h=read('src/services/FirmwareMediaUpdateService.h')
        self.assertIn('/Firmware/Firmware-P4.bin',h)
        self.assertIn('/Firmware/Firmware-C6.bin',h)
        storage=read('src/services/StorageService.cpp')
        self.assertLess(storage.index('if(sdReady_){v.fs=&SD_MMC;'),storage.index('usbAppFileSystemFor(v.generation)'))

    def test_web_direct_ota_and_native_media_paths_remain_verified(self):
        s=read('src/services/FileTransferService.cpp')
        upload=s.split('esp_err_t FileTransferService::firmwareUploadHandler(',1)[1].split('esp_err_t FileTransferService::firmwareRebootHandler(',1)[0]
        self.assertIn('if (!writeAuthorized(req)) return ESP_OK;',upload)
        self.assertIn('OtaWriteSession writer',upload)
        self.assertIn('parseFirmwareInfo(firstChunk, firstGot, info)',upload)
        self.assertIn('memcmp(incomingSha, expectedSha, sizeof(incomingSha))',upload)
        self.assertIn('memcmp(partitionSha, expectedSha, sizeof(partitionSha))',upload)
        self.assertLess(upload.index('memcmp(partitionSha'),upload.index('esp_ota_set_boot_partition'))
        self.assertIn('beginWebUpload(runningVersion, info.version)',upload)
        self.assertIn('endWebUpload(true,',upload)
        self.assertNotIn('stageFirmwareUpload',upload)
        self.assertNotIn('"/firmware/c6-upload"',s)

    def test_offline_worker_flash_is_verified_before_boot_selection(self):
        s=read('src/services/FirmwareMediaUpdateService.cpp')
        self.assertIn('submitLocalTransfer(worker,this',s)
        self.assertIn('OtaWriteSession writer',s)
        self.assertLess(s.index('memcmp(real,expected,32)'),s.index('esp_ota_set_boot_partition(target)'))
        self.assertIn('!validC6Hash(expected)',s)
        self.assertLess(s.index('!validC6Hash(expected)'),s.index('hostedBeginUpdate()'))
        self.assertIn('std::unique_lock<std::mutex> mediaLock',s)

    def test_display_routes(self):
        s=read('src/apps/settings/SettingsApp.cpp')
        self.assertIn('mediaP4UpdateCb',s)
        self.assertIn('mediaC6UpdateCb',s)
        self.assertIn('mediaRebootConfirm_',s)
        self.assertIn('FirmwareMediaUpdateService::instance().start(true)',s)

if __name__=='__main__':unittest.main()

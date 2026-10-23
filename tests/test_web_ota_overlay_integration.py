"""Static regressions for the HTTPS direct-OTA LVGL progress bridge."""
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / 'src'


def test_web_upload_reports_versions_and_progress():
    text = (SRC / 'services/FileTransferService.cpp').read_text()
    begin = text.index('esp_err_t FileTransferService::firmwareUploadHandler(')
    end = text.index('esp_err_t FileTransferService::firmwareRebootHandler(', begin)
    upload = text[begin:end]
    assert 'beginWebUpload(runningVersion, info.version)' in upload
    assert 'FirmwareMediaUpdateService::Phase::Writing' in upload
    assert 'req->content_len - remaining' in upload
    assert 'endWebUpload(true,' in upload
    assert 'failUi(' in upload
    assert upload.index('parseFirmwareInfo(firstChunk') < upload.index('beginWebUpload(')
    assert upload.index('endWebUpload(true,') > upload.index('esp_ota_set_boot_partition(')


def test_web_upload_failures_release_overlay():
    text = (SRC / 'services/FileTransferService.cpp').read_text()
    segment = text[text.index('if (!updateUi.beginWebUpload('):text.index('esp_err_t FileTransferService::firmwareRebootHandler(')]
    for marker in ('OTA begin failed', 'OTA write failed', 'upload SHA-256 mismatch',
                   'firmware image validation failed', 'partition SHA-256 failed',
                   'partition SHA-256 mismatch', 'partition metadata read failed',
                   'written firmware metadata mismatch', 'boot slot failed'):
        i = segment.index(marker)
        assert 'failUi(' in segment[max(0, i - 420):i], marker


def test_overlay_stays_visible_until_user_restart():
    text = (SRC / 'ui/FirmwareUpdateOverlay.cpp').read_text()
    assert 'state.phase==Updater::Phase::Ready&&!state.c6' in text
    assert 'state.phase==Updater::Phase::Failed' in text
    assert 'beginFirmwareUpdateOverlay()' in text
    assert 'lv_layer_top()' in text


def test_ota_status_shared_with_media_updater():
    header = (SRC / 'services/FirmwareMediaUpdateService.h').read_text()
    code = (SRC / 'services/FirmwareMediaUpdateService.cpp').read_text()
    for api in ('beginWebUpload', 'webUploadProgress', 'endWebUpload', 'webRebooting'):
        assert api in header and 'FirmwareMediaUpdateService::'+api in code

"""Static safety checks for the lightweight web-overview refresh."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src/services/WebServerService.cpp').read_text(encoding='utf-8')

class WebOverviewStorageMemoryTests(unittest.TestCase):
    def test_memory_uses_finer_kib_resolution(self):
        self.assertIn("(Number(n)/1024).toFixed(1)+' KiB'", SOURCE)
        self.assertIn("memText(m.largestInternal)", SOURCE)
        self.assertNotIn("mb(s.memory.freeInternal)", SOURCE)

    def test_usb_and_sd_are_nonblocking_status(self):
        section=SOURCE.split('esp_err_t WebServerService::statusHandler(',1)[1].split('esp_err_t WebServerService::csrfHandler(',1)[0]
        for expected in ('usb.present()', 'usb.available()', 'usb.capacity()', 'storage.sdAvailable()', 'storage.sdBytes()'):
            self.assertIn(expected, section)
        self.assertNotIn('usb.usage(', section)
        self.assertNotIn('sdFreeBytes(', section)

    def test_overview_friendly_and_escaped(self):
        self.assertIn('id="mUsb"', SOURCE)
        self.assertIn('id="mSd"', SOURCE)
        self.assertIn("message.textContent=problems.length", SOURCE)
        self.assertIn("activePage==='home'", SOURCE)
        self.assertIn('15000', SOURCE)

if __name__=='__main__': unittest.main()

from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NativeAppWebTests(unittest.TestCase):
    def node(self, source, check=False):
        node = shutil.which('node')
        self.assertIsNotNone(node, 'Node is required for browser-script validation')
        result = subprocess.run([node] + (['--check'] if check else []), input=source,
                                text=True, encoding='utf-8', capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_calendar_browser_syntax_and_date_conversion(self):
        page = (ROOT / 'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        script = re.findall(r'<script>(.*?)</script>', page, re.S)[0]
        self.node(script, check=True)
        functions = script.split('function localInput(')[1].split('async function setupAgenda')[0]
        self.node("const assert=require('assert');function localInput(" + functions + """
const sample=new Date(2026,9,12,14,30);
assert.equal(new Date(localInput(sample.getTime()/1000)).getTime(),sample.getTime());
assert.equal(dateInput(sample),'2026-10-12');
""")

    def test_calendar_uses_shared_worker_and_full_event_model(self):
        server = (ROOT / 'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        handler = server.split('esp_err_t WebServerService::calendarJobHandler')[1].split('esp_err_t WebServerService::calendarResultHandler')[0]
        for op in ['query', 'details', 'save', 'delete']:
            self.assertIn('"' + op + '"', handler)
        self.assertIn('LuaJobService::instance().submit(job)', handler)
        self.assertIn('writeAuthorized(req)', handler)
        self.assertIn('settingsAllowed()', handler)
        self.assertNotIn('executeCalendarOperation(', handler)
        page = (ROOT / 'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        for key in ['street', 'house_number', 'postal_code', 'city', 'region', 'country', 'phone', 'notes', 'participants', 'reminder_minutes', 'excluded', 'revision', 'next_cursor']:
            self.assertIn(key, page)
        self.assertIn('e.read_only', page)
        self.assertIn('c.editable', page)
        self.assertIn('generation!==agendaGeneration', page)
        self.assertIn('button.disabled=true', page)
        self.assertIn('hpWeb.requireVisible()', page)

    def test_files_browser_syntax_and_filename_validation(self):
        source = (ROOT / 'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        tail = source.split('String tail = R"HTML(')[1].split('tail.replace(')[0]
        tail = tail.replace("')HTML\";\n    tail += areaName;\n    tail += R\"HTML('", "sd")
        script = re.findall(r'<script>(.*?)</script>', tail, re.S)[0]
        self.node(script, check=True)
        joined = 'function joined(' + script.split('function joined(')[1].split('function createFolder')[0]
        self.node("const assert=require('assert'),CURRENT='/data';" + joined + """
assert.equal(joined('Grüße.jpg'),'/data/Grüße.jpg');
for(const value of ['', '.', '..', '../x', 'x/y', 'x\\\\y', 'x:y', '\\0'])
 assert.throws(()=>joined(value));
""")

    def test_files_security_and_bounded_transfer(self):
        source = (ROOT / 'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        handler = source.split('esp_err_t FileTransferService::filesOperationHandler')[1].split('esp_err_t FileTransferService::filesDeleteHandler')[0]
        for required in ['writeAuthorized(req)', 'tabletLocked()', 'settingsAllowed()', 'protectedFlashPath(rel)', 'name=="app"', 'std::try_to_lock', '32*1024*1024', '120000', 'uint8_t bytes[4096]', 'fs->exists(rel)', 'fs->rename(temp,rel)', 'fs->remove(temp)', 'notifyMutation()']:
            self.assertIn(required, handler)
        self.assertNotIn('DisplayUiGuard', handler)
        self.assertNotIn('xTaskCreate', handler)
        for op in ['download', 'upload', 'mkdir', 'rename']:
            server = (ROOT / 'src/services/WebServerService.cpp').read_text(encoding='utf-8')
            self.assertIn('{"/files/' + op + '"', server)
        self.assertIn('StorageClass::UsbAppPackage', source)
        self.assertIn('offset+shown', source)


if __name__ == '__main__':
    unittest.main()

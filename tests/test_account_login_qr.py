from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class AccountQrTests(unittest.TestCase):
    def test_protected_bounded_existing_encoder(self):
        source = (ROOT/'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        qr = source.split('esp_err_t loginQr(')[1].split('esp_err_t status(')[0]
        for expected in ('secureRequest(req)', 'FileTransferService::authorize(req)',
                         'authorizationUrl', 'url.length()>1024', 'QrGenerator::matrix',
                         'headers(req)'):
            self.assertIn(expected, qr)
        self.assertNotIn('password', qr)
        self.assertIn('"/api/accounts/login/qr",HTTP_GET,loginQr', source)

    def test_local_canvas_and_honest_apple_setup(self):
        source = (ROOT/'src/services/AccountConnectScript.h').read_text(encoding='utf-8')
        self.assertIn("getContext('2d')", source)
        self.assertIn('Kein Apple-OAuth-Kalenderzugriff', source)
        qr=(ROOT/'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        self.assertIn('/accounts?setup=icloud', qr)
        self.assertNotIn('url="https://account.apple.com/"', qr)
        self.assertIn('canvas.hidden=true', source)
        self.assertNotIn('api.qrserver', source)
        self.assertNotIn('chart.googleapis', source)

    def test_native_cached_qr_and_list_first(self):
        source = (ROOT/'src/ui/EmailAccountsUi.cpp').read_text(encoding='utf-8')
        self.assertIn('url!=form.qrUrl', source)
        self.assertIn('QrGenerator::update', source)
        phonebook = (ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        self.assertIn('void details()', phonebook)
        self.assertIn('phonebookNameLess', phonebook)
        self.assertIn('Alle Kontakte', phonebook)

    def test_calendar_separates_existing_controls(self):
        source = (ROOT/'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        self.assertIn('settingsSections.forEach', source)
        self.assertIn('Ansichtsfilter, keine Anmeldung', source)

if __name__ == '__main__':
    unittest.main()

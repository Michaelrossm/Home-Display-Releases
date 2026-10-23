import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EmailAccountTests(unittest.TestCase):
    def test_native_registry_validation_locking_and_atomic_failure(self):
        with tempfile.TemporaryDirectory(prefix="hp-email-accounts-") as directory:
            folder = Path(directory)
            shutil.copyfile(ROOT / "tests/native/email_accounts.cpp", folder / "check.cpp")
            shutil.copyfile(ROOT / "src/services/EmailAccountService.h", folder / "EmailAccountService.h")
            shutil.copyfile(ROOT / "src/services/AccountAuthTypes.h", folder / "AccountAuthTypes.h")
            shutil.copyfile(ROOT / "src/services/CalDavDiscovery.h", folder / "CalDavDiscovery.h")
            shutil.copyfile(ROOT / "src/services/CalDavDiscovery.cpp", folder / "CalDavDiscovery.cpp")
            body = (ROOT / "src/services/EmailAccountService.cpp").read_text(encoding="utf-8")
            body = re.sub(r'^#include[^\n]*\n', '', body, flags=re.M)
            (folder / "service.inc").write_text(body, encoding="utf-8")
            (folder / "Arduino.h").write_text(r'''
#pragma once
#include <string>
#include <cstdint>
#include <cctype>
class String {
 std::string value;
public:
 String()=default;String(const char* v):value(v?v:""){}String(std::string v):value(v){}
 String(uint8_t v):value(std::to_string(v)){}
 size_t length()const{return value.size();}const char* c_str()const{return value.c_str();}
 char operator[](size_t i)const{return value[i];}
 void trim(){auto a=value.find_first_not_of(" \r\n\t"),b=value.find_last_not_of(" \r\n\t");value=a==std::string::npos?"":value.substr(a,b-a+1);}
 int indexOf(char c)const{auto n=value.find(c);return n==std::string::npos?-1:int(n);}
 int lastIndexOf(char c)const{auto n=value.rfind(c);return n==std::string::npos?-1:int(n);}
 String substring(size_t a,size_t b=std::string::npos)const{return value.substr(a,b==std::string::npos?b:b-a);}
 bool endsWith(const char* c)const{std::string s(c);return value.size()>=s.size()&&value.compare(value.size()-s.size(),s.size(),s)==0;}
 void toLowerCase(){for(char& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));}
 friend String operator+(const String& a,const String& b){return a.value+b.value;}
 friend bool operator==(const String& a,const String& b){return a.value==b.value;}
 friend bool operator!=(const String& a,const String& b){return !(a==b);}
};
''', encoding="utf-8")
            compiler = shutil.which("g++") or shutil.which("clang++")
            exe = folder / ("check.exe" if os.name == "nt" else "check")
            if compiler:
                command = [compiler, "-std=c++17", "-I", str(folder), "check.cpp", "CalDavDiscovery.cpp", "-o", str(exe)]
            else:
                vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists(): self.skipTest("Host compiler missing")
                install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
                vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
                command = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /utf-8 /EHsc /std:c++17 /I"{folder}" check.cpp CalDavDiscovery.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_encrypted_authenticated_records_and_private_secret_api(self):
        credential = (ROOT / "src/services/CredentialStore.cpp").read_text(encoding="utf-8")
        for value in ("MBEDTLS_CIPHER_ID_AES", "keyBytes, 256", "mbedtls_gcm_auth_decrypt", "esp_fill_random(envelope + 1, 12)", "const uint8_t aad[] = {1, slot}", "if (!ok) mbedtls_platform_zeroize(data, size)"):
            self.assertIn(value, credential)
        self.assertIn('if (config.hasSecret(emailRecordKey(slot).c_str())) return false', credential)
        header = (ROOT / "src/services/EmailAccountService.h").read_text(encoding="utf-8")
        self.assertNotIn("loadCredentials", header.split("private:")[0])
        self.assertNotIn("String password", header)
        self.assertIn("loadCredentials", header.split("private:")[1])
        self.assertIn("kMaxAccounts = 5", header)

    def test_ui_never_preloads_secret_and_wipes_on_delete(self):
        ui = (ROOT / "src/ui/EmailAccountsUi.cpp").read_text(encoding="utf-8")
        self.assertIn("lv_textarea_set_password_show_time(input,0)", ui)
        self.assertIn("eraseInput(form->password, true)", ui)
        self.assertIn("LV_EVENT_DELETE", ui)
        self.assertNotIn("loadCredentials", ui)
        self.assertNotIn("getSecretString", ui)
        self.assertIn("Verbindung noch nicht geprüft", ui)

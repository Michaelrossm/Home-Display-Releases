"""Execute production CalDAV parsing and provider-neutral registry on the host."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CloudAccountTests(unittest.TestCase):
    def test_native_multi_provider_capabilities_and_permission_missing(self):
        api = (ROOT / 'src/services/CloudAccountApi.cpp').read_text(encoding='utf-8')
        portable = api[api.index('int CalendarSyncService::accountSlot('):api.index('bool CalendarSyncService::authorizeCapability(')]
        with tempfile.TemporaryDirectory(prefix='hp-cloud-') as directory:
            folder = Path(directory)
            for name in ('CloudAccountTypes.h', 'AccountAuthTypes.h', 'EmailAccountService.h'):
                shutil.copyfile(ROOT / 'src/services' / name, folder / name)
            (folder / 'Arduino.h').write_text(r'''
#pragma once
#include <cstdint>
#include <string>
#include <cctype>
class String {std::string v;public:
 String()=default;String(const char* s):v(s?s:""){}String(std::string s):v(s){}String(uint8_t i):v(std::to_string(i)){}
 size_t length()const{return v.size();}const char* c_str()const{return v.c_str();}char operator[](size_t i)const{return v[i];}
 bool startsWith(const char* s)const{return v.rfind(s,0)==0;}
 friend String operator+(const String& a,const String& b){return a.v+b.v;}
 friend bool operator==(const String&a,const String&b){return a.v==b.v;}friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
''', encoding='utf-8')
            source = r'''
#include <cassert>
#include <cstring>
#include "CloudAccountTypes.h"
#include "AccountAuthTypes.h"
namespace hp {
struct Lock {explicit Lock(int){}};
struct Info {String email,name,message;EmailProvider provider=EmailProvider::Google;OAuthPurpose purpose=OAuthPurpose::Identity;
 AccountAuthState authState=AccountAuthState::Idle;bool connected=false,configured=false,davCalendarConnected=false,contactsConnected=false;};
class CalendarSyncService {public:
 int mutex_=1;Info info_[5];bool loaded_[5]{true,true,true,true,true},davConfigured_[5]{},cloudEnabled_[5]{true,true,true,true,true},permissionMissing_[5]{},phonebookEnabled_[5]{};
 struct Dav {uint8_t provider=0;char displayName[97]{};}dav_[5];
 CloudAccountState cloudStatus_[5]{};AccountCapability missingCapability_[5]{};
 OAuthPurpose previousPurpose_[5]{};
 static int accountSlot(const String&);AccountInfo cloudInfo(uint8_t);AccountInfo accountInfo(const String&);
 std::vector<AccountInfo> accounts();std::vector<AccountInfo> accountsWithCapability(AccountCapability);
 bool isConnected(const String&);AccountCapabilities capabilities(const String&);CloudAccountState requestCapability(const String&,AccountCapability);
};
''' + portable + r'''
}
int main(){using namespace hp;CalendarSyncService service;
 for(int i=0;i<5;++i){service.info_[i].configured=true;service.info_[i].connected=true;service.info_[i].authState=AccountAuthState::Connected;service.info_[i].email="account@example.com";}
 service.info_[0].provider=EmailProvider::Microsoft;service.info_[0].purpose=OAuthPurpose::Calendar;
 service.info_[1].provider=EmailProvider::Google;
 for(int i=2;i<4;++i){service.davConfigured_[i]=true;service.dav_[i].provider=uint8_t(i==2?AccountProvider::ICloud:AccountProvider::CalDav);service.cloudStatus_[i]=CloudAccountState::Connected;service.info_[i].davCalendarConnected=true;}
 service.info_[4].provider=EmailProvider::Google;service.info_[4].purpose=OAuthPurpose::Calendar;
 auto accounts=service.accounts();assert(accounts.size()==5);
 for(const auto& account:accounts)assert(!account.phonebookEnabled);
 service.info_[0].purpose=OAuthPurpose::CalendarContacts;
 assert(!(service.capabilities("account-0")&capabilityBit(AccountCapability::Contacts)));
 service.phonebookEnabled_[0]=true;assert(service.capabilities("account-0")&capabilityBit(AccountCapability::Contacts));
 service.info_[0].authState=AccountAuthState::AwaitingUser;service.previousPurpose_[0]=OAuthPurpose::Calendar;
 assert(!(service.capabilities("account-0")&capabilityBit(AccountCapability::Contacts)));
 assert(service.capabilities("account-0")&capabilityBit(AccountCapability::Calendar));
 service.info_[0].authState=AccountAuthState::Connected;service.phonebookEnabled_[0]=false;service.info_[0].purpose=OAuthPurpose::Calendar;
 assert(accounts[0].provider==AccountProvider::Microsoft);assert(accounts[1].provider==AccountProvider::Google);
 assert(accounts[2].provider==AccountProvider::ICloud);assert(accounts[3].provider==AccountProvider::CalDav);
 assert(service.accountsWithCapability(AccountCapability::Calendar).size()==4);
 assert(service.accountsWithCapability(AccountCapability::Profile).size()==3);
 assert(service.accountsWithCapability(AccountCapability::Mail).empty());
 assert(service.requestCapability("account-1",AccountCapability::Calendar)==CloudAccountState::PermissionMissing);
 assert(service.accountInfo("account-1").state==CloudAccountState::PermissionMissing);
 assert(service.capabilities("account-1")&capabilityBit(AccountCapability::Profile));
 assert(service.requestCapability("account-0",AccountCapability::Calendar)==CloudAccountState::Connected);
 assert(service.requestCapability("account-0",AccountCapability::Mail)==CloudAccountState::PermissionMissing);
 assert(service.capabilities("account-0")&capabilityBit(AccountCapability::Calendar));
 service.cloudEnabled_[2]=false;assert(!service.isConnected("account-2"));assert(service.capabilities("account-2")==0);
 assert(service.accounts().size()==5);assert(service.accountsWithCapability(AccountCapability::Calendar).size()==3);
 service.info_[3].connected=false;service.cloudStatus_[3]=CloudAccountState::InvalidCredentials;
 assert(service.accountInfo("account-3").state==CloudAccountState::InvalidCredentials);assert(service.capabilities("account-3")==0);
 assert(!service.isConnected("account-5"));assert(service.capabilities("../account-0")==0);
 assert(service.requestCapability("account-5",AccountCapability::Calendar)==CloudAccountState::Error);
 service.info_[4].authState=AccountAuthState::Error;service.info_[4].connected=false;service.cloudStatus_[4]=CloudAccountState::Connected;
 assert(service.accountInfo("account-4").state==CloudAccountState::Error);
}
'''
            (folder / 'check.cpp').write_text(source, encoding='utf-8')
            compiler = shutil.which('g++') or shutil.which('clang++')
            exe = folder / ('check.exe' if os.name == 'nt' else 'check')
            if compiler:
                command = [compiler, '-std=c++17', '-I', str(folder), 'check.cpp', '-o', str(exe)]
            else:
                vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
                if not vswhere.exists(): self.skipTest('Host compiler missing')
                install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
                vcvars = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
                command = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /utf-8 /EHsc /std:c++17 /I"{folder}" check.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_one_runtime_private_credentials_worker_tls_and_logout(self):
        alias = (ROOT / 'src/services/CloudAccountService.h').read_text(encoding='utf-8')
        self.assertIn('using CloudAccountService=CalendarSyncService', alias)
        api = (ROOT / 'src/services/CloudAccountApi.cpp').read_text(encoding='utf-8')
        connector = (ROOT / 'src/services/CalDavConnector.cpp').read_text(encoding='utf-8')
        service = (ROOT / 'src/services/CalendarSyncService.cpp').read_text(encoding='utf-8')
        for name in ('accountsWithCapability', 'requestCapability', 'authorizeCapability', 'calendarCollections', 'publishAccountEvents'):
            self.assertIn(name, api)
        for name in ('AccountConnected', 'AccountDisconnected', 'AccountAuthExpired', 'AccountSyncError', 'AccountCapabilitiesChanged', 'AccountPermissionMissing'):
            self.assertIn('EventType::' + name, api)
        self.assertIn('NetworkWorker::instance().submit', api)
        self.assertIn('writeOAuthRecord(davKey(slot)', api)
        self.assertIn('StorageArea::Config,metaPath(slot)', api)
        self.assertIn('removeOAuthRecord(davKey.c_str())', service)
        self.assertIn('removeEmailRecord(slot)', service)
        for required in ('esp_crt_bundle_attach', 'cfg.disable_auto_redirect=true', 'CalDavDiscovery::allowedTarget', 'CalDavCore::instance().parseDiscovery'):
            self.assertIn(required, connector)
        for forbidden in ('Serial.', 'setInsecure', 'verify=false', 'xTaskCreate'):
            self.assertNotIn(forbidden, api + connector)
        routes = (ROOT / 'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        for route in ('/api/accounts/icloud/connect', '/api/accounts/caldav/connect', '/api/accounts/disconnect'):
            self.assertIn(route, routes)
        for secret in ('access_token', 'refresh_token', 'client_secret', 'readOAuthRecord', 'loadCredentials', 'localStorage'):
            self.assertNotIn(secret, routes)
        self.assertIn('type="password"',routes) # write-only app-password provisioning over TLS
        self.assertIn('secureRequest(req)',routes)
        self.assertIn('SecretWipe',routes)
        types = (ROOT / 'src/services/CloudAccountTypes.h').read_text(encoding='utf-8')
        for secret in ('password', 'refreshToken', 'accessToken'):
            self.assertNotIn(secret, types)


if __name__ == '__main__': unittest.main()

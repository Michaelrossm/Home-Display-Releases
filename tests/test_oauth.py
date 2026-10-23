"""Host execution of production OAuth parsing/state guards plus boundary checks."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class OAuthTests(unittest.TestCase):
    def test_native_dates_state_expiry_replay_backoff_and_redirects(self):
        protocol = (ROOT / 'src/services/OAuthProtocol.cpp').read_text(encoding='utf-8')
        # Compile the exact portable production functions, not a Python simulation.
        portable = protocol[protocol.index('String encode('):protocol.index('String randomToken(')]
        portable += protocol[protocol.index('int64_t utcDate('):protocol.index('String utcText(')]
        policy = (ROOT / 'src/services/AccountAuthTypes.h').read_text(encoding='utf-8')
        portable += policy[policy.index('namespace hp {'):]
        production = (ROOT / 'src/services/CalendarSyncService.cpp').read_text(encoding='utf-8')
        portable += r'''
namespace hp {
uint32_t tick=100;uint32_t millis(){return tick;}
bool supported(EmailProvider p){return p==EmailProvider::Google||p==EmailProvider::Microsoft;}
struct Guard {explicit Guard(int){}};
struct SecurityWriteGuard {};
struct SecurityService {bool allowed=true,locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}};
struct Info {EmailProvider provider=EmailProvider::Google;OAuthPurpose purpose=OAuthPurpose::Identity;AccountAuthState authState=AccountAuthState::Idle;bool connected=false,configured=false,working=false;String message;};
struct CalendarSyncService {
 enum class Operation {Connect,Poll,Callback};
 struct Job {uint8_t slot;Operation operation;uint32_t generation;};
 struct Session {uint32_t expires=0;};
 int mutex_=1;Info info_[5],providerStatus_[2];Session sessions_[5];
 bool loaded_[5]{true,true,true,true,true},pending_[5]{},deleting_[5]{},identify_[5]{},restored_[5]{};
 bool davConfigured_[5]{},cloudEnabled_[5]{true,true,true,true,true},permissionMissing_[5]{},phonebookEnabled_[5]{};
 uint32_t generation_[5]{};uint8_t lastLoginSlot_[2]{255,255};OAuthPurpose previousPurpose_[5]{};bool queueAvailable=true;unsigned queued=0;
 bool enqueue(uint8_t slot,Operation){if(!queueAvailable)return false;++queued;pending_[slot]=true;info_[slot].working=true;return true;}
 void clearSession(uint8_t slot){sessions_[slot]={};}
 uint8_t selectSlot(EmailProvider,bool);bool startMicrosoftLogin();bool startGoogleLogin(OAuthPurpose purpose=OAuthPurpose::Identity);
 bool startLogin(EmailProvider,OAuthPurpose,uint8_t target=255);bool cancelLogin(EmailProvider);bool current(const Job&);
};
'''
        portable += production[production.index('uint8_t CalendarSyncService::selectSlot('):production.index('CalendarConnectionInfo CalendarSyncService::getAccountState(')]
        portable += production[production.index('bool CalendarSyncService::startMicrosoftLogin('):production.index('bool CalendarSyncService::disconnectAccount(')]
        portable += production[production.index('bool CalendarSyncService::current('):production.index('void CalendarSyncService::publish(')]
        portable += '}\n'
        with tempfile.TemporaryDirectory(prefix='hp-oauth-') as directory:
            folder = Path(directory)
            source = r'''
#include <string>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cassert>
class String {
 std::string v;
public:
 String()=default;String(const char* s):v(s?s:""){}String(std::string s):v(s){}
 size_t length()const{return v.size();}char operator[](size_t i)const{return v[i];}const char* c_str()const{return v.c_str();}
 int indexOf(char c,size_t at=0)const{auto n=v.find(c,at);return n==std::string::npos?-1:int(n);}
 String substring(size_t a,size_t b=std::string::npos)const{return v.substr(a,b==std::string::npos?b:b-a);}
 bool startsWith(const char* s)const{return v.rfind(s,0)==0;}
 bool endsWith(const char* s)const{std::string x(s);return v.size()>=x.size()&&v.compare(v.size()-x.size(),x.size(),x)==0;}
 void toLowerCase(){for(char& c:v)c=char(std::tolower(uint8_t(c)));}
 String& operator+=(char c){v+=c;return *this;}
 friend bool operator==(const String& a,const String& b){return a.v==b.v;}
 friend bool operator!=(const String& a,const String& b){return !(a==b);}
};
namespace hp {enum class EmailProvider:uint8_t {Google,Microsoft,Count};}
''' + portable + r'''
int main(){
 {
 using namespace hp;CalendarSyncService service;
 assert(service.startMicrosoftLogin());assert(service.queued==1);
 auto slot=service.lastLoginSlot_[1];assert(slot==0);assert(service.info_[slot].provider==EmailProvider::Microsoft);
 assert(service.info_[slot].authState==AccountAuthState::RequestingCode);
 assert(!service.startMicrosoftLogin());assert(service.queued==1);
 CalendarSyncService::Job stale{slot,CalendarSyncService::Operation::Poll,service.generation_[slot]};
 assert(service.cancelLogin(EmailProvider::Microsoft));assert(service.info_[slot].authState==AccountAuthState::Cancelled);
 assert(!service.current(stale));assert(!service.pending_[slot]);assert(!service.cancelLogin(EmailProvider::Microsoft));
 service.info_[slot].connected=true;service.info_[slot].purpose=OAuthPurpose::Calendar;
 assert(service.startMicrosoftLogin());assert(service.info_[slot].purpose==OAuthPurpose::Identity);
 assert(service.cancelLogin(EmailProvider::Microsoft));assert(service.info_[slot].purpose==OAuthPurpose::Calendar);
 service.info_[slot].connected=false;
 service.queueAvailable=false;assert(!service.startMicrosoftLogin());assert(service.info_[slot].authState==AccountAuthState::Error);
 service.queueAvailable=true;assert(service.startGoogleLogin());slot=service.lastLoginSlot_[0];assert(service.info_[slot].provider==EmailProvider::Google);
 assert(service.info_[slot].purpose==OAuthPurpose::Identity);assert(service.cancelLogin(EmailProvider::Google));
 assert(service.startGoogleLogin(OAuthPurpose::Calendar));slot=service.lastLoginSlot_[0];assert(service.info_[slot].purpose==OAuthPurpose::Calendar);
 CalendarSyncService::Job expiry{slot,CalendarSyncService::Operation::Poll,service.generation_[slot]};service.sessions_[slot].expires=tick+1;
 assert(service.current(expiry));++tick;assert(!service.current(expiry));
 assert(service.cancelLogin(EmailProvider::Google));SecurityService::instance().allowed=false;
 assert(!service.startMicrosoftLogin());assert(!service.startGoogleLogin());SecurityService::instance().allowed=true;
 service.info_[4].configured=true;service.info_[4].provider=EmailProvider::Microsoft;
 assert(service.startLogin(EmailProvider::Microsoft,OAuthPurpose::Calendar,4));assert(service.lastLoginSlot_[1]==4);
 assert(service.cancelLogin(EmailProvider::Microsoft));service.cloudEnabled_[4]=false;
 assert(!service.startLogin(EmailProvider::Microsoft,OAuthPurpose::Calendar,4));
 service.cloudEnabled_[4]=true;assert(!service.startLogin(EmailProvider::Google,OAuthPurpose::Identity,4));
 assert(!service.startLogin(EmailProvider::Microsoft,OAuthPurpose::Contacts,4));
 service.phonebookEnabled_[4]=true;assert(service.startLogin(EmailProvider::Microsoft,OAuthPurpose::CalendarContacts,4));
 assert(service.cancelLogin(EmailProvider::Microsoft));
 assert(AccountAuthPolicy::hasCalendar(OAuthPurpose::CalendarContacts)&&AccountAuthPolicy::hasContacts(OAuthPurpose::CalendarContacts));
 for(auto purpose:{OAuthPurpose::Identity,OAuthPurpose::Calendar,OAuthPurpose::Contacts,OAuthPurpose::CalendarContacts})assert(AccountAuthPolicy::fromTag(AccountAuthPolicy::purposeTag(purpose))==purpose);
 assert(AccountAuthPolicy::scopeHas("user.read contacts.read","contacts.read"));
 assert(!AccountAuthPolicy::scopeHas("contacts.readwrite","contacts.read"));
 assert(!AccountAuthPolicy::scopeHas("","contacts.read"));
 }
 assert(utcDate("1970-01-02")==86400);
 assert(utcDate("2024-02-29T12:30:00Z")==1709209800);
 assert(utcDate("2024-02-29T13:30:00+01:00")==1709209800);
 assert(utcDate("2024-02-29T12:30:00.0000000")==1709209800);
 assert(utcDate("2024-02-29T07:00:00-05:30")==1709209800);
 for(const char* s:{"2023-02-29","2024-04-31","2024-13-01","2024-01-00","2024-01-01T-1:00:00Z","2024-01-01T24:00:00Z","2024-01-01T12:30:60Z","2024-01-01T12:30:00+24:00","2024-01-01T12:30:00junk","2024-01-01junk","2024-01-01T12:30:00.Z"})assert(utcDate(s)==0);
 String state(std::string(64,'a')),other(std::string(64,'b'));
 assert(callbackAllowed(state,state,1000,999,false,false));
 assert(!callbackAllowed(state,state,1000,1000,false,false));
 assert(!callbackAllowed(state,state,1000,999,true,false));
 assert(!callbackAllowed(state,state,1000,999,false,true));
 assert(!callbackAllowed(state,other,1000,999,false,false));
 assert(!callbackAllowed("", "",1000,999,false,false));
 assert(callbackAllowed(state,state,100,0xfffffff0u,false,false));
 assert(!callbackAllowed(state,state,0xfffffff0u,100,false,false));
 assert(pollDelay(5000,false)==5000);assert(pollDelay(5000,true)==10000);
 assert(pollDelay(58000,true)==60000);assert(pollDelay(0,false)==1000);
 assert(pollDelay(0xffffffffu,true)==60000);
 assert(httpsRedirect("https://auth.example.com/oauth/callback"));
 for(const char* s:{"http://auth.example.com/oauth/callback","https://localhost/oauth/callback","https://a.local/oauth/callback","https://192.168.1.2/oauth/callback","https://auth.example.com/other","https://user@auth.example.com/oauth/callback","https://auth.example.com/oauth/callback?x=1"})assert(!httpsRedirect(s));
 assert(encode("a+b /?")=="a%2Bb%20%2F%3F");
}
'''
            (folder / 'check.cpp').write_text(source, encoding='utf-8')
            compiler = shutil.which('g++') or shutil.which('clang++')
            executable = folder / ('check.exe' if os.name == 'nt' else 'check')
            if compiler:
                command = [compiler, '-std=c++17', 'check.cpp', '-o', str(executable)]
            else:
                vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
                if not vswhere.exists(): self.skipTest('Host compiler missing')
                install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
                vcvars = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
                command = f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /utf-8 /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_worker_tls_identity_binding_and_private_credentials(self):
        service = (ROOT / 'src/services/CalendarSyncService.cpp').read_text(encoding='utf-8')
        for text in ('NetworkWorker::instance().submit', 'esp_crt_bundle_attach', 'cfg.disable_auto_redirect=true', 'email_verified', 'identity(account.address,email)', 'code_challenge_method=S256', 'form("code_verifier"', 'writeAtomic(StorageArea::UserData', 'clearSession(job.slot)', 'mbedtls_platform_zeroize'):
            self.assertIn(text, service)
        for text in ('setInsecure', 'xTaskCreate', 'Serial.', 'appendEvent('): self.assertNotIn(text, service)
        self.assertIn('HeapErased<TokenRecord>', service)
        self.assertIn('clearAccess(slot)', service)
        self.assertIn('uint32_t(expires-60)*1000', service)
        self.assertNotIn('Erased<TokenRecord> token', service.replace('HeapErased<TokenRecord> token', 'heap-record'))
        credential = (ROOT / 'src/services/CredentialStore.h').read_text(encoding='utf-8')
        self.assertNotIn('OAuthRecord', credential.split('private:')[0])
        self.assertIn('CalendarSyncService::instance().backgroundTick()', (ROOT / 'src/main.cpp').read_text(encoding='utf-8'))

    def test_web_auth_post_csrf_callback_and_no_secret_export(self):
        routes = (ROOT / 'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        self.assertEqual(routes.count('if(!FileTransferService::authorize(req))'), 6)
        self.assertIn('"/accounts/action",HTTP_POST', routes)
        self.assertIn('X-HP-CSRF', routes)
        self.assertIn('completeGoogle(decode(state),decoded)', routes)
        self.assertIn('Referrer-Policy', routes)
        self.assertIn('row["code"]=info.userCode', routes)
        for secret in ('refresh_token', 'access_token', 'device_code', 'client_secret', 'readOAuthRecord', 'loadCredentials'): self.assertNotIn(secret, routes)

    def test_account_change_invalidates_authorization_and_passwordless_oauth(self):
        accounts = (ROOT / 'src/services/EmailAccountService.cpp').read_text(encoding='utf-8')
        self.assertIn('CalendarSyncService::instance().disconnect(slot)', accounts)
        self.assertIn('removeOAuthRecord(id.c_str())', accounts)
        self.assertIn('provider!=EmailProvider::Google&&provider!=EmailProvider::Microsoft', accounts)
        ui = (ROOT / 'src/ui/EmailAccountsUi.cpp').read_text(encoding='utf-8')
        self.assertIn('eraseInput(form->clientSecret, true)', ui)
        self.assertNotIn('readOAuthRecord', ui)

    def test_guided_provider_routes_are_queued_and_status_is_cache_only(self):
        routes = (ROOT / 'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        for path in ('/api/accounts', '/api/accounts/microsoft/start', '/api/accounts/google/start', '/api/accounts/login/status', '/api/accounts/login/cancel', '/api/accounts/microsoft/disconnect', '/api/accounts/google/disconnect'):
            self.assertIn('"' + path + '"', routes)
        self.assertNotIn('EmailAccountService::instance().account', routes)
        self.assertNotIn('localStorage', routes)
        # Provider OAuth remains passwordless; only TLS-gated CalDAV setup has
        # a write-only app-password field.
        self.assertIn('type="password"', routes)
        self.assertIn('secureRequest(req)', routes)
        self.assertIn('SecretWipe', routes)
        self.assertIn('textContent', routes)
        service = (ROOT / 'src/services/CalendarSyncService.cpp').read_text(encoding='utf-8')
        start = service[service.index('bool CalendarSyncService::startLogin('):service.index('bool CalendarSyncService::cancelLogin(')]
        self.assertIn('enqueue(slot,Operation::Connect)', start)
        self.assertNotIn('request(', start)
        cancel = service[service.index('bool CalendarSyncService::cancelLogin('):service.index('bool CalendarSyncService::disconnectAccount(')]
        self.assertIn('++generation_[slot]', cancel)
        self.assertIn('clearSession(slot)', cancel)
        self.assertIn('AccountAuthState::Cancelled', cancel)
        self.assertIn('AccountAuthState::Expired', service)
        self.assertIn('oauth2.googleapis.com/device/code', service)
        self.assertIn('login.microsoftonline.com/common/oauth2/v2.0/devicecode', service)
        self.assertIn('removeOAuthRecord(tokenKey(slot).c_str())', service)
        self.assertIn('deleting_[slot]=!ok', service)
        self.assertIn('Client-ID) eingerichtet', service)
        self.assertIn('Google-Funktion ist eine zusätzliche OAuth-Konfiguration erforderlich', service)
        defaults = (ROOT / 'include/OAuthClientDefaults.h').read_text(encoding='utf-8')
        self.assertNotIn('CLIENT_SECRET', defaults)
        for name in ('MICROSOFT_CLIENT_ID', 'GOOGLE_DEVICE_CLIENT_ID', 'GOOGLE_WEB_CLIENT_ID'):
            self.assertIn('HOMEPANEL_' + name + ' ""', defaults)

if __name__ == '__main__': unittest.main()

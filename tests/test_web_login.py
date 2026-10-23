from pathlib import Path
import unittest
import re
import shutil
import subprocess
import test_system_performance
ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
class WebLoginTests(unittest.TestCase):
    def test_actual_policy_expiry_clock_and_redirects(self):
        code='#include "WebLoginPolicy.h"\n#include <cassert>\n#include <initializer_list>\n'+r'''
int main(){using namespace hp::webLogin;Session s;
 assert(!active(s,1700000000,0));s.issued=1700000000;s.started=100;
 assert(active(s,1700000000,100));assert(active(s,1700000000,100+uint64_t(sessionSeconds)*1000-1));
 assert(!active(s,1700000000,100+uint64_t(sessionSeconds)*1000));assert(!active(s,1700000000,(1ull<<32)+100));
 s.remembered=1;s.expires=s.issued+rememberSeconds;
 assert(active(s,s.issued,0));assert(active(s,s.expires-1,0));assert(!active(s,s.expires,0));assert(!active(s,s.issued-1,0));
 s.expires++;assert(!active(s,s.issued,0));s.expires--;s.remembered=2;assert(!active(s,s.issued,0));
 assert(lockSeconds(4)==0&&lockSeconds(5)==120&&lockSeconds(10)==300&&lockSeconds(15)==600&&lockSeconds(20)==1800);
 assert(next("/calendar?connect=icloud"));for(auto v:{"https://evil.test","//evil.test","/\\evil","/\nLocation:",""})assert(!next(v));
 assert(token("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));assert(!token("bad"));
 uint8_t a[]={1,2},b[]={1,2};assert(equal(a,b,2));b[1]=3;assert(!equal(a,b,2));
}
'''
        test_system_performance.SystemPerformanceTests().native(code,[ROOT/'src/services'])
    def test_routes_security_persistence_and_no_extra_task(self):
        service=read('src/services/WebLoginService.cpp')
        for text in ('esp_fill_random','mbedtls_sha256','Secure; HttpOnly; SameSite=Strict','WebCsrfService::instance().validate(req)','X-HP-Login','disable','PasswordWipe'):
            if text!='disable':self.assertIn(text,service)
        self.assertIn('writeOAuthRecord("web_sessions"',service)
        self.assertIn('passwordMatches(s)',service)
        self.assertIn('s.record=prior',service)
        self.assertNotIn('xTaskCreate',service)
        for path in ['src/services/WebServerService.cpp','src/services/FileTransferService.cpp']:
            src=read(path)
            for endpoint in ('/login','/login.js','/api/auth/login','/api/auth/logout'):self.assertIn('"'+endpoint+'"',src)
        self.assertIn('WWW-Authenticate',read('src/services/RecoveryWebService.cpp'))
        self.assertNotIn('localStorage',read('src/services/WebLoginPage.h'))
        self.assertIn('Angemeldet bleiben (30 Tage)',read('src/services/WebLoginPage.h'))
    def test_actual_browser_login_clears_password_and_preserves_local_destination(self):
        script=re.search(r'webLoginScript\[\]=R"LOGIN\((.*?)\)LOGIN"',read('src/services/WebLoginPage.h'),re.S).group(1)
        code=r'''
const assert=require('assert');
const nodes={};const document={getElementById:id=>(nodes[id]??={value:'',hidden:false,disabled:false})};
const location={search:'?next=%2Fcalendar%3Fconnect%3Dicloud',replace:url=>{location.target=url}};
let requests=[];
const fetch=async(url,args={})=>{requests.push([url,args]);return {ok:url!='/api/csrf',json:async()=>({token:'csrf'}),text:async()=>''}};
'''+script+r'''
(async()=>{$('username').value='admin';$('password').value='secret';$('remember').checked=true;
 await nodes.login.onsubmit({preventDefault(){}});
 assert.equal(nodes.password.value,'');assert.equal(location.target,'/calendar?connect=icloud');
 const [,args]=requests.find(([u])=>u==='/api/auth/login');assert.equal(args.headers['X-HP-Login'],'1');assert.equal(args.credentials,'same-origin');assert.equal(JSON.parse(args.body).remember,true);
 assert.equal(nodes.submit.disabled,false);
})().catch(e=>{console.error(e);process.exitCode=1});
'''
        result=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr)

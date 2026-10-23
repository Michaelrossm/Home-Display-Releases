from pathlib import Path
import re
import shutil
import subprocess
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')

class WebDashboardTests(unittest.TestCase):
    def test_csp_allows_the_shared_same_origin_lifecycle_script(self):
        policies=re.findall(r'"Content-Security-Policy",\s*"([^"]+)"',SOURCE)
        self.assertGreaterEqual(len(policies),2)
        for policy in policies:
            directives={parts[0]:parts[1:] for item in policy.split(';') if (parts:=item.split())}
            if 'script-src' not in directives:
                # The read-only license page intentionally has no JavaScript.
                self.assertEqual(directives['default-src'],["'none'"])
                self.assertEqual(directives['frame-ancestors'],["'none'"])
                license_page=SOURCE.split('esp_err_t WebServerService::licenseHandler',1)[1].split('esp_err_t WebServerService::intercomDiagnosticsHandler',1)[0]
                self.assertNotIn('<script',license_page)
                continue
            self.assertIn("'self'",directives['script-src'], 'hpWeb bootstrap must not be blocked by CSP')
            self.assertIn("'unsafe-inline'",directives['script-src'])
            self.assertNotIn('*',directives['script-src'])
            self.assertNotIn("'unsafe-eval'",directives['script-src'])
            self.assertEqual(directives['connect-src'],["'self'"])
            self.assertEqual(directives['frame-ancestors'],["'none'"])
        self.assertIn('<script src="/app-web-lifecycle.js"></script><script>',SOURCE)
        lifecycle=SOURCE.split('esp_err_t WebServerService::appWebLifecycleHandler',1)[1].split('esp_err_t WebServerService::calendarPageHandler',1)[0]
        self.assertIn('authorized(req)',lifecycle)
        self.assertIn('application/javascript',lifecycle)

    def test_actual_app_route_parser_and_registry_gate(self):
        route=(ROOT/'src/services/AppWebRoute.h').read_text(encoding='utf-8').replace('#pragma once','')
        handler='esp_err_t WebServerService::appPageHandler'+SOURCE.split('esp_err_t WebServerService::appPageHandler',1)[1].split('esp_err_t WebServerService::statusHandler',1)[0]
        fixture=test_system_performance.SystemPerformanceTests().string_fixture()+route+r'''
using esp_err_t=int;constexpr int ESP_OK=0,HTTPD_404_NOT_FOUND=404;
struct httpd_req_t{const char* uri;bool authenticated=true;int status=200;String location;};
bool authorized(httpd_req_t* r){if(!r->authenticated)r->status=401;return r->authenticated;}
void commonHeaders(httpd_req_t*){}
int httpd_resp_send_err(httpd_req_t* r,int code,const char*){r->status=code;return 0;}
void httpd_resp_set_status(httpd_req_t* r,const char*){r->status=308;}
void httpd_resp_set_hdr(httpd_req_t* r,const char*,const char* value){r->location=value;}
int httpd_resp_send(httpd_req_t*,const char*,int){return 0;}
namespace hp {
enum class AppKind{Lua,Native,Web};
struct AppRegistryEntry{AppKind kind=AppKind::Lua;String webPath;};
struct AppRegistry{bool installed=true,web=true;unsigned reads=0;static AppRegistry& instance(){static AppRegistry r;return r;}bool lookup(const char* id,AppRegistryEntry& out){++reads;if(!installed||strcmp(id,"printer_panel"))return false;out.webPath=web?"/apps/printer_panel/":"";return true;}};
unsigned rendered=0;
struct WebServerService{static int appPageHandler(httpd_req_t*);static int rootHandler(httpd_req_t*){++rendered;return 0;}};
'''+handler+r'''
}
int main(){using namespace hp;char id[32];bool slash;
assert(parseAppWebRoute("/apps/printer_panel/",id,slash)&&slash&&!strcmp(id,"printer_panel"));
assert(parseAppWebRoute("/apps/strom/?id=radio",id,slash)&&!strcmp(id,"strom"));
assert(parseAppWebRoute("/apps/radio",id,slash)&&!slash);
for(const char* bad:{"/apps/","/apps/../","/apps/a/b/","/apps/%2e%2e/","/apps/a%2fb/","/apps/a\\b/","/apps/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/","/apps//radio/"})assert(!parseAppWebRoute(bad,id,slash));
httpd_req_t req{"/apps/printer_panel/"};WebServerService::appPageHandler(&req);assert(rendered==1&&req.status==200);
req={"/apps/printer_panel"};WebServerService::appPageHandler(&req);assert(req.status==308&&req.location=="/apps/printer_panel/"&&rendered==1);
auto& registry=AppRegistry::instance();registry.web=false;req={"/apps/printer_panel/"};WebServerService::appPageHandler(&req);assert(req.status==404&&rendered==1);
registry.web=true;registry.installed=false;req={"/apps/printer_panel/"};WebServerService::appPageHandler(&req);assert(req.status==404);
const auto reads=registry.reads;req={"/apps/printer_panel/",false};WebServerService::appPageHandler(&req);assert(req.status==401&&registry.reads==reads);
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture)
        self.assertIn('cfg.uri_match_fn = httpd_uri_match_wildcard',SOURCE)
        self.assertLess(SOURCE.index('registerOAuthWebRoutes(server_)'),SOURCE.index('const httpd_uri_t appRoute='))
        self.assertNotIn('fs.exists',handler)
        self.assertNotIn('entry.app',handler)

    def test_direct_app_page_selects_path_owner_and_skips_dashboard_work(self):
        start=SOURCE.split('const appRoute=',1)[1].split('async function loadDiagnostics',1)[0]
        startup=SOURCE.split('bindAppInstaller();hpWeb.whenVisible(',1)[1].split('\n',1)[0]
        code=r'''
const assert=require('assert');const location={pathname:'/apps/printer_panel/',search:'?app_settings=radio'};
let activePage='home',calls=[];const $=()=>({classList:{add(){}}});
const document={querySelectorAll:()=>[],querySelector:()=>null};
const safeLoad=async(_,fn)=>fn(),ensureCsrf=async()=>calls.push('csrf'),editAppSettings=async id=>calls.push(id),refreshAll=async()=>calls.push('dashboard');
const appRoute='''+start+r'''
(async()=>{assert.equal(settingsOwner,'printer_panel');await ('''+startup.removesuffix(');')+r''')();assert.deepEqual(calls,['csrf','printer_panel']);assert.equal(activePage,'apps');})().catch(e=>{console.error(e);process.exitCode=1});
'''
        run=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(run.returncode,0,run.stderr)
    def test_javascript_navigation_and_lazy_requests(self):
        node=shutil.which('node')
        self.assertIsNotNone(node, 'Node is required to validate the embedded browser code')
        html=SOURCE.split('R"HTML(',1)[1].split(')HTML"',1)[0]
        script=html.split('<script>',1)[1].split('</script>',1)[0]
        check=subprocess.run([node,'--check'],input=script,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(check.returncode,0,check.stderr)
        navigation=script.split("let activePage='home'",1)[1].split("function renderWebApps",1)[0]
        refresh=re.search(r'async function refreshAll\(\)\{.*?\}',script).group()
        code="""
const assert=require('assert');let calls=[];
const $=()=>({textContent:'',scrollIntoView(){}});
const document={hidden:false,querySelectorAll:()=>[]};
const safeLoad=async(n,fn)=>{await fn()};
const loadNetwork=async()=>{calls.push('network')};
const loadSettings=async()=>{calls.push('system')};
const loadLanguage=async()=>{calls.push('language')};
const loadDiagnostics=async()=>{calls.push('diagnostics')};
const loadService8088=async()=>{calls.push('service')};
const loadApps=async()=>{calls.push('apps')};
const loadStatus=async()=>{calls.push('status')};
const ensureCsrf=async()=>{calls.push('csrf')};
let activePage='home';
""".replace("let activePage='home';", "let activePage='home'"+navigation)+refresh+"""
(async()=>{
 await refreshAll();assert.deepEqual(calls,['csrf','status','apps']);calls=[];
 await showPage('settings');assert.deepEqual(calls,['network']);calls=[];
 await showSettings('system');assert.deepEqual(calls,['system','language','service']);calls=[];
 await showPage('home');await refreshSettingsSection('network');assert.deepEqual(calls,[]);
 await showPage('settings');calls=[];document.hidden=true;
 await refreshSettingsSection();assert.deepEqual(calls,[]);
})().catch(e=>{console.error(e);process.exitCode=1});
"""
        run=subprocess.run([node],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(run.returncode,0,run.stderr)

    def test_web_descriptors_are_cached_and_generic(self):
        self.assertIn('virtual const char* webPath()', (ROOT/'src/core/AppManager.h').read_text())
        for path,route in [('calendar/CalendarApp.h','/calendar'),('intercom/IntercomApp.h','/intercom'),('files/FileExplorerApp.h','/files')]:
            self.assertIn(route,(ROOT/'src/apps'/path).read_text())
        registry=(ROOT/'src/apps/lua/LuaAppRegistry.cpp').read_text()
        self.assertIn('result.webSettings=fs.exists',registry)
        self.assertIn('configureWebSettings(item.webSettings)',registry)
        self.assertIn('configureWebSettings(appFs->exists',(ROOT/'src/apps/lua/LuaPackageInstaller.cpp').read_text())
        handler=SOURCE.split('WebServerService::appsHandler(',1)[1].split('WebServerService::appToggleHandler(',1)[0]
        self.assertNotIn('entries[48]',handler)
        self.assertNotIn('fs.exists',handler)
        self.assertIn('jsonEscape(e.webPath)',handler)
        self.assertIn('registry_memory',handler)
        self.assertIn("['http:','https:']",SOURCE)
        self.assertIn('link.rel=\'noopener noreferrer\'',SOURCE)

    def test_catalog_includes_disabled_apps_but_rejects_unsafe_urls(self):
        render='function renderWebApps'+SOURCE.split('function renderWebApps',1)[1].split('let csrfToken',1)[0]
        code="""
const assert=require('assert');
class Element{constructor(){this.children=[]}replaceChildren(){this.children=[]}appendChild(x){this.children.push(x)}get childElementCount(){return this.children.length}}
const host=new Element(),$=()=>host;
const document={createElement:()=>new Element()};
const location={origin:'http://display.local'};
"""+render+"""
renderWebApps([
 {name:'Calendar',kind:'Native',webPath:'/calendar'},
 {name:'Disabled',kind:'Lua',state:'disabled',webPath:'/apps/demo/'},
 {name:'External',kind:'Web',webPath:'http://printer.local/'},
 {name:'No web',kind:'Lua'},
 {name:'Injection',kind:'Web',webPath:'javascript:alert(1)'},
 {name:'Foreign',kind:'Lua',webPath:'//evil.local/'}
]);
assert.equal(host.children.length,3);
assert(host.children[1].children[1].textContent.includes('deaktiviert'));
assert.equal(host.children[2].children[2].rel,'noopener noreferrer');
renderWebApps([]);assert.equal(host.children.length,0);
"""
        run=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(run.returncode,0,run.stderr)

if __name__=='__main__':
    unittest.main()

from pathlib import Path
import unittest
import hashlib
import json
import shutil
import subprocess
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]

class NativeIntercomTests(unittest.TestCase):
    def test_actual_new_device_slots_and_safe_defaults(self):
        source=(ROOT/'src/apps/intercom/IntercomApp.cpp').read_text(encoding='utf-8')
        helper='bool newDevice'+source.split('bool newDevice')[1].split('bool videoStatusDue')[0]
        provider=(ROOT/'src/services/intercom/IntercomProvider.h').read_text(encoding='utf-8').replace('#pragma once','').replace('#include <Arduino.h>','')
        code=r'''
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cassert>
using String=std::string;
size_t strlcpy(char* d,const char* s,size_t n){size_t size=strlen(s);if(n){size_t copy=std::min(size,n-1);memcpy(d,s,copy);d[copy]=0;}return size;}
'''+provider+r'''
using namespace hp;
bool legacy(const IntercomDevice& d){return !strcmp(d.id,"goliath-legacy");}
'''+helper+r'''
int main(){IntercomDevice list[5]{},draft{};
assert(newDevice(list,0,draft));assert(!strcmp(draft.id,"intercom-1"));
assert(!draft.host[0]&&!draft.username[0]&&!draft.participant[0]);assert(draft.enabled&&!draft.ringTone);
assert(!strcmp(draft.owner,"intercom")&&!strcmp(draft.provider,"dahua"));
strlcpy(list[0].id,"goliath-legacy",sizeof(list[0].id));
strlcpy(list[1].id,"intercom-1",sizeof(list[1].id));strlcpy(list[2].id,"intercom-3",sizeof(list[2].id));
assert(newDevice(list,3,draft)&&!strcmp(draft.id,"intercom-2"));
strlcpy(list[3].id,"intercom-2",sizeof(list[3].id));strlcpy(list[4].id,"intercom-4",sizeof(list[4].id));
auto before=draft;assert(!newDevice(list,5,draft));assert(!memcmp(&before,&draft,sizeof(draft)));
// Deleting a profile makes exactly that free slot reusable, regardless of legacy ordering.
list[1]=list[4];assert(newDevice(list,4,draft)&&!strcmp(draft.id,"intercom-1"));
strlcpy(list[0].id,"custom-id",sizeof(list[0].id));assert(!newDevice(list,4,draft));
}
'''
        test_system_performance.SystemPerformanceTests().native(code)
        self.assertIn('if(!selected.id[0]||draft)',source)
        self.assertIn('if(draft)lv_obj_add_state(remove,LV_STATE_DISABLED)',source)
        self.assertIn('const bool current=w.generation==app.generation_.load()',source)
        self.assertIn('if(current&&w.ok',source)
        self.assertIn('p.hideKeyboard()',source)
        self.assertIn('lv_obj_scroll_to_view',source)
        self.assertIn('IntercomPage::outsidePress,LV_EVENT_PRESSED',source)
        self.assertIn('if(p.initialLoad)',source)
        self.assertIn('if(!queue(Operation::Load))',source)

    def test_actual_status_throttling_and_idempotent_controls(self):
        source=(ROOT/'src/apps/intercom/IntercomApp.cpp').read_text(encoding='utf-8')
        gate='bool videoStatusDue'+source.split('bool videoStatusDue')[1].split('const char* displayError')[0]
        disabled='static void setDisabled'+source.split('static void setDisabled')[1].split('void activate')[0]
        code=r'''
#include <cstdint>
#include <cstring>
#include <cassert>
struct VideoSnapshot{uint32_t generation=0,frames=0;bool active=false,connecting=false;char codec[8]{},error[49]{};};
struct lv_obj_t{bool disabled=false;unsigned writes=0;};
constexpr int LV_STATE_DISABLED=1;
bool lv_obj_has_state(lv_obj_t* o,int){return o->disabled;}
void lv_obj_add_state(lv_obj_t* o,int){o->disabled=true;++o->writes;}
void lv_obj_remove_state(lv_obj_t* o,int){o->disabled=false;++o->writes;}
'''+gate+disabled+r'''
int main(){VideoSnapshot old{},next{};assert(videoStatusDue(old,next,false,0,0));
unsigned updates=0;uint32_t at=0;
for(uint32_t now=100;now<=10000;now+=100){next.frames++;if(videoStatusDue(old,next,true,now,at)){old=next;at=now;++updates;}}
assert(updates==10); // 100 frame polls, only 10 periodic status rebuilds.
next.connecting=true;assert(videoStatusDue(old,next,true,10001,10000));old=next;
next.active=true;assert(videoStatusDue(old,next,true,10001,10000));old=next;
strcpy(next.error,"offline");assert(videoStatusDue(old,next,true,10001,10000));old=next;
next.generation++;assert(videoStatusDue(old,next,true,10001,10000));old=next;
assert(!videoStatusDue(old,next,true,10,0xfffffff0u));assert(videoStatusDue(old,next,true,1000,0xfffffff0u));
lv_obj_t button;for(int i=0;i<100;++i)setDisabled(&button,true);assert(button.writes==1);
for(int i=0;i<100;++i)setDisabled(&button,false);assert(button.writes==2);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)
        self.assertIn('lv_timer_set_period(timer,1000/selected.fps)',source)
        self.assertIn('if(next==p.frame)return',source)

    def test_actual_native_navigation_and_reference_layout(self):
        source=(ROOT/'src/apps/intercom/IntercomApp.cpp').read_text(encoding='utf-8')
        activate='void activate'+source.split('void activate')[1].split('static void overview')[0]
        code=r'''
#include <string>
#include <cassert>
struct lv_obj_t{unsigned flags=0,state=0;std::string title;};
constexpr unsigned LV_OBJ_FLAG_HIDDEN=1,LV_STATE_CHECKED=2;
void lv_label_set_text(lv_obj_t* p,const char* s){p->title=s;}
void lv_obj_add_state(lv_obj_t* p,unsigned s){p->state|=s;}
void lv_obj_remove_state(lv_obj_t* p,unsigned s){p->state&=~s;}
void lv_obj_add_flag(lv_obj_t* p,unsigned s){p->flags|=s;}
void lv_obj_remove_flag(lv_obj_t* p,unsigned s){p->flags&=~s;}
struct Page{enum class View{Overview,Devices,Live,Settings,Diagnostics};View view=View::Overview;lv_obj_t *heading,*list,*detail,*navigation[5];
'''+activate+r'''
};
int main(){lv_obj_t heading,list,detail,buttons[5];Page p{};p.heading=&heading;p.list=&list;p.detail=&detail;for(unsigned i=0;i<5;++i)p.navigation[i]=&buttons[i];
for(unsigned i=0;i<5;++i){bool isList=i<2;p.activate(Page::View(i),"Selected",isList);assert(p.view==Page::View(i));assert(heading.title=="Selected");assert(bool(detail.flags&LV_OBJ_FLAG_HIDDEN)==isList);assert(bool(list.flags&LV_OBJ_FLAG_HIDDEN)==!isList);for(unsigned j=0;j<5;++j)assert(bool(buttons[j].state&LV_STATE_CHECKED)==(i==j));}
p.navigation[3]=nullptr;p.activate(Page::View::Settings,"Settings");}
'''
        test_system_performance.SystemPerformanceTests().native(code)
        self.assertIn('p.settingsEntry=showSettings',source)
        self.assertIn('lv_obj_set_width(side,250)',source)
        self.assertIn('SettingsWidgets::content(parent,false)',source)
        self.assertIn('LV_FLEX_FLOW_ROW_WRAP',source)
        self.assertIn('lv_obj_set_flex_grow(image,1)',source)
        self.assertIn('p.deviceList(false)',source)
        self.assertIn('SecurityUi::requestSettingsUnlock()',source)

    def test_actual_profile_crud_ownership_and_write_failures(self):
        include=ROOT/'.pio/libdeps/home_display_p4_r3_dev/ArduinoJson/src'
        if not (include/'ArduinoJson.h').exists():
            include=ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'
        if not (include/'ArduinoJson.h').exists():self.skipTest('Pinned ArduinoJson dependency not installed')
        source=(ROOT/'src/services/IntercomService.cpp').read_text(encoding='utf-8')
        configure='bool IntercomService::configure'+source.split('bool IntercomService::configure')[1].split('bool IntercomService::device')[0]
        remove='bool IntercomService::removeDevice'+source.split('bool IntercomService::removeDevice')[1].split('bool IntercomService::startVideo')[0]
        token='bool token'+source.split('bool token')[1].split('class LanEventProvider')[0]
        provider=(ROOT/'src/services/intercom/IntercomProvider.h').read_text(encoding='utf-8').replace('#pragma once','').replace('#include <Arduino.h>','')
        code=r'''
#include <string>
#include <map>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cassert>
#include <atomic>
#include <ArduinoJson.h>
using String=std::string;
size_t strlcpy(char* d,const char* s,size_t n){size_t size=strlen(s);if(n){size_t copy=std::min(size,n-1);memcpy(d,s,copy);d[copy]=0;}return size;}
constexpr int pdTRUE=1;constexpr int pdMS_TO_TICKS(int n){return n;}
bool lockAvailable=true;int xSemaphoreTake(int,int){return lockAvailable?pdTRUE:0;}void xSemaphoreGive(int){}
'''+provider+r'''
namespace hp {
struct NetworkWorker{bool worker=true;static NetworkWorker& instance(){static NetworkWorker s;return s;}bool isWorkerContext(){return worker;}};
struct SecurityWriteGuard{};
struct SecurityService{bool locked=false,allowed=true;static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}bool settingsAllowed(){return allowed;}};
struct ConfigService{
 std::map<std::string,std::string> values,secrets;bool failWrite=false,failSecret=false;unsigned writes=0;
 static ConfigService& instance(){static ConfigService s;return s;}
 String getString(const char* k,const char* fallback){auto i=values.find(k);return i==values.end()?String(fallback):i->second;}
 String getSecretString(const char* k){return secrets[k];}
 bool putString(const char* k,const String& s){++writes;if(failWrite)return false;values[k]=s;return true;}
 bool putSecretString(const char* k,const String& s){if(failSecret)return false;secrets[k]=s;return true;}
};
struct VideoService{unsigned closes=0;static VideoService& instance(){static VideoService s;return s;}void close(const char*){++closes;}};
enum class IntercomState{Offline,Ready,Ringing,Connecting,Conversation,Ended,Error};
struct FakeProvider:IntercomProvider{
 const char* id()const override{return "dahua";}IntercomCapabilities capabilities(const IntercomDevice&)const override{return {};}
 bool command(const IntercomDevice&,IntercomAction,bool,const String&,String&)override{return false;}
} fakeProvider;
struct IntercomService{
 int mutex_=1;IntercomDevice devices_[4]{};
 struct State{IntercomState state=IntercomState::Offline;char device[33]{};IntercomCapabilities capabilities;unsigned revision=0;}state_;
 unsigned providerCall_=1;bool uiDirty_=false;
 IntercomProvider* provider(const char* p){return !strcmp(p,"dahua")?&fakeProvider:nullptr;}
 bool configure(const IntercomDevice&,const String*,String&);
 bool removeDevice(const char*,const char*,String&);
 bool finished(const char*,unsigned){return true;}void diagnostic(const char*){}
};
'''+token+configure+remove+r'''
}
int main(){using namespace hp;IntercomService service;String error,password="protected";
 auto& config=ConfigService::instance();IntercomDevice d;strlcpy(d.id,"one",sizeof(d.id));strlcpy(d.owner,"intercom",sizeof(d.owner));
 strlcpy(d.name,"Door",sizeof(d.name));strlcpy(d.host,"192.168.178.110",sizeof(d.host));strlcpy(d.provider,"dahua",sizeof(d.provider));d.enabled=true;
 assert(service.configure(d,&password,error));assert(config.secrets["ic_pass0"]==password);
 d.enabled=false;assert(service.configure(d,nullptr,error));assert(!service.devices_[0].enabled);assert(config.secrets["ic_pass0"]==password);
 auto other=d;strlcpy(other.owner,"other",sizeof(other.owner));assert(!service.configure(other,&password,error)&&error=="owner_mismatch");
 assert(!service.removeDevice("other","one",error));assert(config.secrets["ic_pass0"]==password);
 auto before=config.values["ic_devices"];config.failWrite=true;String changed="new-secret";
 d.port=123;assert(!service.configure(d,&changed,error));assert(service.devices_[0].port==80);assert(config.secrets["ic_pass0"]==password);
 assert(!service.removeDevice("intercom","one",error));assert(!strcmp(service.devices_[0].id,"one"));assert(config.values["ic_devices"]==before);
 config.failWrite=false;assert(service.removeDevice("intercom","one",error));assert(!service.devices_[0].id[0]);assert(config.secrets["ic_pass0"].empty());
 config.secrets["ic_pass0"]="leftover";strlcpy(d.id,"two",sizeof(d.id));assert(service.configure(d,nullptr,error));assert(config.secrets["ic_pass0"].empty());
 for(unsigned n=1;n<4;++n){snprintf(d.id,sizeof(d.id),"door%u",n);assert(service.configure(d,&password,error));}
 strlcpy(d.id,"fifth",sizeof(d.id));assert(!service.configure(d,&password,error)&&error=="device_limit");
 SecurityService::instance().locked=true;unsigned writes=config.writes;assert(!service.configure(d,&password,error));assert(!service.removeDevice("intercom","two",error));assert(config.writes==writes);
 SecurityService::instance().locked=false;NetworkWorker::instance().worker=false;assert(!service.configure(d,&password,error)&&error=="worker_required");
 NetworkWorker::instance().worker=true;lockAvailable=false;assert(!service.configure(d,&password,error)&&error=="busy");
}
'''
        test_system_performance.SystemPerformanceTests().native(code,includes=(include,))

    def test_registration_management_and_shared_workers(self):
        header=(ROOT/'src/apps/intercom/IntercomApp.h').read_text(encoding='utf-8')
        source=(ROOT/'src/apps/intercom/IntercomApp.cpp').read_text(encoding='utf-8')
        main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
        self.assertIn('&intercomApp',main)
        for item in ['return "intercom"','return "Sprechanlage"','userInstallable()','enabledByDefault()','hasSettings()']:
            self.assertIn(item,header)
        self.assertIn('submitPriority(worker,this',source)
        worker=source.split('void IntercomApp::worker')[1].split('void IntercomApp::create')[0]
        self.assertNotIn('lv_',worker)
        self.assertIn('w.generation!=app.generation_.load()',worker)
        api=(ROOT/'src/apps/intercom/IntercomSettingsApi.cpp').read_text(encoding='utf-8')
        self.assertIn('SecurityWriteGuard',api)
        self.assertIn('GoliathSettings::instance().load()',api)
        self.assertIn('GoliathSettings::instance().save(old)',api)
        self.assertIn('saveNativeIntercomDevice',worker)
        self.assertIn('lv_image_cache_drop',source)
        self.assertIn('lv_timer_delete',source)
        self.assertIn('allocatePsramArray<IntercomPage>',source)
        self.assertNotIn('xTaskCreate',source)
        self.assertNotIn('NetworkClient',source)
        self.assertNotIn('work_={}',source)

    def test_profile_isolation_rollback_and_secret_reuse(self):
        service=(ROOT/'src/services/IntercomService.cpp').read_text(encoding='utf-8')
        self.assertIn('if(!before.id[0]&&!password)password=&emptyPassword',service)
        remove=service.split('bool IntercomService::removeDevice')[1].split('bool IntercomService::startVideo')[0]
        self.assertIn('strcmp(devices_[i].owner,owner)',remove)
        self.assertIn('devices_[slot]=before',remove)
        self.assertIn('SecurityWriteGuard',remove)
        self.assertIn('worker_required',remove)
        self.assertIn('config.putString("ic_devices",saved)',remove)
        start=service.split('bool IntercomService::startVideo')[1].split('bool IntercomService::snapshot')[0]
        self.assertIn('AppPermission::Camera',start)
        self.assertIn('entry.state!=AppRegistryState::Enabled',start)
        self.assertIn('memset(options.nativePassword',start)
        self.assertIn('"ic_pass%d"',start)
        for key in ['videoPort','videoPath','fps']:
            self.assertIn('row["'+key+'"]',service)

    def test_video_provider_capabilities_are_not_faked(self):
        source=(ROOT/'src/services/IntercomService.cpp').read_text(encoding='utf-8')
        provider=source.split('class CameraProvider')[1].split('struct CallOverlay')[0]
        self.assertIn('c.video=true',provider)
        for flag in ['ringEvents','answer','hangup','door','duplex']:
            self.assertNotIn('c.'+flag+'=true',provider)
        self.assertIn('provider_action_unsupported',provider)
        self.assertNotIn('sipCamera',provider)
        audio=(ROOT/'src/services/IntercomAudioService.cpp').read_text(encoding='utf-8')
        ring=audio.split('void IntercomAudioService::pollRing')[1].split('bool IntercomAudioService::receiveG711')[0]
        self.assertIn('IntercomState::Ringing',ring)
        self.assertIn('startAudio(false,true)',ring)
        self.assertIn('AppPermission::Audio',ring)
        self.assertIn('device.ringTone',ring)
        self.assertNotIn('beginMicrophone',ring)
        self.assertNotIn('xTaskCreate',audio)
        manager=(ROOT/'src/core/AppManager.cpp').read_text(encoding='utf-8')
        self.assertIn('current_->kind()!=AppKind::Lua&&current_->userInstallable()&&!isEnabled(current_)',manager)

    def test_bounded_video_reconnect_policy(self):
        policy=(ROOT/'src/services/intercom/VideoRetryPolicy.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(policy+r'''
#include <cassert>
int main(){using hp::intercom::videoRetryDelay;
 for(const char* code:{"video_connect","video_disconnected","video_timeout","video_offline","rtsp_keepalive_timeout"}){
  assert(videoRetryDelay(code,0)==1000);assert(videoRetryDelay(code,1)==2000);
  assert(videoRetryDelay(code,2)==4000);assert(videoRetryDelay(code,3)==0);assert(videoRetryDelay(code,99)==0);
 }
 for(const char* code:{"rtsp_auth_failed","http_auth_unsupported","video_memory","rtsp_codec_unsupported","door",""})assert(videoRetryDelay(code,0)==0);
 assert(videoRetryDelay(nullptr,0)==0);
}
''')

    def test_web_auth_csrf_worker_and_shared_settings(self):
        web=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        handler=web.split('esp_err_t WebServerService::intercomSettingsHandler')[1].split('esp_err_t WebServerService::intercomResultHandler')[0]
        self.assertIn('writeAuthorized(req)',handler)
        self.assertIn('settingsAllowed()',handler)
        self.assertIn('job.owner="@intercom-web"',handler)
        self.assertIn('LuaJobService::instance().submit(job)',handler)
        self.assertIn('req->content_len>2048',handler)
        job=web.split('void intercomWebJob')[1].split('bool queryValue')[0]
        self.assertIn('saveNativeIntercomDevice',job)
        self.assertIn('request.cancelled',job)
        self.assertNotIn('row["password"]',job)
        self.assertNotIn('getSecretString',job)
        page=(ROOT/'src/services/IntercomWebPage.h').read_text(encoding='utf-8')
        self.assertIn("['X-HP-CSRF']",page)
        self.assertNotIn("['X-CSRF-Token']",page)
        self.assertIn('textContent',page)
        self.assertNotIn('.innerHTML',page)
        native=(ROOT/'src/apps/intercom/IntercomApp.cpp').read_text(encoding='utf-8')
        self.assertIn('dahuaProfile',native)
        self.assertIn('function dahuaProfile(snapshot)',page)
        for path in ['/cgi-bin/snapshot.cgi?channel=1','/cam/realmonitor?channel=1&subtype=1']:
            self.assertIn(path,native)
            self.assertIn(path,page)
        preset=page.split('function dahuaProfile(snapshot)')[1].split('async function load()')[0]
        self.assertNotIn('job(',preset) # choosing a profile must not write/connect automatically
        self.assertNotIn("$('password').value=",preset)
        if shutil.which('node'):
            js=page.split('<script>')[1].split('</script>')[0]
            result=subprocess.run(['node','--check'],input=js,text=True,capture_output=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_actual_authentication_methods_digest_and_http_request(self):
        source=(ROOT/'src/services/VideoService.cpp').read_text(encoding='utf-8')
        methods=source.split('String authentication(')[1].split('~Runtime()')[0]
        methods='String authentication('+methods
        param=source.split('String digestParam(')[1].split('String md5hex(')[0]
        param='String digestParam('+param
        md5=lambda value:hashlib.md5(value.encode()).hexdigest()
        realm='testrealm@host.com'; nonce='dcd98b7102dd2f0e8b11d0f600bfb0c093'; cnonce='cafebabecafebabe'
        a1=md5('Mufasa:'+realm+':Circle Of Life'); a2=md5('GET:/dir/index.html')
        response=md5(a1+':'+nonce+':00000001:'+cnonce+':auth:'+a2)
        fixtures={'100:0':cnonce+'0000000000000000','Mufasa:'+realm+':Circle Of Life':a1,'GET:/dir/index.html':a2,
                  a1+':'+nonce+':00000001:'+cnonce+':auth:'+a2:response}
        table=','.join('{'+json.dumps(k)+','+json.dumps(v)+'}' for k,v in fixtures.items())
        code=r'''
#include <string>
#include <algorithm>
#include <cctype>
#include <map>
#include <cassert>
#include <cstdint>
#define HEX 16
struct String {
 std::string v;String()=default;String(const char* s):v(s?s:""){}String(std::string s):v(std::move(s)){}
 String(unsigned n,int base=10){char b[32];if(base==16)snprintf(b,sizeof(b),"%x",n);else snprintf(b,sizeof(b),"%u",n);v=b;}
 size_t length()const{return v.size();}const char* c_str()const{return v.c_str();}char operator[](size_t i)const{return v[i];}
 int indexOf(char c,int p=0)const{auto n=v.find(c,p);return n==v.npos?-1:int(n);}
 int indexOf(const char* s)const{auto n=v.find(s);return n==v.npos?-1:int(n);}
 bool startsWith(const char* s)const{return v.rfind(s,0)==0;}
 String substring(size_t a,size_t b)const{return v.substr(a,b-a);}String substring(size_t a)const{return v.substr(a);}
 void trim(){auto b=v.find_first_not_of(" \r\n\t");if(b==v.npos){v.clear();return;}v=v.substr(b,v.find_last_not_of(" \r\n\t")-b+1);}
 bool equalsIgnoreCase(const char* s)const{std::string a=v,b=s;for(auto& c:a)c=tolower(c);for(auto& c:b)c=tolower(c);return a==b;}
 String& operator+=(const String& s){v+=s.v;return *this;}String& operator+=(char c){v+=c;return *this;}
};
String operator+(const String& a,const String& b){return a.v+b.v;}
unsigned millis(){return 100;}
struct Socket{std::string written;size_t write(const uint8_t* p,size_t n){written.assign((const char*)p,n);return n;}};
'''+param+'\nString md5hex(const String& s){static const std::map<std::string,std::string> values={'+table+r'''};
 auto p=values.find(s.v);assert(p!=values.end());return p->second;}
struct Runtime{
 Socket socket;struct Options{char host[97]="192.168.178.110",path[193]="/dir/index.html";unsigned port=80;}options;
 String user="Mufasa",password="Circle Of Life",digestRealm,digestNonce,digestOpaque,digestQop,authorization="encoded",session,headers;
 bool digest=false,basic=false;unsigned nonceCount=0,cseq=0,deadline=0;int contentLength=-1;
'''+methods+r'''};
int main(){Runtime r;
 assert(!r.challenge("Digest realm=\"a\", nonce=\"b\", algorithm=SHA-256"));
 assert(!r.challenge("Digest realm=\"a\", nonce=\"b\", qop=\"auth-int\""));
 assert(!r.challenge("Digest realm=\"a\""));
 assert(r.challenge("Basic realm=\"camera\""));assert(r.authentication("GET","/").v=="Authorization: Basic encoded\r\n");
 assert(r.challenge("Digest realm=\"testrealm@host.com\", nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", qop=\"auth\""));
 assert(r.httpRequest());assert(r.socket.written.find("GET /dir/index.html HTTP/1.1\r\n")==0);
 assert(r.socket.written.find("Host: 192.168.178.110:80\r\n")!=std::string::npos);
 assert(r.socket.written.find("nc=00000001")!=std::string::npos);
'''+f' assert(r.socket.written.find("response=\\\"{response}\\\"")!=std::string::npos);\n'+r'''
 assert(r.contentLength==-1&&r.deadline==4100);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

if __name__=='__main__':unittest.main()

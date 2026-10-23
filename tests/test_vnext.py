import unittest
from pathlib import Path
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')

class VNextTests(unittest.TestCase):
    def test_actual_dav_control_bounds_redirects_and_stops_changed_owner(self):
        control=read('src/services/CalDavConnector.h').split('struct CalDavControl{',1)[1].split('struct CalDavConnectionResult',1)[0]
        code=read('src/core/RequestDeadline.h').replace('#pragma once','')+r'''
#include <cassert>
uint32_t now=100;uint32_t millis(){return now;}
namespace hp{struct CalDavControl{
'''+control+r'''}
int main(){hp::CalDavControl c;assert(c.nextTimeout()==8000);now+=15000;assert(c.nextTimeout()==5000);
now+=5000;assert(c.stopped()&&c.nextTimeout()==0);now=0;hp::CalDavControl other;
bool valid=true;other.context=&valid;other.current=[](void* p){return *static_cast<bool*>(p);};
assert(!other.stopped());valid=false;assert(other.stopped()&&other.nextTimeout()==0);}
'''
        test_system_performance.SystemPerformanceTests().native(code)
        connector=read('src/services/CalDavConnector.cpp')
        self.assertIn('reply.control=control',connector)
        self.assertIn('cfg.timeout_ms=timeout',connector)
        self.assertIn('reply.control->stopped()',connector)
        self.assertIn('&control',read('src/services/CloudAccountApi.cpp'))

    def test_actual_launch_callback_is_deferred_until_ui_is_ready(self):
        runtime=read('src/apps/lua/LuaApp.cpp')
        method=runtime.split('void LuaApp::launchArgumentsChanged(){',1)[1].split('void LuaApp::open(',1)[0]
        lifecycle=read('src/core/AppLifecycleCore.h').replace('#pragma once','')
        code=lifecycle+r'''
#include <cassert>
namespace hp {
class LuaApp{public:LuaRuntimeState lifecycle_=LuaRuntimeState::Preparing;bool pendingLaunch_=false,paused_=false,faulted_=false;void* L_=nullptr;unsigned calls=0;
bool hasGlobal(const char*){return true;}bool callGlobal(const char*,unsigned,int){++calls;return true;}void quarantine(const char*){faulted_=true;}void launchArgumentsChanged();};
constexpr unsigned kDefaultCallbackMs=10;constexpr int kDefaultHookCalls=250;
void LuaApp::launchArgumentsChanged(){
'''+method+r'''
}
int main(){hp::LuaApp app;app.launchArgumentsChanged();assert(app.pendingLaunch_&&app.calls==0);
app.lifecycle_=hp::LuaRuntimeState::Opening;app.L_=&app;app.launchArgumentsChanged();assert(app.pendingLaunch_&&app.calls==0);
app.lifecycle_=hp::LuaRuntimeState::Foreground;if(app.pendingLaunch_)app.launchArgumentsChanged();assert(!app.pendingLaunch_&&app.calls==1);
if(app.pendingLaunch_)app.launchArgumentsChanged();assert(app.calls==1);}
'''
        test_system_performance.SystemPerformanceTests().native(code)
        self.assertIn('if(pendingLaunch_)launchArgumentsChanged();',runtime)

    def test_actual_calendar_pagination_cancellation_oom_and_unchanged_storage(self):
        def stripped(path):return '\n'.join(line for line in read(path).splitlines() if not line.startswith('#include') and line!='#pragma once')
        fake=read('tests/native/printer_fake.h').split('namespace hp {')[0]
        fake=fake.replace('return malloc(n);','return calendarOom?nullptr:malloc(n);')
        fake='bool calendarOom=false;\n'+fake+'\nvoid* heap_caps_calloc(size_t n,size_t s,int){return calendarOom?nullptr:calloc(n,s);}\n'
        source=read('src/services/CalendarSyncService.cpp')
        method=source.split('bool CalendarSyncService::fetchCalendar(',1)[1].split('bool CalendarSyncService::restoreCalendarCache(',1)[0]
        method+='bool CalendarSyncService::commitCalendarCache('+source.split('bool CalendarSyncService::commitCalendarCache(',1)[1].split('bool CalendarSyncService::fetchContacts(',1)[0]
        code=fake+stripped('src/core/RequestDeadline.h')+stripped('src/apps/calendar/CalendarTypes.h')+stripped('src/apps/calendar/EventDetails.h')
        discovery=source.split('bool CalendarSyncService::discoverOAuthCalendars(',1)[1].split('bool CalendarSyncService::fetchCalendar(',1)[0]
        code+='\n'+read('tests/native/calendar_vnext_fake.h')+'\nnamespace hp{bool CalendarSyncService::discoverOAuthCalendars('+discovery+'bool CalendarSyncService::fetchCalendar('+method+'}\n'
        code+=r'''
int main(){using namespace hp;CalendarSyncService service;CalendarSyncService::Job job;
auto first=R"json({"items":[{"id":"a","summary":"First","start":{"dateTime":"begin"},"end":{"dateTime":"end"}}],"nextPageToken":"next"})json";
auto second=R"json({"items":[{"id":"b","summary":"Second","start":{"dateTime":"begin"},"end":{"dateTime":"end"}}]})json";
auto load=[&](){service.urls.clear();service.replies={{first,200},{second,200}};};
load();assert(service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));
assert(service.info_[0].eventCount==2&&service.urls.size()==2&&service.urls[1].indexOf("pageToken=next")>=0);
assert(service.urls[0].indexOf("timeMin=13600")>=0&&service.urls[0].indexOf("timeMax=2692000")>=0);
auto& storage=StorageService::instance();auto writes=storage.writes;assert(writes==2&&calendarEvents==1);
load();assert(service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(storage.writes==writes&&calendarEvents==1);
auto* old=service.events_[0];load();service.replies[1].status=500;assert(!service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.events_[0]==old&&storage.writes==writes);
load();service.cancelAt=1;assert(!service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.urls.size()==1&&service.events_[0]==old);service.valid=true;service.cancelAt=-1;
load();calendarOom=true;assert(!service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.events_[0]==old);calendarOom=false;
load();service.delay=15001;assert(!service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.urls.size()==1&&service.events_[0]==old);service.delay=0;
load();NetworkService::instance().connected=false;assert(!service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.events_[0]==old);NetworkService::instance().connected=true;
EmailAccountService::instance().value.provider=EmailProvider::Microsoft;
service.urls.clear();service.replies={{R"json({"value":[],"@odata.nextLink":"https://evil.invalid/calendarView?x=1"})json",200}};
assert(!service.fetchCalendar(job,EmailProvider::Microsoft,"owner@test.invalid","token"));assert(service.urls.size()==1&&service.events_[0]==old);
service.urls.clear();service.replies={{R"json({"value":[],"@odata.nextLink":"https://graph.microsoft.com/v1.0/me/calendarView?$skip=1"})json",200},{R"json({"value":[]})json",200}};
assert(service.fetchCalendar(job,EmailProvider::Microsoft,"owner@test.invalid","token")&&service.urls.size()==2);
EmailAccountService::instance().value.provider=EmailProvider::Google;
auto pages=[&](unsigned total){service.urls.clear();service.replies.clear();for(unsigned p=0;p<total;++p){std::string body="{\"items\":[";
 for(unsigned i=0;i<32;++i){if(i)body+=",";body+="{\"id\":\"event"+std::to_string(p*32+i)+"\",\"start\":{\"dateTime\":\"begin\"},\"end\":{\"dateTime\":\"end\"}}";}
 body+="]";if(p+1<total)body+=",\"nextPageToken\":\""+std::to_string(p+1)+"\"";body+="}";service.replies.push_back({body.c_str(),200});}};
pages(3);assert(service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.info_[0].eventCount==96&&!service.info_[0].truncated&&service.urls.size()==3);
pages(5);assert(service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.info_[0].eventCount==128&&service.info_[0].truncated&&service.urls.size()==4);
service.selected={{"one","Erster",true},{"two","Zweiter",true}};service.urls.clear();const char* same=R"json({"items":[{"id":"same","start":{"dateTime":"begin"},"end":{"dateTime":"end"}}]})json";service.replies={{same,200},{same,200}};assert(service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.info_[0].eventCount==2&&service.urls.size()==2&&service.urls[0].indexOf("/one/events?")>=0&&service.urls[1].indexOf("/two/events?")>=0&&service.events_[0][0].localId!=service.events_[0][1].localId);
service.selected.clear();service.replies.clear();service.urls.clear();assert(service.fetchCalendar(job,EmailProvider::Google,"owner@test.invalid","token"));assert(service.info_[0].eventCount==0&&service.urls.empty());
service.replies={{R"json({"items":[{"id":"first","summary":"Familie"},{"id":"second","summary":"Arbeit"}]})json",200}};assert(service.discoverOAuthCalendars(job,EmailProvider::Google,"token")&&service.collections_[0].size()==2);
service.replies={{"{}",403}};assert(!service.discoverOAuthCalendars(job,EmailProvider::Google,"token")&&service.collections_[0].size()==2);
service.replies={{R"json({"value":[],"@odata.nextLink":"https://evil.invalid/calendars?token=1"})json",200}};assert(!service.discoverOAuthCalendars(job,EmailProvider::Microsoft,"token")&&service.collections_[0].size()==2);
}
'''
        test_system_performance.SystemPerformanceTests().native(code,includes=(ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',))

    def test_real_network_worker_priority_fairness_and_transfer_isolation(self):
        def stripped(path):return '\n'.join(line for line in read(path).splitlines() if not line.startswith('#include') and line!='#pragma once')
        code=read('tests/native/network_v3_fake.h')+stripped('src/core/LatencyHistogram.h')
        code+=stripped('src/services/NetworkWorker.h').replace('private:','public:')+stripped('src/services/NetworkWorker.cpp')
        test_system_performance.SystemPerformanceTests().native(code+r'''
std::vector<int> order;unsigned maintenance=0;
void job(void* id){order.push_back(int(reinterpret_cast<uintptr_t>(id)));now+=10;}
void background(void*){++maintenance;}
int main(){auto& worker=hp::NetworkWorker::instance();assert(worker.begin());worker.addMaintenance(background,nullptr);
assert(worker.submit(job,reinterpret_cast<void*>(9),"normal"));
for(int i=1;i<=4;++i)assert(worker.submitPriority(job,reinterpret_cast<void*>(uintptr_t(i)),"action"));
assert(notifications==5);assert(worker.submitTransfer(job,reinterpret_cast<void*>(8),"upload"));
try{worker.run();assert(false);}catch(const Drained&){}
assert((order==std::vector<int>{1,2,3,9,4}));assert(maintenance>0&&worker.pending()==1);
assert(worker.jobLatency().samples==5&&worker.jobLatency(true).samples==0&&worker.queuePeak()==6);
hp::NetworkWorker::Job transfer;assert(xQueueReceive(worker.transferQ_,&transfer,0));worker.executeJob(transfer,true);
assert(worker.jobLatency(true).samples==1&&worker.completedTransfers()==1&&worker.pending()==0);
}
''')
    def test_directory_page_is_global_and_memory_bounded(self):
        code=read('src/core/SortedPage.h').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
#include <functional>
int main(){hp::SortedPage<int,std::less<int>> first(97,std::less<int>{});
hp::SortedPage<int,std::less<int>> second(193,std::less<int>{});
for(int i=9999;i>=0;--i){first.add(i);second.add(i);assert(first.retained()<=97&&second.retained()<=193);}
auto a=first.sorted();auto b=second.sorted();for(int i=0;i<96;++i){assert(a[i]==i);assert(b[i+96]==i+96);}
hp::SortedPage<int,std::less<int>> empty(0,std::less<int>{});empty.add(1);assert(empty.sorted().empty());}
''')
    def test_deadline_bounds_each_step_and_handles_clock_wrap(self):
        code=read('src/core/RequestDeadline.h').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){hp::RequestDeadline d(100,1500);assert(d.stepTimeout(100,1500)==1500);
assert(d.stepTimeout(1100,1500)==500);assert(!d.remaining(1600));assert(!d.stepTimeout(1700,1500));
hp::RequestDeadline wrap(UINT32_MAX-99,200);assert(wrap.remaining(0)==100);assert(!wrap.remaining(100));}
''')

    def test_energy_updates_do_not_change_revision_for_timestamps_only(self):
        code=read('src/services/EnergyModel.h').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){using namespace hp;EnergySnapshot a,b;assert(sameEnergyContent(a,b));
b.updatedMs=123;b.timestamp=456;b.revision=99;assert(sameEnergyContent(a,b));
b.gridPower.value=10;b.gridPower.quality=EnergyQuality::Measured;assert(!sameEnergyContent(a,b));
a=b;assert(sameEnergyContent(a,b));b.phase[2].current.value=1;assert(!sameEnergyContent(a,b));
b=a;b.online=true;assert(!sameEnergyContent(a,b));b=a;b.source[0]='x';assert(!sameEnergyContent(a,b));}
''')

    def test_validator_standard_password_exception_is_narrow(self):
        import importlib.util,tempfile
        spec=importlib.util.spec_from_file_location('vnext_validator',ROOT/'tools/release_validator.py')
        validator=importlib.util.module_from_spec(spec);spec.loader.exec_module(validator)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'src/services').mkdir(parents=True);(root/'include').mkdir()
            path=root/'src/services/WebAdminAuth.cpp'
            path.write_text('password = "Passwort";',encoding='utf-8')
            validator.ROOT=root;validator.scan_secrets(False)
            self.assertEqual(validator.FAIL, [])
            path.write_text('password = "Passwort"; password = "real-secret";',encoding='utf-8')
            validator.FAIL.clear();validator.scan_secrets(False)
            self.assertEqual(validator.FAIL,['possible secret: src/services/WebAdminAuth.cpp'])
            path.write_text('password = "Passwort";',encoding='utf-8')
            (root/'src/other.cpp').write_text('password = "Passwort";',encoding='utf-8')
            validator.FAIL.clear();validator.scan_secrets(False)
            self.assertEqual(validator.FAIL,['possible secret: src/other.cpp'])
            (root/'src/services/WebServerService.cpp').write_text('password = "Passwort";',encoding='utf-8')
            validator.FAIL.clear();validator.scan_secrets(False)
            self.assertIn('possible secret: src/services/WebServerService.cpp',validator.FAIL)

    def test_real_schema3_parser_permissions_and_settings(self):
        helper=test_system_performance.SystemPerformanceTests()
        fake=read('tests/native/printer_fake.h').split('namespace hp {')[0]
        def stripped(path):return '\n'.join(line for line in read(path).splitlines() if not line.startswith('#include') and line!='#pragma once')
        parser=stripped('src/apps/lua/LuaAppManifest.cpp').split('bool LuaAppManifestParser::load(',1)[0]+'\n}\n'
        code=fake+'\n#include <climits>\nnamespace fs{class FS;}\nbool isDigit(char c){return c>=\'0\'&&c<=\'9\';}\nnamespace hp::build{const char* kVersion="0.3.3b";}\n'
        code+=stripped('src/core/AppPermissions.h')+stripped('src/core/AppLifecycleCore.h')+stripped('src/apps/lua/LuaAppManifest.h')+parser+r'''
int main(){using namespace hp;LuaAppManifest manifest;String error;
auto valid=[](unsigned schema,bool settings,const char* entry)->String{return String("{\"schema\":")+String(schema)+",\"id\":\"demo\",\"name\":\"Demo\",\"version\":\"3.0.0\",\"type\":\"lua\",\"entry\":\"main.lua\",\"permissions\":[],\"settings\":"+(settings?"true":"false")+entry+"}";};
assert(LuaAppManifestParser::parseJson(valid(3,false,""),manifest,error,true)&&manifest.schema==3&&!manifest.settings&&error.empty());
for(unsigned schema:{1,2,4})assert(!LuaAppManifestParser::parseJson(valid(schema,false,""),manifest,error)&&error=="manifest_schema_v3_required");
assert(LuaAppManifestParser::parseJson(valid(3,true,",\"settingsEntry\":\"settings.lua\""),manifest,error,true));
assert(!LuaAppManifestParser::parseJson(valid(3,true,",\"settingsEntry\":\"other.lua\""),manifest,error,true));
assert(!LuaAppManifestParser::parseJson(valid(3,false,",\"settingsEntry\":\"settings.lua\""),manifest,error,true));
auto bad=valid(3,false,"");bad.replace("\"permissions\":[]","\"permissions\":[\"storage\",\"storage.writeAppData\"]");assert(!LuaAppManifestParser::parseJson(bad,manifest,error,true)&&error=="manifest_permission_duplicate");
bad=valid(3,false,"");bad.replace("\"permissions\":[]","\"permissions\":[\"unknown\"]");assert(!LuaAppManifestParser::parseJson(bad,manifest,error,true));
}
'''
        helper.native(code,includes=(ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',))

    def test_persistent_desktop_navigation_actual_method(self):
        shell=read('src/ui/UiShell.cpp')
        method=shell[shell.index('void UiShell::showHome()'):shell.index('void UiShell::hideHomeBackground')]
        test_system_performance.SystemPerformanceTests().native(r'''
#include <cassert>
struct Object{bool hidden=false;int children=1;};
void setHiddenIfChanged(Object* o,bool hidden){o->hidden=hidden;}
enum class IdleContext{Normal};
struct IdleService{static IdleService& instance(){static IdleService s;return s;}void setContext(IdleContext){}};
struct SecurityService{bool locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}void lockSettings(){}};
struct AppManager{int suspended=0;static AppManager& instance(){static AppManager s;return s;}void suspendCurrent(){++suspended;}};
struct UiShell{Object home,app,wallpaper,back,subtitle;Object* appSettingsBack_=&back;Object* subtitle_=&subtitle;Object* home_=&home;Object* content_=&app;Object* wallpaperLayer_=&wallpaper;bool homeDirty_=false,dockDirty_=false,fullscreen=true;int builds=0,dockBuilds=0;void setAppFullscreen(Object* host,bool enabled){assert(host==content_);fullscreen=enabled;}void rebuildHome(){++builds;homeDirty_=false;dockDirty_=false;}void rebuildDock(){++dockBuilds;dockDirty_=false;}void showHome();};
'''+method+r'''
int main(){UiShell shell;shell.home.hidden=true;for(int i=0;i<100;++i)shell.showHome();
assert(shell.builds==0&&shell.home.children==1&&!shell.home.hidden&&shell.app.hidden&&!shell.fullscreen);
assert(shell.back.hidden&&!shell.subtitle.hidden);
shell.homeDirty_=true;shell.showHome();shell.showHome();assert(shell.builds==1);
shell.dockDirty_=true;shell.showHome();shell.showHome();assert(shell.builds==1&&shell.dockBuilds==1);
SecurityService::instance().locked=true;shell.home.hidden=true;shell.showHome();assert(shell.home.hidden);
}
''')

    def test_worker_has_no_lua_or_lvgl_and_cancellation_is_owned(self):
        worker=read('src/apps/lua/LuaScriptPreparation.cpp')
        for forbidden in ('lv_obj_', 'lua_State', 'LuaApp*', 'luaL_load', 'xTaskCreate'):
            self.assertNotIn(forbidden,worker)
        self.assertIn('beginPackageMutation(1500)',worker)
        self.assertIn('std::unique_ptr<std::shared_ptr<LuaScriptPreparation>>',worker)
        runtime=read('src/apps/lua/LuaApp.cpp')
        self.assertIn('ready.load(std::memory_order_acquire)',runtime)
        self.assertIn('cancelled.store(true,std::memory_order_release)',runtime)
        self.assertNotIn('openSdRead(',runtime)
        self.assertIn('hp.v3.ui.step',runtime)

    def test_tile_identity_and_hidden_home_are_not_label_based(self):
        reorder=read('src/ui/HomeReorderController.cpp')
        self.assertNotIn('tileLabel',reorder)
        self.assertIn('registry.lookup(id, entry)',reorder)
        self.assertIn('sameRevision && grid == boundGrid_',reorder)
        shell=read('src/ui/UiShell.cpp')
        home=shell.split('void UiShell::rebuildHome()',1)[1].split('void UiShell::homePageBtn',1)[0]
        self.assertNotIn('lv_obj_clean(content_)',home)
        self.assertIn('setHiddenIfChanged(home_,true)',shell)
        self.assertNotIn('static_cast<IApp*>(lv_event_get_user_data(e))',shell)

    def test_quantiles_handle_zero_large_values_and_separate_classes(self):
        histogram=read('src/core/LatencyHistogram.h').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(histogram+r'''
#include <cassert>
#include <thread>
int main(){hp::LatencyHistogram h,transfer;assert(h.snapshot().samples==0);
for(int i=0;i<100;++i)h.add(i);auto s=h.snapshot();assert(s.samples==100&&s.p95Ms>=94&&s.p99Ms>=98&&s.maxMs==99);
hp::LatencyHistogram extreme;extreme.add(0);extreme.add(UINT32_MAX);assert(extreme.snapshot().p99Ms==UINT32_MAX);
std::thread a([&](){for(int i=0;i<1000;++i)h.add(10);});std::thread b([&](){for(int i=0;i<1000;++i)h.add(20);});a.join();b.join();assert(h.snapshot().samples==2100&&transfer.snapshot().samples==0);
}
''')

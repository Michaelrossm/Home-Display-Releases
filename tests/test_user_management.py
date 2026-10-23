from pathlib import Path
import re,subprocess,shutil,unittest
import test_system_performance
ROOT=Path(__file__).resolve().parents[1]
def read(p):return (ROOT/p).read_text(encoding='utf-8')
class UserManagementTests(unittest.TestCase):
    def test_actual_contact_cache_refresh_is_queued_and_failure_is_bounded(self):
        source=read('src/apps/calendar/CalendarService.cpp')
        method='void CalendarService::backgroundTick()'+source.split('void CalendarService::backgroundTick()',1)[1].rsplit('\n}',1)[0]
        fixture=r'''
#include <atomic>
#include <cstdint>
#include <functional>
#include <cassert>
uint32_t now=400000;uint32_t millis(){return now;}
namespace hp {
struct PerformanceGuard{};
struct PhonebookService{uint32_t version=1;static PhonebookService& instance(){static PhonebookService p;return p;}uint32_t revision(){return version;}};
struct NetworkWorker{unsigned jobs=0;std::function<void()> pending;static NetworkWorker& instance(){static NetworkWorker n;return n;}bool submit(void(*fn)(void*),void* p,const char*){++jobs;pending=[=]{fn(p);};return true;}void run(){auto f=pending;pending={};f();}};
struct CalendarService{std::atomic<bool> cacheInvalidated_{false},cachePending_{false};std::atomic<uint32_t> upcomingBookRevision_{0};uint32_t lastRefresh_=0;unsigned runs=0;bool rebuildUpcoming(){++runs;return false;}void backgroundTick();};
'''+method+r'''
}
int main(){using namespace hp;CalendarService s;auto& n=NetworkWorker::instance();s.backgroundTick();s.backgroundTick();assert(n.jobs==1&&s.runs==0);n.run();s.backgroundTick();assert(n.jobs==1&&s.runs==1);
 ++PhonebookService::instance().version;s.backgroundTick();assert(n.jobs==2&&s.runs==1);n.run();for(unsigned i=0;i<1000;++i)s.backgroundTick();assert(n.jobs==2);
 s.cacheInvalidated_=true;s.backgroundTick();n.run();s.backgroundTick();assert(n.jobs==3);now+=300001;s.backgroundTick();assert(n.jobs==4);
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture)

    def test_shared_lua_contract_and_queued_settings_navigation(self):
        bridge=read('src/apps/lua/CalendarLuaBridge.cpp')
        for name in ('hp_users_list','hp_user_profile','hp_user_save','hp_user_remove','hp_users_revision','hp_calendar_person_save'):
            self.assertIn('"'+name+'"',bridge)
        self.assertIn('job.lane=LuaJobService::Request::Lane::LocalTransfer',bridge)
        self.assertNotIn('job.kind=1',bridge)
        navigation=read('src/apps/lua/LuaNavigationBridge.cpp')
        callback=navigation.split('int bridgeOpenUsers(',1)[1].split('\n}',1)[0]
        self.assertIn('requestAppOpen',callback);self.assertNotIn('AppManager::',callback)
        self.assertIn('profileLock',read('src/apps/calendar/CalendarApi.cpp'))
        query=read('src/apps/calendar/CalendarOperations.cpp').split('bool CalendarService::query(',1)[1]
        self.assertNotIn('SecurityWriteGuard',query)
        self.assertIn('participant_mask',read('src/services/CalendarWebPage.h'))

    def test_profile_editor_async_guard_request_identity_and_stale_results(self):
        page=read('src/services/CalendarWebPage.h')
        args='function profileArguments()'+page.split('function profileArguments()',1)[1].split('async function loadProfileContact',1)[0]
        load='async function loadProfileContact'+page.split('async function loadProfileContact',1)[1].split('if(usersOnly){',1)[0]
        code=r'''
const assert=require('assert');
let profileLoading=false,profileReady=false,profileLoadSequence=0,profileRevision=8,profileRequestToken='';
const usersOnly=true,profileInputs={phone:{value:'123'},region:{value:''},country:{value:'DE'}},profileFields=[['phone']];
const nodes={person:{value:''},savePerson:{disabled:false},profileCreateCalendar:{checked:true},profileDisplayName:{value:'Max'},name:{value:''},lastName:{value:''},email:{value:''},birthday:{value:''}};
const $=id=>nodes[id];const controls=[profileInputs.phone,profileInputs.region,profileInputs.country];const profileSection={querySelectorAll:()=>controls};
const hpWeb={countrySelect:async(field,value)=>{field.value=value||'DE'}},message=()=>{},formatBirthdayInput=v=>v,dateFormat='iso';let pending=[];const job=(op,a)=>new Promise((resolve,reject)=>pending.push({op,a,resolve,reject}));
'''+args+load+r'''
(async()=>{
 assert.throws(profileArguments,/geladen/);await loadProfileContact();assert(profileReady&&!profileLoading&&!nodes.savePerson.disabled);
 const first=profileArguments(),second=profileArguments();assert.equal(first.request_id,second.request_id);assert.equal(first.request_id.length,16);assert.equal(first.create_calendar,true);
 nodes.person.value='00000001';let old=loadProfileContact();assert(profileLoading&&nodes.savePerson.disabled&&controls.every(c=>c.disabled));assert.throws(profileArguments,/geladen/);
 nodes.person.value='00000002';let current=loadProfileContact();pending[0].resolve({revision:10,contact:{phone:'OLD'}});await old;assert(profileLoading&&nodes.savePerson.disabled);assert.notEqual(profileInputs.phone.value,'OLD');
 pending[1].resolve({revision:11,contact:{phone:'NEW',country:'AT'}});await current;assert(!profileLoading&&profileReady&&profileInputs.phone.value==='NEW'&&profileRevision===11);assert.equal(profileArguments().revision,11);assert(!('request_id' in profileArguments()));
 let linked=loadProfileContact('existing');pending[2].resolve({contact:{phone:'LINKED'}});await linked;assert.equal(profileRevision,11);assert.equal(profileInputs.phone.value,'LINKED');
 let failed=loadProfileContact();pending[3].reject(Error('storage'));await assert.rejects(failed,/storage/);assert(!profileReady&&nodes.savePerson.disabled&&controls.every(c=>c.disabled));
})().catch(e=>{console.error(e);process.exitCode=1});
'''
        result=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_shared_routes_and_no_duplicate_editors_or_background_tasks(self):
        page=read('src/services/CalendarWebPage.h');native=read('src/apps/calendar/CalendarApp.cpp');phone=read('src/apps/phonebook/PhonebookApp.cpp')
        self.assertIn('{"/users", HTTP_GET, calendarPageHandler',read('src/services/WebServerService.cpp'))
        self.assertIn("const usersOnly=location.pathname",page)
        self.assertIn('usersWorkspace',page);self.assertIn('userSearch',page)
        self.assertNotIn("['Personen',1]",page)
        self.assertNotIn('userDialog',read('src/services/PhonebookWebPage.h'))
        self.assertNotIn('void userEditor()',phone)
        self.assertEqual(native.count('void buildPersonEditor('),1)
        self.assertIn('requestAppOpen("settings","{\\"view\\":\\"users\\"}")',phone)
        self.assertIn("usersView_.openUsers(content)",read("src/apps/settings/SettingsApp.cpp"))
        self.assertIn('setOwner("@users-native")',native)
        self.assertIn('p.controller.open(p.usersOnly)',native)
        self.assertIn('queryPending_ = !usersOnly_',read('src/apps/calendar/CalendarController.cpp'))
        self.assertIn('request.kind = 5',read('src/apps/calendar/CalendarController.cpp'))
        self.assertIn('if(usersOnly_)request.lane=LuaJobService::Request::Lane::LocalTransfer',read('src/apps/calendar/CalendarController.cpp'))
        self.assertIn('profileDirty',native);self.assertIn('chooseProfile',native)
        self.assertNotIn('xTaskCreate',native)
    def test_actual_user_list_search_sort_selection_and_unsaved_guard(self):
        source=read('src/services/CalendarWebPage.h')
        function='function renderUsers()'+source.split('function renderUsers()',1)[1].split('let profileDirty=',1)[0]
        code=r'''
const assert=require('assert');
class Node{constructor(){this.children=[];this.value='';this.attrs={};this.style={}}replaceChildren(){this.children=[]}append(...items){this.children.push(...items)}setAttribute(k,v){this.attrs[k]=v}}
const document={createElement:()=>new Node()},userList=new Node(),userQuery=new Node(),profileSection=new Node();
const usersOnly=true,calendarPalette=[['Blau','#00f']];let profileDirty=false,confirmation=false,changed=0;
const nodes={person:new Node(),userCount:new Node(),profileEmpty:new Node()};const $=id=>nodes[id];nodes.person.onchange=()=>{changed++};const confirm=()=>confirmation;const matchMedia=()=>({matches:false});
let people=[{id:'2',name:'Zoe Beispiel',firstName:'Zoe',lastName:'Beispiel',email:'z@calendar.local',contact_email:'z@example.org',color:0},{id:'1',name:'Anna Beispiel',firstName:'Anna',lastName:'Beispiel',email:'a@calendar.local',contact_email:'a@example.org',color:0}];
'''+function+r'''
renderUsers();assert.equal(userList.children.length,2);assert.equal(userList.children[0].children[1].children[0].textContent,'Anna Beispiel');
userList.children[0].onclick();assert.equal(nodes.person.value,'1');assert.equal(changed,1);assert.equal(profileSection.hidden,false);
profileDirty=true;userList.children[1].onclick();assert.equal(nodes.person.value,'1');assert.equal(changed,1);
confirmation=true;userList.children[1].onclick();assert.equal(nodes.person.value,'2');assert.equal(profileDirty,false);
userQuery.value='A@EXAMPLE.ORG';renderUsers();assert.equal(userList.children.length,1);assert.equal(userList.children[0].children[1].children[0].textContent,'Anna Beispiel');
userQuery.value='no match';renderUsers();assert.equal(userList.children[0].textContent,'Keine passenden Benutzer.');
'''
        result=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_actual_profile_email_options_validation_and_persistence(self):
        fixture=read('tests/native/printer_fake.h').split('\nnamespace hp {',1)[0]+r'''
namespace hp{struct ConfigService{String saved="{}";bool fail=false;static ConfigService& instance(){static ConfigService c;return c;}String getString(const char*,const char*){return saved;}bool putString(const char*,const String& s){if(fail)return false;saved=s;return true;}};}
'''
        fixture+='\n'.join(l for l in read('src/apps/calendar/CalendarPersonOptions.h').splitlines() if not l.startswith('#include') and l!='#pragma once')+r'''
int main(){using namespace hp::calendar;assert(validProfileEmail(""));assert(validProfileEmail("m@example.org"));assert(!validProfileEmail("invalid"));assert(!validProfileEmail("m@@example.org"));assert(!validProfileEmail("m@example.org\n"));
 JsonDocument d;assert(loadPersonOptions(d));d["emails"]["12345678"]="old@example.org";d["views"]["12345678"].to<JsonArray>().add("00000001");assert(savePersonOptions(d));
 JsonDocument loaded;assert(loadPersonOptions(loaded));loaded["emails"]["12345678"]="new@example.org";assert(savePersonOptions(loaded));assert(loadPersonOptions(d));assert(d["emails"]["12345678"]=="new@example.org"&&d["views"]["12345678"][0]=="00000001");
 auto old=hp::ConfigService::instance().saved;hp::ConfigService::instance().fail=true;d["emails"]["12345678"]="fail@example.org";assert(!savePersonOptions(d)&&hp::ConfigService::instance().saved==old);hp::ConfigService::instance().fail=false;
 hp::ConfigService::instance().saved=R"({"emails":{"bad-id":"x@example.org"}})";assert(!loadPersonOptions(d));
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])
    def test_actual_birthday_clear_validation_and_prepare_failure(self):
        from test_shared_profiles import program
        body=r'''
int main(){using namespace hp;auto& s=CalendarService::instance();auto& storage=StorageService::instance();JsonDocument a,out;String error;
 a["firstName"]="Birthday";a["birthday_year"]=1996;a["birthday_month"]=12;a["birthday_day"]=24;assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));a["id"]=out["id"];
 a["birthday_month"]=2;a["birthday_day"]=30;const auto writes=storage.writes;assert(!s.saveProfile(a.as<JsonVariantConst>(),out,error)&&error=="birthday_invalid"&&storage.writes==writes);
 a["clear_birthday"]=true;assert(s.saveProfile(a.as<JsonVariantConst>(),out,error));assert(!s.persons_[0].birthdayDay);LocalPhonebook book;assert(PhonebookService::instance().loadLocal(book)&&!book.contacts[0].birthday.day);
 a["clear_birthday"]=false;a["birthday_year"]=2000;a["birthday_month"]=2;a["birthday_day"]=29;storage.failAt=storage.writes+1;assert(!s.saveProfile(a.as<JsonVariantConst>(),out,error)&&error=="profile_prepare_failed");assert(!s.persons_[0].birthdayDay);assert(PhonebookService::instance().loadLocal(book)&&!book.contacts[0].birthday.day);
}
'''
        test_system_performance.SystemPerformanceTests().native(program()+body,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])
    def test_actual_profile_poll_bootstraps_once_without_month_or_idle_loop(self):
        source=read('src/apps/calendar/CalendarController.cpp');poll='void CalendarController::poll()'+source.split('void CalendarController::poll()',1)[1].split('void CalendarController::consume(',1)[0]
        stamp=source.split('else if (operation_ == "status" || operation_ == "sync") {',1)[1].split('} else if (operation_ == "details")',1)[0]
        fixture=read('tests/native/printer_fake.h').split('\nnamespace hp {',1)[0]+r'''
namespace hp {
struct CalendarService{unsigned revision=7;static CalendarService& instance(){static CalendarService c;return c;}unsigned syncRevision(){return revision;}};
struct CalendarSyncService{unsigned revision=9;static CalendarSyncService& instance(){static CalendarSyncService c;return c;}unsigned dataRevision(){return revision;}};
struct LuaJobService{struct Result{bool done=true,ok=true,cancelled=false;String body="{}",error;};static LuaJobService& instance(){static LuaJobService s;return s;}bool result(const char*,unsigned,Result&){return true;}bool release(const char*,unsigned){return true;}};
struct CalendarController{bool active_=true,usersOnly_=true,busy=false,dirty=false,queryPending_=false,regionRefreshPending_=true;unsigned job_=0,bootstrap_=2,retryAt_=0,revision_=0,cloudRevision_=0,statusGeneration=0;String message,operation_;JsonDocument status;std::vector<String> operations;
const char* owner(){return "@calendar-native";}bool submit(const char* op,JsonDocument&){operations.push_back(op);operation_=op;job_=1;busy=true;return true;}void query(){assert(false&&"Profile view must not query appointments");}void refresh(bool){queryPending_=!usersOnly_;}void poll();void consume(JsonDocument& doc){if(operation_=="status"){
'''+stamp+r'''
}}};
'''+poll+r'''
}
int main(){hp::CalendarController c;for(unsigned i=0;i<200;++i)c.poll();assert(c.operations.size()==3);assert(c.operations[0]=="calendars"&&c.operations[1]=="people"&&c.operations[2]=="status");assert(!c.busy&&c.bootstrap_==0);
hp::CalendarService::instance().revision++;for(unsigned i=0;i<200;++i)c.poll();assert(c.operations.size()==6&&!c.busy);
hp::CalendarSyncService::instance().revision++;for(unsigned i=0;i<200;++i)c.poll();assert(c.operations.size()==9&&!c.busy);
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

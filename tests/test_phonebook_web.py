from pathlib import Path
import re
import shutil
import subprocess
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')

class PhonebookWebTests(unittest.TestCase):
    def test_actual_web_jobs_pagination_revision_and_security(self):
        source=read('src/services/PhonebookWebApi.cpp')
        body='\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
        fixture=r'''
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <cassert>
#include "PhonebookFilter.h"
using String=std::string;
namespace hp {
struct CalendarService{static std::recursive_mutex& profileMutex(){static std::recursive_mutex m;return m;}};
struct PhonebookContact{String id,name,company,phone,mobile,email,userId,street,city;bool readOnly=false;struct Value{String value;};struct Address{String street,city,postalCode;};std::vector<Value> phones,emails;std::vector<Address> addresses;};
struct PhonebookUser{String id,name,firstName,lastName;};struct LocalPhonebook{std::vector<PhonebookContact> contacts;std::vector<PhonebookUser> users;};
struct NetworkWorker{bool worker=true;static NetworkWorker& instance(){static NetworkWorker w;return w;}bool isWorkerContext(){return worker;}};
struct SecurityWriteGuard{};struct SecurityService{bool allowed=true,locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return allowed;}bool tabletLocked(){return locked;}};
struct LuaJobService{static constexpr size_t kResponseBytes=16384;struct Request{String context,body;const std::atomic<bool>* cancelled=nullptr;};struct Result{String error,body;bool ok=false;};};
void executePostalLookupJob(const LuaJobService::Request&,LuaJobService::Result& result){result.ok=true;result.body="{\"places\":[]}";}
struct PhonebookService{unsigned writes=0,version=10;LocalPhonebook data;enum class LocalAction{SaveContact,RemoveContact,SaveUser};static PhonebookService& instance(){static PhonebookService s;return s;}
 bool loadDirectory(LocalPhonebook& out){out=data;return true;}unsigned revision(){return version;}
 static void encodeContact(JsonObject out,const PhonebookContact& c){out["id"]=c.id;out["name"]=c.name;}
 static bool decodeContact(JsonObjectConst row,PhonebookContact& c){c.name=row["name"]|"";return !c.name.empty();}
 bool changeLocal(LocalAction,const PhonebookContact&,const PhonebookUser&,LocalPhonebook&){++writes;++version;return true;}
 bool exportVcard(const PhonebookContact&,String& out){out="BEGIN:VCARD";return true;}
 bool importVcard(const String&,const String&,LocalPhonebook&){++writes;++version;return true;}};
}
'''+body+r'''
int main(){using namespace hp;auto& service=PhonebookService::instance();for(unsigned i=0;i<20;++i){PhonebookContact c;c.id=std::to_string(i);c.name="Max";service.data.contacts.push_back(c);}
 LuaJobService::Request req;LuaJobService::Result result;req.context="list";req.body="{}";executePhonebookWebJob(req,result);assert(result.ok);JsonDocument doc;assert(!deserializeJson(doc,result.body));assert(doc["items"].size()==16);assert(doc["more"]==true);assert(doc["total"]==20);
 {LuaJobService::Request postal;postal.context="postal_lookup";postal.body="{}";LuaJobService::Result lookup;executePhonebookWebJob(postal,lookup);assert(lookup.ok&&service.writes==0);SecurityService::instance().allowed=false;lookup={};executePhonebookWebJob(postal,lookup);assert(lookup.error=="settings_locked");SecurityService::instance().allowed=true;}
 req.body="{\"offset\":16}";result={};executePhonebookWebJob(req,result);assert(result.ok);deserializeJson(doc,result.body);assert(doc["items"].size()==4);assert(doc["more"]==false);
 req.context="save";req.body="{\"revision\":9,\"contact\":{\"name\":\"Anna\"}}";result={};executePhonebookWebJob(req,result);assert(result.error=="revision_conflict"&&service.writes==0);
 req.body="{\"revision\":10,\"contact\":{\"name\":\"Anna\"}}";result={};executePhonebookWebJob(req,result);assert(result.ok&&service.writes==1);
 SecurityService::instance().allowed=false;result={};executePhonebookWebJob(req,result);assert(result.error=="settings_locked"&&service.writes==1);SecurityService::instance().allowed=true;
 NetworkWorker::instance().worker=false;result={};executePhonebookWebJob(req,result);assert(result.error=="worker_required"&&service.writes==1);NetworkWorker::instance().worker=true;
 std::atomic<bool> cancel{true};req.cancelled=&cancel;result={};executePhonebookWebJob(req,result);assert(result.error=="cancelled"&&service.writes==1);
}
'''
        test_system_performance.SystemPerformanceTests.native(self,fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',ROOT/'src/apps/phonebook'])

    def test_removed_legacy_user_save_route(self):
        api=read('src/services/PhonebookWebApi.cpp')
        web=read('src/services/WebServerService.cpp')
        bridge=read('src/apps/lua/CalendarLuaBridge.cpp')
        self.assertNotIn('"user_save"',api)
        self.assertNotIn('"user_save"',web)
        self.assertIn('"hp_user_save"',bridge)  # central user profile API remains
        self.assertIn('"person_save"',web)  # central web profile API remains

    def test_browser_scripts_parse_and_have_no_background_polling(self):
        node=shutil.which('node');self.assertIsNotNone(node)
        for path,pattern in [('src/services/PhonebookWebPage.h',r'<script>(.*?)</script>'),('src/services/AccountConnectScript.h',r'R"AC\((.*?)\)AC"')]:
            script=re.search(pattern,read(path),re.S).group(1)
            result=subprocess.run([node,'--check'],input=script,text=True,capture_output=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertNotIn('setInterval(',script)
            self.assertNotIn('innerHTML',script)

    def test_shared_users_actual_migration_is_id_based_and_preserves_contacts(self):
        import test_shared_profiles
        source=read('src/services/PhonebookService.cpp')
        method='bool PhonebookService::sharedUsers'+source.split('bool PhonebookService::sharedUsers',1)[1].split('bool PhonebookService::load(',1)[0]
        fixture=test_shared_profiles.program(actual_users=True)+r'''
int main(){using namespace hp;auto& service=PhonebookService::instance();auto& calendar=CalendarService::instance();auto& storage=StorageService::instance();
 LocalPhonebook legacy;legacy.users.push_back({"1234567890abcdef","Max privat"});PhonebookContact contact;contact.id="contact-1";contact.name="Max";contact.firstName="Max";contact.userId="1234567890abcdef";legacy.contacts.push_back(contact);
 assert(service.saveLocal(legacy));const auto initial=storage.writes;
 SecurityService::instance().allowed=false;LocalPhonebook locked;assert(service.loadLocal(locked));assert(storage.writes==initial&&locked.contacts[0].userId=="1234567890abcdef");
 SecurityService::instance().allowed=true;LocalPhonebook migrated;assert(service.loadLocal(migrated));assert(storage.writes>initial&&calendar.personCount_==1);assert(migrated.contacts[0].id=="contact-1");assert(migrated.contacts[0].userId==migrated.users[0].id&&migrated.contacts[0].userId!="1234567890abcdef");
 const auto writes=storage.writes;LocalPhonebook replay;assert(service.loadLocal(replay));assert(storage.writes==writes&&replay.contacts[0].userId==migrated.users[0].id);
 calendar.reboot();LocalPhonebook loaded;assert(service.loadLocal(loaded));assert(loaded.contacts[0].id=="contact-1"&&loaded.contacts[0].userId==migrated.users[0].id);
 loaded.users.push_back({"fedcba0987654321","Max privat"});assert(service.saveLocal(loaded));LocalPhonebook again;assert(service.loadLocal(again));assert(calendar.personCount_==2&&again.users[0].id!=again.users[1].id&&again.contacts[0].id=="contact-1");
}
'''
        test_system_performance.SystemPerformanceTests().native(fixture,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_routes_are_shared_guarded_and_worker_only(self):
        web=read('src/services/WebServerService.cpp');api=read('src/services/PhonebookWebApi.cpp');app=read('src/apps/phonebook/PhonebookApp.cpp')
        for route in ['/phonebook','/api/phonebook/job','/api/phonebook/result','/account-connect.js']:self.assertIn('"'+route+'"',web)
        self.assertIn('return dataJobHandler(req,true)',web)
        self.assertIn('return dataResultHandler(req,true)',web)
        self.assertIn('if(!writeAuthorized(req))',web)
        self.assertIn('@phonebook-web',web)
        self.assertIn('isWorkerContext()',api)
        self.assertIn('SecurityWriteGuard',api)
        self.assertIn('rows.size()<16',api)
        self.assertIn('revision_conflict',api)
        self.assertNotIn('xTaskCreate',api+app)
        self.assertIn('lv_obj_set_flex_grow(searchCell,1)',app)
        self.assertIn('millis()-p.searchChangedAt>=250',app)

if __name__=='__main__':unittest.main()

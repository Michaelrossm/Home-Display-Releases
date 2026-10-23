from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

def function(source,signature):
    start=source.index(signature);opening=source.index('{',start);depth=0
    for i in range(opening,len(source)):
        if source[i]=='{':depth+=1
        elif source[i]=='}':
            depth-=1
            if not depth:return source[start:i+1]
    raise AssertionError('Unterminated function')

class NativePhonebookTests(unittest.TestCase):
    def test_actual_flat_users_validation_and_crud(self):
        import test_shared_profiles
        code=test_shared_profiles.program()+r'''
int main(){using namespace hp;auto& s=PhonebookService::instance();LocalPhonebook out;PhonebookUser u;u.name="Alice";
assert(s.changeLocal(PhonebookService::LocalAction::SaveUser,{},u,out));assert(out.users.size()==1&&out.contacts.size()==1);
PhonebookContact c;c.name="Bob";c.userId=out.users[0].id;c.street="Hauptstrasse";c.houseNumber="12a";c.notes="Notiz";
assert(s.changeLocal(PhonebookService::LocalAction::SaveContact,c,{},out));assert(out.contacts.size()==2&&out.contacts.back().houseNumber=="12a");
c=out.contacts.back();c.mobile="123";assert(s.changeLocal(PhonebookService::LocalAction::SaveContact,c,{},out));assert(out.contacts.size()==2&&out.contacts.back().mobile=="123");
auto bad=out;bad.contacts.back().userId="missing";assert(!validLocal(bad));bad=out;bad.users[0].name="Alice\nAdmin";assert(!validLocal(bad));
bad=out;bad.contacts.push_back(bad.contacts[0]);assert(!validLocal(bad));bad=out;bad.users.push_back(bad.users[0]);assert(!validLocal(bad));
bad=out;bad.contacts.back().birthday={2023,2,29};assert(!validLocal(bad));bad.contacts.back().birthday={0,2,29};assert(validLocal(bad));
bad.contacts.back().phones.resize(9);assert(!validLocal(bad));bad=out;bad.contacts.back().emails={{"Privat","a@test.de"},{"Arbeit","b@test.de"}};assert(validLocal(bad));
bad.contacts.back().emails[0].label="bad\nlabel";assert(!validLocal(bad));bad=out;bad.contacts.back().addresses.resize(5);assert(!validLocal(bad));
SecurityService::instance().allowed=false;assert(!s.changeLocal(PhonebookService::LocalAction::RemoveContact,c,{},out));SecurityService::instance().allowed=true;
NetworkWorker::instance().worker=false;assert(!s.changeLocal(PhonebookService::LocalAction::RemoveContact,c,{},out));NetworkWorker::instance().worker=true;
assert(s.changeLocal(PhonebookService::LocalAction::RemoveContact,c,{},out));assert(out.contacts.size()==1);
bad.users.resize(17);assert(!validLocal(bad));bad={};bad.contacts.resize(129);assert(!validLocal(bad));
}
'''
        test_system_performance.SystemPerformanceTests().native(code,[ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_actual_filter_not_separate_databases(self):
        header=(ROOT/'src/apps/phonebook/PhonebookFilter.h').as_posix()
        code=r'''
#include <string>
#include <cassert>
#include <vector>
'''+f'#include "{header}"\n'+r'''
struct Value{std::string label,value;};struct Address{std::string street,city,postalCode;};
struct Contact{std::string name,email,phone,mobile,userId,company,street,city;std::vector<Value> phones,emails;std::vector<Address> addresses;};
int main(){Contact c{"Anna Müller","a@test.de","123","456","alice"};
assert(hp::phonebookMatches(c,"","ANNA"));assert(hp::phonebookMatches(c,"alice","456"));
assert(!hp::phonebookMatches(c,"bob",""));assert(!hp::phonebookMatches(c,"alice","other"));
assert(hp::phonebookMatches(c,"alice","Müller"));assert(hp::phonebookMatches(c,"alice","MÜLLER"));assert(hp::phonebookMatches(c,"","test.de"));}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_integration_worker_storage_and_limits(self):
        app=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        service=(ROOT/'src/services/PhonebookService.cpp').read_text(encoding='utf-8')
        main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
        self.assertIn('&phonebookApp',main)
        self.assertIn('submitPriority(worker,this,"phonebook")',app)
        self.assertIn('w.generation==app.generation_.load()',app)
        self.assertIn('n<offset+20',app)
        self.assertIn('StorageService::instance().writeAtomic',service)
        self.assertIn('recoverAtomicWrite',service)
        self.assertIn('SecurityWriteGuard guard',service)
        self.assertIn('!account.phonebookEnabled',service)
        self.assertNotIn('xTaskCreate',app)
        self.assertNotIn('lv_',function(service,'bool PhonebookService::changeLocal('))

    def test_actual_completion_generation_and_one_shot_reload(self):
        source=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        tick=function(source,'static void tick(')
        code=r'''
#include <atomic>
#include <string>
#include <utility>
#include <cassert>
struct lv_obj_t{std::string text;};struct lv_timer_t{void* user;};
uint32_t clockMs=1000;uint32_t millis(){return clockMs;}
void* lv_timer_get_user_data(lv_timer_t* t){return t->user;}
void lv_label_set_text(lv_obj_t* o,const char* value){o->text=value;}
struct PhonebookApp{enum class Operation{Load,SaveContact,RemoveContact,SaveUser};
struct Work{unsigned generation=0,revision=0;bool ok=false,conflict=false;int data=0;Operation operation=Operation::Load;}work_;
std::atomic<bool> done_{false},busy_{false};std::atomic<unsigned> generation_{1};unsigned queued=0;
bool queue(Operation){++queued;busy_=true;return true;}};
struct PhonebookService{unsigned value=0;static PhonebookService& instance(){static PhonebookService p;return p;}unsigned revision(){return value;}};
struct PhonebookPage{PhonebookApp* app;int data=0;bool loaded=false,reloadWhenIdle=false;unsigned seenRevision=0,offset=0,optionsCalls=0,contactsCalls=0,usersCalls=0;uint32_t nextRevisionCheckAt=0;lv_obj_t* status;
enum class View{Contacts,Users};View view=View::Contacts;bool searchPending=false;uint32_t searchChangedAt=0;
unsigned postalPolls=0;void pollPostal(){++postalPolls;}
void options(){++optionsCalls;}void contacts(bool=false){++contactsCalls;}void users(){++usersCalls;}
'''+tick+r'''
};
int main(){PhonebookApp app;lv_obj_t status;PhonebookPage p{};p.app=&app;p.status=&status;lv_timer_t timer{&p};
app.busy_=true;app.done_=true;app.work_.generation=0;app.work_.ok=true;app.work_.data=99;p.reloadWhenIdle=true;
PhonebookPage::tick(&timer);assert(p.postalPolls==1);assert(p.data==0&&!p.loaded);assert(app.queued==1&&!p.reloadWhenIdle);
for(unsigned i=0;i<50;++i)PhonebookPage::tick(&timer);assert(app.queued==1);
app.busy_=true;app.done_=true;app.work_.generation=1;app.work_.data=7;PhonebookPage::tick(&timer);
assert(p.loaded&&p.data==7&&p.contactsCalls==1&&!app.busy_);
app.done_=true;app.work_.operation=PhonebookApp::Operation::SaveUser;PhonebookPage::tick(&timer);assert(p.usersCalls==1);
app.done_=true;app.work_.ok=false;app.work_.data=123;PhonebookPage::tick(&timer);assert(p.data==7);assert(status.text.find("fehlgeschlagen")!=std::string::npos);
PhonebookService::instance().value=5;clockMs=3000;PhonebookPage::tick(&timer);assert(app.queued==2);
app.busy_=false;clockMs=3500;PhonebookPage::tick(&timer);assert(app.queued==2);
clockMs=5100;PhonebookPage::tick(&timer);assert(app.queued==3);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_installer_lifetime_and_shared_web_server(self):
        web=(ROOT/'src/services/AppWebSettingsService.cpp').read_text(encoding='utf-8')
        upload=(ROOT/'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        navigation=(ROOT/'src/apps/lua/LuaNavigationBridge.cpp').read_text(encoding='utf-8')
        page=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        for name in ('describe','apply','discover'):
            body=function(web,'bool AppWebSettingsService::'+name+'(')
            self.assertIn('PackageReadGuard packageGuard',body)
        self.assertNotIn('static_cast<LuaApp*>(entry.app)',web)
        for name in ('appsLuaFileHandler','appsLuaCommitHandler','appsLuaCancelHandler'):
            self.assertIn('std::try_to_lock',function(upload,'esp_err_t FileTransferService::'+name+'('))
        self.assertIn('lua_register(L,"hp_web_settings_capabilities"',navigation)
        self.assertIn('lua_register(L,"hp_web_settings_url"',navigation)
        self.assertIn('refreshAll().then(openLinkedAppSettings)',page)

    def test_actual_web_lookup_never_dereferences_stale_pointer(self):
        source=(ROOT/'src/services/AppWebSettingsService.cpp').read_text(encoding='utf-8')
        lookup=function(source,'bool readAppEntry(')
        code=r'''
#include <string>
#include <cassert>
#include <cstdint>
using String=std::string;
enum class AppKind{Lua,Native};
struct IApp{virtual ~IApp()=default;virtual AppKind kind(){return AppKind::Native;}};
struct LuaApp:IApp{String folder="/demo";AppKind kind()override{return AppKind::Lua;}String assetPackageFolder(){return folder;}};
struct AppRegistryEntry{AppKind kind=AppKind::Lua;IApp* app=reinterpret_cast<IApp*>(uintptr_t(1));};
struct AppRegistry{static AppRegistry& instance(){static AppRegistry r;return r;}bool lookup(const char*,AppRegistryEntry& e){e={};return true;}};
struct AppManager{IApp* live=nullptr;static AppManager& instance(){static AppManager a;return a;}IApp* find(const char*){return live;}};
struct LuaAppRegistry{bool owned=true;static LuaAppRegistry& instance(){static LuaAppRegistry r;return r;}bool owns(const char*){return owned;}};
'''+lookup+r'''
int main(){LuaApp actual;AppManager::instance().live=&actual;AppRegistryEntry snapshot;LuaApp* app=nullptr;String error;
assert(readAppEntry("demo",snapshot,app,error));assert(app==&actual); // snapshot.app is deliberately invalid.
AppManager::instance().live=nullptr;assert(!readAppEntry("demo",snapshot,app,error));
IApp native;AppManager::instance().live=&native;assert(!readAppEntry("demo",snapshot,app,error));
AppManager::instance().live=&actual;LuaAppRegistry::instance().owned=false;assert(!readAppEntry("demo",snapshot,app,error));
LuaAppRegistry::instance().owned=true;actual.folder.clear();assert(!readAppEntry("demo",snapshot,app,error));}
'''
        test_system_performance.SystemPerformanceTests().native(code)

if __name__=='__main__':unittest.main()

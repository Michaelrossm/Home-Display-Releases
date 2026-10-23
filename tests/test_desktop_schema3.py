import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]
def read(path): return (ROOT/path).read_text(encoding='utf-8')
def stripped(path): return '\n'.join(x for x in read(path).splitlines() if not x.startswith('#include') and x != '#pragma once')

class DesktopSchema3Tests(unittest.TestCase):
    def js(self, script):
        node = shutil.which('node')
        self.assertIsNotNone(node, 'Node is required for the actual installer JavaScript')
        result = subprocess.run([node, '-'], input=script, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def validators(self):
        source = read('src/services/WebServerService.cpp')
        return re.sub(r'^let preparedAppZip=.*$', '', source[source.index('function appVersionCmp('):source.index('function validateWebSettingsBytes(')], flags=re.M)

    def test_installer_matches_real_native_schema3_parser(self):
        base = dict(schema=3,id='demo',name='Demo',version='1.0.0',type='lua',entry='main.lua',settings=False,permissions=[])
        cases = [base]
        for key, values in {
            'schema':[1,2,4,None,True,'3'], 'id':['','A','a'*32,' demo '],
            'name':['','ä'*24,'ä'*25,' Demo '], 'version':['no','1@2','1'*25,' 1.2b '],
            'type':['native',' lua '], 'entry':['other.lua',' main.lua '],
            'settings':[None,0,'true',True], 'settingsEntry':[None,'settings.lua','other.lua'],
            'enabled':[None,0,'true',False], 'icon':['x'*32,'home',12],
            'minFirmware':['bad','0.3.3b','0.3.4','0.3.3b9',' 0.3.3b ',1],
            'permissions':[None,{},['storage','storage.writeAppData'],['unknown'],[0],['calendar.read','calendar.write']],
        }.items():
            cases += [{**base,key:value} for value in values]
        for key in ['settings','permissions','id','name','version','type','entry']:
            cases.append({k:v for k,v in base.items() if k != key})
        cases += [{**base, 'settings':True, 'settingsEntry':entry} for entry in ['settings.lua',None,'other.lua',' settings.lua ']]
        bg = dict(enabled=True,priority='low',preferredIntervalMinutes=30,maximumDelayMinutes=60)
        cases += [{**base,'background':b} for b in [None,[],bg,{**bg,'enabled':False},
            {**bg,'enabled':1},{**bg,'extra':1},{**bg,'priority':'none'},
            {**bg,'priority':'normal','preferredIntervalMinutes':15},
            {**bg,'priority':'important','preferredIntervalMinutes':5},
            {**bg,'preferredIntervalMinutes':29},{**bg,'preferredIntervalMinutes':-1},
            {**bg,'preferredIntervalMinutes':1.5},{**bg,'maximumDelayMinutes':10081},
            {**bg,'maximumDelayMinutes':29},{**bg,'requiresNetwork':True},
            {**bg,'requiresNetwork':1},{**bg,'allowDuringHighLoad':'true'}]]
        current = re.search(r'kVersion\s*=\s*"([^"]+)"',read('include/BuildInfo.h'))[1]
        outcomes = json.loads(self.js(self.validators()+f'\nconst corpus={json.dumps(cases)};console.log(JSON.stringify(corpus.map(o=>{{try{{validateAppManifest(o,{json.dumps(current)});return true}}catch(e){{return false}}}})));'))
        fake = read('tests/native/printer_fake.h').split('namespace hp {')[0]
        parser = stripped('src/apps/lua/LuaAppManifest.cpp').split('bool LuaAppManifestParser::load(',1)[0]+'\n}\n'
        code = fake+'\n#include <climits>\nnamespace fs{class FS;}\nbool isDigit(char c){return c>=\'0\'&&c<=\'9\';}\nnamespace hp::build{const char* kVersion='+json.dumps(current)+';}\n'
        code += stripped('src/core/AppPermissions.h')+stripped('src/core/AppLifecycleCore.h')+stripped('src/apps/lua/LuaAppManifest.h')+parser
        code += '\nint main(){hp::LuaAppManifest manifest;String error;\n'
        for index,(case,expected) in enumerate(zip(cases,outcomes)):
            manifest = json.dumps(case, ensure_ascii=True)
            code += f'assert(hp::LuaAppManifestParser::parseJson(R"case({manifest})case",manifest,error,true)=={str(expected).lower()}); // case {index}\n'
        code += '}\n'
        test_system_performance.SystemPerformanceTests().native(code, includes=(ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',))

    def test_actual_web_package_preparation_schema_and_settings(self):
        source = read('src/services/WebServerService.cpp')
        scripts = re.findall(r'<script>(.*?)</script>',source,re.S)
        self.assertTrue(scripts)
        self.js('const vm=require("node:vm");'+''.join('new vm.Script('+json.dumps(script)+');' for script in scripts))
        code = source[source.index('async function prepareAppZip('):source.index('\n',source.index('async function prepareAppZip('))]
        self.js(self.validators()+code+r'''
const assert=require('node:assert/strict');let preparedAppZip=null,installedApps={},lastError='',lastAppZipError='',prepareSequence=0;
const elements={};function $(id){return elements[id]||(elements[id]={style:{}})}
function esc(x){return x}function appInstallProgress(n,t){lastError=t}
async function readAppZip(file){return file}async function api(){return {version:'0.3.3b'}}
function validateWebSettingsBytes(bytes){assert.equal(JSON.parse(new TextDecoder().decode(bytes)).schema,1)}
const base={schema:3,id:'demo',name:'Demo',version:'1.0.0',type:'lua',entry:'main.lua',settings:false,permissions:[]};
const enc=o=>new TextEncoder().encode(JSON.stringify(o));
const pack=o=>({name:'demo.zip',size:100,'app.json':enc(o),'main.lua':enc('return {}')});
(async()=>{
 await prepareAppZip(pack(base));assert(preparedAppZip);
 await prepareAppZip(pack({...base,schema:2}));assert.equal(preparedAppZip,null);
 await prepareAppZip(pack({...base,settings:true,settingsEntry:'settings.lua'}));assert.equal(preparedAppZip,null);
 let z=pack({...base,settings:true,settingsEntry:'settings.lua'});z['settings.lua']=enc('return {}');z['web_settings.json']=enc({schema:1});
 await prepareAppZip(z);assert(preparedAppZip.settings&&preparedAppZip.webSettings);
 z=pack(base);z['settings.lua']=enc('return {}');await prepareAppZip(z);assert.equal(preparedAppZip,null);
 z=pack({...base,minFirmware:'0.3.4'});await prepareAppZip(z);assert.equal(preparedAppZip,null);
 z=pack(base);z['app.json']=new Uint8Array(8193);await prepareAppZip(z);assert.equal(preparedAppZip,null);assert.match(lastError,/8192/);
 z=pack({...base,extra:{a:{b:{c:{d:1}}}}});await prepareAppZip(z);assert.equal(preparedAppZip,null);assert.match(lastError,/Verschachtelung/);
})().catch(e=>{console.error(e);process.exitCode=1});
''')
        web_settings = source.split('function validateWebSettingsBytes(',1)[1].split('\n',1)[0]
        self.assertIn('w.schema!==1',web_settings)

    def test_actual_persistent_shortcut_service(self):
        fake = test_system_performance.SystemPerformanceTests().string_fixture()
        fake = fake.replace('int indexOf(char s)const{auto n=find(s);', 'int indexOf(char s,size_t start=0)const{auto n=find(s,start);')
        fake += r'''
size_t strlcpy(char* out,const char* in,size_t n){auto len=strlen(in);if(n){memcpy(out,in,std::min(len,n-1));out[std::min(len,n-1)]=0;}return len;}
namespace hp{
enum class AppRegistryState{Enabled,Disabled};struct AppRegistryEntry{bool launcherVisible=true;AppRegistryState state=AppRegistryState::Enabled;};
struct AppRegistry{std::map<String,AppRegistryEntry> entries;static AppRegistry& instance(){static AppRegistry r;return r;}bool lookup(const char* id,AppRegistryEntry& e){auto i=entries.find(id);if(i==entries.end())return false;e=i->second;return true;}};
struct ConfigService{std::map<String,String> data;unsigned writes=0;bool writable=true;static ConfigService& instance(){static ConfigService c;return c;}bool has(const char* k){return data.count(k);}String getString(const char* k,const char* fallback){return has(k)?data[k]:String(fallback);}bool putString(const char* k,const String& value){assert(strlen(k)<=15);if(!writable)return false;data[k]=value;++writes;return true;}};
}
'''
        code = fake+stripped('src/services/DockShortcutService.h')+stripped('src/services/DockShortcutService.cpp')+r'''
int main(){using namespace hp;auto& dock=DockShortcutService::instance();auto& cfg=ConfigService::instance();auto& registry=AppRegistry::instance();
char ids[48][40]{};assert(dock.load(ids,48)==4);
registry.entries["radio"]={};registry.entries["music"]={};registry.entries["weather"]={};
registry.entries["files"]={};registry.entries["settings"]={};registry.entries["web-00000001"]={};
registry.entries["hidden"].launcherVisible=false;registry.entries["disabled"].state=AppRegistryState::Disabled;
assert(dock.add("files"));assert(dock.add("settings"));assert(dock.add("web-00000001"));assert(dock.load(ids,24)==6);
auto writes=cfg.writes;assert(dock.add("files")&&cfg.writes==writes);
assert(!dock.add("hidden")&&!dock.add("disabled")&&!dock.add("unknown")&&!dock.add("../files"));
DockShortcutService reboot;assert(reboot.load(ids,24)==6);assert(strcmp(ids[4],"files")==0);
assert(dock.remove("files"));assert(registry.entries.count("files"));assert(!dock.remove("files"));assert(dock.load(ids,24)==5);
cfg.writable=false;assert(!dock.remove("settings"));assert(dock.load(ids,24)==5);cfg.writable=true;
while(dock.load(ids,24))assert(dock.remove(ids[0]));assert(reboot.load(ids,24)==0);assert(cfg.data["ui_dock_apps"]=="/");
registry.entries["-"]={};assert(dock.add("-"));assert(dock.add("files"));assert(dock.remove("files"));assert(dock.load(ids,48)==1&&strcmp(ids[0],"-")==0);assert(dock.remove("-"));
for(unsigned i=0;i<48;++i){String id=String("app")+String(i);registry.entries[id]={};assert(dock.add(id.c_str()));}
registry.entries["overflow"]={};assert(!dock.add("overflow"));assert(dock.load(ids,48)==48);
registry.entries.erase("app0");assert(dock.add("overflow"));assert(dock.load(ids,48)==48);
assert(dock.load(ids,2)==2);assert(dock.load(nullptr,24)==0);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_actual_drag_to_dock_and_bounded_callback_queue(self):
        source = read('src/ui/HomeReorderController.cpp')
        request = source.split('bool HomeReorderController::requestDockChange(',1)[1].split('\nsize_t HomeReorderController::childIndex(',1)[0]
        methods = source.split('void HomeReorderController::finishDrag(',1)[1].split('void HomeReorderController::tileEvent(',1)[0]
        fake = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <deque>
#include <vector>
#include <algorithm>
#include <atomic>
struct lv_area_t{int32_t x1=0,y1=0,x2=0,y2=0;};struct lv_point_t{int32_t x=0,y=0;};
struct lv_obj_t{lv_area_t area;unsigned flags=1;std::vector<lv_obj_t*> children;};
constexpr unsigned LV_OBJ_FLAG_FLOATING=1,LV_OBJ_FLAG_OVERFLOW_VISIBLE=2,LV_OPA_COVER=255;
struct lv_indev_t{} input;bool active=true;lv_point_t touch;unsigned layoutCalls=0;
lv_indev_t* lv_indev_active(){return active?&input:nullptr;}
void lv_indev_get_point(lv_indev_t*,lv_point_t* p){*p=touch;}
void lv_obj_get_coords(lv_obj_t* o,lv_area_t* a){*a=o->area;}
void lv_obj_clear_flag(lv_obj_t* o,unsigned f){o->flags&=~f;}
void lv_obj_set_style_outline_width(lv_obj_t*,int,int){}void lv_obj_set_style_opa(lv_obj_t*,int,int){}
void lv_obj_update_layout(lv_obj_t*){++layoutCalls;}
lv_obj_t* lv_obj_get_child(lv_obj_t* o,size_t n){return o->children.at(n);}
void lv_obj_move_to_index(lv_obj_t*,int32_t){}
uint32_t millis(){return 100;}size_t strlcpy(char* out,const char* in,size_t n){size_t len=strlen(in);memcpy(out,in,std::min(len,n-1));out[std::min(len,n-1)]=0;return len;}
namespace hp {
bool pointInside(const lv_area_t& a,const lv_point_t& p){return p.x>=a.x1&&p.x<=a.x2&&p.y>=a.y1&&p.y<=a.y2;}
struct SecurityService{bool locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}};
struct UiShell{lv_obj_t home{{0,0,1280,724}};lv_obj_t* bar=nullptr;static UiShell& instance(){static UiShell s;return s;}lv_obj_t* dock(){return bar;}lv_obj_t* homeContent(){return &home;}};
struct EventLog{static EventLog& instance(){static EventLog e;return e;}void add(const char*,const char*,const char*){}};
struct HomeReorderController{
struct Binding{lv_obj_t* button=nullptr;char id[40]{};};Binding bindings_[10];size_t bindingCount_=0;
lv_obj_t* boundGrid_=nullptr;lv_obj_t* boundFirst_=nullptr;Binding* dragging_=nullptr;size_t dragSourceIndex_=0;
char pendingDragged_[40]{},pendingTarget_[40]{};bool pendingAfter_=false;int8_t pendingPageDirection_=0;
struct Pending{bool value=false;template<class T>void store(bool v,T){value=v;}}pendingMove_;uint32_t suppressClicksUntil_=0;
struct DockChange{char id[40]{};bool remove=false;};std::deque<DockChange> queue;std::deque<DockChange>* dockQueue_=&queue;
size_t childIndex(lv_obj_t* o){for(size_t i=0;i<boundGrid_->children.size();++i)if(boundGrid_->children[i]==o)return i;return SIZE_MAX;}
Binding* bindingForButton(lv_obj_t* o){for(size_t i=0;i<bindingCount_;++i)if(bindings_[i].button==o)return &bindings_[i];return nullptr;}
bool requestDockChange(const char*,bool);void finishDrag(Binding&);void cancelDrag(Binding&);
};
constexpr int pdPASS=1;int xQueueSend(std::deque<HomeReorderController::DockChange>* q,const HomeReorderController::DockChange* c,int wait){assert(wait==0);if(q->size()==4)return 0;q->push_back(*c);return pdPASS;}
}
'''
        code = fake+'namespace hp{\nbool HomeReorderController::requestDockChange('+request+'\nvoid HomeReorderController::finishDrag('+methods+'\n}\n'+r'''
int main(){using namespace hp;HomeReorderController c;lv_obj_t grid{{0,0,1080,430}},dock{{0,560,1120,672}},tile{{0,0,160,160}};
grid.children={&tile};c.boundGrid_=&grid;c.bindings_[0].button=&tile;c.bindingCount_=1;UiShell::instance().bar=&dock;
for(const char* id:{"files","settings","calculator","web-00000001"}){
strlcpy(c.bindings_[0].id,id,40);c.dragging_=&c.bindings_[0];grid.flags=3;tile.flags=1;touch={500,610};
c.finishDrag(c.bindings_[0]);assert(c.dragging_==nullptr&&!c.pendingMove_.value);
assert(c.queue.size()==1&&strcmp(c.queue.front().id,id)==0&&!c.queue.front().remove);c.queue.clear();
assert(!(tile.flags&LV_OBJ_FLAG_FLOATING)&&!(grid.flags&LV_OBJ_FLAG_OVERFLOW_VISIBLE));}
strlcpy(c.bindings_[0].id,"files",40);c.dragging_=&c.bindings_[0];touch={1250,200};c.finishDrag(c.bindings_[0]);
assert(c.pendingMove_.value&&c.pendingPageDirection_==1&&!c.dragging_&&c.pendingTarget_[0]==0&&strcmp(c.pendingDragged_,"files")==0);
c.pendingMove_.value=false;c.dragging_=&c.bindings_[0];touch={20,200};c.finishDrag(c.bindings_[0]);assert(c.pendingMove_.value&&c.pendingPageDirection_==-1&&!c.dragging_);c.pendingMove_.value=false;
strlcpy(c.bindings_[0].id,"files",40);c.dragging_=&c.bindings_[0];touch={1200,700};c.finishDrag(c.bindings_[0]);assert(c.queue.empty());
c.dragging_=&c.bindings_[0];active=false;c.finishDrag(c.bindings_[0]);assert(c.queue.empty());active=true;
SecurityService::instance().locked=true;assert(!c.requestDockChange("files",false));SecurityService::instance().locked=false;
assert(!c.requestDockChange(nullptr,false));assert(!c.requestDockChange("",false));assert(!c.requestDockChange("abcdefghijklmnopqrstuvwxyzabcdefghijklmn",false));
for(unsigned i=0;i<4;++i)assert(c.requestDockChange("settings",i%2));assert(!c.requestDockChange("files",false));
assert(c.queue.size()==4&&c.queue[1].remove&&c.queue[3].remove);assert(layoutCalls>=6);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_visibility_and_management_are_independent(self):
        for path in ['src/apps/files/FileExplorerApp.h','src/apps/settings/SettingsApp.h']:
            app = read(path)
            self.assertIn('bool launcherVisible()const override{return true;}',app)
            self.assertNotIn('bool userInstallable()const override{return true;}',app)
        manager = read('src/core/AppManager.cpp')
        self.assertIn('if (!app || !app->userInstallable())',manager)
        registry = read('src/core/AppRegistry.cpp')
        self.assertIn('e.launcherVisible=app->launcherVisible()',registry)
        self.assertIn('!e.userInstallable)return false',registry)
        self.assertIn('launcherIdList()',read('src/services/HomeAppOrderService.cpp'))
        shell = read('src/ui/UiShell.cpp')
        self.assertIn('!e->launcherVisible',shell)
        self.assertIn('void UiShell::requestDockRefresh() {',shell)
        refresh = shell.split('void UiShell::requestDockRefresh() {')[1].split('}',1)[0]
        self.assertNotIn('lv_obj_',refresh)
        self.assertIn('dockDirty_.store(true',refresh)
        dock = shell.split('void UiShell::rebuildDock()')[1].split('void UiShell::requestDockRefresh()',1)[0]
        self.assertNotIn('lv_obj_clean(home_)',dock)
        self.assertNotIn('lv_obj_clean(content_)',dock)
        controller = read('src/ui/HomeReorderController.cpp')
        callback = controller.split('void HomeReorderController::finishDrag(')[1].split('void HomeReorderController::cancelDrag(',1)[0]
        self.assertIn('pointInside(dockArea,p)',callback)
        self.assertIn('requestDockChange(binding.id,false)',callback)
        self.assertNotIn('putString',callback)
        launch = controller.split('void HomeReorderController::launch(')[1].split('void HomeReorderController::beginDrag(',1)[0]
        self.assertNotIn('manager.open(',launch)
        self.assertIn('requestLauncherOpen(binding.id)',launch)

if __name__ == '__main__': unittest.main()

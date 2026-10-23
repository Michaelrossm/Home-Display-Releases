import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
def source(path): return (ROOT/path).read_text(encoding="utf-8")

class LuaSettingsTests(unittest.TestCase):
    def test_app_list_returns_after_reused_panel_is_cleaned(self):
        compiler=shutil.which("g++") or shutil.which("clang++")
        src=source("src/apps/settings/DynamicAppSettings.cpp")
        update=src[src.index("void DynamicAppSettings::update()"):src.rfind("\n}")]
        program=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
struct lv_obj_t { void* data=nullptr; std::vector<lv_obj_t*> children; };
constexpr uintptr_t kMarkerValue=0x41505053u;
lv_obj_t panel, list;
lv_obj_t **cachedList=nullptr, **cachedPanel=nullptr;
bool apps=true, settings=true;
uint32_t clockMs=100;uint32_t millis(){return clockMs;}
unsigned injections=0;
uint32_t lv_obj_get_child_count(lv_obj_t* o){return o->children.size();}
lv_obj_t* lv_obj_get_child(lv_obj_t* o,uint32_t i){return o->children.at(i);}
void* lv_obj_get_user_data(lv_obj_t* o){return o->data;}
void lv_obj_delete(lv_obj_t*){panel.children.clear();
if(cachedList)*cachedList=nullptr;if(cachedPanel)*cachedPanel=nullptr;}
struct App {const char* id(){return settings?"settings":"home";}} app;
struct AppManager {static AppManager& instance(){static AppManager m;return m;}
App* current(){return &app;}};
struct UiShell {static UiShell& instance(){static UiShell s;return s;}
lv_obj_t* content(){return &panel;}};
struct DynamicAppSettings {
lv_obj_t* injectedPanel_=nullptr;lv_obj_t* list_=nullptr;bool dirty_=false;uint32_t nextScanMs_=0;
lv_obj_t* findAppsPanel(lv_obj_t*){return apps?&panel:nullptr;}
void inject(lv_obj_t* p){++injections;list.data=reinterpret_cast<void*>(kMarkerValue);
cachedList=&list_;cachedPanel=&injectedPanel_;
p->children.push_back(&list);list_=&list;injectedPanel_=p;}
void update();};
'''+update+r'''
int main(){DynamicAppSettings d;d.update();assert(injections==1);
d.update();assert(injections==1);
// Clean the reused panel and return before update ever observes another page.
lv_obj_delete(&list);d.nextScanMs_=0;d.update();assert(injections==2);
// Diagnose/System removes the list, while the panel address stays identical.
for(int i=0;i<4;++i){apps=false;lv_obj_delete(&list);d.nextScanMs_=0;d.update();
apps=true;d.dirty_=true;d.nextScanMs_=0;d.update();assert(injections==unsigned(3+i));}
d.dirty_=true;d.nextScanMs_=0;d.update();assert(injections==7);
d.update();assert(panel.children.size()==1);assert(injections==7);
settings=false;d.update();settings=true;d.update();assert(injections==7);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/"settings_navigation.cpp";exe=Path(tmp)/("check.exe" if os.name=="nt" else "check")
            cpp.write_text(program,encoding="utf-8")
            if compiler:
                cmd=[compiler,"-std=c++17",str(cpp),"-o",str(exe)]
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists(): self.skipTest("Host C++ compiler unavailable")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
                if not install: self.skipTest("Host C++ compiler unavailable")
                vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 "{cpp}" /Fe:check.exe"'
            result=subprocess.run(cmd,cwd=tmp,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_web_manifest_policy_and_optional_settings(self):
        node=shutil.which("node")
        if not node: self.skipTest("Node required for actual webinstaller JS")
        web=source("src/services/FileTransferService.cpp")
        code=web[web.index("function validId(x)"):web.index("async function luaChoose(file)")]
        choose=web[web.index("async function luaChoose(file)"):web.index("bindDrop(ld,lf,luaChoose)")]
        cases=r'''
const assert=require('node:assert/strict');
const base={schema:3,id:'calculator',name:'Calculator',version:'1.0.0',type:'lua',entry:'main.lua',settings:false,permissions:[]};
checkManifest(base); // No settingsEntry/settings.lua is needed.
checkManifest({...base,settings:true,settingsEntry:'settings.lua'});
const v2={...base,schema:3,permissions:[]};checkManifest(v2);
for(const schema of [1,2,4,undefined])assert.throws(()=>checkManifest({...base,schema}));
checkManifest({...v2,permissions:['network.local','storage','secrets','discovery','energy']});
for(const permissions of [undefined,null,['unknown'],['storage','storage'],[0]])
  assert.throws(()=>checkManifest({...v2,permissions}));
assert.throws(()=>checkManifest({...v2,settingsEntry:'settings.lua'}));
for(const settings of [undefined,null,0,1,'true','false'])
  assert.throws(()=>checkManifest({...base,settings}));
for(const settingsEntry of [undefined,null,'../settings.lua','folder/settings.lua','/settings.lua','other.lua','settings.lua/','SETTINGS.LUA'])
  assert.throws(()=>checkManifest({...base,settings:true,settingsEntry}));
'''
        package_cases=r'''
let prepared, lastError;
const lu={},lf={},lm={style:{}},lcheckLabel={style:{}},lcheck={},lb={},ls={},installedLua={};
function uiP(_b,_s,_v,text){lastError=text}
async function readZip(file){return file}
function packageFiles(manifest,settings){
  const files={'app.json':new TextEncoder().encode(JSON.stringify(manifest)),
    'main.lua':new TextEncoder().encode('function on_open() end')};
  if(settings!==undefined)files['settings.lua']=new TextEncoder().encode(settings);
  return files;
}
(async()=>{
  await luaChoose(packageFiles(base));assert(prepared);assert(!prepared.settings);
  await luaChoose(packageFiles({...base,settings:true,settingsEntry:'settings.lua'}));
  assert.equal(prepared,null);assert.match(lastError,/settings_file_missing/);
  await luaChoose(packageFiles({...base,settings:true,settingsEntry:'settings.lua'},'function on_open() end'));
  assert(prepared.settings);assert.equal(prepared.manifestName,'app.json');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
        result=subprocess.run([node,"-e",code+cases+choose+package_cases],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn("if(obj.settings&&!z['settings.lua'])",web)
        self.assertIn("settings_file_missing",web)
        self.assertIn("'X-App-File':'settings.lua'",web)

    def test_manifest_and_independent_runtime(self):
        lua=source("src/apps/lua/LuaApp.cpp")
        runtime=lua.split("bool LuaApp::openSettings",1)[1].split("void LuaApp::close",1)[0]
        self.assertIn("queuePreparation(parent,true)",runtime)
        self.assertIn('hasGlobal("on_open")',lua.split('void LuaApp::serviceUi()',1)[1])
        close=lua.split("void LuaApp::close",1)[1]
        self.assertNotIn("RadioService",close)
        self.assertNotIn("MediaCommand::Stop",close)
        has=lua.split("bool LuaApp::hasSettings",1)[1].split("String LuaApp::loadScript",1)[0]
        self.assertNotIn("loadScript()",has)
        registry=source("src/apps/lua/LuaAppRegistry.cpp")
        self.assertIn("configurePackage",registry)
        self.assertIn("LUA_MANIFEST_INVALID",registry)

    def test_deferred_transition_and_return_to_apps(self):
        bridge=source("src/apps/lua/LuaNavigationBridge.cpp")
        self.assertIn("requestAppSettings(app->id())",bridge)
        for forbidden in ("close()","lua_close","openSettings("): self.assertNotIn(forbidden,bridge)
        ui=source("src/ui/UiShell.cpp")
        request=ui.split("bool UiShell::requestAppSettings",1)[1].split("bool UiShell::requestAppOpen",1)[0]
        self.assertIn("xQueueOverwrite",request)
        self.assertNotIn("AppManager",request)
        loop=ui.split("void UiShell::loop",1)[1]
        self.assertLess(loop.index("DisplayUiGuard"),loop.index("openSettings(appId+10"))
        self.assertIn('if(!strcmp(appId,"@apps"))openAppsSettingsList()',loop)
        self.assertIn('settings->selectAppsPage();manager.open("settings",content_)',ui)
        self.assertIn('requestAppSettings(app->id())',source("src/apps/settings/DynamicAppSettings.cpp"))
        self.assertIn("setEnabled(app,enabled)",source("src/apps/settings/DynamicAppSettings.cpp"))
        self.assertIn("enabledCb",source("src/apps/settings/DynamicAppSettings.h"))
        self.assertIn("requestAppOpen",ui)
        self.assertIn("DynamicAppSettings::instance().update()",ui)

    def test_syntax_validation_before_live_mutation_and_complete_rollback(self):
        installer=source("src/apps/lua/LuaPackageInstaller.cpp")
        validate=installer.split("bool LuaPackageInstaller::validateStagedFiles",1)[1].split("bool LuaPackageInstaller::validateStage",1)[0]
        self.assertIn('stage+"/settings.lua",manifest,error',validate)
        self.assertNotIn("clearPackageFiles",validate)
        self.assertIn("luaL_loadbuffer",installer)
        self.assertIn('"settings_file_missing"',installer)
        self.assertIn("load(*fs, manifestPath, manifest, error, true)",validate)
        commit=installer.split("bool LuaPackageInstaller::commit(",1)[1].split("bool LuaPackageInstaller::cancel",1)[0]
        self.assertLess(commit.index("validateStage"),commit.index("clearPackageFiles(*appFs, live)"))
        compact=''.join(commit.split())
        failed_backup=compact.split('error="backup_current_failed:',1)[0].rsplit('if(hadLive',1)[1]
        self.assertIn("clearPackageFiles(*appFs,backup)",failed_backup)
        self.assertIn("appFs->rmdir(backup)",failed_backup)
        self.assertLess(compact.index('error="backup_current_failed:'),compact.index("clearPackageFiles(*appFs,live)"))
        enabled_failure=commit.split('error = "enabled_state_persist_failed:',1)[1].split('return false;',1)[0]
        self.assertIn("rollback(id, hadLive, hadRegistered, oldEnabled, error)",enabled_failure)
        self.assertIn("hadRegistered ? oldEnabled : plan.manifest.enabled",commit)
        self.assertIn("removeAppEnabled(id.c_str())",source("src/services/LuaPackageService.cpp"))
        rollback=installer.split("bool LuaPackageInstaller::rollback",1)[1].split("bool LuaPackageInstaller::commit(",1)[0]
        self.assertIn("copyPackageFiles(*fs, backup, live)",rollback)
        parser=source("src/apps/lua/LuaAppManifest.cpp")
        self.assertIn('!doc["settings"].is<bool>()',parser)
        self.assertIn('doc["settingsEntry"].as<String>() != "settings.lua"',parser)

    def test_real_installer_copy_cleanup_and_failed_update_restore(self):
        installer=source("src/apps/lua/LuaPackageInstaller.cpp")
        helpers=installer[installer.index("bool ensureFsDir"):installer.index("\n}\n\nLuaPackageInstaller&")]
        compiler=shutil.which("g++") or shutil.which("clang++")
        with tempfile.TemporaryDirectory(prefix="lua-settings-files-") as temp:
            folder=Path(temp);(folder/"package_helpers.inc").write_text(helpers,encoding="utf-8")
            config=source("src/services/ConfigService.cpp")
            config_helpers=config[config.index("namespace { std::mutex appStateMutex;"):config.index("ConfigService& ConfigService::instance")]
            (folder/"config_helpers.inc").write_text(config_helpers,encoding="utf-8")
            fixture=ROOT/"tests/native/lua_package_files.cpp"
            exe=folder/("check.exe" if os.name=="nt" else "check")
            if compiler:
                cmd=[compiler,"-std=c++17",str(fixture),"-I",str(folder),"-o",str(exe)]
            else:
                vswhere=Path(os.environ.get("ProgramFiles(x86)","C:/Program Files (x86)"))/"Microsoft Visual Studio/Installer/vswhere.exe"
                if not vswhere.exists(): self.skipTest("Host C++ compiler unavailable")
                install=subprocess.check_output([str(vswhere),"-latest","-products","*","-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64","-property","installationPath"],text=True).strip()
                if not install: self.skipTest("Host C++ compiler unavailable")
                vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
                cmd=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /EHsc /std:c++17 /I"{folder}" "{fixture}" /Fe:check.exe"'
            result=subprocess.run(cmd,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

if __name__=="__main__": unittest.main()

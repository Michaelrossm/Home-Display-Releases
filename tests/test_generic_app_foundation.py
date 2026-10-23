from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
def source(path): return (ROOT/path).read_text(encoding='utf-8')
def stripped(text): return '\n'.join(x for x in text.splitlines() if not x.startswith('#include') and x != '#pragma once')

class GenericAppFoundationTests(unittest.TestCase):
    def native(self, code):
        with tempfile.TemporaryDirectory(prefix='generic-app-') as folder:
            work=Path(folder)
            (work/'check.cpp').write_text(code,encoding='utf-8')
            if shutil.which('g++'):
                command=['g++','-std=c++17','check.cpp','-o','check.exe']
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists(): self.skipTest('No native compiler')
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result=subprocess.run(command,cwd=work,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(work/'check.exe')],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_real_filters_cleanup_registry_and_error_codes(self):
        headers='\n'.join(stripped(source('src/apps/lua/'+p)) for p in ['LuaOwnerCleanup.h','LuaEventFilter.h','LuaJobError.h'])
        self.native('#include <cstring>\n#include <cstddef>\n#include <cassert>\n#include <initializer_list>\n'+headers+r'''
int a=0,b=0;
void first(const char* owner){assert(!strcmp(owner,"owner"));++a;}
void second(const char*){++b;}
int main(){hp::LuaOwnerCleanup cleanup;assert(!cleanup.add(nullptr));assert(cleanup.add(first));assert(cleanup.add(first));assert(cleanup.add(second));
 cleanup.run("owner");assert(a==1&&b==1);cleanup.run(nullptr);cleanup.run("");assert(a==1&&b==1);
 hp::LuaEventFilter events;assert(events.accepts("anything"));events.begin();assert(!events.accepts("theme.changed"));
 assert(events.accepts("http.completed",true));assert(events.add("theme.changed"));assert(events.add("theme.changed"));assert(events.count()==1);
 assert(events.accepts("theme.changed")&&!events.accepts("network.changed"));assert(!events.add(""));
 for(const char* name:{"a","b","c","d","e","f","g"})assert(events.add(name));assert(!events.add("h"));events.reset();assert(events.accepts("anything"));
 assert(!strcmp(hp::luaJobErrorCode(""),"none"));assert(!strcmp(hp::luaJobErrorCode("timeout"),"timeout"));
 assert(!strcmp(hp::luaJobErrorCode("storage_permission"),"permission_denied"));assert(!strcmp(hp::luaJobErrorCode("stream_quota"),"limit_exceeded"));
 assert(!strcmp(hp::luaJobErrorCode("stream_busy"),"busy"));assert(!strcmp(hp::luaJobErrorCode("http_status"),"http_error"));}
''')

    def test_real_stream_transaction_ownership_cancel_deadline_and_quota(self):
        self.native(source('tests/native/app_stream_fake.h')+'\n'+stripped(source('src/services/AppFileStreamService.h'))+'\n'+stripped(source('src/services/AppFileStreamService.cpp'))+r'''
int main(){using namespace hp;auto& files=AppFileStreamService::instance();String error;size_t written=0;
 for(const char* name:{"../escape","/absolute","a//b","a.tmp","a.part","a/b/c/d/e"})assert(!files.validName(name));
 assert(!files.validOwner("../owner")&&!files.validOwner("a/b"));assert(files.validName("images/photo.jpg"));
 auto id=files.begin("owner","photo.jpg",3,error);assert(id);const uint8_t data[]={1,2,3};
 assert(!files.write("other",id,0,data,3,written,error));assert(!files.write("owner",id,1,data,3,written,error));
 assert(!files.open("owner","photo.jpg"));assert(!files.commit("owner",id,written,error));
 assert(files.write("owner",id,0,data,3,written,error)&&written==3);assert(files.commit("owner",id,written,error));
 auto original=files.open("owner","photo.jpg");assert(original&&original.size()==3);uint8_t read[3];assert(original.read(read,3)==3&&read[2]==3);
 id=files.begin("owner","photo.jpg",3,error);assert(id);assert(files.write("owner",id,0,data,3,written,error));failCommit=true;
 assert(!files.commit("owner",id,written,error));assert(files.open("owner","photo.jpg").size()==3);
 id=files.begin("owner","cancel.jpg",3,error);assert(id);files.cancelOwner("owner");NetworkWorker::instance().maintain();assert(!files.commit("owner",id,written,error));
 assert(!disk.count("/streams/owner/cancel.jpg.stream.part"));
 id=files.begin("owner","expired.jpg",3,error);assert(id);now+=120001;NetworkWorker::instance().maintain();assert(!files.abort("owner",id));
 assert(!disk.count("/streams/owner/expired.jpg.stream.part"));
 StorageService::instance().sd=false;assert(!files.begin("owner","large.jpg",AppFileStreamService::kInternalFile+1,error));
 id=files.begin("owner","unknown.jpg",0,error);assert(id);std::vector<uint8_t> chunk(8192,7);
 for(size_t offset=0;offset<1024*1024-8192;offset+=8192)assert(files.write("owner",id,offset,chunk.data(),chunk.size(),written,error));
 assert(!files.write("owner",id,1024*1024-8192,chunk.data(),chunk.size(),written,error));assert(files.abort("owner",id));
 StorageService::instance().sd=true;id=files.begin("owner","badwrite.jpg",3,error);assert(id);failWrite=true;
 assert(!files.write("owner",id,0,data,3,written,error));assert(!disk.count("/streams/owner/badwrite.jpg.stream.part"));
 auto one=files.begin("one","a",1,error),two=files.begin("two","b",1,error),three=files.begin("three","c",1,error);assert(one&&two&&three);
 assert(!files.begin("four","d",1,error));files.cancelOwner("one");NetworkWorker::instance().maintain();assert(files.abort("two",two)&&files.abort("three",three));}
''')

    def test_bridge_registration_background_safety_and_no_new_tasks(self):
        app=source('src/apps/lua/LuaApp.cpp');background=source('src/apps/lua/LuaBackgroundRun.cpp')
        self.assertIn('registerLuaPlatformBridge(L)',app);self.assertIn('registerLuaStreamBridge(L)',app)
        for api in ['hp_capabilities','hp_system_snapshot','hp_events_status','hp_events_subscribe','hp_job_release','hp_stream_begin','hp_stream_write','hp_stream_read','hp_stream_commit','hp_stream_abort','hp_http_download','hp_http_upload','hp_storage_read_range']:
            self.assertIn('"'+api+'"',background)
        for path in ['src/apps/lua/LuaStreamBridge.cpp','src/apps/lua/LuaPlatformBridge.cpp','src/services/AppFileStreamService.cpp']:
            self.assertNotIn('xTaskCreate',source(path));self.assertNotIn('lv_timer_handler',source(path))
        worker=source('src/services/NetworkWorker.cpp');self.assertEqual(worker.count('xTaskCreatePinnedToCore('),2)
        self.assertIn('xTaskGetCurrentTaskHandle()!=task_',worker)
        http=source('src/apps/lua/LuaJobService.cpp');self.assertIn('disable_auto_redirect=true',http)
        self.assertIn('streams.abort(request.owner,downloadId)',http)
        self.assertIn('!slot.request.streamMode&&slot.request.method==0',http)

    def test_real_http_run_streams_without_partial_commit_or_automatic_retry(self):
        headers=stripped(source('src/core/RequestDeadline.h'))+'\n'+stripped(source('src/apps/lua/LuaOwnerCleanup.h'))+'\n'+stripped(source('src/core/AppPermissions.h'))+'\n'+stripped(source('src/apps/lua/LuaJobService.h')).replace('private:','public:')
        worker=source('src/apps/lua/LuaJobService.cpp')
        transport=worker[worker.index('void LuaJobService::executeHttp('):worker.index('void LuaJobService::execute(void*')]
        comparison=worker[worker.index('bool LuaJobService::sameRead('):worker.index('uint32_t LuaJobService::submit(')]
        run=comparison+transport+worker[worker.index('void LuaJobService::run('):worker.rfind('\n}')]
        code=source('tests/native/app_stream_fake.h')+'\n'+stripped(source('src/services/AppFileStreamService.h'))+'\n'+stripped(source('src/services/AppFileStreamService.cpp'))+'\n'+headers+'\n'+source('tests/native/lua_http_stream_fake.h')+'\nnamespace hp {\n'+run+'\n}\n'
        self.native(code+r'''
int main(){using namespace hp;auto& service=LuaJobService::instance();auto& slot=service.slots_[0];
 auto reset=[&](uint8_t mode){assert(NetworkWorker::instance().networkActivity==0);slot.request={};slot.result={};slot.cancelled=false;slot.busy=true;slot.id=4;
  slot.request.owner="http_owner";slot.request.fileName="photo.jpg";slot.request.streamMode=mode;slot.request.method=mode==2?2:0;
  slot.request.cancelled=&slot.cancelled;http.status=200;http.complete=true;http.connect=true;http.cancellation=nullptr;http.readDelay=0;http.length=3;http.response="ABC";};
 reset(1);service.run(slot);assert(slot.result.ok&&slot.result.size==3&&slot.result.processed==3);
 auto file=AppFileStreamService::instance().open("http_owner","photo.jpg");uint8_t bytes[3];assert(file.read(bytes,3)==3&&bytes[0]=='A');
 reset(1);http.response="broken";http.length=6;http.complete=false;service.run(slot);assert(!slot.result.ok);
 file=AppFileStreamService::instance().open("http_owner","photo.jpg");assert(file.size()==3);
 reset(1);http.status=302;unsigned before=http.requests;service.run(slot);assert(!slot.result.ok&&http.requests==before+1&&!slot.result.retryable);
 reset(1);slot.request.fileName="cancel.jpg";http.cancellation=&slot.cancelled;service.run(slot);assert(!AppFileStreamService::instance().open("http_owner","cancel.jpg"));
 assert(!disk.count("/streams/http_owner/cancel.jpg.stream.part"));
 reset(1);slot.request.fileName="timeout.jpg";slot.request.deadlineMs=1000;http.readDelay=1001;service.run(slot);assert(!slot.result.ok&&slot.result.error=="timeout");
 assert(!AppFileStreamService::instance().open("http_owner","timeout.jpg"));
 reset(2);http.response="OK";http.length=2;service.run(slot);assert(slot.result.ok&&http.upload=="ABC"&&slot.result.body=="OK"&&!slot.result.retryable);
 reset(2);http.status=500;before=http.requests;service.run(slot);assert(!slot.result.ok&&!slot.result.retryable&&http.requests==before+1);
 reset(1);slot.request.bearerSecret="missing";service.run(slot);assert(!slot.result.ok&&slot.result.error=="secret_missing");assert(NetworkWorker::instance().networkActivity==0);
 // Actual queued slots: one HTTP read, independent handles and results.
 reset(0);slot.request.coalesce=true;slot.request.url="http://192.168.1.2/status";slot.credentialRevision=7;
 auto& follower=service.slots_[1];follower.request=slot.request;follower.id=5;follower.busy=true;follower.cancelled=false;follower.credentialRevision=7;
 before=http.requests;service.run(slot);assert(http.requests==before+1&&follower.sharedReady&&follower.busy&&!follower.result.done);
 slot.result.body="released-source";service.run(follower);assert(http.requests==before+1&&follower.result.coalesced&&follower.result.body=="ABC"&&follower.result.done);
 auto a=slot.request,b=a;assert(service.sameRead(a,b));b.owner="other";assert(!service.sameRead(a,b));
 b=a;b.bearerSecret="other-secret";assert(!service.sameRead(a,b));b=a;b.method=1;assert(!service.sameRead(a,b));
 b=a;b.context="other-profile";assert(!service.sameRead(a,b));a.url=b.url="http://192.168.1.2/rr_gcode?gcode=M112";assert(!service.sameRead(a,b));
 reset(0);slot.request.coalesce=true;slot.request.url="http://192.168.1.2/status";slot.credentialRevision=7;
 follower.request=slot.request;follower.id=9;follower.busy=true;follower.cancelled=false;follower.sharedReady=false;follower.credentialRevision=7;
 service.run(slot);assert(follower.sharedReady);follower.cancelled=true;before=http.requests;service.run(follower);
 assert(http.requests==before&&follower.id==0&&!follower.busy&&slot.result.ok);
 reset(0);slot.request.coalesce=true;slot.request.url="http://192.168.1.2/status";slot.credentialRevision=7;
 follower.request=slot.request;follower.id=13;follower.busy=true;follower.cancelled=false;follower.sharedReady=false;follower.credentialRevision=7;
 service.run(slot);assert(follower.sharedReady);ConfigService::instance().version=8;before=http.requests;service.run(follower);
 assert(http.requests==before+1&&follower.result.ok&&!follower.result.coalesced);}
''')

    @unittest.skipUnless((ROOT/'examples/lua-app-template/app.json').is_file(), 'Optional local Lua template not present')
    def test_template_uses_standard_manifest(self):
        manifest=json.loads(source('examples/lua-app-template/app.json'))
        self.assertEqual(manifest['schema'],2);self.assertTrue(manifest['settings']);self.assertEqual(manifest['settingsEntry'],'settings.lua')
        self.assertIn('hp_open_app_settings()',source('examples/lua-app-template/main.lua'))
        self.assertIn('hp_open_app()',source('examples/lua-app-template/settings.lua'))
    def test_central_app_cleanup_contract(self):
        cleanup=source('src/services/LuaAppDataCleanupService.cpp')
        self.assertEqual(cleanup.count('for(const char* prefix:{"apps/","streams/"})'),2)
        self.assertIn('streamFiles(req,*fs,root,root,first,0,true)',cleanup)
        self.assertIn('path.endsWith(".part")',cleanup)

    def test_retired_paths_are_removed_without_removing_shared_services(self):
        network=source('src/apps/lua/LuaNetworkBridge.cpp')
        for old in ['hp_http_status','hp_http_result','hp_http_cancel']:
            self.assertNotIn('"'+old+'"',network)
        for current in ['hp_job_status','hp_job_result','hp_job_cancel','hp_http_get']:
            self.assertIn('"'+current+'"',network)
        jobs=source('src/apps/lua/LuaJobService.h')+source('src/apps/lua/LuaJobService.cpp')
        self.assertNotIn('setOwnerCleanup',jobs);self.assertNotIn('resolveTarget',jobs)
        self.assertIn('target(request,pinned,hostname)',jobs)
        nas=source('src/services/NasService.h')+source('src/services/NasService.cpp')
        for old in ['NasTrack','tracks_','refreshMusic','refreshWebDav','refreshDlna','refreshSmb','nasMediaCommand','mediaSourceId_']:
            self.assertNotIn(old,nas)
        for current in ['startSmbBrowse','readRange','statFile','CredentialStore::instance().nasPassword']:
            self.assertIn(current,nas)

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

def without_includes(text):
    return '\n'.join(line for line in text.splitlines() if not line.startswith('#include') and line != '#pragma once')

class PhotoFoundationTests(unittest.TestCase):
    def native(self, code):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / 'check.cpp').write_text(code, encoding='utf-8')
            if shutil.which('g++'):
                command = ['g++', '-std=c++17', 'check.cpp', '-o', 'check.exe']
            else:
                setup = Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists(): self.skipTest('No native compiler')
                command = f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result = subprocess.run(command, cwd=work, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(work / 'check.exe')], cwd=work, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_real_resource_pool_owner_pinning_budget_and_cleanup(self):
        preamble = r'''
#include <array>
#include <memory>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
struct String:std::string {using std::string::string; size_t length()const{return size();}};
using SemaphoreHandle_t=void*;
constexpr int pdTRUE=1,portMAX_DELAY=-1,LV_IMAGE_HEADER_MAGIC=1,LV_COLOR_FORMAT_RGB565=2,LV_COLOR_FORMAT_ARGB8888=4;
SemaphoreHandle_t xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
int xSemaphoreTake(SemaphoreHandle_t,int){return pdTRUE;}void xSemaphoreGive(SemaphoreHandle_t){}
uint32_t millis(){static uint32_t n=0;return ++n;}
struct lv_image_dsc_t {struct {uint32_t magic=0,cf=0,w=0,h=0,stride=0;} header;uint32_t data_size=0;const uint8_t* data=nullptr;};
namespace hp {struct DecodedImage {uint8_t* pixels=nullptr;uint32_t width=0,height=0;bool rgb565=false;~DecodedImage(){free(pixels);}};}
#define CHECK(x) do{if(!(x))return __LINE__;}while(false)
'''
        body = without_includes(source('src/services/ImageResourceService.h')) + '\n' + without_includes(source('src/services/ImageResourceService.cpp'))
        self.native(preamble + body + r'''
std::shared_ptr<hp::DecodedImage> picture(){auto p=std::make_shared<hp::DecodedImage>();p->width=1024;p->height=1024;p->pixels=static_cast<uint8_t*>(malloc(4*1024*1024));return p;}
int main(){auto& pool=hp::ImageResourceService::instance();CHECK(pool.begin());
 auto a=pool.store("one",picture()),b=pool.store("one",picture());CHECK(a&&b);
 CHECK(!pool.acquire("other",a));CHECK(!pool.release("other",a));CHECK(!pool.store("one",picture()));
 auto pin=pool.acquire("one",a);CHECK(pin);CHECK(pool.release("one",a));CHECK(!pool.acquire("one",a));
 CHECK(!pool.store("one",picture()));pin.reset();auto c=pool.store("two",picture());CHECK(c);
 pool.releaseOwner("one");CHECK(!pool.acquire("one",b));CHECK(pool.acquire("two",c));
 auto d=pool.store("two",picture());CHECK(d);pool.releaseOwner("two");
 for(int i=0;i<200;++i){auto id=pool.store("loop",picture());CHECK(id);CHECK(pool.release("loop",id));}
 pool.releaseOwner("loop");return 0;}
''')

    def test_async_owner_cleanup_and_fenced_tables(self):
        bridge=source('src/apps/lua/LuaMediaStorageBridge.cpp')
        app=source('src/apps/lua/LuaApp.cpp')
        jobs=source('src/apps/lua/LuaJobService.cpp')
        self.assertIn('r.handler=execute',bridge)
        self.assertIn('r.owner=app->id()',bridge)
        self.assertIn('r.cancelled',bridge)
        self.assertIn('releaseOwner(owner)',bridge)
        self.assertIn('ownerCleanup_.run(owner)',jobs)
        self.assertLess(app.index('protectLuaUiBridge(L)'),app.index('installLuaMediaStorageTables(L)'))
        self.assertLess(app.index('protectLuaBackgroundCalls(L)'),app.index('installLuaMediaStorageTables(L)'))
        for path in ('src/apps/lua/LuaMediaStorageBridge.cpp','src/services/ImageResourceService.cpp','src/services/AppIdleActionService.cpp'):
            self.assertNotIn('xTaskCreate',source(path))

    def test_real_sorted_window_large_folder_filter_and_cancel(self):
        bridge=source('src/apps/lua/LuaMediaStorageBridge.cpp')
        window=bridge[bridge.index('struct Window {'):bridge.index('String thumbnailPath')]
        self.native(r'''
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdio>
struct String:std::string {using std::string::string;String(const std::string& s):std::string(s){}
 size_t length()const{return size();}bool startsWith(const char* s)const{return rfind(s,0)==0;}
 int indexOf(char c)const{auto p=find(c);return p==npos?-1:int(p);}int lastIndexOf(char c)const{auto p=rfind(c);return p==npos?-1:int(p);}
 String substring(size_t p)const{return substr(p);}void toLowerCase(){for(auto& c:*this)if(c>='A'&&c<='Z')c+=32;}
 int compareTo(const String& s)const{return compare(s);}};
struct NasService {struct FileInfo {String name;uint64_t size=0,modified=0;bool directory=false;};};
struct LuaJobService {struct Request {bool stopped=false;};};
bool cancelled(const LuaJobService::Request& r){return r.stopped;}uint32_t millis(){return 1;}
#define CHECK(x) do{if(!(x))return __LINE__;}while(false)
''' + window + r'''
int main(){LuaJobService::Request request;Window w;w.request=&request;w.started=1;w.capacity=100;
 w.extensionCount=1;w.extensions[0]="jpg";w.folders=false;
 for(int page=0;page<6;++page){w.entries.clear();w.more=false;
  for(int i=549;i>=0;--i){char name[32];snprintf(name,sizeof(name),"photo%04d.jpg",i);CHECK(Window::visit({name,10,uint64_t(i),false},&w));CHECK(w.entries.size()<=100);}
  CHECK(Window::visit({"ignore.png",10,0,false},&w));CHECK(Window::visit({".hidden.jpg",10,0,false},&w));
  CHECK(w.entries.size()==size_t(page==5?50:100));CHECK(w.more==(page<5));
  char first[32];snprintf(first,sizeof(first),"photo%04d.jpg",page*100);CHECK(w.entries.front().name==first);
  w.after=w.entries.back();w.haveAfter=true;
 }
 w.haveAfter=false;w.entries.clear();w.dates=true;
 for(int i=0;i<550;++i){char n[32];snprintf(n,sizeof(n),"photo%04d.jpg",i);CHECK(Window::visit({n,10,uint64_t(i),false},&w));}
 CHECK(w.entries.front().modified==549);CHECK(w.entries.back().modified==450);
 request.stopped=true;CHECK(!Window::visit({"new.jpg",10,999,false},&w));CHECK(w.timedOut);return 0;
}
''')

    def test_permissions_private_paths_and_credentials(self):
        manifest=source('src/apps/lua/LuaAppManifest.cpp')
        for permission in ('storage.read','storage.writeAppData','storage.nasRead','system.wallpaper','power.idleIntegration'):
            self.assertIn(permission,manifest)
            self.assertIn(permission,source('src/services/FileTransferService.cpp'))
        bridge=source('src/apps/lua/LuaMediaStorageBridge.cpp')
        self.assertIn('relative=="/homepanel"',bridge)
        self.assertIn('relative.startsWith("/homepanel/")',bridge)
        self.assertNotIn('nasPassword',bridge)
        self.assertNotIn('p.password',bridge)

    def test_real_smb_page_bounds_unicode_and_bad_packets(self):
        parser=without_includes(source('src/services/SmbDirectoryPage.h'))
        self.native(r'''
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
''' + parser + r'''
#define CHECK(x) do{if(!(x))return __LINE__;}while(false)
void put(std::vector<uint8_t>& p,size_t offset,uint32_t value){for(int i=0;i<4;++i)p[offset+i]=value>>(i*8);}
bool collect(const char* name,uint64_t bytes,uint64_t,bool folder,void* data){auto& names=*static_cast<std::vector<std::string>*>(data);names.push_back(name);return bytes==123&&!folder;}
int main(){std::vector<uint8_t> p(88);put(p,40,123);put(p,60,6);p[80]='A';p[82]=0xfc;p[84]='B';
 std::vector<std::string> names;bool stopped=false;CHECK(hp::SmbDirectoryPage::visit(p.data(),p.size(),collect,&names,stopped));
 CHECK(!stopped&&names.size()==1&&names[0]=="A\xc3\xbc" "B");
 put(p,0,1);CHECK(!hp::SmbDirectoryPage::visit(p.data(),p.size(),collect,&names,stopped));put(p,0,0);
 put(p,60,7);CHECK(!hp::SmbDirectoryPage::visit(p.data(),p.size(),collect,&names,stopped));put(p,60,200);
 CHECK(!hp::SmbDirectoryPage::visit(p.data(),p.size(),collect,&names,stopped));
 put(p,60,6);p[80]='/';size_t before=names.size();CHECK(hp::SmbDirectoryPage::visit(p.data(),p.size(),collect,&names,stopped));CHECK(names.size()==before);
 p[80]=0;p[81]=0xd8;p[82]='A';p[83]=0;CHECK(hp::SmbDirectoryPage::visit(p.data(),p.size(),collect,&names,stopped));CHECK(names.size()==before);
 CHECK(!hp::SmbDirectoryPage::visit(p.data(),8193,collect,&names,stopped));return 0;}
''')
        nas=source('src/services/NasService.cpp')
        visit=nas[nas.index('bool NasService::visit('):nas.index('bool NasService::statFile(')]
        self.assertNotIn('smb2_opendir(',visit)
        self.assertIn('query.output_buffer_length=8192',visit)
        self.assertIn('waitDirectory(session,state,cancel)',visit)

    def test_wallpaper_local_atomic_copy_and_decode_cancel(self):
        browser=source('src/services/FileBrowserService.cpp')
        self.assertIn('selected_a.hpi',browser)
        self.assertIn('selected_b.hpi',browser)
        self.assertIn('writeAtomic',browser)
        self.assertIn('job->cover,job->cancel',browser)
        codec=source('src/services/ImageCodec.cpp')
        self.assertIn('cancelled&&cancelled->load()',codec)
        self.assertIn('for(uint32_t y=0;y<result->height;++y)',codec)

    def test_navigation_uses_existing_queue_and_idle_pass(self):
        navigation=source('src/apps/lua/LuaNavigationBridge.cpp')
        self.assertIn('requestAppOpen',navigation)
        self.assertNotIn('AppManager::instance().open(',navigation)
        self.assertIn('AppIdleActionService::instance().poll()',source('src/ui/UiShell.cpp'))
        idle=source('src/services/AppIdleActionService.cpp')
        self.assertIn('apps.isEnabled(app)',idle)
        self.assertIn('haveFired_',idle)
        self.assertIn('!idle.idleActionActive()',idle)

from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

def strip(path):
    return '\n'.join(line for line in read(path).splitlines()
                     if not line.startswith('#include') and line != '#pragma once')

class NativeMusicTests(unittest.TestCase):
    def native(self, code):
        test_system_performance.SystemPerformanceTests().native(code, [ROOT / '.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src'])

    def test_real_library_merges_usb_sd_nas_and_deduplicates(self):
        fixture=read('tests/native/printer_fake.h').split('\nnamespace hp {',1)[0]+r'''
#include <strings.h>
namespace hp {
template<class T>T* allocatePsramArray(size_t n){auto* p=static_cast<T*>(malloc(sizeof(T)*n));for(size_t i=0;i<n;++i)new(p+i)T{};return p;}
struct NasProfile{bool enabled=true;uint32_t id=7;};
struct NasService {struct FileInfo{String name;uint64_t size=0,modified=0;bool directory=false;};using Visitor=bool(*)(const FileInfo&,void*);
 static NasService& instance(){static NasService s;return s;}size_t profileCount(){return 1;}bool copyProfile(size_t,NasProfile& p){p={};return true;}};
struct ConfigService{static ConfigService& instance(){static ConfigService s;return s;}String getString(const char*,const char*){return R"({"folders":["usb:/Music","nas7:/Shared"]})";}};
struct FileBrowserService{static bool validUri(const String& s){return s.startsWith("usb:/")||s.startsWith("nas7:/");}
 static bool visit(const String& uri,NasService::Visitor visit,void* context){
 if(uri=="sd:/Musik")visit({"SongB.mp3",12,1,false},context);
 if(uri=="usb:/Music"){visit({"Album",0,0,true},context);visit({"Ignored.aac",12,0,false},context);}
 if(uri=="usb:/Music/Album"){visit({"SongA.wav",12,2,false},context);visit({"../bad.mp3",12,2,false},context);}
 if(uri=="nas7:/Shared")visit({"SongC.MP3",12,3,false},context);return true;}};
}
'''+strip('src/apps/music/MusicTypes.h')+strip('src/apps/music/MusicLibrary.h')+strip('src/apps/music/MusicLibrary.cpp')+r'''
int main(){hp::MusicLibrary library;assert(library.begin()&&library.scan()&&library.count()==3);
 assert(String(library.at(0)->path)=="usb:/Music/Album/SongA.wav");assert(library.at(1)->modified==1);
 assert(String(library.at(2)->path)=="nas7:/Shared/SongC.MP3");assert(library.scan()&&library.count()==3);assert(!library.at(3));}
'''
        # MSVC uses the same comparison through its standard alias.
        fixture=fixture.replace('#include <strings.h>','#ifdef _MSC_VER\n#define strcasecmp _stricmp\n#else\n#include <strings.h>\n#endif')
        fixture=fixture.replace('void remove(size_t first,size_t count)', 'void remove(size_t first){erase(first);}\n    void remove(size_t first,size_t count)')
        self.native(fixture)

    def test_real_stream_cookie_worker_reads_rewind_cancel_and_close(self):
        source=read('src/services/AudioFileStream.cpp').split('FILE* AudioFileStream::open',1)[0]
        source='\n'.join(l for l in source.splitlines() if not l.startswith('#include'))+'\n}\n'
        fixture=read('tests/native/printer_fake.h').split('\nnamespace hp {',1)[0]+r'''
#include <array>
#include <cerrno>
using esp_http_client_handle_t=void*;constexpr int ESP_OK=0;
int closed=0,cleaned=0;int esp_crt_bundle_attach(void*){return 0;}
struct esp_http_client_config_t {const char* url;int timeout_ms;int(*crt_bundle_attach)(void*);bool disable_auto_redirect;int buffer_size;};
void* esp_http_client_init(esp_http_client_config_t*){return reinterpret_cast<void*>(1);}
int esp_http_client_set_header(void*,const char*,const char*){return 0;}int esp_http_client_open(void*,int){return 0;}
int esp_http_client_fetch_headers(void*){return 100;}int esp_http_client_get_status_code(void*){return 200;}
int esp_http_client_read(void*,char* out,int n){n=std::min(n,6);memcpy(out,"abcdef",n);return n;}
int esp_http_client_close(void*){++closed;return 0;}int esp_http_client_cleanup(void*){++cleaned;return 0;}
unsigned pdMS_TO_TICKS(unsigned n){return n;}void vTaskDelay(unsigned n){now+=n;}
namespace hp {
struct SecurityService{static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return false;}};
struct UsbStorageService{unsigned value=4;static UsbStorageService& instance(){static UsbStorageService s;return s;}unsigned generation(){return value;}};
struct NetworkWorker {static NetworkWorker& instance(){static NetworkWorker w;return w;}
 bool submitPriority(void(*f)(void*),void* arg,const char*){f(arg);return true;}bool submitLocal(void(*f)(void*),void* arg,const char*,bool){f(arg);return true;}};
struct FileBrowserService {static bool readRange(const String&,uint64_t at,uint8_t* out,size_t cap,size_t& size,uint64_t& total,const std::atomic<bool>*){total=6;size=std::min<uint64_t>(cap,6-at);memcpy(out,"abcdef"+at,size);return true;}};
}
'''+source+r'''
int main(){using namespace hp;auto source=std::make_shared<Source>();source->uri="usb:/Music/a.mp3";source->canceled=std::make_shared<std::atomic<bool>>(false);
 Cookie c{source,0};char out[16]{};assert(readCookie(&c,out,3)==3&&!memcmp(out,"abc",3));assert(seekCookie(&c,0,SEEK_SET)==0);
 assert(readCookie(&c,out,6)==6&&!memcmp(out,"abcdef",6));source->canceled->store(true);assert(readCookie(&c,out,1)==-1);
 source->canceled->store(false);source->usb=true;source->usbGeneration=4;UsbStorageService::instance().value=5;assert(readCookie(&c,out,1)==-1);source->usb=false;
 source->canceled->store(false);source->network=true;source->opened=false;c.offset=0;
 assert(readCookie(&c,out,6)==6);assert(seekCookie(&c,0,SEEK_SET)==0);assert(readCookie(&c,out,3)==3&&!memcmp(out,"abc",3));
 assert(seekCookie(&c,100,SEEK_SET)==-1);source.reset();c.source.reset();assert(closed==1&&cleaned==1);}
'''
        self.native(fixture)

    def test_native_registration_real_decoder_focus_and_bounded_sources(self):
        main=read('src/main.cpp');service=read('src/apps/music/MusicPlayerService.cpp')
        self.assertIn('&musicApp',main)
        for name in ('audio_player_new','audio_player_play','AudioFileStream::open','preparePlayback','submitLocal','registerSource'):
            self.assertIn(name,service)
        self.assertNotIn('PCM-Ausgang wartet',service)
        self.assertNotIn('xTaskCreate',service)
        self.assertIn('if(decoder_)return true',service)
        self.assertIn('music_prefs',service)
        self.assertIn('NetworkWorker::instance()',read('src/services/AudioFileStream.cpp'))
        self.assertIn('crt_bundle_attach=esp_crt_bundle_attach',read('src/services/AudioFileStream.cpp'))
        self.assertIn('state_.state==MusicState::Playing',service)
        self.assertTrue((ROOT/'devices/co2-scale/platformio.ini').is_file())
        self.assertIn('HX711',read('devices/co2-scale/src/main.cpp'))
        self.assertNotIn('HX711',main)

if __name__=='__main__':unittest.main()

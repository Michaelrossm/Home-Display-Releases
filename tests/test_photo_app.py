from pathlib import Path
import unittest
import test_system_performance as native
import test_printer_service as printer

ROOT=Path(__file__).resolve().parents[1]

class PhotoAppTests(unittest.TestCase):
    def test_schedule_midnight_weekdays_and_invalid_clock(self):
        native.SystemPerformanceTests().native((ROOT/'src/apps/photos/PhotoFramePolicy.h').read_text(encoding='utf-8')+r'''
#include <cassert>
int main(){using namespace hp;
 assert(photoWindow(true,1,600,2,480,1320));assert(!photoWindow(true,2,600,2,480,1320));
 assert(!photoWindow(false,1,600,127,480,1320));
 assert(photoWindow(true,1,1400,2,1320,360));assert(photoWindow(true,2,100,2,1320,360));
 assert(!photoWindow(true,2,360,2,1320,360));assert(!photoWindow(true,1,100,2,1320,360));
 assert(photoWindow(true,1,0,2,0,0));assert(!photoWindow(true,7,0,127,0,0));
 assert(!photoWindow(true,1,1440,127,0,0));
}
''')

    def test_shuffle_every_image_once_and_no_boundary_repeat(self):
        native.SystemPerformanceTests().native((ROOT/'src/apps/photos/PhotoFramePolicy.h').read_text(encoding='utf-8')+r'''
#include <cassert>
#include <set>
int main(){using namespace hp;PhotoShuffleBag bag;uint32_t state=129837;auto random=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return state;};
 uint16_t last=UINT16_MAX;std::set<unsigned> starts;
 for(unsigned cycle=0;cycle<100;++cycle){std::set<unsigned> seen;
  for(unsigned i=0;i<101;++i){auto v=bag.next(101,random);assert(v<101&&v!=last&&seen.insert(v).second);if(!i)starts.insert(v);last=v;}
  assert(seen.size()==101);
 }assert(starts.size()>20);
 bag.reset();assert(bag.next(0,random)==UINT16_MAX);assert(bag.next(1025,random)==UINT16_MAX);
 assert(bag.next(1,random)==0&&bag.next(1,random)==0);
 for(unsigned i=0;i<2000;++i)assert(bag.next(100,[](){return 0u;})<100); // broken RNG cannot busy-loop
}
''')

    def test_actual_worker_scan_persistence_and_schedule(self):
        fake=r'''
#include <array>
#include <ctime>
#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif
void taskYIELD(){}
namespace hp {
bool unlocked=true,locked=false,nasEnabled=true,writeOk=true;unsigned reads=0,writes=0,opened=0;String config;
struct SecurityWriteGuard{};
struct SecurityService {static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}bool settingsAllowed(){return unlocked;}};
struct ConfigService {static ConfigService& instance(){static ConfigService s;return s;}uint32_t revision(){return writes;}String getString(const char*,const char*){return config;}bool getBool(const char*,bool){return nasEnabled;}bool putString(const char*,const String& v){++writes;if(writeOk)config=v;return writeOk;}};
struct NetworkWorker {using Fn=void(*)(void*);struct Job{Fn fn;void* arg;};std::vector<Job> jobs;bool reject=false;
 static NetworkWorker& instance(){static NetworkWorker n;return n;}
 bool submitTransfer(Fn fn,void* p,const char*){if(reject)return false;jobs.push_back({fn,p});return true;}
 bool submitLocalTransfer(Fn fn,void* p,const char* tag){return submitTransfer(fn,p,tag);}
 void drain(){while(!jobs.empty()){auto job=jobs.front();jobs.erase(jobs.begin());job.fn(job.arg);}}
};
struct NasService {struct FileInfo{String name;uint64_t size=0,modified=0;bool directory=false;};};
struct FileBrowserService {static bool validUri(const String& s){return s.indexOf("../")<0&&s.indexOf("//")<0&&s.indexOf(":/")>0;}
 static bool removePhoto(const String&,DirectoryPageCache::Revision,const std::atomic<bool>* cancel,String& error){if(cancel->load()||locked||!unlocked){error="locked_or_cancelled";return false;}error="deleted";return true;}
 static bool stat(const String& uri,NasService::FileInfo& out){out.directory=true;return uri=="sd:/Bilder"||uri=="usb:/Bilder";}
 static bool visit(const String& uri,bool(*fn)(const NasService::FileInfo&,void*),void* p,const std::atomic<bool>* cancel){++reads;if(cancel->load())return false;
  if(uri=="sd:/album"){fn({"one.jpg",10,0,false},p);fn({"notes.txt",2,0,false},p);fn({"sub",0,0,true},p);}
  else if(uri=="sd:/album/sub")fn({"two.png",20,0,false},p);
  else if(uri=="usb:/album")fn({"three.webp",30,0,false},p);
  else if(uri=="sd:/Bilder"){fn({"root.jpg",10,0,false},p);fn({"Urlaub",0,0,true},p);}
  else if(uri=="usb:/Bilder")fn({"Urlaub",0,0,true},p);
  else if(uri=="sd:/Bilder/Urlaub")fn({"sd.jpg",10,0,false},p);
  else if(uri=="usb:/Bilder/Urlaub")fn({"usb.jpg",10,0,false},p);
  else if(uri=="sd:/sort"){fn({"a.jpg",10,100,false},p);fn({"b.jpg",10,300,false},p);fn({"c.jpg",10,0,false},p);}
  else return false;return true;
 }};
struct ImageCodec {static bool supported(const char* s){String v=s;return v.endsWith(".jpg")||v.endsWith(".png")||v.endsWith(".webp");}};
enum class IdleLease{PhotoFrame};
struct IdleProfile {unsigned dimAfter=40,saverAfter=120,offAfter=600;bool saverEnabled=true;};
struct IdleService {uint32_t lease=0;static IdleService& instance(){static IdleService s;return s;}IdleProfile profile(){return {};}uint32_t lastActivityMillis(){return 0;}void keepAwake(IdleLease,uint32_t ms){lease=ms;}};
struct DummyApp{const char* id(){return "photos";}};
struct AppManager {DummyApp* app=nullptr;static AppManager& instance(){static AppManager s;return s;}DummyApp* current(){return app;}};
struct UiShell {static UiShell& instance(){static UiShell s;return s;}bool requestAppOpen(const char* id,const char*){assert(!strcmp(id,"photos"));++opened;return true;}void showHome(){AppManager::instance().app=nullptr;}};
struct TimeService {bool validClock=true;unsigned day=1,minute=600;static TimeService& instance(){static TimeService s;return s;}bool valid(){return validClock;}bool localNow(tm& out){out={};out.tm_wday=day;out.tm_hour=minute/60;out.tm_min=minute%60;return validClock;}};
}
'''
        body=r'''
int main(){using namespace hp;auto& s=PhotoLibraryService::instance();auto& worker=NetworkWorker::instance();
 assert(s.begin()&&!s.loaded());worker.drain();now=1000;s.pollUi();assert(s.loaded());
 PhotoSettings defaults;auto automatic=PhotoLibraryService::scan(defaults);assert(!automatic->ready);worker.drain();
 assert(automatic->ready&&automatic->library->count==3); // SD and USB, including albums, with no configured roots
 assert(photoAlbum("sd:/Bilder/Urlaub/sd.jpg")==photoAlbum("usb:/Bilder/Urlaub/usb.jpg"));
 assert(photoInAlbum("usb:/Bilder/Urlaub/usb.jpg","Bilder")&&!photoInAlbum("sd:/BilderAlt/a.jpg","Bilder"));
 assert(photoAlbum("sd:/root.jpg")=="Ohne Ordner");reads=0;
 PhotoSettings sorted;sorted.automaticFolders=false;sorted.folders[0]="sd:/sort";
 auto byDate=PhotoLibraryService::scan(sorted);worker.drain();assert(String(byDate->library->path(0))=="sd:/sort/b.jpg"&&String(byDate->library->path(2))=="sd:/sort/c.jpg");
 sorted.sortDate=false;sorted.descending=false;auto byName=PhotoLibraryService::scan(sorted);worker.drain();assert(String(byName->library->path(0))=="sd:/sort/a.jpg");
 String settingsError;PhotoSettings decoded;assert(PhotoLibraryService::settingsFromJson(PhotoLibraryService::settingsJson(sorted),decoded,settingsError)&&!decoded.sortDate&&!decoded.descending);
 String invalid=PhotoLibraryService::settingsJson(sorted);invalid.replace("\"start\":420","\"start\":65536");assert(!PhotoLibraryService::settingsFromJson(invalid,decoded,settingsError));reads=0;
 PhotoSettings settings;settings.automaticFolders=false;settings.folders[0]="sd:/album";settings.folders[1]="usb:/album";settings.folders[2]="sd:/album/sub";settings.recursive=true;
 auto scan=PhotoLibraryService::scan(settings);assert(!scan->ready&&reads==0);worker.drain();assert(scan->ready&&scan->library&&scan->library->count==3);
 auto canceled=PhotoLibraryService::scan(settings);canceled->canceled=true;const auto before=reads;worker.drain();assert(reads==before);
 assert(s.save(settings)&&writes==0);unlocked=false;worker.drain();now+=1001;s.pollUi();assert(config.empty());unlocked=true;
 assert(s.save(settings));worker.drain();now+=1001;s.pollUi();assert(s.settings().folders[0]=="sd:/album"&&!s.settings().automaticFolders&&config.length());
 assert(s.toggleFavorite("sd:/album/one.jpg"));worker.drain();now+=1001;s.pollUi();assert(s.favorite("sd:/album/one.jpg"));
 assert(!s.toggleFavorite("system:/config.json")&&!s.toggleFavorite("sd:/album/notes.txt"));
 settings=s.settings();settings.schedule=true;settings.days=2;settings.start=480;settings.end=1320;
 assert(s.save(settings));worker.drain();now=40000;s.pollUi();assert(opened&&IdleService::instance().lease==5000);
 DummyApp app;AppManager::instance().app=&app;assert(s.startFrame(true));now+=1001;s.pollUi();assert(s.frameActive()&&IdleService::instance().lease==5000);
 TimeService::instance().minute=1320;now+=1001;s.pollUi();assert(!s.frameActive()&&!AppManager::instance().app&&IdleService::instance().lease==0);
 locked=true;TimeService::instance().minute=600;now+=1001;const auto requests=opened;s.pollUi();assert(opened==requests);
 worker.reject=true;auto rejected=PhotoLibraryService::scan(settings);assert(rejected->ready&&!rejected->library);
 settings.interval=0;assert(!PhotoLibraryService::validSettings(settings));
}
'''
        code=printer.stripped('src/core/DirectoryPageCache.h')+printer.stripped('src/services/ImageRenderPolicy.h')+fake+printer.stripped('src/apps/photos/PhotoFramePolicy.h')+printer.stripped('src/services/PhotoLibraryService.h')+printer.stripped('src/services/PhotoLibraryService.cpp')+body
        printer.PrinterServiceTests().native(code)

    def test_native_registration_and_async_boundaries(self):
        photo=(ROOT/'src/apps/photos/PhotoApp.cpp').read_text(encoding='utf-8')
        service=(ROOT/'src/services/PhotoLibraryService.cpp').read_text(encoding='utf-8')
        self.assertIn('PhotoApp photoApp',(ROOT/'src/main.cpp').read_text(encoding='utf-8'))
        self.assertIn('PhotoLibraryService::instance().pollUi(callActive)',(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8'))
        for token in ('xTaskCreate','lv_timer_handler','SD.open','USB.open','smb2_connect'):
            self.assertNotIn(token,photo+service)
        self.assertIn('FileBrowserService::image',photo)
        self.assertIn('submitTransfer(scanWorker',service)
        self.assertIn('submitLocalTransfer(settingsWorker',service)
        self.assertIn('ImageResourceService::instance().lease("native.photos"',photo)
        self.assertIn('PhotoView::Albums',photo)
        self.assertIn('photoInAlbum(p.library->path(i),p.filter)',photo)
        self.assertIn('FileBrowserService::stat(root,info)',service)
        web=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        api=(ROOT/'src/services/PhotoWebApi.h').read_text(encoding='utf-8')
        self.assertIn('{"/photos", HTTP_GET, photosPageHandler',web)
        self.assertIn('{"/photos/upload", HTTP_POST, FileTransferService::filesOperationHandler',web)
        self.assertIn('job.owner="@photos-web";job.kind=1',web)
        self.assertIn('requestControl',api)
        self.assertNotIn('AppManager::instance().open',api)

if __name__=='__main__':unittest.main()

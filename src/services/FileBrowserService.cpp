#include "FileBrowserService.h"
#include "StorageService.h"
#include "UsbAppFileSystem.h"
#include "NetworkWorker.h"
#include "SettingsService.h"
#include "ConfigService.h"
#include "SecurityService.h"
#include "UsbStorageService.h"
#include "NetworkService.h"
#include "../core/DirectoryPageCache.h"
#include <mutex>
#include "../core/SortedPage.h"
#include <esp_heap_caps.h>
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
namespace hp {namespace {
constexpr size_t pageSize=96;
std::mutex pageMutex;
DirectoryPageCache* pageCache=nullptr; // Lazy bounded PSRAM, shared by existing workers.
DirectoryPageCache::Revision pageRevision(){return {StorageService::instance().contentRevision(),UsbStorageService::instance().generation(),NasService::instance().contentRevision()};}
bool cacheSource(const String& uri){if(SecurityService::instance().tabletLocked())return false;
    if(uri.startsWith("sd:"))return StorageService::instance().sdAvailable();
    if(uri.startsWith("usb:"))return UsbStorageService::instance().available();
    if(uri.startsWith("nas"))return NetworkService::instance().online();
    return StorageService::instance().internalReady();}
struct Location {String path;bool sd=false,usb=false;uint32_t nas=0;StorageArea area=StorageArea::UserData;};
bool location(const String& uri,Location& result){const int colon=uri.indexOf(':');if(colon<0)return false;
    const String type=uri.substring(0,colon);result.path=uri.substring(colon+1);
    if(type=="sd")result.sd=true;else if(type=="usb")result.usb=true;else if(type=="data")result.area=StorageArea::UserData;else if(type=="apps")result.area=StorageArea::AppData;
    else if(type.startsWith("nas")){const String id=type.substring(3);if(!id.length()||id.length()>10)return false;
        for(size_t i=0;i<id.length();++i)if(id[i]<'0'||id[i]>'9')return false;char* end=nullptr;const unsigned long long value=strtoull(id.c_str(),&end,10);
        if(!value||value>UINT32_MAX||*end)return false;result.nas=value;}
    else return false;
    return result.path.startsWith("/")&&StorageService::instance().validateRelativePath(result.path)&&result.path.indexOf(':')<0&&result.path.indexOf("//")<0;
}
File open(const Location& loc,bool directory=false){auto& store=StorageService::instance();
    if(loc.usb)return store.canStore(StorageClass::UsbAppPackage)?usbAppFileSystem().open(loc.path,FILE_READ):File();
    if(loc.sd)return directory?store.openSdDirectory(loc.path):store.openSdRead(loc.path);
    auto* fs=store.fsFor(loc.area);return fs?fs->open(store.pathFor(loc.area,loc.path),FILE_READ):File();}
struct ListJob {std::shared_ptr<DirectoryRequest> request;String uri;size_t offset;};
// Stable global name sorting before page slicing; bounded RAM for even huge NAS dirs.
void readDirectory(void* pointer){
 std::unique_ptr<ListJob> job(static_cast<ListJob*>(pointer));auto r=job->request;
 if(r->canceled)return;Location loc;
 if(!location(job->uri,loc)||SecurityService::instance().tabletLocked()){
     r->message="Speicherpfad gesperrt oder ungueltig.";r->ready=true;return;
 }
 constexpr size_t kMaxSortedEntries=16384;
 if(job->offset>=kMaxSortedEntries){r->message="Verzeichnislimit (16384) erreicht.";r->ready=true;return;}
 const String cacheKey=job->uri+"|native-name-dir-first|"+String(job->offset);
 const auto revision=pageRevision();std::vector<NasService::FileInfo> cached;bool cachedMore=false;
 if(FileBrowserService::cachedPage(job->uri,cacheKey,cached,cachedMore)){
     if(r->canceled)return;for(const auto& item:cached)r->entries.push_back({item.name,job->uri+(job->uri.endsWith("/")?"":"/")+item.name,item.size,item.directory});
     r->more=cachedMore;r->message="Ordner aus kurzem Seitencache geladen.";r->ready=true;return;
 }
 auto compare=[](const FileBrowserEntry& x,const FileBrowserEntry& y){
    if(x.directory!=y.directory)return x.directory;
    String a=x.name,b=y.name;a.toLowerCase();b.toLowerCase();
    const int order=a.compareTo(b);return order<0||(order==0&&x.name.compareTo(y.name)<0);
 };
 // Keep one page, not thousands of String allocations in internal SRAM.
 // Sequential browsing reuses the previous page as a stable sort cursor.
 size_t skip=job->offset;FileBrowserEntry after;bool haveAfter=false;
 if(job->offset>=pageSize){std::vector<NasService::FileInfo> previous;bool more=false;
     const String previousKey=job->uri+"|native-name-dir-first|"+String(job->offset-pageSize);
     if(FileBrowserService::cachedPage(job->uri,previousKey,previous,more)&&previous.size()==pageSize){const auto& last=previous.back();after={last.name,"",last.size,last.directory};haveAfter=true;skip=0;}}
 const size_t retained=2*pageSize+1;
 auto* entries=static_cast<FileBrowserEntry*>(heap_caps_malloc(retained*sizeof(FileBrowserEntry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!entries){r->message="Nicht genug PSRAM für die Verzeichnisseite.";r->ready=true;return;}
 for(size_t i=0;i<retained;++i)new(entries+i) FileBrowserEntry{};
 auto releaseEntries=[retained](FileBrowserEntry* value){for(size_t i=0;i<retained;++i)value[i].~FileBrowserEntry();heap_caps_free(value);};
 std::unique_ptr<FileBrowserEntry,decltype(releaseEntries)> buffer(entries,releaseEntries);
 using Selection=SortedPage<FileBrowserEntry,decltype(compare)>;
 struct Walk {ListJob* job;Selection* page;decltype(compare)* comparator;FileBrowserEntry* after;bool haveAfter;size_t seen=0;uint32_t started;bool timedOut=false;};
 const uint32_t began=millis();
 for(;;){
 Selection selected(entries,retained,compare);
 Walk walk{job.get(),&selected,&compare,&after,haveAfter,0,began,false};
 const bool available=FileBrowserService::visit(job->uri,[](const NasService::FileInfo& info,void* context){
    auto& w=*static_cast<Walk*>(context);
    if(w.job->request->canceled)return false;
    if(uint32_t(millis()-w.started)>15000){w.timedOut=true;return false;}
    const String& name=info.name;
    const String uri=w.job->uri+(w.job->uri.endsWith("/")?"":"/")+name;
    if(name.startsWith(".")||name.endsWith(".tmp")||name.endsWith(".bak")||name.endsWith(".part")||
       !FileBrowserService::validUri(uri))return true;
    FileBrowserEntry entry{name,uri,info.size,info.directory};if(w.haveAfter&&!(*w.comparator)(*w.after,entry))return true;
    if(!(++w.seen%64))NetworkWorker::instance().maintain();w.page->add(std::move(entry));
    return true;
 },&walk,&r->canceled);
 if(r->canceled)return;
 if(!available||walk.timedOut){r->message=walk.timedOut?"Verzeichnisabruf zu langsam; erneut versuchen.":"Ordner nicht erreichbar (SD, USB oder NAS pruefen).";r->ready=true;return;}
 auto all=selected.sorted();
 const size_t consumed=std::min(pageSize,all.size());
 if(skip>=consumed&&consumed){skip-=consumed;after=all[consumed-1];haveAfter=true;continue;}
 const size_t start=std::min(skip,all.size());
 const size_t end=std::min(start+std::min(pageSize,kMaxSortedEntries-job->offset),all.size());
 r->entries.reserve(end-start);
 for(size_t i=start;i<end;++i)r->entries.push_back(std::move(all[i]));
 r->more=end<walk.seen&&job->offset+r->entries.size()<kMaxSortedEntries;
 r->message=job->offset+r->entries.size()>=kMaxSortedEntries&&end<walk.seen?"Verzeichnislimit 16384 erreicht.":
             (r->more?"Weitere Dateien: Naechste Seite.":"Ordner geladen.");
 if(!(pageRevision()==revision)){r->entries.clear();r->more=false;r->message="Speicher waehrend des Ladens geaendert; erneut laden.";}
 else {std::vector<NasService::FileInfo> page;for(const auto& item:r->entries)page.push_back({item.name,item.size,0,item.directory});FileBrowserService::cachePage(job->uri,cacheKey,page,r->more,revision);}
 r->ready.store(true,std::memory_order_release);
 break;
 }
}
struct NativeImageHeader {uint32_t magic,width,height,format,bytes,crc;};
constexpr uint32_t nativeMagic=0x48504931;
bool nativeHeader(const NativeImageHeader& h,size_t size){return h.magic==nativeMagic&&h.width&&h.height&&h.width<=1280&&h.height<=1280&&h.format==16&&h.bytes==uint64_t(h.width)*h.height*2&&size==sizeof(h)+h.bytes;}
using NativeBytes=std::unique_ptr<uint8_t,decltype(&heap_caps_free)>;
NativeBytes encodeNative(const DecodedImage& image,size_t& total){
    const size_t pixelBytes=size_t(image.width)*image.height*2;total=sizeof(NativeImageHeader)+pixelBytes;
    NativeBytes body(static_cast<uint8_t*>(heap_caps_malloc(total,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);if(!body)return body;
    auto* pixels=reinterpret_cast<uint16_t*>(body.get()+sizeof(NativeImageHeader));
    if(image.rgb565)memcpy(pixels,image.pixels,pixelBytes);
    else for(size_t i=0;i<pixelBytes/2;++i){const uint8_t* c=image.pixels+i*4;const uint32_t alpha=c[3];
        const uint8_t b=(c[0]*alpha+0x1f*(255-alpha))/255,g=(c[1]*alpha+0x11*(255-alpha))/255,red=(c[2]*alpha+0x07*(255-alpha))/255;
        pixels[i]=((red>>3)<<11)|((g>>2)<<5)|(b>>3);}
    NativeImageHeader header{nativeMagic,image.width,image.height,16,uint32_t(pixelBytes),uint32_t(::crc32(0,body.get()+sizeof(NativeImageHeader),pixelBytes))};memcpy(body.get(),&header,sizeof(header));return body;
}
bool validateNative(fs::FS& fs,const String& path){File f=fs.open(path,FILE_READ);NativeImageHeader saved{};
    if(!f||f.read(reinterpret_cast<uint8_t*>(&saved),sizeof(saved))!=sizeof(saved)||!nativeHeader(saved,f.size()))return false;
    uint8_t scratch[1024];uint32_t sum=0;size_t remaining=saved.bytes;while(remaining){const size_t n=std::min(remaining,sizeof(scratch));
        if(f.read(scratch,n)!=n)return false;sum=::crc32(sum,scratch,n);remaining-=n;}return sum==saved.crc;
}
struct ImageJob {std::shared_ptr<ImageRequest> request;String uri;uint32_t width,height;bool wallpaper;bool cover=false;const std::atomic<bool>* cancel=nullptr;bool center=false;bool rgb565=false;};
void readImage(void* pointer){std::unique_ptr<ImageJob> job(static_cast<ImageJob*>(pointer));auto r=job->request;if(r->canceled)return;
    r->queueMs=millis()-r->queuedAt;const uint32_t readStarted=millis();
    auto done=[&](const char* message){r->message=message;r->ready.store(true,std::memory_order_release);};
    Location loc;if(!location(job->uri,loc)||SecurityService::instance().tabletLocked()){done("Bildpfad gesperrt oder ungültig.");return;}
    if(loc.nas&&!ConfigService::instance().getBool("files_nas",false)){done("NAS-Zugriff ist ausgeschaltet.");return;}
    NasService::FileInfo info;if(!FileBrowserService::stat(job->uri,info)||info.directory||info.size<12||info.size>ImageCodec::maxFileBytes){done("Bild fehlt oder ist größer als 4 MiB.");return;}
    const auto revision=pageRevision();size_t size=info.size;auto source=r->source;
    if(!source||source->uri!=job->uri||source->size!=size||!(source->revision==revision)){source=std::make_shared<ImageSource>();source->uri=job->uri;source->revision=revision;source->size=size;source->bytes=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!source->bytes||!FileBrowserService::read(job->uri,source->bytes,size,size,job->cancel)||size!=source->size){done("Bild konnte nicht gelesen werden. Speicher / SD prüfen.");return;}}
    auto* data=source->bytes;r->readMs=millis()-readStarted;const uint32_t decodeStarted=millis();
    if(r->canceled||(job->cancel&&job->cancel->load()))return;
    std::shared_ptr<DecodedImage> decoded;
    if(job->uri.endsWith(".hpi")){NativeImageHeader h{};if(size<sizeof(h)){done("Hintergrunddatei beschädigt.");return;}memcpy(&h,data,sizeof(h));
        if(!nativeHeader(h,size)||::crc32(0,data+sizeof(h),h.bytes)!=h.crc){done("Hintergrunddatei beschädigt (Prüfsumme).");return;}
        decoded=std::make_shared<DecodedImage>();decoded->width=h.width;decoded->height=h.height;decoded->rgb565=true;
        memmove(data,data+sizeof(h),h.bytes);decoded->pixels=data;source->bytes=nullptr;
    }else{if(!ImageCodec::supported(job->uri.c_str())){done("Keine Bildvorschau: PNG, JPEG, BMP, GIF, WebP oder TGA wählen.");return;}
        if(job->wallpaper&&!StorageService::instance().sdAvailable()){job->width=std::min<uint32_t>(job->width,640);job->height=std::min<uint32_t>(job->height,400);}
        ImageCodec::dimensions(data,size,r->sourceWidth,r->sourceHeight);
        decoded=ImageCodec::decode(data,size,job->width,job->height,job->cover,job->cancel,job->center,job->rgb565);
    }
    r->decodeMs=millis()-decodeStarted;
    if(r->canceled||(job->cancel&&job->cancel->load()))return;
    if(!(revision==pageRevision())){done("Datenträger während des Ladens geändert; erneut öffnen.");return;}
    if(job->rgb565)r->source=source;else r->source.reset();source.reset();
    if(!decoded){done("Bild nicht unterstützt, beschädigt, über 4 Megapixel oder zu wenig PSRAM.");return;}
    if(job->wallpaper){if(decoded->rgb565){done("Diese Datei ist bereits ein gespeicherter Hintergrund.");return;}
        size_t total=0;auto body=encodeNative(*decoded,total);
        if(!body){done("Zu wenig Speicher für den Hintergrund.");return;}
        decoded.reset();auto& security=SecurityService::instance();auto& config=ConfigService::instance();String target;
        {SecurityWriteGuard guard;if(r->canceled||security.tabletLocked()||!security.settingsAllowed()||!config.writable()){done("Einstellungen zuerst entsperren; nichts gespeichert.");return;}
            const String current=config.getString("ui_wall","");target=current=="data:/wallpapers/selected_a.hpi"?"/wallpapers/selected_b.hpi":"/wallpapers/selected_a.hpi";}
        auto& storage=StorageService::instance();if(!storage.writeAtomic(StorageArea::UserData,target,body.get(),total,validateNative)){done("Hintergrund nicht gespeichert. Freien Speicher / SD-Karte prüfen; alter Hintergrund bleibt erhalten.");return;}
        // Only the inactive managed image slot is written above. Do not hold
        // the PIN mutex over megabytes of disk I/O: the active setting changes
        // only after a second, serialized authorization check.
        SecurityWriteGuard guard;if(r->canceled||security.tabletLocked()||!security.settingsAllowed()){done("Auswahl abgebrochen oder wieder gesperrt. Bisheriger Hintergrund bleibt aktiv.");return;}
        if(!config.putString("ui_wall",String("data:")+target)||!config.putBool("ui_wallon",true)){done("Hintergrunddatei geschrieben, Einstellung konnte nicht gespeichert werden.");return;}
        FileBrowserService::notifyWallpaperChanged();r->savedWallpaper=true;done("Hintergrund gespeichert. Der Homescreen verwendet eine unabhängige lokale Kopie.");return;
    }
    r->image=std::move(decoded);done("Bild geladen.");
}
}
std::atomic<uint32_t> FileBrowserService::wallpaperRevision_{0};
ImageSource::~ImageSource(){heap_caps_free(bytes);}
bool FileBrowserService::cachedPage(const String& uri,const String& key,std::vector<NasService::FileInfo>& out,bool& more){
    Location loc;if(key.length()>1024||!location(uri,loc)||!cacheSource(uri)||(loc.nas&&NasService::instance().providerStatus(loc.nas)!=1))return false;
    std::lock_guard<std::mutex> lock(pageMutex);if(!pageCache)return false;
    auto* page=pageCache->get(key.c_str(),pageRevision(),millis(),loc.nas?2000:10000);if(!page)return false;
    out.clear();out.reserve(page->count);for(unsigned i=0;i<page->count;++i){const auto& entry=page->entries[i];out.push_back({entry.name,entry.size,entry.modified,entry.directory});}
    more=page->more;return true;
}
DirectoryPageCache::Revision FileBrowserService::directoryRevision(){return pageRevision();}
void FileBrowserService::cachePage(const String& uri,const String& key,const std::vector<NasService::FileInfo>& entries,bool more,DirectoryPageCache::Revision expected){
    if(!cacheSource(uri)||key.length()>1024||entries.size()>DirectoryPageCache::items)return;
    for(const auto& item:entries)if(!item.name.length()||item.name.length()>96)return;
    std::lock_guard<std::mutex> lock(pageMutex);if(!pageCache){auto* memory=heap_caps_malloc(sizeof(DirectoryPageCache),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!memory)return;pageCache=new(memory) DirectoryPageCache;}
    if(!(pageRevision()==expected))return;
    auto* page=pageCache->replace(key.c_str(),expected,millis());if(!page)return;
    page->count=entries.size();page->more=more;for(unsigned i=0;i<page->count;++i){auto& dest=page->entries[i];const auto& item=entries[i];strlcpy(dest.name,item.name.c_str(),sizeof(dest.name));dest.size=item.size;dest.modified=item.modified;dest.directory=item.directory;}page->valid=true;
}
bool FileBrowserService::validUri(const String& uri){Location loc;return location(uri,loc);}
bool FileBrowserService::removePhoto(const String& uri,DirectoryPageCache::Revision expected,const std::atomic<bool>* cancel,String& error){
    if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
    Location loc;
    if(!location(uri,loc)||(!loc.sd&&!loc.usb)||!loc.path.startsWith("/Bilder/")||!ImageCodec::supported(loc.path.c_str())){error="Löschen ist nur für Bilder unter SD/USB:/Bilder erlaubt. NAS und Systemdateien bleiben geschützt.";return false;}
    std::lock_guard<std::mutex> mutation(StorageService::mutationMutex());SecurityWriteGuard security;
    if((cancel&&cancel->load())||SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){error="Löschen abgebrochen oder Einstellungen gesperrt.";return false;}
    if(!(pageRevision()==expected)){error="Datenträger oder Dateien haben sich geändert. Bild erneut öffnen.";return false;}
    auto usb=loc.usb?usbAppFileSystemFor(expected.usb):std::unique_ptr<fs::FS>{};
    auto* fs=loc.sd?StorageService::instance().sdUserDataFs():usb.get();
    if(!fs){error="Datenträger ist nicht verfügbar.";return false;}
    File file=fs->open(loc.path,FILE_READ);if(!file||file.isDirectory()){error="Bild wurde nicht gefunden.";return false;}file.close();
    if((cancel&&cancel->load())||!fs->remove(loc.path)){error="Bild konnte nicht gelöscht werden. Schreibschutz oder Datenträger prüfen.";return false;}
    StorageService::instance().notifyMutation();error="Bild gelöscht.";return true;
}
File FileBrowserService::openRead(const String& uri){Location loc;if(!location(uri,loc)||loc.nas||SecurityService::instance().tabletLocked())return {};return open(loc);}
String FileBrowserService::normalizeUri(const String& uri){
    if(!uri.startsWith("storage://"))return validUri(uri)?uri:String();
    const int slash=uri.indexOf('/',10);if(slash<0)return {};
    const String normalized=uri.substring(10,slash)+":"+uri.substring(slash);return validUri(normalized)?normalized:String();
}
bool FileBrowserService::stat(const String& uri,NasService::FileInfo& info){Location loc;if(!location(uri,loc)||SecurityService::instance().tabletLocked())return false;
    if(loc.nas)return NasService::instance().statFile(loc.nas,loc.path.substring(1),info);
    File f=open(loc);if(!f)return false;info={f.name(),uint64_t(f.size()),uint64_t(f.getLastWrite()),f.isDirectory()};return true;
}
bool FileBrowserService::readRange(const String& uri,uint64_t offset,uint8_t* bytes,size_t capacity,size_t& size,uint64_t& total,const std::atomic<bool>* cancel){
    size=0;total=0;Location loc;if(!bytes||capacity>8192||!location(uri,loc)||SecurityService::instance().tabletLocked())return false;
    if(loc.nas)return NasService::instance().readRange(loc.nas,loc.path.substring(1),offset,bytes,capacity,size,total,cancel);
    File f=open(loc);if(!f||f.isDirectory()||offset>f.size()||!f.seek(offset))return false;
    total=f.size();const size_t wanted=std::min<uint64_t>(capacity,total-offset);
    while(size<wanted){if((cancel&&cancel->load())||SecurityService::instance().tabletLocked())return false;
        const size_t n=f.read(bytes+size,wanted-size);if(!n)return false;size+=n;}return true;
}
bool FileBrowserService::read(const String& uri,uint8_t* bytes,size_t capacity,size_t& size,const std::atomic<bool>* cancel){
    size=0;Location loc;if(!bytes||!location(uri,loc)||SecurityService::instance().tabletLocked())return false;
    if(loc.nas)return NasService::instance().readFile(loc.nas,loc.path.substring(1),bytes,capacity,size,cancel);
    File f=open(loc);if(!f||f.isDirectory()||f.size()>capacity)return false;
    const size_t total=f.size();while(size<total){if((cancel&&cancel->load())||SecurityService::instance().tabletLocked())return false;
        const size_t n=f.read(bytes+size,std::min<size_t>(8192,total-size));if(!n)return false;size+=n;taskYIELD();}return true;
}
bool FileBrowserService::visit(const String& uri,NasService::Visitor visitor,void* context,const std::atomic<bool>* cancel){
    Location loc;if(!visitor||!location(uri,loc)||SecurityService::instance().tabletLocked())return false;
    if(loc.nas)return NasService::instance().visit(loc.nas,loc.path.substring(1),visitor,context,cancel);
    File folder=open(loc,true);if(!folder||!folder.isDirectory())return false;
    for(File f=folder.openNextFile();f;f=folder.openNextFile()){
        if((cancel&&cancel->load())||SecurityService::instance().tabletLocked())return false;
        String name=f.name();name=name.substring(name.lastIndexOf('/')+1);
        NasService::FileInfo info{name,uint64_t(f.size()),uint64_t(f.getLastWrite()),f.isDirectory()};
        if(!visitor(info,context))break;
    }return true;
}
std::shared_ptr<ImageRequest> FileBrowserService::imageNow(const String& uri,uint32_t width,uint32_t height,bool wallpaper,bool cover,const std::atomic<bool>* cancel,bool center,bool rgb565){
    auto r=std::make_shared<ImageRequest>();r->queuedAt=millis();readImage(new ImageJob{r,uri,width,height,wallpaper,cover,cancel,center,rgb565});return r;
}
bool FileBrowserService::saveNativeImage(const String& path,const DecodedImage& image){
    if(!image.pixels||!image.width||!image.height||image.width>1280||image.height>1280)return false;
    size_t size=0;auto body=encodeNative(image,size);return body&&StorageService::instance().writeAtomic(StorageArea::UserData,path,body.get(),size,validateNative);
}
String FileBrowserService::parent(const String& uri){Location loc;if(!location(uri,loc))return {};const int colon=uri.indexOf(':');const int slash=uri.lastIndexOf('/');if(slash<=colon+1)return uri.substring(0,colon+1)+"/";return uri.substring(0,slash);}
std::shared_ptr<DirectoryRequest> FileBrowserService::list(const String& uri,size_t offset){auto r=std::make_shared<DirectoryRequest>();auto* job=new(std::nothrow) ListJob{r,uri,offset};
    if(!job||!NetworkWorker::instance().submitTransfer(readDirectory,job,"file-list")){delete job;r->message="Dateidienst beschäftigt. Erneut versuchen.";r->ready=true;}return r;}
std::shared_ptr<ImageRequest> FileBrowserService::image(const String& uri,uint32_t width,uint32_t height,bool wallpaper,bool rgb565,std::shared_ptr<ImageSource> source){auto r=std::make_shared<ImageRequest>();r->queuedAt=millis();r->source=std::move(source);
    const String mode=wallpaper?ConfigService::instance().getString("ui_wmode","cover"):String("contain");
    auto* job=new(std::nothrow) ImageJob{r,uri,width,height,wallpaper,mode=="cover",&r->canceled,mode=="center",rgb565};
    if(!job||(uri.startsWith("nas")?!NetworkWorker::instance().submitTransfer(readImage,job,"file-image"):!NetworkWorker::instance().submitLocalTransfer(readImage,job,"file-image"))){delete job;r->message="Bilddienst beschäftigt. Erneut versuchen.";r->ready=true;}return r;}
}

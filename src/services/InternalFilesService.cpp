#include "InternalFilesService.h"
#include "StorageService.h"
#include "SecurityService.h"
#include "NetworkWorker.h"
#include <algorithm>
#include <memory>
#include <new>

namespace hp {namespace {
struct ListJob {std::shared_ptr<DirectoryRequest> request;String uri;size_t offset;};
bool resolve(const String& uri,StorageArea& area,String& path) {
    if(uri.startsWith("system:/")) {area=StorageArea::SystemData;path=uri.substring(7);}
    else if(uri.startsWith("config:/")) {area=StorageArea::Config;path=uri.substring(7);}
    else if(uri.startsWith("cache:/")) {area=StorageArea::Cache;path=uri.substring(6);}
    else if(uri.startsWith("recovery:/")) {area=StorageArea::Recovery;path=uri.substring(9);}
    else return false;
    return path.startsWith("/")&&path.indexOf(':')<0&&path.indexOf("//")<0&&
           StorageService::instance().validateRelativePath(path);
}
}
bool InternalFilesService::isUri(const String& uri){StorageArea area;String path;return resolve(uri,area,path);}
std::shared_ptr<DirectoryRequest> InternalFilesService::list(const String& uri,size_t offset){
    auto r=std::make_shared<DirectoryRequest>();auto* job=new(std::nothrow)ListJob{r,uri,offset};
    if(!job||!NetworkWorker::instance().submit(run,job,"flash-list")){
        delete job;r->message="Dateidienst beschäftigt.";r->ready=true;
    }
    return r;
}
void InternalFilesService::run(void* context){
    std::unique_ptr<ListJob> job(static_cast<ListJob*>(context));auto r=job->request;
    if(r->canceled)return;
    auto fail=[&](const char* message){if(!r->canceled){r->message=message;r->ready=true;}};
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){
        fail("Systemordner nur nach Entsperren der Einstellungen verfügbar.");return;
    }
    StorageArea area;String path;
    if(!resolve(job->uri,area,path)){fail("Systempfad ungültig.");return;}
    auto& s=StorageService::instance();auto* fs=s.fsFor(area);
    if(!fs){fail("Interner Flash nicht eingebunden.");return;}
    File folder=fs->open(s.pathFor(area,path),FILE_READ);
    if(!folder||!folder.isDirectory()){fail("Dieser Systemordner ist nicht vorhanden.");return;}
    std::vector<FileBrowserEntry> entries;entries.reserve(48);
    size_t seen=0;
    for(File f=folder.openNextFile();f;f=folder.openNextFile()){
        if(r->canceled||SecurityService::instance().tabletLocked()){return;}
        String name=f.name();name=name.substring(name.lastIndexOf('/')+1);
        if(name=="."||name==".."||name.length()==0||name.indexOf('/')>=0||name.indexOf(':')>=0){f.close();continue;}
        const bool directory=f.isDirectory();const uint64_t bytes=f.size();f.close();
        const String uri=job->uri+(job->uri.endsWith("/")?"":"/")+name;
        if(!isUri(uri))continue;
        if(seen++<job->offset)continue;
        if(entries.size()>=96){r->more=true;break;}
        entries.push_back({name,uri,bytes,directory});
        if((seen&15)==0)taskYIELD();
    }
    folder.close();
    std::sort(entries.begin(),entries.end(),[](const FileBrowserEntry& a,const FileBrowserEntry& b){
        if(a.directory!=b.directory)return a.directory;
        String x=a.name,y=b.name;x.toLowerCase();y.toLowerCase();return x.compareTo(y)<0;
    });
    if(r->canceled)return;
    r->entries=std::move(entries);r->message=r->more?"Weitere Dateien: Nächste Seite.":"Interne Dateien (nur lesen).";
    r->ready.store(true,std::memory_order_release);
}
bool InternalFilesService::readRange(const String& uri,uint8_t* buffer,size_t cap,size_t& read,uint64_t& total,const std::atomic<bool>* cancel){
    read=0;total=0;
    if(!buffer||!cap||cap>4096||(cancel&&cancel->load())||SecurityService::instance().tabletLocked()||
       !SecurityService::instance().settingsAllowed())return false;
    StorageArea area;String path;if(!resolve(uri,area,path))return false;
    auto& s=StorageService::instance();auto* fs=s.fsFor(area);if(!fs)return false;
    File file=fs->open(s.pathFor(area,path),FILE_READ);
    if(!file||file.isDirectory())return false;
    total=file.size();const size_t amount=std::min<uint64_t>(cap,total);
    while(read<amount){if((cancel&&cancel->load())||SecurityService::instance().tabletLocked())return false;
        const size_t n=file.read(buffer+read,amount-read);if(!n)return false;read+=n;}
    return true;
}
}

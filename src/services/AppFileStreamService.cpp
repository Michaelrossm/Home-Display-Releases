#include "AppFileStreamService.h"
#include "StorageService.h"
#include "SecurityService.h"
#include "NetworkWorker.h"
#include <algorithm>
namespace hp {namespace {
bool measure(fs::FS& fs,const String& path,size_t& bytes,unsigned& files,unsigned& nodes,const String* active,unsigned depth=0){
    if(depth>4||++nodes>160)return false;File root=fs.open(path);if(!root)return true;
    if(!root.isDirectory()){bytes+=root.size();return ++files<=32;}
    for(File f=root.openNextFile();f;f=root.openNextFile()){const String child=f.path();const bool directory=f.isDirectory();const size_t size=f.size();f.close();
        if(directory){if(!measure(fs,child,bytes,files,nodes,active,depth+1))return false;}
        else{bool streaming=false;for(size_t i=0;i<AppFileStreamService::kSessions;++i)if(active[i]==child)streaming=true;
            if(!streaming){bytes+=size;if(++files>32)return false;}}}
    return true;
}
}
AppFileStreamService& AppFileStreamService::instance(){static AppFileStreamService service;return service;}
bool AppFileStreamService::validOwner(const String& owner){if(!owner.length()||owner.length()>31)return false;
    for(size_t i=0;i<owner.length();++i){const unsigned char c=owner[i];if(!isalnum(c)&&c!='_'&&c!='-')return false;}return true;}
bool AppFileStreamService::validName(const String& name){if(!name.length()||name.length()>96||name.startsWith("/")||name.endsWith("/")||
    name.indexOf("..")>=0||name.indexOf("//")>=0||name.endsWith(".tmp")||name.endsWith(".bak")||name.endsWith(".part"))return false;
    unsigned depth=0;for(size_t i=0;i<name.length();++i){const unsigned char c=name[i];if(c=='/')++depth;else if(!isalnum(c)&&c!='_'&&c!='-'&&c!='.')return false;}return depth<=3;}
AppFileStreamService::Session* AppFileStreamService::find(const String& owner,uint32_t id){for(auto& s:slots_)if(s.active&&s.id==id&&owner==s.owner)return &s;return nullptr;}
void AppFileStreamService::discard(Session& s){s.file.close();auto& store=StorageService::instance();
    if(store.usesSd(StorageClass::UserData)==s.sd)store.remove(StorageArea::UserData,s.path+".stream.part");
    portENTER_CRITICAL(&mux_);s.active=false;s.id=0;portEXIT_CRITICAL(&mux_);s.name="";s.path="";s.written=s.reserved=s.expected=0;
}
void AppFileStreamService::cleanup(){const uint32_t now=millis();for(auto& s:slots_)if(s.active&&(s.cancelled||uint32_t(now-s.last)>120000||
    StorageService::instance().usesSd(StorageClass::UserData)!=s.sd))discard(s);}
void AppFileStreamService::cancelOwner(const char* owner){if(!owner)return;portENTER_CRITICAL(&mux_);
    for(auto& s:slots_)if(s.active&&!strcmp(s.owner,owner))s.cancelled=true;portEXIT_CRITICAL(&mux_);}
uint32_t AppFileStreamService::begin(const String& owner,const String& name,size_t expected,String& error){cleanup();
    if(!validOwner(owner)||!validName(name)||SecurityService::instance().tabletLocked()){error="file_path_or_locked";return 0;}
    auto& store=StorageService::instance();auto* fs=store.fsFor(StorageClass::UserData);const bool sd=store.usesSd(StorageClass::UserData);
    if(!fs){error="storage_unavailable";return 0;}if(expected>(sd?kSdFile:kInternalFile)){error="file_limit";return 0;}
    const String path="streams/"+owner+"/"+name;Session* slot=nullptr;size_t reservations=0;String active[kSessions];unsigned activeFiles=0;
    for(auto& s:slots_){if(s.active&&owner==s.owner){if(s.path==path){error="stream_busy";return 0;}reservations+=s.reserved;
            active[activeFiles++]=store.pathFor(StorageArea::UserData,s.path+".stream.part");}
        if(!s.active&&!slot)slot=&s;}if(!slot){error="stream_limit";return 0;}
    // Old complete backup may be recovered; .stream.part is never promoted.
    store.recoverAtomicWrite(StorageArea::UserData,path);
    {File existing=fs->open(store.pathFor(StorageArea::UserData,path),FILE_READ);if(existing&&existing.isDirectory()){error="file_directory";return 0;}}
    if(!store.remove(StorageArea::UserData,path+".stream.part")){error="file_partial_remove";return 0;}
    size_t bytes=0;unsigned files=0,nodes=0;
    if(!measure(*fs,store.pathFor(StorageClass::UserData,"streams/"+owner),bytes,files,nodes,active)||files+activeFiles>=32||
       bytes+reservations+expected>(sd?64*1024*1024:kInternalFile)){error="stream_quota";return 0;}
    if(!store.ensureParentDirs(StorageArea::UserData,path)){error="file_directory";return 0;}
    File file=fs->open(store.pathFor(StorageArea::UserData,path+".stream.part"),FILE_WRITE);if(!file){error="file_write";return 0;}
    slot->file=std::move(file);slot->path=path;slot->name=name;slot->sd=sd;slot->expected=expected;slot->reserved=expected;slot->written=0;slot->last=millis();slot->cancelled=false;
    sequence_=(sequence_+1)&0x1fffffff;if(!sequence_)++sequence_;
    portENTER_CRITICAL(&mux_);strlcpy(slot->owner,owner.c_str(),sizeof(slot->owner));slot->id=sequence_;slot->active=true;portEXIT_CRITICAL(&mux_);
    NetworkWorker::instance().addMaintenance([](void* context){static_cast<AppFileStreamService*>(context)->cleanup();},this);return slot->id;
}
bool AppFileStreamService::write(const String& owner,uint32_t id,size_t offset,const uint8_t* bytes,size_t size,size_t& written,String& error){cleanup();auto* s=find(owner,id);
    if(!s||s->cancelled){error="stream_cancelled_or_missing";return false;}written=s->written;
    if(SecurityService::instance().tabletLocked()){error="tablet_locked";return false;}
    if(!bytes||size>kChunk||offset!=s->written){error="stream_offset_or_chunk_limit";return false;}
    const size_t limit=s->sd?kSdFile:kInternalFile;if(size>limit-s->written||(s->expected&&size>s->expected-s->written)){error="file_limit";return false;}
    if(!s->expected){auto& store=StorageService::instance();auto* fs=store.fsFor(StorageClass::UserData);size_t used=0,reserved=0;unsigned files=0,nodes=0;
        String active[kSessions];unsigned count=0;for(auto& other:slots_)if(other.active&&owner==other.owner){reserved+=other.reserved;
            active[count++]=store.pathFor(StorageArea::UserData,other.path+".stream.part");}
        if(!fs||!measure(*fs,store.pathFor(StorageClass::UserData,"streams/"+owner),used,files,nodes,active)||used+reserved+size>(s->sd?64*1024*1024:kInternalFile)){error="stream_quota";return false;}}
    if(s->file.write(bytes,size)!=size){error="file_write";discard(*s);return false;}
    s->written+=size;s->reserved=std::max(s->reserved,s->written);s->last=millis();written=s->written;return true;
}
bool AppFileStreamService::commit(const String& owner,uint32_t id,size_t& written,String& error){cleanup();auto* s=find(owner,id);
    if(!s||s->cancelled||SecurityService::instance().tabletLocked()){error="stream_cancelled_or_locked";return false;}
    written=s->written;if(s->expected&&s->expected!=written){error="stream_incomplete";return false;}s->file.flush();s->file.close();
    if(s->cancelled||SecurityService::instance().tabletLocked()){error="cancelled_or_locked";discard(*s);return false;}
    auto& store=StorageService::instance();const String backup=s->path+".bak",part=s->path+".stream.part";
    store.remove(StorageArea::UserData,backup);const bool existing=store.exists(StorageArea::UserData,s->path);
    if(existing&&!store.rename(StorageArea::UserData,s->path,backup)){error="file_backup";discard(*s);return false;}
    if(s->cancelled||SecurityService::instance().tabletLocked()){if(existing)store.rename(StorageArea::UserData,backup,s->path);
        error="cancelled_or_locked";discard(*s);return false;}
    if(!store.rename(StorageArea::UserData,part,s->path)){if(existing)store.rename(StorageArea::UserData,backup,s->path);error="file_commit";discard(*s);return false;}
    store.remove(StorageArea::UserData,backup);portENTER_CRITICAL(&mux_);s->active=false;s->id=0;portEXIT_CRITICAL(&mux_);s->path="";s->name="";return true;
}
bool AppFileStreamService::abort(const String& owner,uint32_t id){if(auto* s=find(owner,id)){discard(*s);return true;}return false;}
File AppFileStreamService::open(const String& owner,const String& name){auto& store=StorageService::instance();if(!validOwner(owner)||!validName(name)||SecurityService::instance().tabletLocked())return {};
    const String path="streams/"+owner+"/"+name;store.recoverAtomicWrite(StorageArea::UserData,path);auto* fs=store.fsFor(StorageClass::UserData);
    if(!fs)return {};File file=fs->open(store.pathFor(StorageClass::UserData,path),FILE_READ);
    if(!file||file.isDirectory()||file.size()>(store.usesSd(StorageClass::UserData)?kSdFile:kInternalFile))return {};return file;}
}

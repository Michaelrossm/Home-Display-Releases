#include "MusicLibrary.h"
#include "../../services/FileBrowserService.h"
#include "../../services/NasService.h"
#include "../../services/ConfigService.h"
#include "../../core/PsramArray.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>
namespace hp {
bool MusicLibrary::begin(){if(!tracks_)tracks_=allocatePsramArray<MusicTrack>(kMaxTracks);return tracks_;}
MusicLibrary::~MusicLibrary(){if(tracks_){for(size_t i=0;i<kMaxTracks;++i)tracks_[i].~MusicTrack();heap_caps_free(tracks_);}}
bool MusicLibrary::supported(const char* name)const{String s(name);s.toLowerCase();return s.endsWith(".mp3")||s.endsWith(".wav");}
void MusicLibrary::addFile(const char* path,const char* name,uint32_t bytes,uint64_t modified){
 if(!path||!name||strlen(path)>=sizeof(MusicTrack::path)||count_>=kMaxTracks)return;
 for(size_t i=0;i<count_;++i)if(!strcmp(tracks_[i].path,path))return;
 auto& t=tracks_[count_++];strlcpy(t.path,path,sizeof(t.path));String title(name);const int dot=title.lastIndexOf('.');if(dot>0)title.remove(dot);
 strlcpy(t.title,title.c_str(),sizeof(t.title));t.size=bytes;t.modified=modified;
}
bool MusicLibrary::scan(){
 count_=0;if(!tracks_)return false;
 std::vector<String> roots={"sd:/Musik","sd:/Music","usb:/Musik","usb:/Music","sd:/homepanel/media/music","usb:/homepanel/media/music"};
 for(size_t i=0;i<NasService::instance().profileCount();++i){NasProfile p;if(NasService::instance().copyProfile(i,p)&&p.enabled){const String base="nas"+String(p.id)+":";roots.push_back(base+"/Musik");roots.push_back(base+"/Music");}}
 JsonDocument settings;if(!deserializeJson(settings,ConfigService::instance().getString("music_prefs","{}"))){for(JsonVariant v:settings["folders"].as<JsonArray>()){const String uri=v.as<String>();if(roots.size()<32&&FileBrowserService::validUri(uri))roots.push_back(uri);}}
 size_t visits=0;
 for(size_t root=0;root<roots.size()&&visits<128&&count_<kMaxTracks;++root){
  struct Folder{String uri;unsigned depth;};std::vector<Folder> pending={{roots[root],0}};
  while(!pending.empty()&&visits++<128&&count_<kMaxTracks){const auto folder=pending.back();pending.pop_back();
   struct Context{MusicLibrary* self;Folder folder;std::vector<Folder>* pending;};Context ctx{this,folder,&pending};
   FileBrowserService::visit(folder.uri,[](const NasService::FileInfo& f,void* p){auto& c=*static_cast<Context*>(p);
    if(!f.name.length()||f.name.indexOf('/')>=0||f.name=="."||f.name=="..")return true;
    const String uri=c.folder.uri+(c.folder.uri.endsWith("/")?"":"/")+f.name;
    if(f.directory){if(c.folder.depth<8&&c.pending->size()<64)c.pending->push_back({uri,c.folder.depth+1});}
    else if(c.self->supported(f.name.c_str()))c.self->addFile(uri.c_str(),f.name.c_str(),uint32_t(std::min<uint64_t>(UINT32_MAX,f.size)),f.modified);
    return c.self->count_<kMaxTracks;
   },&ctx);
  }
 }
 std::sort(tracks_,tracks_+count_,[](const MusicTrack& a,const MusicTrack& b){const int n=strcasecmp(a.title,b.title);return n<0||(n==0&&strcmp(a.path,b.path)<0);});return true;
}
}

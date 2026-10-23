#include "LuaMediaStorageBridge.h"
#include "LuaApp.h"
#include "LuaJobService.h"
#include "LuaEvents.h"
#include "../../services/FileBrowserService.h"
#include "../../services/ImageResourceService.h"
#include "../../services/StorageService.h"
#include "../../services/UsbStorageService.h"
#include "../../services/SettingsService.h"
#include "../../services/SecurityService.h"
#include "../../services/NetworkService.h"
#include "../../services/ConfigService.h"
#include "../../services/IdleService.h"
#include "../../services/AppIdleActionService.h"
#include "../../core/EventBus.h"
#include "../../core/AppManager.h"
#include "../../ui/UiTheme.h"
#include "../../ui/SecurityUi.h"
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cmath>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp { namespace {
bool allowedPath(const String& path,AppPermissions permissions){
    if(!FileBrowserService::validUri(path))return false;
    if(path.startsWith("nas"))return (permissions&perm(AppPermission::NasRead))!=0;
    if((!path.startsWith("sd:")&&!path.startsWith("usb:"))||!(permissions&perm(AppPermission::StorageRead)))return false;
    // The SD media provider never grants access to firmware/app secrets/cache.
    const String relative=path.substring(path.indexOf(':')+1);
    if(path.startsWith("usb:")&&(relative=="/Apps"||relative.startsWith("/Apps/")))return false;
    if(relative=="/homepanel")return false;
    if(relative.startsWith("/homepanel/")&&relative!="/homepanel/media"&&!relative.startsWith("/homepanel/media/"))return false;
    for(int pos=0;pos<static_cast<int>(relative.length());++pos)
        if(relative[pos]=='/'&&pos+1<static_cast<int>(relative.length())&&relative[pos+1]=='.')return false;
    return true;
}
bool cancelled(const LuaJobService::Request& r){return (r.cancelled&&r.cancelled->load())||SecurityService::instance().tabletLocked();}
struct Window {
    std::vector<NasService::FileInfo> entries;
    NasService::FileInfo after;
    bool haveAfter=false,dates=false,files=true,folders=true,more=false;
    String extensions[8];size_t extensionCount=0,capacity=100;
    const LuaJobService::Request* request=nullptr;uint32_t started=0;bool timedOut=false;
    bool less(const NasService::FileInfo& a,const NasService::FileInfo& b)const{
        if(dates&&a.modified!=b.modified)return a.modified>b.modified;
        return a.name.compareTo(b.name)<0;
    }
    static bool visit(const NasService::FileInfo& entry,void* context){auto& w=*static_cast<Window*>(context);
        if(cancelled(*w.request)||uint32_t(millis()-w.started)>15000){w.timedOut=true;return false;}
        if(!entry.name.length()||entry.name.startsWith(".")||entry.name.length()>96||entry.name.indexOf('/')>=0||entry.name.indexOf('\\')>=0)return true;
        if(entry.directory?!w.folders:!w.files)return true;
        if(!entry.directory&&w.extensionCount){String ext=entry.name.substring(entry.name.lastIndexOf('.')+1);ext.toLowerCase();bool match=false;
            for(size_t i=0;i<w.extensionCount;++i)match|=ext==w.extensions[i];if(!match)return true;}
        if(w.haveAfter&&!w.less(w.after,entry))return true;
        auto pos=std::lower_bound(w.entries.begin(),w.entries.end(),entry,[&](const auto& a,const auto& b){return w.less(a,b);});
        if(w.entries.size()==w.capacity){w.more=true;if(pos==w.entries.end())return true;w.entries.insert(pos,entry);w.entries.pop_back();}
        else w.entries.insert(pos,entry);return true;
    }
};
String thumbnailPath(const String& uri,const NasService::FileInfo& info,uint32_t width,uint32_t height,bool cover){
    const String identity="v1|"+uri+"|"+String(static_cast<unsigned long long>(info.modified))+"|"+String(static_cast<unsigned long long>(info.size))+"|"+String(width)+"x"+String(height)+(cover?"cover":"contain");
    unsigned char digest[32];if(mbedtls_sha256(reinterpret_cast<const unsigned char*>(identity.c_str()),identity.length(),digest,0))return {};
    char hex[65];for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",digest[i]);return String("/cache/images/")+hex+".hpi";
}
bool cacheName(const String& name){if(name.length()!=68||!name.endsWith(".hpi"))return false;for(unsigned i=0;i<64;++i)if(!((name[i]>='0'&&name[i]<='9')||(name[i]>='a'&&name[i]<='f')))return false;return true;}
void trimThumbnails(){auto& storage=StorageService::instance();auto* fs=storage.sdUserDataFs();if(!fs)return;
    const String directory=storage.sdUserDataPath("/cache/images");
    // Only firmware-created SHA256 .hpi files are candidates. Never traverse.
    for(unsigned pass=0;pass<65;++pass){File dir=fs->open(directory);if(!dir)return;
        uint64_t bytes=0;unsigned count=0;uint64_t oldest=UINT64_MAX;String remove;
        for(File f=dir.openNextFile();f;f=dir.openNextFile()){String name=f.name();name=name.substring(name.lastIndexOf('/')+1);
            if(f.isDirectory()||!cacheName(name))continue;bytes+=f.size();++count;
            if(f.getLastWrite()<=oldest){oldest=f.getLastWrite();remove=directory+"/"+name;}}
        dir.close();if(bytes<=8*1024*1024&&count<=64)return;if(!remove.length()||!fs->remove(remove))return;
    }
}
void execute(const LuaJobService::Request& r,LuaJobService::Result& result){
    if(cancelled(r)){result.error="cancelled";return;}
    JsonDocument options,output;if(deserializeJson(options,r.body)){result.error="options";return;}
    auto& storage=StorageService::instance();
    if(r.method==0){auto providers=output.to<JsonArray>();auto sd=providers.add<JsonObject>();sd["id"]="sd";sd["name"]="SD-Karte";sd["online"]=storage.sdAvailable();sd["root"]="storage://sd/";
        auto usb=providers.add<JsonObject>();usb["id"]="usb";usb["name"]="USB-Speicher";usb["online"]=UsbStorageService::instance().available();usb["root"]="storage://usb/";
        if(r.permissions&perm(AppPermission::NasRead)){auto& nas=NasService::instance();
            for(size_t i=0;i<nas.profileCount();++i){NasProfile p;if(!nas.copyProfile(i,p)||!p.enabled||p.protocol!=NasProtocol::Smb2)continue;
                auto provider=providers.add<JsonObject>();const String id="nas"+String(p.id);const int status=nas.providerStatus(p.id);provider["id"]=id;provider["name"]=p.name;provider["online"]=NetworkService::instance().online()&&status==1;provider["known"]=status>=0;provider["root"]="storage://"+id+"/";}}
    }else if(r.method==8){result.ok=AppIdleActionService::instance().configure(r.owner,options["after"]|300u,options["arguments"]|"{}",options["enabled"]|true);if(!result.ok)result.error="settings_locked_or_arguments";return;
    }else if(r.method==7){result.ok=SettingsService::instance().setWallpaperControls(false,36);if(!result.ok)result.error="settings_locked";return;
    }else{
        if(!allowedPath(r.url,r.permissions)){result.error="storage_permission";return;}
        if(r.method==1){Window window;window.request=&r;window.started=millis();window.dates=options["sort"]=="date";
            window.files=options["files"]|true;window.folders=options["directories"]|true;
            for(auto ext:options["extensions"].as<JsonArray>()){if(window.extensionCount>=8)break;window.extensions[window.extensionCount++]=ext.as<String>();}
            const size_t offset=options["offset"]|0u;size_t skip=offset;window.capacity=std::min<size_t>(100,options["limit"]|100u);
            if(!window.capacity||offset>1000000){result.error="page_range";return;}
            window.entries.reserve(window.capacity+1);
            const auto directoryRevision=FileBrowserService::directoryRevision();
            const String cacheKey=r.url+"|lua|"+r.body;
            bool pageCached=FileBrowserService::cachedPage(r.url,cacheKey,window.entries,window.more);
            if(pageCached)skip=0;
            // Repeated bounded windows provide globally sorted offset paging
            // without retaining the folder. Late pages cost worker scans, not RAM.
            for(;!pageCached;){window.entries.clear();window.more=false;
                if(!FileBrowserService::visit(r.url,Window::visit,&window,r.cancelled)||window.timedOut){result.error=cancelled(r)?"cancelled":"provider_timeout";return;}
                if(skip<window.entries.size()||!skip)break;
                if(window.entries.empty()){skip=0;break;}skip-=window.entries.size();window.after=window.entries.back();window.haveAfter=true;
            }
            if(cancelled(r)||!(FileBrowserService::directoryRevision()==directoryRevision)){result.error=cancelled(r)?"cancelled":"storage_changed";return;}
            auto entries=output["entries"].to<JsonArray>();size_t emitted=0;
            for(size_t i=skip;i<window.entries.size();++i){const auto& item=window.entries[i];auto entry=entries.add<JsonObject>();
                entry["name"]=item.name;entry["path"]=r.url+(r.url.endsWith("/")?"":"/")+item.name;entry["size"]=item.size;entry["date"]=item.modified;entry["directory"]=item.directory;
                if(measureJson(output)>LuaJobService::kResponseBytes-256){entries.remove(entries.size()-1);window.more=true;break;}++emitted;}
            output["more"]=window.more;output["nextOffset"]=offset+emitted;
            if(!pageCached){std::vector<NasService::FileInfo> page;for(size_t i=skip;i<skip+emitted;++i)page.push_back(window.entries[i]);FileBrowserService::cachePage(r.url,cacheKey,page,window.more,directoryRevision);}
        }else if(r.method==2){NasService::FileInfo info;const bool exists=FileBrowserService::stat(r.url,info);output["exists"]=exists;
            if(exists){output["size"]=info.size;output["date"]=info.modified;output["directory"]=info.directory;}
        }else if(r.method==10){const uint32_t offset=options["offset"]|0u,length=options["limit"]|8192u;
            if(length>8192){result.error="range_limit";return;}
            std::unique_ptr<uint8_t,decltype(&heap_caps_free)> bytes(static_cast<uint8_t*>(heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);size_t size=0;
            if(!bytes||!FileBrowserService::readRange(r.url,offset,bytes.get(),length,size,result.total,r.cancelled)){result.error="read_range_or_provider";return;}
            result.body=String(reinterpret_cast<const char*>(bytes.get()),size);result.size=size;result.processed=uint64_t(offset)+size;
            result.ok=result.body.length()==size;if(!result.ok)result.error="response_memory";return;
        }else if(r.method==3){std::unique_ptr<uint8_t,decltype(&heap_caps_free)> bytes(static_cast<uint8_t*>(heap_caps_malloc(LuaJobService::kResponseBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);size_t size=0;
            if(!bytes||!FileBrowserService::read(r.url,bytes.get(),LuaJobService::kResponseBytes,size,r.cancelled)){result.error="read_limit_or_provider";return;}
            result.body=String(reinterpret_cast<const char*>(bytes.get()),size);result.size=size;result.ok=true;return;
        }else if(r.method==4||r.method==5||r.method==6||r.method==9){
            uint32_t width=options["maxWidth"]|1280u,height=options["maxHeight"]|800u;
            if(!width||!height||width>1280||height>1280||(r.method==5&&(width>512||height>512))){result.error="image_dimensions";return;}
            const bool cover=options["fit"]=="cover",center=options["fit"]=="center";std::shared_ptr<ImageRequest> image;String cache;
            const bool packed=options["opaque"]|false;
            if(packed&&(cover||center||r.method==6)){result.error="image_opaque_requires_contain";return;}
            if(r.method==5&&storage.sdAvailable()){NasService::FileInfo info;if(FileBrowserService::stat(r.url,info)){
                cache=thumbnailPath(r.url+(center?"|center":""),info,width,height,cover);
                if(cache.length()){image=FileBrowserService::imageNow("sd:"+storage.sdUserDataPath(cache),width,height,false,false,r.cancelled);if(image->image)storage.touchSdUserData(cache);}}}
            if(!image||!image->image){image=FileBrowserService::imageNow(r.url,width,height,r.method==6,cover,r.cancelled,center,packed);
                if(r.method==5&&image->image&&cache.length()&&!cancelled(r)){FileBrowserService::saveNativeImage(cache,*image->image);trimThumbnails();}}
            if(cancelled(r)){result.error="cancelled";return;}
            if(r.method==6){result.ok=image->savedWallpaper;if(result.ok)result.ok=SettingsService::instance().setWallpaperControls(true,options["dim"]|36u);
                if(result.ok)result.ok=SettingsService::instance().setWallpaperMode(options["fit"]|"cover");
                if(!result.ok)result.error=image->message;return;}
            if(!image->image){result.error=image->message;return;}
            const uint32_t handle=ImageResourceService::instance().store(r.owner,image->image);
            if(!handle){result.error="image_budget";return;}
            if(cancelled(r)){ImageResourceService::instance().release(r.owner,handle);result.error="cancelled";return;}
            output["handle"]=handle;output["width"]=image->image->width;output["height"]=image->image->height;
        }else{result.error="operation";return;}
    }
    serializeJson(output,result.body);result.size=result.body.length();result.ok=!cancelled(r)&&result.size<=LuaJobService::kResponseBytes;
    if(!result.ok){result.body="";result.error="cancelled_or_response_limit";}
}
int queue(lua_State* L,uint8_t operation,LuaJobService::Request* copy=nullptr){auto* app=LuaApp::self(L);if(!app)return luaL_error(L,"app state missing");
    LuaJobService::Request r;r.kind=5;r.handler=execute;r.method=operation;r.owner=app->id();r.permissions=app->permissions();
    r.required=perm(operation==8?AppPermission::IdleIntegration:operation==6||operation==7||operation==9?AppPermission::Wallpaper:AppPermission::StorageRead);
    JsonDocument options;
    if(operation==8){luaL_checktype(L,1,LUA_TTABLE);lua_getfield(L,1,"after");const lua_Integer after=luaL_optinteger(L,-1,300);lua_pop(L,1);if(after<10||after>86400)return luaL_error(L,"idle timeout must be 10..86400 seconds");options["after"]=after;
        lua_getfield(L,1,"enabled");options["enabled"]=lua_isnil(L,-1)||lua_toboolean(L,-1);lua_pop(L,1);
        lua_getfield(L,1,"arguments");if(lua_istable(L,-1)){lua_getglobal(L,"hp_json_encode");lua_pushvalue(L,-2);lua_call(L,1,1);size_t n;const char* json=lua_tolstring(L,-1,&n);if(!json||n>256)return luaL_error(L,"idle arguments limit");options["arguments"]=String(json,n);lua_pop(L,1);}lua_pop(L,1);
    }
    if(operation!=0&&operation!=7&&operation!=8){size_t size;const char* raw=luaL_checklstring(L,1,&size);
        if(size>200||strlen(raw)!=size)return luaL_error(L,"invalid virtual path");r.url=FileBrowserService::normalizeUri(String(raw,size));
        if(!allowedPath(r.url,r.permissions))return luaL_error(L,"storage permission denied");
        if(r.url.startsWith("nas"))r.required|=perm(AppPermission::NasRead);
    }
    if(lua_istable(L,2)){
        for(const char* name:{"maxWidth","maxHeight","offset","limit"}){lua_getfield(L,2,name);if(!lua_isnil(L,-1)){
            if(!lua_isinteger(L,-1)||lua_tointeger(L,-1)<0||lua_tointeger(L,-1)>(operation==10?16777216:1000000))return luaL_error(L,"invalid numeric option");options[name]=lua_tointeger(L,-1);}lua_pop(L,1);}
        for(const char* name:{"files","directories","opaque"}){lua_getfield(L,2,name);if(!lua_isnil(L,-1)){if(!lua_isboolean(L,-1))return luaL_error(L,"invalid boolean option");options[name]=bool(lua_toboolean(L,-1));}lua_pop(L,1);}
        for(const char* name:{"fit","mode","sort"}){lua_getfield(L,2,name);if(!lua_isnil(L,-1)){size_t n;const char* value=luaL_checklstring(L,-1,&n);if(n>12||strlen(value)!=n)return luaL_error(L,"invalid option");options[name]=value;}lua_pop(L,1);}
        lua_getfield(L,2,"dim");if(!lua_isnil(L,-1)){const double dim=luaL_checknumber(L,-1);if(!std::isfinite(dim)||dim<0||dim>1)return luaL_error(L,"dim must be 0..1");options["dim"]=static_cast<unsigned>(dim*100);}lua_pop(L,1);
        lua_getfield(L,2,"extensions");if(!lua_isnil(L,-1)){luaL_checktype(L,-1,LUA_TTABLE);if(lua_rawlen(L,-1)>8)return luaL_error(L,"too many extensions");
            auto extensions=options["extensions"].to<JsonArray>();for(unsigned i=1;i<=lua_rawlen(L,-1);++i){lua_rawgeti(L,-1,i);size_t n;const char* ext=luaL_checklstring(L,-1,&n);
                if(!n||n>7)return luaL_error(L,"invalid extension");for(size_t j=0;j<n;++j)if(!isalnum(static_cast<unsigned char>(ext[j])))return luaL_error(L,"invalid extension");String normalized(ext,n);normalized.toLowerCase();extensions.add(normalized);lua_pop(L,1);}}lua_pop(L,1);
    }
    if(operation==6||operation==9){options["maxWidth"]=1280;options["maxHeight"]=800;options["fit"]=options["mode"]|"cover";}
    if(operation==5){if(!options["maxWidth"].is<unsigned>())options["maxWidth"]=220;if(!options["maxHeight"].is<unsigned>())options["maxHeight"]=150;}
    for(const char* key:{"fit","mode"})if(options[key].is<const char*>()){
        const String value=options[key].as<String>();if(value!="cover"&&value!="contain"&&value!="center")return luaL_error(L,"invalid image fit");}
    if(options["sort"].is<const char*>()){const String value=options["sort"].as<String>();if(value!="name"&&value!="date")return luaL_error(L,"invalid sort");}
    // Directory scans, range reads and image decode share the existing transfer
    // worker with native Files. Never block short network/status jobs with a
    // Lua storage scan. Local SD/USB work must not wake the C6 radio.
    if((operation>=1&&operation<=6)||operation==9||operation==10)
        r.lane=r.url.startsWith("nas")?LuaJobService::Request::Lane::Transfer:LuaJobService::Request::Lane::LocalTransfer;
    serializeJson(options,r.body);r.completion=operation==4||operation==5||operation==9?"image.ready":operation==6||operation==7?"wallpaper.changed":"storage.completed";
    if(copy)*copy=r;
    const uint32_t id=LuaJobService::instance().submit(r);if(id)lua_pushinteger(L,id);else lua_pushnil(L);return 1;
}
int providers(lua_State* L){return queue(L,0);}int list(lua_State* L){return queue(L,1);}int stat(lua_State* L){return queue(L,2);}int read(lua_State* L){return queue(L,3);}
int readRange(lua_State* L){return queue(L,10);}
int load(lua_State* L){return queue(L,4);}int thumbnail(lua_State* L){
    if(lua_isnumber(L,2)){const auto width=luaL_checkinteger(L,2),height=luaL_checkinteger(L,3);
        if(width<1||height<1||width>512||height>512)return luaL_error(L,"thumbnail dimensions must be 1..512");
        lua_newtable(L);lua_pushinteger(L,width);lua_setfield(L,-2,"maxWidth");lua_pushinteger(L,height);lua_setfield(L,-2,"maxHeight");lua_replace(L,2);lua_settop(L,2);}
    return queue(L,5);}int wallpaper(lua_State* L){return queue(L,6);}int reset(lua_State* L){return queue(L,7);}
int idleAction(lua_State* L){return queue(L,8);}
int release(lua_State* L){auto* app=LuaApp::self(L);lua_pushboolean(L,app&&ImageResourceService::instance().release(app->id(),luaL_checkinteger(L,1)));return 1;}
int cancel(lua_State* L){auto* app=LuaApp::self(L);lua_pushboolean(L,app&&LuaJobService::instance().cancel(app->id(),luaL_checkinteger(L,1)));return 1;}
int wallpaperGet(lua_State* L){const auto settings=SettingsService::instance().snapshot();lua_newtable(L);
    lua_pushstring(L,settings.ui.wallpaperMode.c_str());lua_setfield(L,-2,"mode");
    lua_pushboolean(L,settings.ui.wallpaperEnabled);lua_setfield(L,-2,"enabled");lua_pushstring(L,settings.ui.wallpaperPath.c_str());lua_setfield(L,-2,"path");lua_pushnumber(L,settings.ui.wallpaperDim/100.0);lua_setfield(L,-2,"dim");return 1;}
struct ImageHolder {std::shared_ptr<ImageResourceService::Resource> resource;LuaApp* owner;};
void imageDeleted(lv_event_t* e){auto* h=static_cast<ImageHolder*>(lv_event_get_user_data(e));
    h->owner->releaseUiObjects(1);
    lv_image_cache_drop(&h->resource->descriptor);lv_image_header_cache_drop(&h->resource->descriptor);delete h;}
bool owned(LuaApp* app,lv_obj_t* object){if(!app||!object||!lv_obj_is_valid(object))return false;
    for(auto* p=object;p;p=lv_obj_get_parent(p))if(p==app->screenRoot())return true;return false;}
int sourceImage(lua_State* L){auto* app=LuaApp::self(L);auto* parent=static_cast<lv_obj_t*>(lua_touserdata(L,1));
    if(!app||app->lifecycle()!=LuaRuntimeState::Foreground||!owned(app,parent))return luaL_error(L,"foreground app-owned parent required");
    auto resource=ImageResourceService::instance().acquire(app->id(),luaL_checkinteger(L,2));if(!resource){lua_pushnil(L);return 1;}
    if(!app->reserveUiObjects(1)){lua_pushnil(L);return 1;}auto* holder=new(std::nothrow) ImageHolder{std::move(resource),app};if(!holder){app->releaseUiObjects(1);lua_pushnil(L);return 1;}
    auto* image=lv_image_create(parent);lv_image_set_src(image,&holder->resource->descriptor);lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_event_cb(image,imageDeleted,LV_EVENT_DELETE,holder);
    lua_pushlightuserdata(L,image);return 1;
}
int setSource(lua_State* L){auto* app=LuaApp::self(L);auto* object=static_cast<lv_obj_t*>(lua_touserdata(L,1));
    if(!app||app->lifecycle()!=LuaRuntimeState::Foreground||!owned(app,object))return luaL_error(L,"foreground app-owned image required");
    ImageHolder* holder=nullptr;
    for(uint32_t i=0;i<lv_obj_get_event_count(object);++i){auto* descriptor=lv_obj_get_event_dsc(object,i);
        if(lv_event_dsc_get_cb(descriptor)==imageDeleted){holder=static_cast<ImageHolder*>(lv_event_dsc_get_user_data(descriptor));break;}}
    if(!holder)return luaL_error(L,"image.create object required");
    auto resource=ImageResourceService::instance().acquire(app->id(),luaL_checkinteger(L,2));if(!resource){lua_pushboolean(L,false);return 1;}
    lv_image_cache_drop(&holder->resource->descriptor);lv_image_header_cache_drop(&holder->resource->descriptor);
    holder->resource=std::move(resource);lv_image_set_src(object,&holder->resource->descriptor);lua_pushboolean(L,true);return 1;
}
struct Preview {LuaJobService::Request request;uint32_t job=0;bool applying=false;
    lv_obj_t* root=nullptr;lv_obj_t* image=nullptr;lv_obj_t* status=nullptr;lv_obj_t* apply=nullptr;
    std::shared_ptr<ImageResourceService::Resource> resource;};
Preview* preview=nullptr; // Accessed exclusively by the existing UI task.
void previewDeleted(lv_event_t* event){auto* p=static_cast<Preview*>(lv_event_get_user_data(event));
    if(p->job)LuaJobService::instance().cancel(p->request.owner.c_str(),p->job);
    if(p->resource){lv_image_set_src(p->image,nullptr);lv_image_cache_drop(&p->resource->descriptor);lv_image_header_cache_drop(&p->resource->descriptor);}
    if(preview==p)preview=nullptr;delete p;
}
int wallpaperPreview(lua_State* L){auto* app=LuaApp::self(L);
    if(!app||app->lifecycle()!=LuaRuntimeState::Foreground||!app->screenRoot())return luaL_error(L,"foreground required");
    if(!app->allows(AppPermission::Wallpaper))return luaL_error(L,"wallpaper permission required");
    if(preview)lv_obj_delete(preview->root);
    if(!app->reserveUiObjects(8))return luaL_error(L,"UI object budget exceeded");
    LuaJobService::Request request;queue(L,9,&request);const uint32_t job=lua_tointeger(L,-1);if(!job)return 1;
    auto* p=new(std::nothrow) Preview;if(!p){LuaJobService::instance().cancel(app->id(),job);lua_pop(L,1);lua_pushnil(L);return 1;}
    p->request=request;p->job=job;preview=p;p->root=lv_obj_create(app->screenRoot());
    lv_obj_set_size(p->root,LV_PCT(100),LV_PCT(100));lv_obj_set_pos(p->root,0,0);UiTheme::instance().styleScreen(p->root);
    lv_obj_set_style_bg_opa(p->root,LV_OPA_COVER,0);lv_obj_remove_flag(p->root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(p->root,previewDeleted,LV_EVENT_DELETE,p);
    p->image=lv_image_create(p->root);lv_obj_set_size(p->image,LV_PCT(100),LV_PCT(85));lv_image_set_inner_align(p->image,LV_IMAGE_ALIGN_CONTAIN);
    JsonDocument options;deserializeJson(options,request.body);auto* scrim=lv_obj_create(p->root);lv_obj_set_size(scrim,LV_PCT(100),LV_PCT(85));
    lv_obj_set_style_bg_color(scrim,lv_color_black(),0);lv_obj_set_style_bg_opa(scrim,static_cast<lv_opa_t>(std::min<unsigned>(100,options["dim"]|36u)*255/100),0);
    lv_obj_set_style_border_width(scrim,0,0);lv_obj_remove_flag(scrim,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_flag(scrim,LV_OBJ_FLAG_SCROLLABLE);
    p->status=lv_label_create(p->root);lv_label_set_text(p->status,"Hintergrundvorschau wird geladen ...");lv_obj_align(p->status,LV_ALIGN_BOTTOM_MID,0,-70);
    auto* cancelButton=lv_button_create(p->root);lv_obj_set_size(cancelButton,220,52);lv_obj_align(cancelButton,LV_ALIGN_BOTTOM_LEFT,16,-8);
    auto* text=lv_label_create(cancelButton);lv_label_set_text(text,"Abbrechen");lv_obj_center(text);
    lv_obj_add_event_cb(cancelButton,[](lv_event_t* e){lv_obj_delete(static_cast<Preview*>(lv_event_get_user_data(e))->root);},LV_EVENT_CLICKED,p);
    p->apply=lv_button_create(p->root);lv_obj_set_size(p->apply,220,52);lv_obj_align(p->apply,LV_ALIGN_BOTTOM_RIGHT,-16,-8);lv_obj_add_state(p->apply,LV_STATE_DISABLED);
    text=lv_label_create(p->apply);lv_label_set_text(text,"Setzen");lv_obj_center(text);
    lv_obj_add_event_cb(p->apply,[](lv_event_t* e){auto& p=*static_cast<Preview*>(lv_event_get_user_data(e));
        if(!SecurityService::instance().settingsAllowed()){SecurityUi::requestSettingsUnlock();lv_label_set_text(p.status,"Einstellungen entsperren; danach Setzen erneut antippen.");return;}
        p.request.method=6;p.request.completion="wallpaper.changed";const uint32_t job=LuaJobService::instance().submit(p.request);
        if(job){p.job=job;p.applying=true;lv_obj_add_state(p.apply,LV_STATE_DISABLED);lv_label_set_text(p.status,"Lokale Hintergrundkopie wird gespeichert ...");}
        else lv_label_set_text(p.status,"Dienst beschaeftigt. Erneut versuchen.");},LV_EVENT_CLICKED,p);
    return 1;
}
void events(const Event& event){const char* name=nullptr;
    if(event.type==EventType::StorageProviderChanged&&event.payload){const auto& status=*static_cast<const StorageProviderEvent*>(event.payload);
        char qualified[48];snprintf(qualified,sizeof(qualified),"storage.provider%s.nas%lu",status.online?"Online":"Offline",static_cast<unsigned long>(status.id));publishLuaEvent(nullptr,qualified);return;}
    switch(event.type){case EventType::SdInserted:name="storage.mounted";break;case EventType::SdRemoved:name="storage.unmounted";break;
        case EventType::NetworkUp:name="storage.providerOnline";break;case EventType::NetworkDown:name="storage.providerOffline";break;
        case EventType::ThemeChanged:name="theme.changed";break;
        case EventType::IdleStateChanged:if(event.payload)name=*static_cast<const IdleStage*>(event.payload)==IdleStage::Active?"power.wake":"power.sleep";break;default:break;}
    if(name)publishLuaEvent(nullptr,name);
}
} // namespace
void pollLuaWallpaperPreview(){if(!preview)return;auto* p=preview;auto* current=AppManager::instance().current();
    if(!current||p->request.owner!=current->id()||SecurityService::instance().tabletLocked()){lv_obj_delete(p->root);return;}
    if(!p->job)return;LuaJobService::Result result;if(!LuaJobService::instance().result(p->request.owner.c_str(),p->job,result)||!result.done)return;
    const uint32_t completed=p->job;p->job=0;if(!result.ok){lv_label_set_text(p->status,result.error.c_str());p->applying=false;if(p->resource)lv_obj_remove_state(p->apply,LV_STATE_DISABLED);return;}
    if(p->applying){lv_obj_delete(p->root);return;}
    JsonDocument output;if(deserializeJson(output,result.body)){lv_label_set_text(p->status,"Ungueltiges Bildergebnis.");return;}
    const uint32_t handle=output["handle"]|0u;p->resource=ImageResourceService::instance().acquire(p->request.owner,handle);
    if(!p->resource){p->job=completed;return;}
    ImageResourceService::instance().release(p->request.owner,handle);
    lv_image_set_src(p->image,&p->resource->descriptor);lv_obj_remove_state(p->apply,LV_STATE_DISABLED);
    lv_label_set_text(p->status,"Vorschau - das bisherige Hintergrundbild bleibt bis Setzen unveraendert.");
}
void registerLuaMediaStorageBridge(lua_State* L){static bool subscribed=false;if(!subscribed)subscribed=EventBus::instance().subscribe(events);
    lua_register(L,"hp_storage_read_range",readRange);
    ImageResourceService::instance().begin();
    LuaJobService::instance().registerOwnerCleanup([](const char* owner){ImageResourceService::instance().releaseOwner(owner);});
    lua_register(L,"hp_storage_providers",providers);lua_register(L,"hp_storage_list",list);lua_register(L,"hp_storage_stat",stat);lua_register(L,"hp_storage_read",read);
    lua_register(L,"hp_image_load_async",load);lua_register(L,"hp_image_thumbnail_async",thumbnail);lua_register(L,"hp_image_release",release);lua_register(L,"hp_image_cancel",cancel);
    lua_register(L,"hp_ui_image_resource",sourceImage);lua_register(L,"hp_wallpaper_set",wallpaper);lua_register(L,"hp_wallpaper_get",wallpaperGet);lua_register(L,"hp_wallpaper_reset",reset);
    lua_register(L,"hp_ui_image_set_source",setSource);
    lua_register(L,"hp_wallpaper_preview",wallpaperPreview);
    lua_register(L,"hp_idle_register_action",idleAction);
}
void installLuaMediaStorageTables(lua_State* L){
    auto field=[&](const char* key,const char* global){lua_getglobal(L,global);lua_setfield(L,-2,key);};
    lua_newtable(L);field("providers","hp_storage_providers");field("list","hp_storage_list");field("stat","hp_storage_stat");field("exists","hp_storage_stat");field("read","hp_storage_read");field("open","hp_storage_read");field("readRange","hp_storage_read_range");lua_setglobal(L,"storage");
    lua_newtable(L);field("loadAsync","hp_image_load_async");field("thumbnailAsync","hp_image_thumbnail_async");field("release","hp_image_release");field("cancel","hp_image_cancel");field("create","hp_ui_image_resource");field("setSource","hp_ui_image_set_source");lua_setglobal(L,"image");
    lua_newtable(L);lua_newtable(L);field("set","hp_wallpaper_set");field("get","hp_wallpaper_get");field("reset","hp_wallpaper_reset");field("preview","hp_wallpaper_preview");lua_setfield(L,-2,"wallpaper");lua_setglobal(L,"system");
    lua_newtable(L);field("arguments","hp_app_arguments");field("launch","hp_app_launch");lua_setglobal(L,"app");
    lua_newtable(L);field("registerIdleAction","hp_idle_register_action");lua_setglobal(L,"power");
}
} // namespace hp

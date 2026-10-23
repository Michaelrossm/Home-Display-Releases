#include "LuaScriptPreparation.h"
#include "LuaAppRegistry.h"
#include "../../services/NetworkWorker.h"
#include "../../services/StorageService.h"
#include "../../services/UsbAppFileSystem.h"
#include <new>

namespace hp {
bool LuaScriptPreparation::enqueue(const std::shared_ptr<LuaScriptPreparation>& request){
    auto* holder=new(std::nothrow) std::shared_ptr<LuaScriptPreparation>(request);
    if(!holder)return false;
    if(!NetworkWorker::instance().submitPriority(run,holder,"lua-prepare")){delete holder;return false;}
    return true;
}
void LuaScriptPreparation::run(void* context){
    std::unique_ptr<std::shared_ptr<LuaScriptPreparation>> holder(
        static_cast<std::shared_ptr<LuaScriptPreparation>*>(context));
    auto& p=**holder;
    if(p.cancelled.load(std::memory_order_acquire))return;
    auto& registry=LuaAppRegistry::instance();
    if(!registry.beginPackageMutation(1500)){p.error="lua_package_busy";p.ready.store(true,std::memory_order_release);return;}
    auto& storage=StorageService::instance();
    const auto read=[&](const String& folder,bool appFs,bool usb){
        const String path=folder+(p.settings?"/settings.lua":"/main.lua");File f;
        if(usb){if(auto fs=usbAppFileSystemFor(p.usbGeneration))f=fs->open(path,FILE_READ);}
        else if(appFs){if(auto* fs=storage.fsFor(StorageClass::AppPackage))f=fs->open(storage.pathFor(StorageClass::AppPackage,path),FILE_READ);}
        else f=storage.openSdRead(path);
        if(!f||f.isDirectory()){if(f)f.close();return false;}
        const bool ok=readLuaScript(f,p.script,p.error,&p.cancelled);f.close();return ok;
    };
    if(p.folder.length())read(p.folder,p.appFs,p.usb);
    else{
        // Built-in/default scripts retain the existing canonical root priority.
        for(const auto* root:{"/","/apps/","/Apps/32/"})
            if(!p.cancelled.load()&&read(String(root)+p.id,true,false))break;
        if(!p.script.length())for(const auto* root:{"/Apps/32/","/apps/32/","/apps/"})
            if(!p.cancelled.load()&&read(String(root)+p.id,false,false))break;
        if(!p.script.length()&&!p.settings&&p.fallback.length()){
            if(!p.script.reserve(p.fallback.length())||!p.script.concat(p.fallback.c_str(),p.fallback.length()))p.error="lua_memory:script_buffer";
            else p.error=nullptr;
        }
    }
    registry.endPackageMutation();
    if(!p.script.length()&&!p.error)p.error="lua_script_missing";
    p.ready.store(true,std::memory_order_release);
}
}

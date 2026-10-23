#pragma once
#include "PhotoLibraryService.h"
#include "ConfigService.h"
#include "SecurityService.h"
#include "NetworkWorker.h"
#include "StorageService.h"
#include "UsbStorageService.h"
#include "PhotoImageCache.h"
#include "../apps/lua/LuaJobService.h"
#include <ArduinoJson.h>
namespace hp {
inline void executePhotoWebJob(const LuaJobService::Request& request,LuaJobService::Result& result){
    if(!NetworkWorker::instance().isWorkerContext()){result.error="worker_required";return;}
    SecurityWriteGuard guard;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){result.error="settings_locked";return;}
    if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return;}
    JsonDocument args,document,out;if(deserializeJson(args,request.body,DeserializationOption::NestingLimit(5))||!args.is<JsonObject>()){result.error="photos_json";return;}
    PhotoSettings settings;String error;if(!PhotoLibraryService::readStored(settings,error)){result.error=error;return;}
    if(deserializeJson(document,PhotoLibraryService::settingsJson(settings))){result.error="photos_memory";return;}
    if(request.context=="save"){
        if(!args["revision"].is<uint32_t>()||args["revision"].as<uint32_t>()!=ConfigService::instance().revision()){result.error="revision_conflict";return;}
        if(!args["patch"].is<JsonObject>()){result.error="photos_patch";return;}
        for(JsonPairConst pair:args["patch"].as<JsonObjectConst>()){
            bool allowed=false;for(const char* key:{"start","end","interval","days","schedule","recursive","automaticFolders","takeover","returnAfter","sortDate","descending","renderEdge","folders","favorites"})if(!strcmp(key,pair.key().c_str()))allowed=true;
            if(!allowed){result.error="photos_unknown_setting";return;}document[pair.key().c_str()].set(pair.value());
        }
        String raw;serializeJson(document,raw);
        if(!PhotoLibraryService::settingsFromJson(raw,settings,error)||!PhotoLibraryService::writeStored(settings,error)){result.error=error;return;}out["saved"]=true;
    }else if(request.context=="pause"||request.context=="resume"||request.context=="start"){
        PhotoLibraryService::instance().requestControl(request.context=="pause"?1:request.context=="resume"?2:3);out["queued"]=true;
    }else if(request.context!="get"){result.error="photos_operation";return;}
    out["settings"].set(document.as<JsonVariantConst>());out["revision"]=ConfigService::instance().revision();
    out["storage"]["sd"]=StorageService::instance().sdAvailable();out["storage"]["usb"]=UsbStorageService::instance().available();
    if(StorageService::instance().sdAvailable())out["storageFree"]["sd"]=StorageService::instance().sdFreeBytes();
    uint64_t total=0,used=0;if(UsbStorageService::instance().usage(total,used))out["storageFree"]["usb"]=total>used?total-used:0;
    JsonDocument metrics;if(!deserializeJson(metrics,PhotoImageCache::metricsJson()))out["imageMetrics"].set(metrics.as<JsonVariantConst>());
    if(out.overflowed()||measureJson(out)>LuaJobService::kResponseBytes){result.error="photos_response_limit";return;}
    serializeJson(out,result.body);result.ok=true;
}
}

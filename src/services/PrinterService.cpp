#include "PrinterService.h"
#include "NetworkWorker.h"
#include "NetworkService.h"
#include "SecurityService.h"
#include "../apps/lua/LuaStorageBridge.h"
#include "../apps/lua/LuaEvents.h"
#include "../core/AppBackgroundScheduler.h"
#include "printer/PrinterJson.h"
#include "printer/PrinterFileJobs.h"
#include <algorithm>
namespace hp {namespace {
class WorkerPrinterTransport final:public PrinterTransport {
    const PrinterProfile& profile_;AppPermissions permissions_;
    const std::atomic<bool>& active_;const std::atomic<bool>* cancelled_;
    uint32_t start_=millis(),deadline_;
    const LuaJobService::Request* parent_=nullptr;
    const String* source_=nullptr;const String* prefix_=nullptr;const String* suffix_=nullptr;
    bool cooperative_=false;mutable bool yielded_=false;
public:
    uint32_t requests=0;
    void allowPriorityYield(){cooperative_=true;}
    bool yielded()const{return yielded_;}
    WorkerPrinterTransport(const PrinterProfile& p,AppPermissions permissions,const std::atomic<bool>& active,
        const std::atomic<bool>* cancelled=nullptr,uint32_t deadline=4000,const LuaJobService::Request* parent=nullptr):
        profile_(p),permissions_(permissions),active_(active),cancelled_(cancelled),deadline_(deadline),parent_(parent){}
    bool stopped()const override{
        // Read maintenance may yield BETWEEN synchronous requests. Commands and
        // uploads never enable this path and are never automatically replayed.
        if(cooperative_&&NetworkWorker::instance().priorityDepth()){yielded_=true;return true;}
        return !active_.load()||(cancelled_&&cancelled_->load())||
        uint32_t(millis()-start_)>=deadline_||!NetworkService::instance().online()||SecurityService::instance().tabletLocked();}
    String password()override{
        if(!profile_.passwordSecret.length())return "reprap";
        if(!(permissions_&perm(AppPermission::Secrets)))return "";
        return luaSecretForNative(profile_.owner,profile_.passwordSecret);
    }
    LuaJobService::Result request(const String& path,const String& session,uint8_t method,const String& body,size_t limit)override{
        LuaJobService::Result r;if(stopped()){r.error="cancelled_or_offline";return r;}
        LuaJobService::Request q;q.owner=profile_.owner;
        // Even an app with Internet permission cannot direct this LAN service off-LAN.
        q.permissions=permissions_&perm(AppPermission::Network);q.method=method;q.body=body;q.contentType=(profile_.protocol=="moonraker"||profile_.protocol=="klipper"||profile_.protocol=="octoprint"||profile_.protocol=="marlin_octoprint")?"application/json":"text/plain";
        q.url="http://"+profile_.host+":"+String(profile_.port)+path;q.responseLimit=limit;q.cancelled=cancelled_;
        q.timeout=std::min<uint32_t>(1500,deadline_-uint32_t(millis()-start_));
        if(q.timeout<100){r.error="timeout";return r;}
        if(session.length()){q.headerCount=1;q.headers[0]={"X-Session-Key",session};}
        if((profile_.protocol=="moonraker"||profile_.protocol=="klipper"||profile_.protocol=="octoprint"||profile_.protocol=="marlin_octoprint")&&profile_.passwordSecret.length()){
            if(!(permissions_&perm(AppPermission::Secrets))){r.error="printer_secret_permission";return r;}
            const String key=luaSecretForNative(profile_.owner,profile_.passwordSecret);
            if(!key.length()){r.error="printer_secret_missing";return r;}
            if(key.length()>256||key.length()!=strlen(key.c_str())||key.indexOf('\r')>=0||key.indexOf('\n')>=0){r.error="printer_secret_invalid";return r;}
            q.headerCount=1;q.headers[0]={"X-Api-Key",key};
        }
        if((profile_.protocol=="octoprint"||profile_.protocol=="marlin_octoprint")&&q.headerCount==0){r.error="printer_secret_missing";return r;}
        ++requests;
        if(source_){q.permissions|=permissions_&perm(AppPermission::StorageRead);q.contentType="application/octet-stream";
            if(prefix_->length()){const int end=prefix_->indexOf("\r\n");if(end<3||end>80||!prefix_->startsWith("--")){r.error="printer_upload_boundary_invalid";return r;}
                q.contentType="multipart/form-data; boundary="+prefix_->substring(2,end);}
            if(parent_){q.progress=parent_->progress;q.progressContext=parent_->progressContext;}
            printerUploadHttp(q,*source_,*prefix_,*suffix_,r);
        }else LuaJobService::executeHttp(q,r);return r;
    }
    LuaJobService::Result upload(const String& path,const String& session,const String& source,const String& prefix,const String& suffix,uint8_t method)override{
        source_=&source;prefix_=&prefix;suffix_=&suffix;auto r=request(path,session,method,"",2048);source_=prefix_=suffix_=nullptr;return r;
    }
};
bool supported(const PrinterProfile& p){return p.protocol=="reprap"||p.protocol=="rrf"||p.protocol=="moonraker"||p.protocol=="klipper"||p.protocol=="octoprint"||p.protocol=="marlin_octoprint";}
String identity(const PrinterProfile& p){return p.owner+":"+p.host+":"+String(p.port)+":"+p.protocol+":"+p.mode+":"+p.passwordSecret+":"+(p.enabled?"1":"0")+":"+p.calibration+":"+p.alignment;}
void appendConsole(String& cache,const String& message){
    if(!message.length())return;JsonDocument doc;if(cache.length())deserializeJson(doc,cache);
    auto lines=doc["lines"].is<JsonArray>()?doc["lines"].as<JsonArray>():doc["lines"].to<JsonArray>();
    while(lines.size()>=12)lines.remove(0);lines.add(message.substring(0,512));cache="";serializeJson(doc,cache);
}
String initial(const String& error){JsonDocument d;d["online"]=false;d["pending"]=error.length()==0;
    d["schema_version"]=kPrinterSchemaVersion;d["availability"]["temperatures"]=error.length()?"unavailable":"not_loaded";
    for(const char* key:{"temperatures","heaters","fans","axes","tools","extruders"})d[key].to<JsonArray>();
    d["status"]=error.length()?PrinterService::diagnosis(error):"Verbinden";d["error"]=error;d["revision"]=0;
    String out;serializeJson(d,out);return out;}
}
PrinterService& PrinterService::instance(){static PrinterService s;return s;}
bool PrinterService::begin(){if(lock_)return true;lock_=xSemaphoreCreateMutex();if(!lock_)return false;
    return NetworkWorker::instance().addMaintenance(maintenance,this);}
PrinterService::Owner* PrinterService::findOwner(const char* id){if(!id)return nullptr;for(auto& o:owners_)if(o.active&&strcmp(o.id,id)==0)return &o;return nullptr;}
PrinterService::Entry* PrinterService::findEntry(Owner* o,const char* id){if(!o||!id)return nullptr;for(size_t i=0;i<o->count;++i)if(o->entries[i].profile.id==id)return &o->entries[i];return nullptr;}
bool PrinterService::arm(const char* id,AppPermissions permissions){
    if(!printerIdentity(id,31)||!(permissions&perm(AppPermission::Network))||!(permissions&perm(AppPermission::UserStorage))||
        !lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return false;
    auto* owner=findOwner(id);
    // Reopening the SAME owner within two minutes keeps its printer-specific RAM
    // cache and sessions. A different owner may never inherit that cache.
    if(!owner)for(auto& o:owners_)if(!o.active&&!o.busy&&o.count&&
        !strcmp(o.id,id)&&uint32_t(millis()-o.closedAt)<120000UL){
        owner=&o;o.active=true;o.closedAt=0;o.sync=true;++o.generation;break;
    }
    if(!owner)for(auto& o:owners_)if(!o.active&&!o.busy&&!o.count){
        owner=&o;strlcpy(o.id,id,sizeof(o.id));++o.generation;
        o.count=0;o.profiles="";o.error="";o.sync=true;o.syncedAt=0;o.closedAt=0;o.active=true;break;
    }
    if(!owner)for(auto& o:owners_)if(!o.active&&!o.busy){
        owner=&o;strlcpy(o.id,id,sizeof(o.id));++o.generation;
        o.count=0;o.profiles="";o.error="";o.sync=true;o.syncedAt=0;o.closedAt=0;o.active=true;break;
    }
    if(owner)owner->permissions=permissions;xSemaphoreGive(lock_);return owner;
}
String PrinterService::profiles(const char* owner,String* error,bool refresh){
    if(error)*error="";
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE){if(error)*error="printer_busy";return "";}
    auto* o=findOwner(owner);String out=o?o->profiles:String();
    // Keep the last valid profiles visible during explicit refresh.
    if(error&&!out.length())*error=o&&o->error.length()?o->error:String("printer_profiles_loading");
    if(o&&(refresh||!o->profiles.length()))o->sync=true;xSemaphoreGive(lock_);return out;}
String PrinterService::profileState(const char* owner){
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return "{\"refreshing\":true,\"error\":\"printer_busy\"}";
    auto* o=findOwner(owner);JsonDocument result;
    result["cached"]=o&&o->count!=0;
    result["refreshing"]=o&&o->sync;
    result["updated_at_ms"]=o?o->syncedAt:0;
    result["error"]=o?o->error:String("printer_profiles_loading");
    String json;serializeJson(result,json);xSemaphoreGive(lock_);return json;
}
String PrinterService::snapshot(const char* owner,const char* id,bool foreground){
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return initial("printer_busy");
    auto* o=findOwner(owner);auto* e=findEntry(o,id);String out;
    if(e){if(foreground)e->lastSeen=millis();out=e->snapshot.length()?e->snapshot:initial("");}
    else{if(o)o->sync=true;out=initial(o&&o->error.length()?o->error:o&&o->syncedAt?String("printer_profile_unavailable"):String());}
    xSemaphoreGive(lock_);return out;
}
bool PrinterService::watch(const char* owner,const char* id,bool enabled){
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return false;
    auto* e=findEntry(findOwner(owner),id);if(e)e->observed=enabled;
    xSemaphoreGive(lock_);return e!=nullptr;
}
bool PrinterService::snapshotRevision(const char* owner,const char* id,uint32_t& revision,uint32_t& timestamp,bool& pending,bool& online){
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return false;
    auto* e=findEntry(findOwner(owner),id);const bool found=e!=nullptr;
    if(e){revision=e->revision;timestamp=e->lastUpdated;pending=e->pendingUntil!=0;
        online=e->status&&e->status->valid&&!e->connectionError.length()&&strcmp(e->status->state,"error")&&strcmp(e->status->state,"connecting");}
    xSemaphoreGive(lock_);return found;
}
String PrinterService::cachedSnapshot(const PrinterProfile& expected){
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return "";
    const auto* e=findEntry(findOwner(expected.owner.c_str()),expected.id.c_str());
    String out=e&&identity(e->profile)==identity(expected)?e->snapshot:String();
    xSemaphoreGive(lock_);return out;
}
String PrinterService::files(const char* owner,const char* id,const String& directory,bool macros,bool refresh,uint16_t offset){
    String dir=directory;if(dir=="/"||!dir.length())dir=macros?"0:/macros":"0:/gcodes";
    else if(dir.startsWith("/"))dir="0:"+dir;
    const String root=macros?"0:/macros":"0:/gcodes";
    if(dir!=root&&!printerPath(dir.c_str(),(root+"/").c_str()))return "{\"entries\":[],\"error\":\"printer_path_invalid\"}";
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return "{\"entries\":[],\"pending\":true}";
    auto* o=findOwner(owner);auto* e=findEntry(o,id);String out;
    if(e&&!e->profile.enabled){xSemaphoreGive(lock_);return "{\"entries\":[],\"pending\":false,\"error\":\"printer_profile_disabled\"}";}
    if(e){if(macros){if(refresh||e->macrosDirectory!=dir||e->macrosOffset!=offset){e->macros="";e->macrosDirectory=dir;e->macrosOffset=offset;}out=e->macros;if(!out.length())e->wantMacros=true;}
        else{if(refresh||e->filesDirectory!=dir||e->filesOffset!=offset){e->files="";e->filesDirectory=dir;e->filesOffset=offset;}out=e->files;if(!out.length())e->wantFiles=true;}}
    const bool available=o&&o->active&&e;
    xSemaphoreGive(lock_);
    if(!available)return "{\"entries\":[],\"pending\":false,\"error\":\"printer_profile_unavailable\"}";
    if(!out.length()&&!NetworkService::instance().online())return "{\"entries\":[],\"pending\":false,\"error\":\"network_offline\"}";
    return out.length()?out:String("{\"entries\":[],\"pending\":true}");
}
String PrinterService::console(const char* owner,const char* id){if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return "{\"lines\":[]}";
    auto* e=findEntry(findOwner(owner),id);if(e)e->consoleSeen=millis();String out=e?e->console:String();xSemaphoreGive(lock_);
    return out.length()?out:String("{\"lines\":[]}");
}
String PrinterService::operation(const char* owner,const char* id){if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return "{\"state\":\"busy\"}";
    JsonDocument d;auto* e=findEntry(findOwner(owner),id);if(e)printerWriteOperation(d.to<JsonObject>(),e->operation);xSemaphoreGive(lock_);String result;serializeJson(d,result);return result;}
String PrinterService::history(const char* owner,const char* id,const char* sensor,size_t offset,size_t count){
    if(!sensor||strlen(sensor)>63||offset>=60||count<1||count>16)return "{}";
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return "{\"busy\":true}";
    auto* e=findEntry(findOwner(owner),id);JsonDocument d;d["sensor"]=sensor;d["clock"]="monotonic_ms";auto out=d["points"].to<JsonArray>();
    if(e&&e->history){d["revision"]=e->history->revision;for(const auto& c:e->history->series)if(!strcmp(c.id,sensor)){
        d["total"]=c.count;for(size_t i=offset;i<c.count&&i<offset+count;++i){const auto& p=c.data[(c.next+60-c.count+i)%60];auto v=out.add<JsonObject>();v["ms"]=p.ms;printerValue(v["current"],p.current);printerValue(v["target"],p.target);}}}
    xSemaphoreGive(lock_);String result;serializeJson(d,result);return result;
}
void PrinterService::cleanupOwner(const char* id,bool suspending){
    // Identity is only replaced on the UI, while !busy. Cancellation never waits for HTTP.
    for(auto& o:owners_)if(!strcmp(o.id,id)){
        if(!suspending){o.closedAt=millis();o.active=false;}
        if(xSemaphoreTake(lock_,0)==pdTRUE){for(auto& e:o.entries){e.lastSeen=0;e.pendingUntil=0;if(!suspending)e.observed=false;}xSemaphoreGive(lock_);}
    }
}
const char* PrinterService::diagnosis(const String& error){
    if(error=="printer_calibration_not_safe")return "Kalibrierung nicht freigegeben: Leerlauf, Referenzierung und Drucker-Konfiguration pruefen";
    if(error=="printer_extrude_not_safe")return "Extrusion gesperrt: aktive heisse Duese, Leerlauf und sichere Menge/Geschwindigkeit erforderlich";
    if(error=="printer_tool_not_active")return "Gewaehltes Werkzeug ist nicht aktiv oder nicht eindeutig einem Extruder zugeordnet";
    if(error=="printer_tool_not_safe"||error=="printer_tool_unavailable")return "Werkzeugwechsel gesperrt: Werkzeug, Leerlauf und Referenzierung pruefen";
    if(error=="printer_memory")return "Druckerstatus: nicht genug freier PSRAM";
    if(error=="printer_secret_permission")return "App braucht die Secrets-Berechtigung fuer den Drucker-API-Key";
    if(error=="printer_secret_invalid")return "Drucker-API-Key ist ungueltig";
    if(error=="printer_secret_missing")return "Druckerpasswort oder API-Key fehlt im zentralen Secret-Speicher";
    if(error=="printer_macro_not_configured")return "Makro ist auf diesem Drucker nicht konfiguriert";
    if(error=="printer_command_unsupported")return "Dieser Steuerbefehl wird vom Druckeradapter nicht sicher unterstuetzt";
    if(error.indexOf("endpoint_missing")>=0)return "Drucker-API nicht gefunden: Protokoll und Port pruefen";
    if(error=="printer_service_unavailable")return "Druckerserver erreichbar, Drucker-Firmware nicht bereit";
    if(error.indexOf("auth")>=0)return "Anmeldung fehlgeschlagen";
    if(error.indexOf("secret")>=0)return "Druckerpasswort fehlt oder Berechtigung fehlt";
    if(error.indexOf("unsupported")>=0)return "Druckerprotokoll noch nicht unterstützt";
    if(error.indexOf("offline")>=0)return "Netzwerk offline";
    if(error.indexOf("sessions")>=0)return "Drucker: zu viele Sitzungen";
    if(error.indexOf("limit")>=0)return "Druckerantwort überschreitet Speichergrenze";
    if(error.indexOf("model")>=0)return "Ungültiges oder unvollständiges Object Model";
    if(error.indexOf("profile")>=0)return "Druckerprofil ungültig oder nicht gefunden";
    if(error.indexOf("unknown")>=0)return "Befehlsausgang unklar – Status prüfen";
    if(error.indexOf("timeout")>=0)return "Drucker antwortet nicht rechtzeitig";
    if(error.indexOf("url")>=0)return "Adresse nicht auflösbar oder keine erlaubte LAN-IP";
    if(error.indexOf("busy")>=0)return "Druckerdienst beschäftigt";
    if(error.indexOf("safe")>=0||error.indexOf("protected")>=0)return "Befehl aus Sicherheitsgründen abgelehnt";
    if(error=="printer_not_idle")return "Drucker ist nicht im Leerlauf";
    if(error=="printer_not_printing")return "Kein aktiver Druck";
    if(error=="printer_not_paused")return "Druck ist nicht pausiert";
    if(error=="printer_gcode_use_typed_command")return "Bitte den geprüften Steuerbefehl statt freien G-Codes verwenden";
    if(error.indexOf("argument")>=0||error.indexOf("path")>=0)return "Ungültiger Befehlsparameter oder Dateipfad";
    if(error=="printer_command_rejected")return "Drucker hat den Befehl abgelehnt";
    return "Druckerverbindung fehlgeschlagen";
}
String PrinterService::makeSnapshot(const Entry& e,const String& error,uint32_t* digest)const{
    if(!e.status)return initial("printer_memory");const auto& s=*e.status;PrinterJsonAllocator memory;JsonDocument d(&memory);d["online"]=error.length()==0&&s.valid&&strcmp(s.state,"error")&&strcmp(s.state,"connecting");
    d["stale"]=error.length()!=0;d["error"]=error;d["message"]=error.length()?diagnosis(error):"";
    d["last_command_error"]=e.lastCommandError;d["last_command_message"]=e.lastCommandError.length()?diagnosis(e.lastCommandError):"";
    d["status"]=error.length()?diagnosis(error):s.state;d["state"]=s.state;d["api"]=e.selectedApi();d["profile_id"]=e.profile.id;
    d["filename"]=s.filename;printerValue(d["progress"],s.progress);printerValue(d["duration"],s.duration);printerValue(d["remaining"],s.remaining);if(s.layerKnown)d["layer"]=s.layer;else d["layer"]=nullptr;if(s.layersKnown)d["layers"]=s.layers;else d["layers"]=nullptr;
    printerValue(d["speed_factor"],s.speedFactor);printerValue(d["flow_factor"],s.flowFactor);
    auto addHeater=[&](const char* name,const PrinterHeater& h){if(h.index<0)return;auto v=d[name].to<JsonObject>();
        v["index"]=h.index;printerValue(v["current"],h.current);printerValue(v["target"],h.target);printerValue(v["max"],h.max);v["fault"]=h.fault;v["limit_known"]=h.limit;};
    addHeater("nozzle",s.nozzle);addHeater("bed",s.bed);
    auto axes=d["axes"].to<JsonArray>();for(size_t i=0;i<s.axisCount;++i){const auto& a=s.axes[i];auto v=axes.add<JsonObject>();
        char letter[2]{a.letter,0};v["id"]=letter;v["name"]=letter;v["letter"]=letter;v["unit"]="mm";printerValue(v["position"],a.position);printerValue(v["machine_position"],a.machinePosition);if(a.homedKnown)v["homed"]=a.homed;else v["homed"]=nullptr;printerValue(v["min"],a.min);printerValue(v["max"],a.max);v["controllable"]=a.homed&&a.limits&&s.httpChannelIdle;}
    auto fans=d["fans"].to<JsonArray>();for(size_t i=0;i<s.fanCount;++i){const auto& f=s.fans[i];auto v=fans.add<JsonObject>();v["id"]=f.id;v["name"]=f.name;v["index"]=f.index;printerValue(v["percent"],f.value*100);printerValue(v["rpm"],f.rpm);v["protected"]=f.automatic;v["controllable"]=!f.automatic;}
    printerWriteComponents(d.as<JsonObject>(),s);
    printerWriteActionStates(d["actions"].to<JsonArray>(),s,error.length()==0);
    printerWriteOperation(d["operation"].to<JsonObject>(),e.operation);
    auto job=d["job"].to<JsonObject>();job["filename"]=s.filename;job["state"]=s.state;printerValue(job["progress"],s.progress);printerValue(job["duration"],s.duration);printerValue(job["remaining"],s.remaining);if(s.layerKnown)job["layer"]=s.layer;else job["layer"]=nullptr;if(s.layersKnown)job["layers"]=s.layers;else job["layers"]=nullptr;
    auto motion=d["motion"].to<JsonObject>();motion["channel_idle"]=s.httpChannelIdle;motion["millimeters"]=s.httpMillimeters;
    auto overrides=d["overrides"].to<JsonObject>();printerValue(overrides["speed_factor"],s.speedFactor);printerValue(overrides["flow_factor"],s.flowFactor);
    auto connection=d["connection"].to<JsonObject>();connection["online"]=d["online"];connection["stale"]=d["stale"];connection["error"]=error;connection["server_reachable"]=s.valid&&error.length()==0;
    String out;serializeJson(d,out);if(d.overflowed()||out.length()>32000)return initial("printer_snapshot_limit");
    uint32_t hash=2166136261u;for(size_t i=0;i<out.length();++i)hash=(hash^uint8_t(out[i]))*16777619u;
    if(digest)*digest=hash;
    const uint32_t revision=digest&&hash!=e.contentHash?e.revision+1:e.revision;
    out.remove(out.length()-1,1);out+=",\"revision\":"+String(revision)+",\"timestamp\":"+String(e.lastUpdated)+",\"updated_at\":"+String(s.updatedAt)+",\"diagnostics\":{\"http_requests\":"+String(e.httpRequests)+",\"last_poll_requests\":"+String(e.lastPollRequests)+",\"last_poll_ms\":"+String(e.lastPollMs)+"}}";return out;
}
void PrinterService::loadProfiles(Owner& o){
    // Called only by NetworkWorker; KV identity and storage namespace stay centralized.
    const String raw=luaStringForNative(o.id,"printer_profiles");PrinterJsonAllocator allocator;JsonDocument doc(&allocator);
    String error;if(raw.length()>8192||deserializeJson(doc,raw)||!doc.is<JsonArray>()||doc.size()>kProfiles)error="printer_profiles_invalid";
    PrinterProfile valid[kProfiles];size_t count=0;JsonDocument publicProfiles;auto publicArray=publicProfiles.to<JsonArray>();
    const String selected=luaStringForNative(o.id,"selected_printer");
    if(!error.length())for(auto p:doc.as<JsonArrayConst>()){
        PrinterProfile v;v.owner=o.id;v.id=p["id"]|"";v.name=p["name"]|"";v.host=p["host"]|"";v.protocol=p["protocol"]|"reprap";
        v.mode=p["mode"]|"auto";v.passwordSecret=p["password_secret"]|"";v.background=p["background"]|true;v.enabled=p["enabled"]|true;
        v.calibration=p["calibration"]|"none";v.alignment=p["alignment"]|"none";
        if(v.protocol=="mainsail")v.protocol="moonraker";
        const int port=p["port"]|printerDefaultPort(v.protocol.c_str());bool ok=supported(v)&&printerIdentity(v.id.c_str(),36)&&v.name.length()<=48&&v.host.length()>0&&v.host.length()<=120&&port>0&&port<=65535&&
            (v.mode=="auto"||v.mode=="standalone"||v.mode=="sbc")&&(!v.passwordSecret.length()||printerIdentity(v.passwordSecret.c_str(),48));
        for(size_t i=0;i<v.host.length();++i)if(!isalnum(static_cast<unsigned char>(v.host[i]))&&v.host[i]!='.'&&v.host[i]!='-')ok=false;
        if(!printerCalibrationProfile(v.calibration.c_str())||!printerAlignmentProfile(v.alignment.c_str()))ok=false;
        for(const char* field:{"calibration","alignment"})if(!p[field].isNull()&&(!p[field].is<const char*>()||p[field].as<JsonString>().size()!=strlen(p[field].as<const char*>())))ok=false;
        if(!p["enabled"].isNull()&&!p["enabled"].is<bool>()||!p["background"].isNull()&&!p["background"].is<bool>())ok=false;
        for(size_t i=0;i<count;++i)if(valid[i].id==v.id)ok=false;
        if(!ok){error="printer_profiles_invalid";break;}v.port=port;valid[count++]=v;
        auto pub=publicArray.add<JsonObject>();pub["id"]=v.id;pub["name"]=v.name;pub["host"]=v.host;pub["port"]=v.port;pub["protocol"]=v.protocol;pub["camera_url"]=p["camera_url"]|"";
        pub["mode"]=v.mode;pub["enabled"]=v.enabled;pub["background"]=v.background;pub["selected"]=v.id==selected;
        pub["calibration"]=v.calibration;pub["alignment"]=v.alignment;
    }
    String json;serializeJson(publicProfiles,json);
    xSemaphoreTake(lock_,portMAX_DELAY);
    const String eventOwner=o.active&&(o.error!=error||(!error.length()&&o.profiles!=json))?String(o.id):String();
    if(o.active){o.error=error;
        if(!error.length()){
            // Preserve caches/session by identity, not array index; never mix reordered profiles.
            for(size_t i=0;i<count;++i){size_t found=i;for(size_t j=i;j<o.count;++j)if(o.entries[j].profile.id==valid[i].id){found=j;break;}
                if(found!=i)std::swap(o.entries[i],o.entries[found]);
                auto& e=o.entries[i];const auto& old=e.profile;
                if(old.owner!=valid[i].owner||old.id!=valid[i].id||old.host!=valid[i].host||old.port!=valid[i].port||old.mode!=valid[i].mode||
                    old.protocol!=valid[i].protocol||old.passwordSecret!=valid[i].passwordSecret)e=Entry{};
                e.profile=valid[i];}
            for(size_t i=0;i<count;++i)if(!o.entries[i].status)o.entries[i].status=printerStatusBuffer();
            for(size_t i=count;i<kProfiles;++i)o.entries[i]=Entry{};
            o.count=count;o.profiles=json;
        }else{
            // A broken/unavailable profile read must not erase the last valid
            // printer list, snapshots or sessions. Commands fail closed below.
            if(!o.count)o.profiles="";
        }
        o.syncedAt=millis();o.sync=false;
    }xSemaphoreGive(lock_);
    if(eventOwner.length())publishLuaEvent(eventOwner.c_str(),"printer.profiles",0);
}
void PrinterService::maintenance(void* context){static_cast<PrinterService*>(context)->tick();}
void PrinterService::tick(){
    if(!NetworkWorker::instance().backgroundWindow()||!lock_||int32_t(millis()-nextNetwork_)<0)return;
    const uint32_t now=millis();
    xSemaphoreTake(lock_,portMAX_DELAY);
    for(auto& o:owners_)if(!o.active&&!o.busy&&o.count&&uint32_t(now-o.closedAt)>=120000UL){for(auto& e:o.entries)e=Entry{};o.count=0;o.profiles="";o.error="";}
    xSemaphoreGive(lock_);
    for(auto& o:owners_){
        if(!o.active)continue;
        if(xSemaphoreTake(lock_,0)!=pdTRUE)return;
        const bool sync=!o.busy&&(o.sync||uint32_t(now-o.syncedAt)>=120000UL);
        if(sync)o.busy=true;xSemaphoreGive(lock_);
        if(sync){loadProfiles(o);xSemaphoreTake(lock_,portMAX_DELAY);o.busy=false;xSemaphoreGive(lock_);return;}
    }
    if(!NetworkService::instance().online()||SecurityService::instance().tabletLocked()){
        const String error=SecurityService::instance().tabletLocked()?"tablet_locked":"printer_network_offline";
        xSemaphoreTake(lock_,portMAX_DELAY);
        for(auto& o:owners_)if(o.active)for(size_t i=0;i<o.count;++i){auto& e=o.entries[i];
            if(e.connectionError!=error){e.connectionError=error;++e.revision;e.snapshot=makeSnapshot(e,error);}}
        xSemaphoreGive(lock_);nextNetwork_=now+5000;return;}
    for(size_t scan=0;scan<kOwners*kProfiles;++scan){
        const size_t n=cursor_++%(kOwners*kProfiles);auto& o=owners_[n/kProfiles];
        if(!o.active)continue;if(xSemaphoreTake(lock_,0)!=pdTRUE)return;
        const size_t idx=n%kProfiles;if(o.busy||idx>=o.count){xSemaphoreGive(lock_);continue;}
        auto& e=o.entries[idx];const bool foreground=(e.observed&&AppBackgroundScheduler::instance().isForeground(o.id))||
            (e.lastSeen&&uint32_t(now-e.lastSeen)<4000);
        // Only retire abandoned reservations after the shared worker has drained
        // its queue. A slow queue must never allow two overlapping controls.
        if(e.pendingUntil&&int32_t(now-e.pendingUntil)>=0&&NetworkWorker::instance().queueEmpty()){e.pendingUntil=0;
            if(e.operation.phase==PrinterOperationPhase::Queued)e.operation.set(PrinterOperationPhase::Unknown,now);}
        // A queued control owns this printer's next worker turn. Fair status
        // maintenance must not delay or invalidate an accepted command.
        if(e.pendingUntil&&!NetworkWorker::instance().queueEmpty()){xSemaphoreGive(lock_);continue;}
        const bool high=AppBackgroundScheduler::instance().foregroundLoad()==ForegroundLoad::High;
        const bool requestedDirectory=e.wantFiles||e.wantMacros;
        const bool consoleRead=foreground&&(e.profile.protocol=="reprap"||e.profile.protocol=="rrf")&&e.profile.mode!="sbc"&&
            e.consoleSeen&&uint32_t(now-e.consoleSeen)<2500&&uint32_t(now-e.consolePolled)>=1000;
        if(!e.profile.enabled||(!e.status&&!requestedDirectory&&!consoleRead)||(!foreground&&(!e.profile.background||high))||(!consoleRead&&int32_t(now-e.due)<0)){xSemaphoreGive(lock_);continue;}
        if(consoleRead){
            o.busy=true;e.consolePolled=now;const auto permissions=o.permissions;xSemaphoreGive(lock_);
            WorkerPrinterTransport http(e.profile,permissions,o.active,nullptr,2500);http.allowPriorityYield();
            String error;e.adapter.pollConsole(http,e.profile,error);
            xSemaphoreTake(lock_,portMAX_DELAY);
            if(o.active)appendConsole(e.console,e.takeReply());
            e.httpRequests+=http.requests;o.busy=false;xSemaphoreGive(lock_);
            nextNetwork_=millis()+250;return;
        }
        // File metadata is independently readable even when a temperature or
        // motion branch makes the full status invalid. Explicit reads must
        // finish (or fail), never remain pending behind model validity.
        o.busy=true;const bool listing=requestedDirectory;const bool macro=listing&&!e.wantFiles;
        const String dir=macro?e.macrosDirectory:e.filesDirectory;
        const uint16_t pageOffset=macro?e.macrosOffset:e.filesOffset;
        const auto permissions=o.permissions;
        xSemaphoreGive(lock_);
        const auto began=millis();
        // Standalone RepRapFirmware reads multiple bounded model branches.
        // Reserve a larger cumulative deadline without changing other adapters.
        const bool reprapModel=e.profile.protocol=="reprap"||e.profile.protocol=="rrf";
        WorkerPrinterTransport http(e.profile,permissions,o.active,nullptr,reprapModel?12000:4000);
        http.allowPriorityYield();
        String error,json;PrinterStatusPtr status;
        if(!listing){status=printerStatusBuffer();if(status)*status=*e.status;}
        bool ok=false,changed=false;
        if(!supported(e.profile))error="printer_adapter_unsupported";
        else if(listing){
            if(reprapModel)e.adapter.setListOffset(pageOffset);
            ok=e.selectedAdapter().list(http,e.profile,dir,json,error);
        }
        else if(!status)error="printer_memory";
        else ok=e.selectedAdapter().poll(http,e.profile,*status,error);
        xSemaphoreTake(lock_,portMAX_DELAY);
        if(http.yielded()){
            // Discard a partial model: keep the last valid snapshot, revisions,
            // connection diagnosis and pending file request unchanged.
            e.httpRequests+=http.requests;e.lastPollRequests=http.requests;e.lastPollMs=millis()-began;
            e.due=millis()+250;o.busy=false;xSemaphoreGive(lock_);
            nextNetwork_=millis()+250;return;
        }
        if(o.active){
            e.httpRequests+=http.requests;e.lastPollRequests=http.requests;e.lastPollMs=millis()-began;
            if(listing){JsonDocument failed;failed["entries"].to<JsonArray>();failed["pending"]=false;failed["error"]=error;failed["message"]=diagnosis(error);
                if(!ok)serializeJson(failed,json);
                // A UI request may have selected another directory while the
                // worker was listing. Never publish the previous folder there.
                if(macro){if(e.macrosDirectory==dir&&e.macrosOffset==pageOffset){e.macros=json;e.wantMacros=false;}}
                else if(e.filesDirectory==dir&&e.filesOffset==pageOffset){e.files=json;e.wantFiles=false;}
                e.due=millis()+500; // Give the next read phase a bounded opportunity.
            }else{
                if(status)*e.status=*status;e.connectionError=error;e.lastUpdated=millis();
                if(ok){e.operation.observe(*e.status,millis());
                    if(!e.history){void* memory=heap_caps_malloc(sizeof(PrinterTemperatureHistory),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                        if(memory)e.history=std::shared_ptr<PrinterTemperatureHistory>(new(memory) PrinterTemperatureHistory,[](PrinterTemperatureHistory* p){p->~PrinterTemperatureHistory();heap_caps_free(p);});}
                    if(e.history)e.history->sample(*e.status,millis());}
                else if(e.operation.active()&&uint32_t(millis()-e.operation.started)>600000)e.operation.set(PrinterOperationPhase::Unknown,millis());
                uint32_t digest=e.contentHash;e.snapshot=makeSnapshot(e,error,&digest);changed=digest!=e.contentHash;if(changed)++e.revision;e.contentHash=digest;
                appendConsole(e.console,e.takeReply());
                e.failures=ok?0:std::min<unsigned>(e.failures+1,5);
                // Account for polling time: do not add two seconds after every
                // successful foreground round. Keep a floor to avoid busy loops.
                const uint32_t elapsed=millis()-began;
                e.due=millis()+(ok?(foreground?(elapsed<1000?1000-elapsed:250):15000):printerBackoff(e.failures));
            }
        }const String eventOwner=o.active&&!listing&&changed?String(o.id):String();o.busy=false;xSemaphoreGive(lock_);
        nextNetwork_=millis()+500;if(eventOwner.length())publishLuaEvent(eventOwner.c_str(),"printer.status",0);return;
    }
    nextNetwork_=now+500;
}
uint32_t PrinterService::command(const char* owner,AppPermissions permissions,const char* id,const String& kind,const String& parameters){
    if(kind=="upload_file"&&!(permissions&perm(AppPermission::StorageRead)))return 0;
    if(!printerIdentity(id,36)||parameters.length()>1024||kind.length()>32||!(permissions&perm(AppPermission::Network))||
        !lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return 0;
    auto* o=findOwner(owner);auto* e=findEntry(o,id);const uint32_t generation=o?o->generation:0;
    const bool preview=kind=="preview_file";
    const bool valid=e&&e->profile.enabled&&!o->busy&&!e->pendingUntil&&supported(e->profile)&&permissions==o->permissions;const String target=e?identity(e->profile):String();uint32_t operation=0;
    if(valid&&!preview){e->pendingUntil=millis()+60000;operation=e->operation.id+1;if(!operation)operation=1;e->operation.begin(operation,kind.c_str(),millis());}
    xSemaphoreGive(lock_);if(!valid)return 0;
    LuaJobService::Request q;q.kind=5;q.owner=owner;q.permissions=permissions;q.required=perm(AppPermission::Network);
    q.handler=executeCommand;q.completion="printer.changed";q.url=id;q.fileName=kind;q.body=parameters;q.resource=generation;q.context=target;
    q.offset=operation;
    if(kind=="preview_file")q.discardResult=printerDiscardPreview;
    const auto job=LuaJobService::instance().submit(q);if(!job&&!preview&&xSemaphoreTake(lock_,0)==pdTRUE){auto* current=findEntry(findOwner(owner),id);if(current){current->pendingUntil=0;current->operation.set(PrinterOperationPhase::Failed,millis());}xSemaphoreGive(lock_);}return job;
}
void PrinterService::executeCommand(const LuaJobService::Request& q,LuaJobService::Result& r){instance().runCommand(q,r);}
void PrinterService::runCommand(const LuaJobService::Request& q,LuaJobService::Result& r){
    if(!NetworkWorker::instance().isWorkerContext()){r.error="worker_required";return;}
    xSemaphoreTake(lock_,portMAX_DELAY);auto* o=findOwner(q.owner.c_str());auto* e=findEntry(o,q.url.c_str());
    if(!e||o->generation!=q.resource||o->busy){xSemaphoreGive(lock_);r.error="printer_profile_unavailable";return;}
    // A queued user action must not execute much later without renewed intent.
    // Expire before any adapter/network operation; never retry a write command.
    if(printerCommandQueueExpired(q.fileName.c_str(),r.queuedAt,millis())){
        if(q.fileName!="preview_file"){e->pendingUntil=0;e->operation.set(PrinterOperationPhase::Failed,millis());}
        xSemaphoreGive(lock_);r.error="printer_command_queue_expired";return;
    }
    o->busy=true;xSemaphoreGive(lock_);
    // Resync immediately so queued commands cannot use an old IP after profile edits.
    loadProfiles(*o);
    xSemaphoreTake(lock_,portMAX_DELAY);e=findEntry(o,q.url.c_str());
    const bool profileRefreshFailed=o->error.length()!=0;
    xSemaphoreGive(lock_);
    String reply;auto status=printerStatusBuffer();
    PrinterJsonAllocator allocator;JsonDocument parameters(&allocator);
    if(!e||!o->active||!e->profile.enabled)r.error="printer_profile_unavailable";
    else if(profileRefreshFailed)r.error="printer_profiles_invalid";
    else if(!status||!e->status)r.error="printer_memory";
    else if(identity(e->profile)!=q.context)r.error="printer_profile_changed";
    else if(!supported(e->profile))r.error="printer_adapter_unsupported";
    else if(deserializeJson(parameters,q.body)||!parameters.is<JsonObject>())r.error="printer_argument_invalid";
    else{
        const bool reprapCommand=e->profile.protocol=="reprap"||e->profile.protocol=="rrf";
        WorkerPrinterTransport http(e->profile,q.permissions,o->active,q.cancelled,reprapCommand?14000:6000,&q);
        if(q.fileName=="preview_file"){
            const unsigned width=parameters["maxWidth"]|320u,height=parameters["maxHeight"]|220u;
            if(!width||!height||width>512||height>512||
               (parameters.containsKey("maxWidth")&&!parameters["maxWidth"].is<unsigned>())||
               (parameters.containsKey("maxHeight")&&!parameters["maxHeight"].is<unsigned>())){
                r.error="printer_preview_dimensions";
            }else{
            http.previewWidth=width;http.previewHeight=height;
            String bytes;const String source=parameters["source"]|"",path=parameters["path"]|"";
            bool found=false;
            if(source.length()){
                if(q.permissions&perm(AppPermission::StorageRead))found=printerLocalThumbnail(source,bytes,q.cancelled,r.error,width,height);
                else r.error="printer_upload_source_denied";
            }else found=e->selectedAdapter().thumbnail(http,e->profile,path,bytes,r.error);
            if(found)r.ok=printerPreviewImage(q.owner,bytes,q.cancelled,r.body,r.error,width,height);
            }
        }else if(q.fileName=="upload_file"){
            const String source=parameters["source"]|"",target=parameters["path"]|"";
            if(!printerLocalGcode(source)||!printerPath(target.c_str(),"0:/gcodes/"))r.error="printer_path_invalid";
            else if(reprapCommand)r.ok=e->adapter.upload(http,e->profile,source,target,r.error);
            else{
                const String relative=target.substring(10),name=relative.substring(relative.lastIndexOf('/')+1);
                const String folder=relative.lastIndexOf('/')>=0?relative.substring(0,relative.lastIndexOf('/')):String();
                const String boundary=printerMultipartBoundary();
                String prefix="--"+boundary+"\r\nContent-Disposition: form-data; name=\"path\"\r\n\r\n"+folder+"\r\n--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\""+name+"\"\r\nContent-Type: application/octet-stream\r\n\r\n";
                auto response=http.upload(e->isMoonraker()?"/server/files/upload":"/api/files/local","",source,prefix,"\r\n--"+boundary+"--\r\n",1);
                r.ok=response.ok;r.error=response.error;if(!r.ok&&!r.error.length())r.error="printer_upload_rejected";
            }
            if(r.ok){e->files="";e->wantFiles=true;reply="Datei hochgeladen; Druck wurde nicht gestartet";}
        }else r.ok=e->selectedAdapter().command(http,e->profile,q.fileName,parameters.as<JsonVariantConst>(),*status,reply,r.error);
    }
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(e&&q.fileName!="preview_file")e->pendingUntil=0;
    if(e&&o->active&&e->status&&q.fileName!="preview_file"){if(status&&status->valid){*e->status=*status;e->connectionError="";e->lastUpdated=millis();}
        if(e->operation.id==q.offset){
            strlcpy(e->operation.component,parameters["heater"]|parameters["tool"]|"",sizeof(e->operation.component));
            e->operation.target=parameters["target"]|parameters["percent"]|NAN;
            e->operation.set(r.ok?PrinterOperationPhase::Accepted:r.error=="printer_command_outcome_unknown"?PrinterOperationPhase::Unknown:PrinterOperationPhase::Failed,millis());
            if(r.ok&&q.fileName=="upload_file")e->operation.set(PrinterOperationPhase::Completed,millis());
        }
        e->lastCommandError=r.error;e->due=millis()+500;++e->revision;e->snapshot=makeSnapshot(*e,e->connectionError);
        appendConsole(e->console,e->takeReply());
        appendConsole(e->console,reply.length()?reply:r.ok?String("Befehl angenommen: ")+q.fileName:String(diagnosis(r.error)));
    }o->busy=false;xSemaphoreGive(lock_);
    r.status=r.ok?202:0;if(q.fileName!="preview_file")r.body="{\"accepted\":true,\"execution_complete\":false}";if(!r.ok)r.body="";
}
}

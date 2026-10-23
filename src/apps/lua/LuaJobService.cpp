#include "LuaJobService.h"
#include "../../core/RequestDeadline.h"
#include "../../services/AppFileStreamService.h"
#include "../../services/SecurityService.h"
#include "LuaEvents.h"
#include "LuaStorageBridge.h"
#include "LuaDiscoveryBridge.h"
#include "../../services/EnergyHistoryService.h"
#include "../../services/MediaSessionService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/ConfigService.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <lwip/netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <mbedtls/base64.h>
namespace hp {
namespace {
bool localAddress(uint32_t address) {
    const uint8_t a=address>>24,b=(address>>16)&255;
    return a==10 || (a==172&&b>=16&&b<=31) || (a==192&&b==168);
}
// Resolve on worker, validate the resolved IPv4, then pin the connection to it.
// No redirects, IPv6, credentials in URL, loopback, link-local or multicast.
bool target(const LuaJobService::Request& r,String& pinned,String& hostname) {
    const bool tls=r.url.startsWith("https://");
    if(!tls&&!r.url.startsWith("http://"))return false;
    const size_t start=tls?8:7;
    int end=r.url.indexOf('/',start);if(end<0)end=r.url.length();
    const String authority=r.url.substring(start,end);
    if(!authority.length()||authority.indexOf('@')>=0||authority.indexOf('[')>=0||authority.indexOf('?')>=0||authority.indexOf('#')>=0)return false;
    const int colon=authority.indexOf(':');
    hostname=colon<0?authority:authority.substring(0,colon);
    if(!hostname.length()||hostname.length()>253)return false;
    for(size_t i=0;i<hostname.length();++i) {
        const char c=hostname[i];if(!isalnum(static_cast<unsigned char>(c))&&c!='.'&&c!='-')return false;
    }
    String port=colon<0?String(tls?443:80):authority.substring(colon+1);
    if(!port.length()||port.length()>5)return false;
    for(size_t i=0;i<port.length();++i)if(!isdigit(static_cast<unsigned char>(port[i])))return false;
    if(port.toInt()<1||port.toInt()>65535)return false;
    addrinfo hints{},*answer=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(hostname.c_str(),nullptr,&hints,&answer)!=0||!answer)return false;
    const auto* addr=reinterpret_cast<sockaddr_in*>(answer->ai_addr);
    const uint32_t ip=ntohl(addr->sin_addr.s_addr);
    char buffer[INET_ADDRSTRLEN];inet_ntop(AF_INET,&addr->sin_addr,buffer,sizeof(buffer));freeaddrinfo(answer);
    const uint8_t first=ip>>24;
    if(first==0||first==127||first>=224||(first==169&&((ip>>16)&255)==254)||ip==0xffffffff)return false;
    if(localAddress(ip)) {if(!(r.permissions&perm(AppPermission::Network)))return false;}
    else if(!(r.permissions&perm(AppPermission::NetworkInternet)))return false;
    pinned=String(tls?"https://":"http://")+buffer+":"+port+(end<static_cast<int>(r.url.length())?r.url.substring(end):"/");
    return true;
}
}
bool LuaJobService::resolveHttpTarget(const Request& request,String& pinned,String& hostname){return target(request,pinned,hostname);}
LuaJobService& LuaJobService::instance(){static LuaJobService service;
    // Function-local initialization is thread-safe even when the first caller
    // is the existing HTTP server, rather than the UI task.
    static const bool initialized=[](){service.lock_=xSemaphoreCreateMutex();return true;}();
    (void)initialized;return service;
}
bool LuaJobService::sameRead(const Request& a,const Request& b){
    if(!a.coalesce||!b.coalesce||a.kind||b.kind||a.method||b.method||a.streamMode||b.streamMode||
       a.body.length()||b.body.length()||a.owner!=b.owner||a.url!=b.url||a.permissions!=b.permissions||
       a.timeout!=b.timeout||a.responseLimit!=b.responseLimit||a.context!=b.context||
       a.bearerSecret!=b.bearerSecret||a.basicSecret!=b.basicSecret||a.contentType!=b.contentType||a.headerCount!=b.headerCount)return false;
    // These GET endpoints can write or mutate a session; never reuse their result.
    for(const char* path:{"rr_gcode","rr_upload","rr_delete","rr_mkdir","rr_move","rr_connect","rr_disconnect","machine/code"})
        if(a.url.indexOf(path)>=0)return false;
    for(size_t i=0;i<a.headerCount;++i)if(a.headers[i].name!=b.headers[i].name||a.headers[i].value!=b.headers[i].value)return false;
    return true;
}
uint32_t LuaJobService::submit(const Request& request) {
    if(static_cast<unsigned>(request.lane)>2 ||
       (request.lane!=Request::Lane::Automatic&&request.kind!=5))return 0;
    if(SecurityService::instance().tabletLocked())return 0;
    if(!request.owner.length()||request.owner.length()>31||request.kind>5||
       request.body.length()>kBodyBytes||request.context.length()>512||request.headerCount>8||
       (request.kind==0&&(request.method>3||request.streamMode>2)))return 0;
    if(request.streamMode&&(!(request.permissions&perm(AppPermission::UserStorage))||
       !AppFileStreamService::validName(request.fileName)||request.body.length()))return 0;
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return 0;
    Slot* selected=nullptr;const uint32_t now=millis();
    for(auto& slot:slots_) {
        if(!slot.busy&&slot.id&&static_cast<uint32_t>(now-slot.finished)>60000){if(slot.request.discardResult)slot.request.discardResult(slot.owner,slot.result.body);slot.request={};slot.result={};slot.id=0;}
        if(!slot.busy&&!slot.id&&!selected)selected=&slot;
    }
    if(!selected){xSemaphoreGive(lock_);return 0;}
    if(request.kind==5&&(!request.handler||!request.required||(request.permissions&request.required)!=request.required)){xSemaphoreGive(lock_);return 0;}
    if(request.kind==4&&(!(request.permissions&perm(AppPermission::Audio))||request.method>static_cast<uint8_t>(MediaCommand::VolumeDown))){xSemaphoreGive(lock_);return 0;}
    if(request.kind==3&&!(request.permissions&perm(AppPermission::Energy))){xSemaphoreGive(lock_);return 0;}
    if(request.kind==2&&(!(request.permissions&perm(AppPermission::Discovery))||!(request.permissions&perm(AppPermission::Network)))){xSemaphoreGive(lock_);return 0;}
    if(request.kind==1) {
        const AppPermission required=request.method>=4&&request.method<10?AppPermission::Secrets:AppPermission::UserStorage;
        if(!(request.permissions&perm(required))){xSemaphoreGive(lock_);return 0;}
    }
    selected->request=request;selected->result={};selected->cancelled=false;selected->busy=true;
    selected->sharedReady=false;selected->credentialRevision=ConfigService::instance().revision();
    selected->result.queuedAt=now;
    selected->request.progress=progress;selected->request.progressContext=selected;
    selected->request.cancelled=&selected->cancelled;
    strlcpy(selected->owner,request.owner.c_str(),sizeof(selected->owner));
    sequence_=(sequence_+1)&0x1fffffff;if(!sequence_)sequence_=1;
    selected->id=sequence_*4+static_cast<uint32_t>(selected-slots_);
    const uint32_t id=selected->id;
    // A single transfer queue lets short status/energy/weather requests continue.
    // Keep interactive native events high-priority but never prioritize uploads.
    const bool forcedTransfer=request.lane!=Request::Lane::Automatic;
    const bool transfer=forcedTransfer||(request.kind==0&&(request.streamMode||request.timeout>750))||request.kind==2||
       (request.kind==5&&(request.fileName=="upload_file"||request.fileName=="calendar_query"||request.fileName=="preview_file"));
    const bool urgent=!transfer&&(request.kind==4||request.kind==3||
       (request.kind==5&&request.completion=="printer.changed"&&
        request.fileName!="preview_file"));
    const bool queued=request.lane==Request::Lane::LocalTransfer
        ?NetworkWorker::instance().submitLocalTransfer(execute,selected,"lua-file")
        :request.kind==1
        ?NetworkWorker::instance().submitLocal(execute,selected,"lua-storage")
        :transfer
        ?NetworkWorker::instance().submitTransfer(execute,selected,"lua-transfer")
        :urgent?NetworkWorker::instance().submitPriority(execute,selected,"lua-urgent")
               :NetworkWorker::instance().submit(execute,selected,"lua-job");
    if(!queued) {
        selected->busy=false;selected->id=0;selected->request={};xSemaphoreGive(lock_);return 0;
    }
    xSemaphoreGive(lock_);
    return id;
}
bool LuaJobService::result(const char* owner,uint32_t id,Result& out) {
    if(!lock_||!id||xSemaphoreTake(lock_,0)!=pdTRUE)return false;
    auto& slot=slots_[id%4];
    const bool valid=slot.id==id&&slot.request.owner==owner;
    if(valid){out=slot.result;out.cancelled=slot.cancelled.load();}
    xSemaphoreGive(lock_);return valid;
}
bool LuaJobService::cancel(const char* owner,uint32_t id) {
    if(!lock_||!id||xSemaphoreTake(lock_,0)!=pdTRUE)return false;
    auto& slot=slots_[id%4];const bool valid=slot.id==id&&slot.request.owner==owner;
    if(valid){slot.cancelled=true;if(!slot.busy){if(slot.request.discardResult)slot.request.discardResult(owner,slot.result.body);slot.id=0;slot.request={};slot.result={};}}
    xSemaphoreGive(lock_);return valid;
}
bool LuaJobService::release(const char* owner,uint32_t id){if(!lock_||!owner||!id||xSemaphoreTake(lock_,0)!=pdTRUE)return false;
    auto& slot=slots_[id%kSlots];const bool valid=slot.id==id&&slot.request.owner==owner&&!slot.busy&&slot.result.done;
    if(valid){if(slot.request.discardResult)slot.request.discardResult(owner,slot.result.body);slot.id=0;slot.request={};slot.result={};}xSemaphoreGive(lock_);return valid;
}
void LuaJobService::progress(void* context,uint64_t processed,uint64_t total){auto& service=instance();
    if(!service.lock_||xSemaphoreTake(service.lock_,0)!=pdTRUE)return;auto& slot=*static_cast<Slot*>(context);
    if(slot.busy){slot.result.processed=processed;slot.result.total=total;}xSemaphoreGive(service.lock_);
}
int LuaJobService::completionState(const char* owner,uint32_t id){if(!lock_||!id||!owner)return 0;if(xSemaphoreTake(lock_,0)!=pdTRUE)return -1;
    const auto& slot=slots_[id%4];const bool valid=slot.id==id&&slot.request.owner==owner&&!slot.busy&&slot.result.done&&!slot.cancelled;
    xSemaphoreGive(lock_);return valid?1:0;
}
void LuaJobService::cancelOwner(const char* owner,bool suspending) {
    // UI never waits for a result-cache lock while closing a state. Owners
    // are only changed by the same guarded UI context; cancellation is atomic.
    for(auto& slot:slots_)if(!strcmp(slot.owner,owner))slot.cancelled=true;
    cleanup();
    discardLuaEvents(owner);
    ownerSuspending_=suspending;ownerCleanup_.run(owner);ownerSuspending_=false;
}
void LuaJobService::cleanup() {
    if(!lock_||xSemaphoreTake(lock_,0)!=pdTRUE)return;
    const uint32_t now=millis();
    for(auto& slot:slots_)if(!slot.busy&&slot.id&&(slot.cancelled||static_cast<uint32_t>(now-slot.finished)>60000)){if(slot.request.discardResult)slot.request.discardResult(slot.owner,slot.result.body);slot.request={};slot.result={};slot.id=0;}
    xSemaphoreGive(lock_);
}
void LuaJobService::executeHttp(const Request& request,Result& result) {
    if(!NetworkWorker::instance().isWorkerContext()){result.error="worker_required";return;}
    // Also cover maintenance HTTP calls that do not dequeue a foreground job.
    struct RadioActivity {
        RadioActivity(){NetworkWorker::instance().beginNetworkActivity();}
        ~RadioActivity(){NetworkWorker::instance().endNetworkActivity();}
    } radioActivity;
    if(request.method>3||request.headerCount>8||request.timeout<100||request.timeout>5000){result.error="http_request";return;}
    if(SecurityService::instance().tabletLocked()){result.error="tablet_locked";return;}
    if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return;}
    const RequestDeadline budget(millis(),request.streamMode?request.deadlineMs:request.timeout);
    const size_t responseLimit=std::min<size_t>(65536,request.responseLimit);
    String pinned,hostname;
    if(!target(request,pinned,hostname))result.error="url_or_permission";
    else {
        auto& streams=AppFileStreamService::instance();uint32_t downloadId=0;
        File uploadFile;
        if(request.streamMode==2&&!request.nativeRead){uploadFile=streams.open(request.owner,request.fileName);
            if(!uploadFile||uploadFile.isDirectory())result.error="file_unavailable";}
        esp_http_client_config_t config{};config.url=pinned.c_str();
        config.timeout_ms=request.timeout;config.disable_auto_redirect=true;
        config.crt_bundle_attach=esp_crt_bundle_attach;config.common_name=hostname.c_str();
        esp_http_client_handle_t client=esp_http_client_init(&config);
        if(!client)result.error="http_memory";
        else {
            const esp_http_client_method_t methods[]={HTTP_METHOD_GET,HTTP_METHOD_POST,HTTP_METHOD_PUT,HTTP_METHOD_DELETE};
            esp_http_client_set_method(client,methods[request.method]);
            esp_http_client_set_header(client,"Host",hostname.c_str());
            if(request.contentType.length())esp_http_client_set_header(client,"Content-Type",request.contentType.c_str());
            for(size_t i=0;i<request.headerCount;++i)esp_http_client_set_header(client,request.headers[i].name.c_str(),request.headers[i].value.c_str());
            String authorization;
            if(request.bearerSecret.length()) {
                authorization=luaSecretForNative(request.owner,request.bearerSecret);
                if(authorization.length())authorization="Bearer "+authorization;
                else result.error="secret_missing";
                if(authorization.indexOf('\r')>=0||authorization.indexOf('\n')>=0)result.error="secret_invalid";
                if(!result.error.length())esp_http_client_set_header(client,"Authorization",authorization.c_str());
            }
            if(request.basicSecret.length()) {
                const String credentials=luaSecretForNative(request.owner,request.basicSecret);
                unsigned char encoded[2800];size_t size=0;
                if(!credentials.length()||credentials.indexOf(':')<1||credentials.indexOf('\r')>=0||credentials.indexOf('\n')>=0)
                    result.error="secret_invalid";
                else if(mbedtls_base64_encode(encoded,sizeof(encoded),&size,reinterpret_cast<const unsigned char*>(credentials.c_str()),credentials.length()))result.error="secret_limit";
                else {
                    authorization="Basic "+String(reinterpret_cast<const char*>(encoded),size);
                    esp_http_client_set_header(client,"Authorization",authorization.c_str());
                }
            }
            const auto stopped=[&](){if(SecurityService::instance().tabletLocked()){result.error="tablet_locked";return true;}
                if(!budget.remaining(millis())){result.error="timeout";return true;}
                if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return true;}
                // Applies to the next connect/write/header/read call, not an
                // already running synchronous request or resolver operation.
                const auto timeout=budget.stepTimeout(millis(),request.timeout);
                if(!timeout){result.error="timeout";return true;}
                esp_http_client_set_timeout_ms(client,timeout);return false;};
            const size_t uploadSize=request.streamMode==2?(request.nativeRead?request.expected:uploadFile.size()):request.body.length();
            if(!result.error.length()&&!stopped()&&esp_http_client_open(client,uploadSize)==ESP_OK) {
                const String& body=request.body;
                size_t written=0;
                char uploadChunk[512];
                while(written<uploadSize&&!stopped()) {
                    size_t available=std::min<size_t>(sizeof(uploadChunk),uploadSize-written);
                    const char* data=body.c_str();
                    if(request.streamMode!=2)data+=written;
                    if(request.streamMode==2){available=request.nativeRead?request.nativeRead(request.nativeReadContext,written,reinterpret_cast<uint8_t*>(uploadChunk),available):uploadFile.read(reinterpret_cast<uint8_t*>(uploadChunk),available);data=uploadChunk;
                        if(!available||available>sizeof(uploadChunk)||available>uploadSize-written){result.error="file_read";break;}}
                    size_t sent=0;while(sent<available&&!stopped()){const int n=esp_http_client_write(client,data+sent,available-sent);
                        if(n<=0){result.error="http_write";break;}sent+=n;written+=n;}
                    if(request.streamMode==2&&request.progress)request.progress(request.progressContext,written,uploadSize);
                    NetworkWorker::instance().maintain();if(result.error.length())break;
                }
                if(!result.error.length()&&!stopped()) {
                    const int64_t length=esp_http_client_fetch_headers(client);
                    result.status=esp_http_client_get_status_code(client);
                    if(request.streamMode==1){
                        if(result.status<200||result.status>=300)result.error="http_status";
                        else if(length>static_cast<int64_t>(AppFileStreamService::kSdFile))result.error="file_limit";
                        else if(!request.nativeWrite)downloadId=streams.begin(request.owner,request.fileName,length>0?size_t(length):0,result.error);
                    }
                    if(!request.streamMode&&length>static_cast<int64_t>(responseLimit))result.error="response_limit";
                    else if(request.streamMode==2&&length>static_cast<int64_t>(responseLimit))result.error="response_limit";
                    else {
                        if(request.streamMode!=1)result.body.reserve(responseLimit);char chunk[512];size_t received=0;
                        while(!stopped()&&!result.error.length()) {
                            const int n=esp_http_client_read(client,chunk,sizeof(chunk));
                            if(n<0){result.error="http_read";break;}
                            if(!n){if(!esp_http_client_is_complete_data_received(client))result.error="http_incomplete";break;}
                            if(request.streamMode==1){size_t saved=0;
                                if(received+size_t(n)>AppFileStreamService::kSdFile){result.error="file_limit";break;}
                                if(request.nativeWrite){if(!request.nativeWrite(request.nativeWriteContext,reinterpret_cast<uint8_t*>(chunk),n,result.error))break;}
                                else if(!streams.write(request.owner,downloadId,received,reinterpret_cast<uint8_t*>(chunk),n,saved,result.error))break;
                                received+=n;result.processed=received;result.total=length>0?length:0;
                                if(request.progress)request.progress(request.progressContext,received,result.total);
                            }else{if(result.body.length()+n>responseLimit){result.error="response_limit";break;}
                                if(!result.body.concat(chunk,n)){result.error="response_memory";break;}
                                if(request.streamMode!=2){result.processed=result.body.length();result.total=length>0?length:0;
                                    if(request.progress)request.progress(request.progressContext,result.processed,result.total);}}
                            NetworkWorker::instance().maintain();
                        }
                    }
                }
            } else if(!result.error.length())result.error="http_connect";
            esp_http_client_close(client);esp_http_client_cleanup(client);
        }
        if(downloadId){if(!(request.cancelled&&request.cancelled->load())&&!result.error.length()&&!SecurityService::instance().tabletLocked()){
                size_t size=0;if(streams.commit(request.owner,downloadId,size,result.error)){result.size=size;downloadId=0;}}
            if(downloadId)streams.abort(request.owner,downloadId);}
    }
    result.cancelled=request.cancelled&&request.cancelled->load();
    if(result.cancelled)result.error="cancelled";
    if(request.streamMode!=1)result.size=result.body.length();
    result.ok=!result.error.length()&&result.status>=200&&result.status<300;
    if(!result.ok&&!result.error.length())result.error=result.status==401||result.status==403?"authentication":"http_status";
}
void LuaJobService::execute(void* context){instance().run(*static_cast<Slot*>(context));}
void LuaJobService::run(Slot& slot) {
    Result result;
    xSemaphoreTake(lock_,portMAX_DELAY);
    const bool shared=slot.sharedReady&&slot.credentialRevision==ConfigService::instance().revision();
    if(shared)result=std::move(slot.result);
    slot.sharedReady=false; // Changed credentials must never consume the shared response.
    result.queuedAt=slot.result.queuedAt;result.startedAt=millis();slot.result.startedAt=result.startedAt;xSemaphoreGive(lock_);
    if(SecurityService::instance().tabletLocked())result.error="tablet_locked";
    else if(slot.cancelled)result.error="cancelled";
    else if(shared){result.coalesced=true;++coalesced_;}
    else if(slot.request.kind==5){slot.request.handler(slot.request,result);}
    else if(slot.request.kind==4){result.ok=MediaSessionService::instance().command(static_cast<MediaCommand>(slot.request.method));}
    else if(slot.request.kind==3){result.body=EnergyHistoryService::instance().query(slot.request.url,64);result.ok=result.body.length()>0;}
    else if(slot.request.kind==2){result.body=executeLuaDiscovery(slot.request.body);result.ok=true;}
    else if(slot.request.kind==1) {
        const auto storage=executeLuaStorage(slot.request.owner,slot.request.method,slot.request.url,slot.request.body);
        result.ok=storage.ok;result.body=storage.value;result.error=storage.error;result.size=storage.size;
    }
    else executeHttp(slot.request,result);
    result.cancelled=slot.cancelled;result.done=true;
    if(result.body.length()>kResponseBytes){result.body="";result.error="response_limit";result.ok=false;}
    if(result.cancelled){if(slot.request.discardResult)slot.request.discardResult(slot.owner,result.body);result.ok=false;result.error="cancelled";result.body="";}
    if(slot.request.kind==0&&slot.request.streamMode!=1)result.size=result.body.length();
    if(slot.request.kind==0){result.ok=!result.error.length()&&result.status>=200&&result.status<300;
        if(!result.ok&&!result.error.length())result.error=result.status==401||result.status==403?"authentication":"http_status";}
    if(!result.ok&&!result.error.length())result.error="operation_failed";
    result.retryable=!result.ok&&!result.cancelled&&slot.request.kind==0&&!slot.request.streamMode&&slot.request.method==0&&
        (result.error=="timeout"||result.error=="http_connect"||result.status==429||result.status>=500);
    result.finishedAt=millis();
    ++completed_;if(result.cancelled)++cancelled_;if(result.error=="timeout")++timeouts_;
    if(result.retryable)++retryable_; // Eligibility only: this worker never replays a command.
    const String owner=slot.request.owner;
    const uint32_t completedId=slot.id;
    const char* eventName=slot.request.kind==5?(slot.request.completion?slot.request.completion:"native.completed"):slot.request.kind==4?"media.updated":slot.request.kind==3?"energy.history":slot.request.kind==2?"discovery.completed":slot.request.kind?"storage.changed":"http.completed";
    xSemaphoreTake(lock_,portMAX_DELAY);
    if(!shared&&!result.cancelled&&slot.credentialRevision==ConfigService::instance().revision()){
        for(auto& follower:slots_)if(&follower!=&slot&&follower.busy&&!follower.sharedReady&&!follower.cancelled&&
           follower.credentialRevision==slot.credentialRevision&&sameRead(slot.request,follower.request)){
            const auto queuedAt=follower.result.queuedAt;
            follower.result=result;follower.result.done=false;follower.result.queuedAt=queuedAt;
            // Keep this slot busy until its own queue entry drains. Release/reuse
            // can never invalidate a queued pointer; each consumer keeps its handle.
            follower.sharedReady=true;
        }
    }
    if(result.processed<slot.result.processed){result.processed=slot.result.processed;result.total=slot.result.total;}
    slot.result=std::move(result);slot.finished=millis();slot.busy=false;
    slot.request.body=""; // Do not keep secret/request payloads in completed slots.
    // Retain bounded results for 60 seconds; explicit cancel releases immediately.
    if(slot.cancelled){slot.id=0;slot.request={};slot.result={};}
    const bool notify=!slot.cancelled;
    xSemaphoreGive(lock_);
    if(notify)publishLuaEvent(owner.c_str(),eventName,completedId);
}
}

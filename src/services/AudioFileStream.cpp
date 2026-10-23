#include "AudioFileStream.h"
#include "FileBrowserService.h"
#include "NetworkWorker.h"
#include "SecurityService.h"
#include "UsbStorageService.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cerrno>
namespace hp {namespace {
struct Source {
    String uri; std::shared_ptr<std::atomic<bool>> canceled;
    esp_http_client_handle_t http=nullptr;
    bool network=false,opened=false;uint64_t position=0,total=0;
    uint32_t usbGeneration=0;bool usb=false;
    std::array<uint8_t,8192> prefix{};size_t prefixSize=0;
    ~Source(){if(http){esp_http_client_close(http);esp_http_client_cleanup(http);}}
};
struct Read {std::shared_ptr<Source> source;uint64_t offset=0;size_t count=0,size=0;
    std::array<uint8_t,8192> bytes{};std::atomic<bool> ready{false},abandoned{false};bool ok=false;};
struct Cookie {std::shared_ptr<Source> source;uint64_t offset=0;};
void readWorker(void* arg){
    std::unique_ptr<std::shared_ptr<Read>> owner(static_cast<std::shared_ptr<Read>*>(arg));
    auto r=*owner;auto& s=*r->source;
    if(r->abandoned||s.canceled->load()||SecurityService::instance().tabletLocked()||(s.usb&&s.usbGeneration!=UsbStorageService::instance().generation())){r->ready=true;return;}
    if(!s.network){r->ok=FileBrowserService::readRange(s.uri,r->offset,r->bytes.data(),r->count,r->size,s.total,s.canceled.get());}
    else {
        if(!s.opened){
            esp_http_client_config_t cfg{};cfg.url=s.uri.c_str();cfg.timeout_ms=2000;
            cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;cfg.buffer_size=4096;
            s.http=esp_http_client_init(&cfg);
            if(s.http){esp_http_client_set_header(s.http,"Accept","audio/mpeg, audio/wav");esp_http_client_set_header(s.http,"Icy-MetaData","0");
                if(esp_http_client_open(s.http,0)==ESP_OK){esp_http_client_fetch_headers(s.http);
                    s.opened=esp_http_client_get_status_code(s.http)==200;}}
        }
        if(s.opened){
            if(r->offset<s.prefixSize){r->size=std::min(r->count,s.prefixSize-size_t(r->offset));memcpy(r->bytes.data(),s.prefix.data()+r->offset,r->size);r->ok=true;}
            else if(r->offset==s.position){
                const int n=esp_http_client_read(s.http,reinterpret_cast<char*>(r->bytes.data()),r->count);
                if(n>=0){r->size=size_t(n);r->ok=true;
                    if(s.position<s.prefix.size()){const size_t retained=std::min(r->size,s.prefix.size()-size_t(s.position));memcpy(s.prefix.data()+s.position,r->bytes.data(),retained);s.prefixSize=s.position+retained;}
                    s.position+=r->size;
                }
            }
        }
    }
    r->ready.store(true,std::memory_order_release);
}
int readCookie(void* ptr,char* bytes,int count){
    auto& c=*static_cast<Cookie*>(ptr);if(count<=0)return 0;
    if(c.source->canceled->load()){errno=ECANCELED;return -1;}
    auto r=std::make_shared<Read>();r->source=c.source;r->offset=c.offset;r->count=std::min(size_t(count),r->bytes.size());
    auto* job=new(std::nothrow) std::shared_ptr<Read>(r);
    if(!job){errno=ENOMEM;return -1;}
    // Small playback reads must not sit behind whole-file uploads/scans on the
    // transfer lane. This is priority queuing, not preemption of active I/O.
    const bool remote=c.source->network||c.source->uri.startsWith("nas");
    const bool queued=remote?NetworkWorker::instance().submitPriority(readWorker,job,"audio-read"):NetworkWorker::instance().submitLocal(readWorker,job,"audio-read",true);
    if(!queued){delete job;errno=EBUSY;return -1;}
    const uint32_t started=millis();
    while(!r->ready.load(std::memory_order_acquire)&&!c.source->canceled->load()&&millis()-started<3500)vTaskDelay(pdMS_TO_TICKS(2));
    if(!r->ready.load(std::memory_order_acquire)||!r->ok){r->abandoned=true;errno=EIO;return -1;}
    memcpy(bytes,r->bytes.data(),r->size);c.offset+=r->size;return int(r->size);
}
fpos_t seekCookie(void* ptr,fpos_t offset,int whence){
    auto& c=*static_cast<Cookie*>(ptr);int64_t target=offset;
    if(whence==SEEK_CUR)target+=c.offset;
    else if(whence==SEEK_END){if(c.source->network){errno=ESPIPE;return -1;}target+=c.source->total;}
    else if(whence!=SEEK_SET)return -1;
    if(target<0||(c.source->network&&uint64_t(target)>c.source->prefixSize&&uint64_t(target)!=c.source->position)){errno=ESPIPE;return -1;}
    c.offset=target;return fpos_t(target);
}
// An in-flight worker owns a shared reference. Closing a decoder FILE can never
// destroy its HTTP handle while that worker is still using it.
int closeCookie(void* ptr){delete static_cast<Cookie*>(ptr);return 0;}
}
FILE* AudioFileStream::open(const String& uri,std::shared_ptr<std::atomic<bool>> canceled){
    if(!canceled)return nullptr;const bool network=uri.startsWith("http://")||uri.startsWith("https://");
    if(uri.length()>512||(!network&&!FileBrowserService::validUri(uri))||uri.indexOf('\r')>=0||uri.indexOf('\n')>=0)return nullptr;
    if(network){const int authority=uri.indexOf("://")+3;int end=uri.indexOf('/',authority);if(end<0)end=uri.length();if(uri.substring(authority,end).indexOf('@')>=0)return nullptr;}
    auto source=std::make_shared<Source>();source->uri=uri;source->network=network;source->canceled=canceled;
    source->usb=uri.startsWith("usb:");source->usbGeneration=UsbStorageService::instance().generation();
    auto* cookie=new(std::nothrow) Cookie{source,0};if(!cookie)return nullptr;
    FILE* file=funopen(cookie,readCookie,nullptr,seekCookie,closeCookie);if(!file){delete cookie;return nullptr;}
    setvbuf(file,nullptr,_IONBF,0);return file;
}
}

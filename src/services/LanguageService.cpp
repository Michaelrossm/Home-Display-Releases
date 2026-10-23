#include "LanguageService.h"
#include "languages/LanguageAssets.h"
#include "languages/LanguageArchive.h"
#include "ConfigService.h"
#include "SecurityService.h"
#include "NetworkWorker.h"
#include "StorageService.h"
#include "UsbAppFileSystem.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
extern "C" {
#include <zlib.h>
}
namespace hp {namespace {
class PackAllocator:public ArduinoJson::Allocator {
    struct alignas(std::max_align_t) Header {size_t size;};size_t used_=0;static constexpr size_t limit=1024*1024;
public:
    void* allocate(size_t n)override{if(n>limit-used_)return nullptr;auto* h=static_cast<Header*>(heap_caps_malloc(sizeof(Header)+n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!h)return nullptr;h->size=n;used_+=n;return h+1;}
    void deallocate(void* p)override{if(!p)return;auto* h=static_cast<Header*>(p)-1;used_-=h->size;heap_caps_free(h);}
    void* reallocate(void* p,size_t n)override{if(!p)return allocate(n);auto* h=static_cast<Header*>(p)-1;if(n>limit-(used_-h->size))return nullptr;const size_t old=h->size;
        auto* next=static_cast<Header*>(heap_caps_realloc(h,sizeof(Header)+n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!next)return nullptr;next->size=n;used_=used_-old+n;return next+1;}
};
bool validText(const char* text,size_t length,size_t limit){if(!text||!length||length>limit||strlen(text)!=length)return false;
    for(size_t i=0;i<length;){const auto c=static_cast<uint8_t>(text[i++]);if(c<32&&c!='\n')return false;if(c<128)continue;
        unsigned need=0;uint32_t value=0,min=0;if(c>=0xc2&&c<=0xdf){need=1;value=c&31;min=0x80;}else if(c>=0xe0&&c<=0xef){need=2;value=c&15;min=0x800;}else if(c>=0xf0&&c<=0xf4){need=3;value=c&7;min=0x10000;}else return false;
        if(need>length-i)return false;while(need--){const auto b=static_cast<uint8_t>(text[i++]);if((b&0xc0)!=0x80)return false;value=(value<<6)|(b&63);}if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }return true;
}
}
struct LanguageService::Pack {
    struct Entry {const char* key;const char* value;};
    PackAllocator allocator;char* bytes=nullptr;JsonDocument document{&allocator};Entry* keys=nullptr,*sources=nullptr;size_t count=0;
    ~Pack(){heap_caps_free(keys);heap_caps_free(sources);/* document dies before its zero-copy backing bytes */}
    const char* find(const char* key)const{auto* entry=std::lower_bound(keys,keys+count,key,[](const Entry& e,const char* k){return strcmp(e.key,k)<0;});return entry!=keys+count&&!strcmp(entry->key,key)?entry->value:nullptr;}
};
struct LanguageService::Change {char locale[16]{};std::atomic<bool> done{false};bool ok=false;};
LanguageService& LanguageService::instance(){static LanguageService s;return s;}
size_t LanguageService::count()const{return sizeof(languageAssets::catalog)/sizeof(languageAssets::catalog[0]);}
const char* LanguageService::id(size_t index)const{return index<count()?languageAssets::catalog[index].id:"";}
const char* LanguageService::name(size_t index)const{return index<count()?languageAssets::catalog[index].name:"";}
bool LanguageService::validId(const char* locale){if(!locale)return false;const size_t n=strlen(locale);if(n<2||n>6)return false;
    size_t i=0;while(i<n&&locale[i]>='a'&&locale[i]<='z')++i;if(i<2||i>3)return false;if(i==n)return true;
    return n==i+3&&locale[i]=='-'&&locale[i+1]>='A'&&locale[i+1]<='Z'&&locale[i+2]>='A'&&locale[i+2]<='Z';}
std::shared_ptr<LanguageService::Pack> LanguageService::load(size_t index){if(index>=instance().count())return {};
    const auto& asset=languageAssets::catalog[index];return decode(asset.zip,asset.size,index);
}
std::shared_ptr<LanguageService::Pack> LanguageService::decode(const uint8_t* zip,size_t size,size_t index){if(index>=instance().count())return {};
    const auto& asset=languageAssets::catalog[index];LanguageArchive archive;if(!archive.parse(zip,size))return {};
    auto* raw=static_cast<char*>(heap_caps_malloc(archive.size+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!raw)return {};
    auto pack=std::shared_ptr<Pack>(new(std::nothrow) Pack,[](Pack* p){if(p){char* buffer=p->bytes;delete p;heap_caps_free(buffer);}});
    if(!pack){heap_caps_free(raw);return {};}pack->bytes=raw;
    z_stream stream{};stream.next_in=const_cast<Bytef*>(archive.compressed);stream.avail_in=archive.compressedSize;stream.next_out=reinterpret_cast<Bytef*>(raw);stream.avail_out=archive.size;
    if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)return {};const int rc=inflate(&stream,Z_FINISH);const bool complete=rc==Z_STREAM_END&&stream.total_out==archive.size&&stream.total_in==archive.compressedSize;inflateEnd(&stream);
    if(!complete||crc32(0,reinterpret_cast<const Bytef*>(raw),archive.size)!=archive.crc)return {};raw[archive.size]=0;
    if(deserializeJson(pack->document,raw,archive.size,DeserializationOption::NestingLimit(3)))return {};
    auto root=pack->document.as<JsonObjectConst>();
    if(root.size()!=5||root["schema"]!=1||!root["locale"].is<const char*>()||strcmp(root["locale"],asset.id)||!root["name"].is<const char*>()||strcmp(root["name"],asset.name)||root["direction"]!="ltr"||!root["messages"].is<JsonObjectConst>())return {};
    auto messages=root["messages"].as<JsonObjectConst>();pack->count=messages.size();if(!pack->count||pack->count>4096)return {};
    pack->keys=static_cast<Pack::Entry*>(heap_caps_malloc(pack->count*sizeof(Pack::Entry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    pack->sources=static_cast<Pack::Entry*>(heap_caps_malloc(pack->count*sizeof(Pack::Entry),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!pack->keys||!pack->sources)return {};
    size_t at=0;for(JsonPairConst pair:messages){const auto key=pair.key();const auto value=pair.value().as<JsonString>();
        if(!validText(key.c_str(),key.size(),96)||!validText(value.c_str(),value.size(),2048))return {};
        if(key.c_str()[0]<'a'||key.c_str()[0]>'z')return {};
        for(size_t j=0;j<key.size();++j){const char c=key.c_str()[j];if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='.'))return {};}
        pack->keys[at]={key.c_str(),value.c_str()};pack->sources[at]={value.c_str(),key.c_str()};++at;
    }
    auto less=[](const Pack::Entry& a,const Pack::Entry& b){return strcmp(a.key,b.key)<0;};std::sort(pack->keys,pack->keys+pack->count,less);std::sort(pack->sources,pack->sources+pack->count,less);return pack;
}
bool LanguageService::begin(){if(ready_)return true;
    size_t fallback=count();for(size_t i=0;i<count();++i)if(!strcmp(id(i),"de-DE"))fallback=i;
    german_=load(fallback);if(!german_)return false;selected_=german_;
    const String wanted=ConfigService::instance().getString("ui_lang","de-DE");strlcpy(requested_,"de-DE",sizeof(requested_));
    if(validId(wanted.c_str()))for(size_t i=0;i<count();++i)if(wanted==id(i)){if(i==fallback){strlcpy(requested_,id(i),sizeof(requested_));break;}auto candidate=load(i);if(candidate){selected_=candidate;strlcpy(activeId_,id(i),sizeof(activeId_));strlcpy(requested_,id(i),sizeof(requested_));}break;}
    ready_=true;return true;
}
void LanguageService::loadExternalAtBoot(){
    if(!ready_)return;
    // Only registered locales can override the embedded catalog. Never scan arbitrary
    // directories or extract files. The trusted embedded German pack remains intact.
    const String wanted=ConfigService::instance().getString("ui_lang","de-DE");
    if(!validId(wanted.c_str()))return;
    for(size_t index=0;index<count();++index){
        if(wanted!=id(index))continue;
        const String path=String("/Sprachen/")+id(index)+".zip";
        for(unsigned medium=0;medium<2;++medium){
            File file=medium==0?StorageService::instance().openSdRead(path):usbAppFileSystem().open(path,FILE_READ);
            if(!file||file.isDirectory())continue;
            const size_t length=file.size();
            if(!length||length>256*1024){file.close();continue;}
            auto* bytes=static_cast<uint8_t*>(heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
            if(!bytes){file.close();return;}
            const size_t got=file.read(bytes,length);file.close();
            auto candidate=got==length?decode(bytes,length,index):std::shared_ptr<Pack>{};
            heap_caps_free(bytes);
            if(!candidate)continue;
            bool complete=candidate->count==german_->count;
            for(size_t k=0;complete&&k<german_->count;++k)complete=candidate->find(german_->keys[k].key)!=nullptr;
            if(!complete)continue;
            selected_=candidate;strlcpy(activeId_,id(index),sizeof(activeId_));strlcpy(requested_,id(index),sizeof(requested_));return;
        }
        return;
    }
}
const char* LanguageService::tr(const char* key,const char* fallback)const{if(!key)return fallback?fallback:"";if(selected_)if(auto* value=selected_->find(key))return value;
    if(german_)if(auto* value=german_->find(key))return value;return fallback?fallback:key;}
const char* LanguageService::text(const char* german)const{if(!german)return "";if(selected_==german_||!german_)return german;
    auto* entry=std::lower_bound(german_->sources,german_->sources+german_->count,german,[](const Pack::Entry& e,const char* k){return strcmp(e.key,k)<0;});
    return entry!=german_->sources+german_->count&&!strcmp(entry->key,german)?tr(entry->value,german):german;}
void LanguageService::saveWorker(void* p){std::unique_ptr<std::shared_ptr<Change>> holder(static_cast<std::shared_ptr<Change>*>(p));auto state=*holder;size_t index=instance().count();for(size_t i=0;i<instance().count();++i)if(!strcmp(instance().id(i),state->locale))index=i;
    auto verified=load(index);if(verified){SecurityWriteGuard guard;if(SecurityService::instance().settingsAllowed()&&!SecurityService::instance().tabletLocked())state->ok=ConfigService::instance().putString("ui_lang",state->locale);}state->done=true;
}
bool LanguageService::request(const char* locale){if(!validId(locale)||change_||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
    bool found=false;for(size_t i=0;i<count();++i)found|=!strcmp(id(i),locale);if(!found)return false;
    if(!strcmp(locale,requested_)){statusKey_="language.unchanged";return true;}
    change_=std::make_shared<Change>();strlcpy(change_->locale,locale,sizeof(change_->locale));auto* job=new(std::nothrow) std::shared_ptr<Change>(change_);
    if(!job||!NetworkWorker::instance().submitLocalTransfer(saveWorker,job,"language-save")){delete job;change_.reset();return false;}statusKey_="language.pending";return true;
}
void LanguageService::pollUi(){if(change_&&change_->done){if(change_->ok)strlcpy(requested_,change_->locale,sizeof(requested_));statusKey_=change_->ok?"language.saved":"language.error";change_.reset();}}
bool LanguageService::busy()const{return bool(change_);}
const char* LanguageService::status()const{return tr(statusKey_,"Spracheinstellung nicht verfügbar. Deutsch bleibt aktiv.");}
String LanguageService::catalogJson(const char* keys)const{JsonDocument out;out["schema"]=1;out["active"]=active();out["restartRequired"]=true;auto list=out["languages"].to<JsonArray>();for(size_t i=0;i<count();++i){auto p=list.add<JsonObject>();p["id"]=id(i);p["name"]=name(i);}
    if(keys){if(strlen(keys)>1551)return "{\"error\":\"language_key_limit\"}";auto messages=out["strings"].to<JsonObject>();size_t count=0;
        while(*keys){const char* end=strchr(keys,',');const size_t n=end?size_t(end-keys):strlen(keys);if(!n||n>96||++count>16)return "{\"error\":\"language_key_limit\"}";
            char key[97]{};memcpy(key,keys,n);for(size_t i=0;i<n;++i)if(!((key[i]>='a'&&key[i]<='z')||(key[i]>='0'&&key[i]<='9')||key[i]=='_'||key[i]=='.'))return "{\"error\":\"language_key_invalid\"}";
            messages[key]=tr(key);if(!end)break;keys=end+1;}
    }if(out.overflowed()||measureJson(out)>32768)return "{\"error\":\"language_response_limit\"}";String body;serializeJson(out,body);return body;}
}

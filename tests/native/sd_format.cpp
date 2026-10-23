#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <cstdio>
#include <cstdlib>
#include "SdFormatConfirmation.h"
class String:public std::string{public:using std::string::string;using std::string::operator=;String(const std::string& s):std::string(s){}String(uint32_t n):std::string(std::to_string(n)){}size_t length()const{return size();}};
const int CARD_NONE=0,FF_VOLUMES=2,FR_OK=0,RES_OK=0,GET_SECTOR_COUNT=1,FM_ANY=7,MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2,ESP_RST_SW=1;
using DWORD=uint32_t;using LBA_t=uint32_t;struct FATFS{uint32_t volbase=0;};struct MKFS_PARM{uint8_t fmt=0;};FATFS volumes[2];
bool ambiguous=false,rawFailure=false,formatFailure=false,mountFailure=false;int formats=0,restarts=0,resetReason=ESP_RST_SW;uint32_t serial=42;
int f_getfree(const char* drive,DWORD*,FATFS** out){*out=&volumes[drive[0]-'0'];return FR_OK;}int disk_ioctl(unsigned,int,LBA_t* out){*out=4000;return RES_OK;}
void sectorImage(uint8_t* out,unsigned sector){memset(out,sector+123,512);if(!sector){out[510]=0x55;out[511]=0xaa;out[11]=0;out[12]=2;memcpy(out+67,&serial,4);}}
int disk_read(unsigned drive,uint8_t* out,unsigned sector,unsigned){if(drive==1||ambiguous)sectorImage(out,sector);else memset(out,0,512);return RES_OK;}
int f_getlabel(const char*,char*,DWORD* out){*out=serial;return FR_OK;}
int f_mount(FATFS* fs,const char* drive,int){assert(std::string(drive)=="1:");return fs&&mountFailure?1:FR_OK;}
int f_mkfs(const char* drive,const MKFS_PARM*,void*,size_t){assert(std::string(drive)=="1:");++formats;return formatFailure?1:FR_OK;}
void* heap_caps_malloc(size_t n,int){return malloc(n);}void heap_caps_free(void* p){free(p);}int esp_reset_reason(){return resetReason;}void esp_restart(){++restarts;}
int mbedtls_sha256(const uint8_t* input,size_t n,uint8_t* out,int){uint32_t hash=2166136261u;for(size_t i=0;i<n;++i)hash=(hash^input[i])*16777619;for(unsigned i=0;i<32;++i){hash=hash*1664525+1013904223;out[i]=hash>>24;}return 0;}
struct Sd{int cardType(){return 1;}int sectorSize(){return 512;}int numSectors(){return 4000;}uint64_t cardSize(){return 4000*512;}uint64_t totalBytes(){return cardSize();}bool readRAW(uint8_t* out,unsigned sector){sectorImage(out,sector);return !rawFailure;}bool mkdir(const char*){return true;}}SD_MMC;
namespace hp {
class ConfigService{public:std::map<std::string,String> data;bool writable=true;uint32_t bootCount=10;uint32_t getU32(const char*){return bootCount;}static ConfigService& instance(){static ConfigService c;return c;}bool has(const char* key){return data.count(key);}String getString(const char* key){return data[key];}bool putString(const char* key,const String& value){if(!writable)return false;data[key]=value;return true;}bool remove(const char* key){if(!writable)return false;data.erase(key);return true;}};
class SecurityService{public:bool allowed=true;static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return allowed;}};struct SecurityWriteGuard{};
class NetworkWorker{public:using Callback=void(*)(void*);Callback cb=nullptr;void* context=nullptr;bool accept=true;static NetworkWorker& instance(){static NetworkWorker w;return w;}bool submit(Callback c,void* ctx,const char*){if(!accept)return false;cb=c;context=ctx;return true;}void drain(){auto c=cb;cb=nullptr;if(c)c(context);}};
class StorageService{public:bool sdReady_=true;unsigned layoutPreparations=0;std::atomic<uint8_t> sdFormatState_{0};static StorageService& instance();const char* sdFormatStatus()const;bool requestSdFormat();static void prepareSdFormat(void*);void applyBootSdFormat(bool);void reportSdMounted(bool ready,uint64_t){sdReady_=ready;}bool prepareExternalDirectories(Sd& fs){assert(&fs==&SD_MMC);++layoutPreparations;return true;}};
#include "format.inc"
}
int main(){using namespace hp;SdFormatConfirmation gate;assert(!gate.confirm(0));gate.begin(1000);assert(!gate.confirm(1100)&&gate.stage(1100)==1);assert(!gate.confirm(2000)&&gate.stage(2000)==2);assert(!gate.confirm(2100));assert(gate.confirm(3000)&&gate.stage(3000)==0);assert(!gate.confirm(4000));gate.begin(0xffffff00u);assert(!gate.confirm(0xffffff00u+1000)&&gate.stage(0xffffff00u+1000)==2);assert(gate.confirm(0xffffff00u+2000));gate.begin(100);assert(!gate.confirm(60100)&&gate.stage(60100)==0);gate.begin(1000);gate.reset();assert(!gate.confirm(3000));
 auto& storage=StorageService::instance();auto& config=ConfigService::instance();auto& security=SecurityService::instance();auto& worker=NetworkWorker::instance();
 security.allowed=false;assert(!storage.requestSdFormat());security.allowed=true;worker.accept=false;assert(!storage.requestSdFormat());worker.accept=true;assert(storage.requestSdFormat());assert(!storage.requestSdFormat()&&formats==0&&restarts==0);worker.drain();assert(formats==0&&restarts==1&&config.has("sd_fmt_req"));
 ++config.bootCount;storage.applyBootSdFormat(true);assert(formats==1&&!config.has("sd_fmt_req")&&storage.layoutPreparations==1);storage.applyBootSdFormat(true);assert(formats==1&&storage.layoutPreparations==1);
 auto arm=[&](){storage.sdFormatState_=0;assert(storage.requestSdFormat());worker.drain();assert(config.has("sd_fmt_req"));++config.bootCount;};
 arm();serial++;storage.applyBootSdFormat(true);assert(formats==1&&!config.has("sd_fmt_req"));
 arm();resetReason=2;storage.applyBootSdFormat(true);assert(formats==1&&!config.has("sd_fmt_req"));resetReason=ESP_RST_SW;
 arm();storage.applyBootSdFormat(false);assert(formats==1&&!config.has("sd_fmt_req"));
 arm();ambiguous=true;storage.applyBootSdFormat(true);assert(formats==1&&!config.has("sd_fmt_req"));ambiguous=false;
 arm();rawFailure=true;storage.applyBootSdFormat(true);assert(formats==1&&!config.has("sd_fmt_req"));rawFailure=false;
 arm();config.writable=false;storage.applyBootSdFormat(true);assert(formats==1);config.writable=true;++config.bootCount;storage.applyBootSdFormat(true);assert(formats==1&&!config.has("sd_fmt_req"));
 arm();formatFailure=true;storage.applyBootSdFormat(true);assert(formats==2&&!config.has("sd_fmt_req")&&storage.sdFormatState_==3);formatFailure=false;storage.applyBootSdFormat(true);assert(formats==2);
 puts("PASS double confirmation, exact SD target, one-shot boot format and fail-closed paths");}

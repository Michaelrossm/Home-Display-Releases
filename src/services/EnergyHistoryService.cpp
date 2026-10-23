#include "EnergyHistoryService.h"
#include "StorageService.h"
#include "TimeService.h"
#include "SettingsService.h"
#include <esp_timer.h>
#include <new>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <algorithm>
namespace hp {
namespace {
class HistoryGuard {
    SemaphoreHandle_t mutex_;
public:
    explicit HistoryGuard(SemaphoreHandle_t mutex):mutex_(mutex){if(mutex_)xSemaphoreTake(mutex_,portMAX_DELAY);}
    ~HistoryGuard(){if(mutex_)xSemaphoreGive(mutex_);}
};
int64_t localMidnight(int64_t epoch) {
    tm local{};if(!TimeService::instance().localFromEpoch(epoch,local))return epoch/86400*86400;
    local.tm_hour=local.tm_min=local.tm_sec=0;local.tm_isdst=-1;return mktime(&local);
}
}
EnergyHistoryService& EnergyHistoryService::instance(){static EnergyHistoryService service;return service;}
bool EnergyHistoryService::begin(){if(!mutex_)return false;if(!statusMutex_)statusMutex_=xSemaphoreCreateMutex();return statusMutex_&&BackgroundScheduler::instance().add(this);}
void EnergyHistoryService::updateDetailStatus(){if(!statusMutex_)return;EnergyDetailStatus value=detail_?detail_->status:detailStatus_;if(detail_){value.bufferPoints=detail_->buffer.count;value.bufferedMinutes=value.bufferPoints*value.intervalMinutes;value.droppedPoints=detail_->buffer.dropped;}xSemaphoreTake(statusMutex_,portMAX_DELAY);detailStatus_=value;xSemaphoreGive(statusMutex_);}
bool EnergyHistoryService::tryDetailStatus(EnergyDetailStatus& out){if(!statusMutex_||xSemaphoreTake(statusMutex_,0)!=pdTRUE)return false;out=detailStatus_;xSemaphoreGive(statusMutex_);return true;}
void EnergyHistoryService::backgroundTick(){if(!mutex_)return;HistoryGuard guard(mutex_);const auto settings=SettingsService::instance().snapshot();
    if(settings.energyDetailHistoryEnabled&&!detail_){void* memory=heap_caps_malloc(sizeof(EnergyDetailArchive),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(memory)detail_=new(memory)EnergyDetailArchive;else {detailStatus_.enabled=true;++detailStatus_.writeErrors;}}
    if(detail_)detail_->maintain(settings.energyDetailHistoryEnabled,settings.energyDetailHistoryIntervalMinutes,settings.energyDetailHistoryRetentionDays,settings.energyDetailHistoryPreferProviderBackfill,historyProvider_);
    else {detailStatus_.enabled=settings.energyDetailHistoryEnabled;detailStatus_.sdAvailable=StorageService::instance().sdAvailable();detailStatus_.intervalMinutes=settings.energyDetailHistoryIntervalMinutes;detailStatus_.retentionDays=settings.energyDetailHistoryRetentionDays;}
    updateDetailStatus();
}
bool EnergyHistoryService::visitDetail(uint32_t from,uint32_t to,const std::function<bool(const DetailRecord&)>& visitor){if(!mutex_||!detail_||!visitor||from>=to)return false;HistoryGuard guard(mutex_);return detail_->visit(from,to,visitor);}
bool EnergyHistoryService::validFile(fs::FS& fs,const String& path) {
    File file=fs.open(path,FILE_READ);if(!file)return false;
    State saved;if(file.read(reinterpret_cast<uint8_t*>(&saved),sizeof(saved))!=sizeof(saved)){file.close();return false;}
    if(saved.magic!=state_.magic||saved.version!=state_.version){file.close();return false;}
    size_t total=0;
    for(unsigned i=0;i<4;++i){if(saved.capacity[i]!=state_.capacity[i]||saved.count[i]>saved.capacity[i]||saved.head[i]>=saved.capacity[i]){file.close();return false;}total+=saved.capacity[i];}
    if(file.size()!=sizeof(State)+total*sizeof(Point)){file.close();return false;}
    const uint32_t expected=saved.checksum;saved.checksum=0;
    uint32_t checksum=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(&saved),sizeof(saved));
    uint8_t buffer[512];
    while(file.available()){const size_t size=file.read(buffer,sizeof(buffer));if(!size){file.close();return false;}checksum=esp_rom_crc32_le(checksum,buffer,size);}
    file.close();return checksum==expected;
}
bool EnergyHistoryService::initialize() {
    if(points_)return true;
    auto& storage=StorageService::instance();const bool sd=storage.usesSd(StorageClass::UserData);
    const uint16_t capacitySd[]={1440,168,366,24},capacityFlash[]={60,48,31,12};size_t total=0;
    for(unsigned i=0;i<4;++i){state_.capacity[i]=sd?capacitySd[i]:capacityFlash[i];total+=state_.capacity[i];}
    points_=static_cast<Point*>(heap_caps_calloc(total,sizeof(Point),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!points_)return false;
    // Atomic writer recovery does not format or delete unrelated data.
    storage.recoverAtomicWrite(StorageArea::UserData,"energy/history.bin",[this](fs::FS& fs,const String& path){return validFile(fs,path);});
    const size_t bytes=sizeof(State)+total*sizeof(Point);
    auto* image=static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(image) {
        if(storage.readBinary(StorageClass::UserData,"energy/history.bin",image,bytes)==bytes) {
            State saved;memcpy(&saved,image,sizeof(saved));bool valid=saved.magic==state_.magic&&saved.version==state_.version;
            for(unsigned i=0;i<4;++i)if(saved.capacity[i]!=state_.capacity[i]||saved.count[i]>saved.capacity[i]||saved.head[i]>=saved.capacity[i])valid=false;
            const uint32_t expected=saved.checksum;saved.checksum=0;
            const uint32_t headerCrc=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(&saved),sizeof(saved));
            if(valid&&expected==esp_rom_crc32_le(headerCrc,image+sizeof(State),bytes-sizeof(State))) {
                saved.checksum=expected;
                state_=saved;memcpy(points_,image+sizeof(State),bytes-sizeof(State));
            }
        }
        heap_caps_free(image);
    }
    lastPersist_=millis();return true;
}
void EnergyHistoryService::aggregate(unsigned tier,const Point& point,int64_t bucket) {
    size_t offset=0;for(unsigned i=0;i<tier;++i)offset+=state_.capacity[i];
    const auto capacity=state_.capacity[tier];auto& count=state_.count[tier];auto& head=state_.head[tier];
    if(count) {
        auto& last=points_[offset+(head+capacity-1)%capacity];
        if(last.timestamp==bucket) {
            for(unsigned i=0;i<7;++i)if(std::isfinite(point.values[i])) {
                if(i>=5||!std::isfinite(last.values[i]))last.values[i]=point.values[i];
                else last.values[i]+=(point.values[i]-last.values[i])/static_cast<float>(last.samples[i]+1);
                if(i<5&&last.samples[i]<65535)++last.samples[i];
            }
            return;
        }
    }
    Point value=point;value.timestamp=bucket;for(unsigned i=0;i<5;++i)value.samples[i]=std::isfinite(value.values[i])?1:0;points_[offset+head]=value;
    head=(head+1)%capacity;if(count<capacity)++count;
}
void EnergyHistoryService::record(const EnergySnapshot& snapshot) {
    if(!mutex_)return;HistoryGuard guard(mutex_);
    if(detail_){detail_->sample(snapshot);updateDetailStatus();}
    if(!snapshot.online||!TimeService::instance().valid()||!initialize())return;
    const int64_t minute=snapshot.timestamp/60*60;if(minute==lastMinute_)return;lastMinute_=minute;
    Point point;point.timestamp=minute;
    const EnergyValue* values[]={&snapshot.gridPower,&snapshot.pvPower,&snapshot.housePower,&snapshot.batteryPower,&snapshot.batterySoc,&snapshot.gridImportTotal,&snapshot.gridExportTotal};
    for(unsigned i=0;i<7;++i)if(values[i]->available())point.values[i]=values[i]->value;
    aggregate(0,point,minute);aggregate(1,point,minute/3600*3600);aggregate(2,point,localMidnight(minute));
    tm local{};TimeService::instance().localFromEpoch(snapshot.timestamp,local);
    // Stable calendar-month key, without inventing 30-day months.
    aggregate(3,point,static_cast<int64_t>(local.tm_year+1900)*12+local.tm_mon);
    if(static_cast<uint32_t>(millis()-lastPersist_)>=1800000){lastPersist_=millis();persist();}
}
void EnergyHistoryService::persist() {
    size_t total=0;for(auto capacity:state_.capacity)total+=capacity;
    const size_t payload=total*sizeof(Point),bytes=sizeof(State)+payload;
    auto* image=static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!image)return;
    state_.checksum=0;
    const uint32_t headerCrc=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(&state_),sizeof(state_));
    state_.checksum=esp_rom_crc32_le(headerCrc,reinterpret_cast<uint8_t*>(points_),payload);
    memcpy(image,&state_,sizeof(State));memcpy(image+sizeof(State),points_,payload);
    StorageService::instance().writeAtomic(StorageArea::UserData,"energy/history.bin",image,bytes,[this](fs::FS& fs,const String& path){return validFile(fs,path);});heap_caps_free(image);
}
String EnergyHistoryService::query(const String& period,size_t maxPoints) {
    if(!mutex_)return "";HistoryGuard guard(mutex_);
    if(!initialize())return "";
    unsigned tier=0;int64_t threshold=0;const int64_t now=TimeService::instance().nowUtc();
    if(period=="live")threshold=now-3600;
    else if(period=="hour")threshold=now-3600;
    else if(period=="today"){tier=1;threshold=localMidnight(now);}
    else if(period=="week"){tier=1;threshold=now-7*86400;}
    else if(period=="month"){tier=2;threshold=now-31*86400;}
    else if(period=="year"){tier=2;threshold=now-366*86400;}
    else return "";
    maxPoints=std::min<size_t>(std::max<size_t>(maxPoints,1),256);
    size_t offset=0;for(unsigned i=0;i<tier;++i)offset+=state_.capacity[i];
    JsonDocument doc;auto array=doc.to<JsonArray>();
    const auto count=state_.count[tier],capacity=state_.capacity[tier],head=state_.head[tier];
    const size_t step=std::max<size_t>(1,(count+maxPoints-1)/maxPoints);
    const char* names[]={"gridPower","pvPower","housePower","batteryPower","batterySoc","gridImportTotal","gridExportTotal"};
    for(size_t i=0;i<count;i+=step) {
        const auto& point=points_[offset+(head+capacity-count+i)%capacity];if(point.timestamp<threshold)continue;
        auto item=array.add<JsonObject>();item["timestamp"]=point.timestamp;
        for(unsigned j=0;j<7;++j)if(std::isfinite(point.values[j]))item[names[j]]=point.values[j];
    }
    String output;serializeJson(doc,output);return output;
}

namespace {
struct DetailJournalHeader {uint32_t magic=0x314a4845,offset=0;DetailBlockHeader block;uint32_t checksum=0;};
bool fileHeaderValid(const DetailFileHeader& h){return h.magic==0x31444845&&h.version==1&&h.recordBytes==sizeof(DetailRecord);}
uint32_t nextLocalDay(uint32_t epoch){tm local{};if(!TimeService::instance().localFromEpoch(epoch,local))return epoch+86400;local.tm_hour=local.tm_min=local.tm_sec=0;++local.tm_mday;local.tm_isdst=-1;const time_t next=mktime(&local);return next>epoch?static_cast<uint32_t>(next):epoch+86400;}
}
bool EnergyDetailArchive::attach(){auto& storage=StorageService::instance();StorageService::ArchiveVolume volume;
 const bool ready=storage.archiveVolume(volume);const bool changed=usb_!=volume.usb||generation_!=volume.generation;
 ownedFs_=std::move(volume.owned);fs_=volume.fs;usb_=volume.usb;generation_=volume.generation;
 status.sdAvailable=ready&&!usb_;status.usbAvailable=ready&&usb_;if(changed)lastInventory_=0;return ready;
}
bool EnergyDetailArchive::parents(const String& path){StorageService::ArchiveVolume volume;volume.fs=fs_;volume.usb=usb_;volume.generation=generation_;return StorageService::instance().archiveParents(volume,path);}
bool EnergyDetailArchive::truncate(const String& path,size_t size){StorageService::ArchiveVolume volume;volume.fs=fs_;volume.usb=usb_;volume.generation=generation_;return StorageService::instance().archiveTruncate(volume,path,size);}
uint64_t EnergyDetailArchive::freeBytes(){StorageService::ArchiveVolume volume;volume.fs=fs_;volume.usb=usb_;volume.generation=generation_;return StorageService::instance().archiveFreeBytes(volume);}
String EnergyDetailArchive::dayPath(uint32_t time)const {tm local{};if(!TimeService::instance().localFromEpoch(time,local))return {};char path[64];snprintf(path,sizeof(path),"energy/detail/%04d/%02d/%04d-%02d-%02d.bin",local.tm_year+1900,local.tm_mon+1,local.tm_year+1900,local.tm_mon+1,local.tm_mday);return path;}
void EnergyDetailArchive::sample(const EnergySnapshot& snapshot){
    if(!status.enabled||!snapshot.online||!TimeService::instance().valid()||snapshot.timestamp<1577836800LL||snapshot.timestamp>UINT32_MAX||static_cast<uint32_t>(millis()-snapshot.updatedMs)>30000)return;
    const uint32_t time=snapshot.timestamp;if(time<=latest_)return;
    const uint16_t seconds=latest_?std::min<uint32_t>(60,time-latest_):3;latest_=time;
    const EnergyValue* fields[]={&snapshot.gridPower,&snapshot.pvPower,&snapshot.housePower,&snapshot.batteryPower,&snapshot.batterySoc,&snapshot.gridImportTotal,&snapshot.gridExportTotal};
    float values[7];uint16_t quality=0;for(unsigned i=0;i<7;++i){values[i]=fields[i]->available()?fields[i]->value:NAN;quality|=static_cast<uint16_t>(fields[i]->quality)<<(i*2);}
    const int64_t start=esp_timer_get_time();buffer.add(time,values,quality,seconds,status.intervalMinutes,false);status.aggregationUs+=esp_timer_get_time()-start;++status.aggregationSamples;
}
bool EnergyDetailArchive::recoverJournal(fs::FS& fs,const String& relative){auto& storage=StorageService::instance();const String path=storage.sdUserDataPath(relative),redo=path+".redo";
    if(!fs.exists(redo))return true;File journal=fs.open(redo,FILE_READ);DetailJournalHeader h;
    if(!journal||journal.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))!=sizeof(h)){if(journal)journal.close();return false;}
    const uint32_t crc=h.checksum;h.checksum=0;
    if(h.magic!=0x314a4845||crc!=detailCrc(&h,sizeof(h))||!detailHeaderValid(h.block)||h.offset<sizeof(DetailFileHeader)||journal.size()!=sizeof(h)+h.block.count*sizeof(DetailRecord)){journal.close();return false;}
    const size_t bytes=h.block.count*sizeof(DetailRecord);bool valid=journal.read(reinterpret_cast<uint8_t*>(scratch_),bytes)==bytes&&detailCrc(scratch_,bytes)==h.block.payloadCrc;journal.close();if(!valid)return false;
    File file=fs.open(path,"r+");DetailFileHeader fileHeader;if(!file)return false;
    valid=file.read(reinterpret_cast<uint8_t*>(&fileHeader),sizeof(fileHeader))==sizeof(fileHeader)&&fileHeaderValid(fileHeader)&&static_cast<uint64_t>(h.offset)+sizeof(h.block)+bytes<=file.size()&&file.seek(h.offset);
    if(valid)valid=file.write(reinterpret_cast<const uint8_t*>(&h.block),sizeof(h.block))==sizeof(h.block)&&file.write(reinterpret_cast<const uint8_t*>(scratch_),bytes)==bytes;
    file.flush();file.close();if(!valid)return false;
    if(!fs.remove(redo))return false;++status.recoveredBlocks;return true;
}
bool EnergyDetailArchive::scan(fs::FS& fs,const String& relative,bool repair){
    indexCount_=0;auto& storage=StorageService::instance();if(repair&&!recoverJournal(fs,relative))return false;
    File file=fs.open(storage.sdUserDataPath(relative),FILE_READ);if(!file)return false;DetailFileHeader fileHeader;
    if(file.read(reinterpret_cast<uint8_t*>(&fileHeader),sizeof(fileHeader))!=sizeof(fileHeader)||!fileHeaderValid(fileHeader)){file.close();return false;}
    size_t validEnd=sizeof(fileHeader);const size_t original=file.size();
    while(file.position()<original){const uint32_t offset=file.position();DetailBlockHeader header;
        if(file.read(reinterpret_cast<uint8_t*>(&header),sizeof(header))!=sizeof(header)||!detailHeaderValid(header))break;
        const size_t bytes=header.count*sizeof(DetailRecord);
        if(file.read(reinterpret_cast<uint8_t*>(scratch_),bytes)!=bytes||detailCrc(scratch_,bytes)!=header.payloadCrc)break;
        if(indexCount_+header.count>2048){file.close();return false;}
        const size_t previous=indexCount_;bool valid=true;
        for(unsigned i=0;i<header.count;++i){auto& r=scratch_[i];if(r.timestamp<1577836800||r.intervalSeconds<60||!r.sourceSeconds||r.sourceSeconds>3600||r.intervalSeconds>3600||(r.historyFields&~0x7fu)||(r.quality&0xc000u)||dayPath(r.timestamp)!=relative){valid=false;break;}
            for(unsigned field=0;field<7;++field){const unsigned quality=(r.quality>>(field*2))&3;if(quality==3||std::isinf(r.values[field])||(!quality&&std::isfinite(r.values[field])))valid=false;}
            if(!valid)break;
            for(size_t j=0;j<indexCount_;++j)if(index_[j].time==r.timestamp){valid=false;break;}
            if(!valid)break;index_[indexCount_++]={r.timestamp,offset,static_cast<uint16_t>(i)};
        }
        if(!valid){indexCount_=previous;break;}validEnd=file.position();
    }
    file.close();if(validEnd!=original&&repair){if(!truncate(relative,validEnd))return false;++status.recoveredBlocks;status.bytesUsed-=std::min<uint64_t>(status.bytesUsed,original-validEnd);}return true;
}
bool EnergyDetailArchive::writeBlock(fs::FS& fs,const String& relative,uint32_t offset,const DetailBlockHeader& header){
    auto& storage=StorageService::instance();const String path=storage.sdUserDataPath(relative);const String redo=path+".redo",temp=redo+".tmp";if(fs.exists(redo))return false;
    DetailJournalHeader h;h.offset=offset;h.block=header;h.checksum=detailCrc(&h,sizeof(h));const size_t bytes=header.count*sizeof(DetailRecord);
    File journal=fs.open(temp,FILE_WRITE);if(!journal)return false;const bool written=journal.write(reinterpret_cast<const uint8_t*>(&h),sizeof(h))==sizeof(h)&&journal.write(reinterpret_cast<const uint8_t*>(scratch_),bytes)==bytes;journal.flush();journal.close();
    if(!written||!fs.rename(temp,redo))return false;
    // Replaying this same bounded block is safe after an interrupted in-place update.
    if(!recoverJournal(fs,relative))return false;--status.recoveredBlocks;status.flushBytes+=sizeof(h)+sizeof(header)+bytes*2;return true;
}
bool EnergyDetailArchive::flushDay(fs::FS& fs,const String& relative,uint32_t now){auto& storage=StorageService::instance();const String path=storage.sdUserDataPath(relative);
    if(!fs.exists(path)){if(!parents(relative))return false;const String temp=path+".new";File file=fs.open(temp,FILE_WRITE);if(!file)return false;DetailFileHeader h;const bool written=file.write(reinterpret_cast<const uint8_t*>(&h),sizeof(h))==sizeof(h);file.flush();file.close();if(!written||!fs.rename(temp,path))return false;status.bytesUsed+=sizeof(h);++status.storedDays;}
    if(!scan(fs,relative,true))return false;
    uint32_t patched[96]{};size_t patchedCount=0,appendCount=0;
    for(size_t i=0;i<buffer.count;++i){const auto& incoming=buffer.slots[i].record;if(static_cast<uint64_t>(incoming.timestamp)+incoming.intervalSeconds>now||dayPath(incoming.timestamp)!=relative)continue;
        const Index* found=nullptr;for(size_t j=0;j<indexCount_;++j)if(index_[j].time==incoming.timestamp){found=&index_[j];break;}
        if(!found){append_[appendCount++]=incoming;continue;}
        bool done=false;for(size_t j=0;j<patchedCount;++j)if(patched[j]==found->offset)done=true;if(done)continue;patched[patchedCount++]=found->offset;
        File file=fs.open(path,FILE_READ);DetailBlockHeader header;if(!file||!file.seek(found->offset)||file.read(reinterpret_cast<uint8_t*>(&header),sizeof(header))!=sizeof(header)||!detailHeaderValid(header)){if(file)file.close();return false;}
        const size_t bytes=header.count*sizeof(DetailRecord);const bool read=file.read(reinterpret_cast<uint8_t*>(scratch_),bytes)==bytes;file.close();if(!read||detailCrc(scratch_,bytes)!=header.payloadCrc)return false;
        bool changed=false;
        for(size_t k=0;k<buffer.count;++k){const auto& replacement=buffer.slots[k].record;if(static_cast<uint64_t>(replacement.timestamp)+replacement.intervalSeconds>now)continue;for(unsigned j=0;j<header.count;++j)if(scratch_[j].timestamp==replacement.timestamp)changed=detailMerge(scratch_[j],replacement)||changed;}
        if(changed){detailSeal(header,scratch_,header.count);if(!writeBlock(fs,relative,found->offset,header))return false;}
    }
    if(appendCount){DetailBlockHeader header;detailSeal(header,append_,appendCount);const size_t bytes=appendCount*sizeof(DetailRecord);File file=fs.open(path,FILE_APPEND);if(!file)return false;
        const bool written=file.write(reinterpret_cast<const uint8_t*>(&header),sizeof(header))==sizeof(header)&&file.write(reinterpret_cast<const uint8_t*>(append_),bytes)==bytes;file.flush();file.close();if(!written)return false;status.bytesUsed+=sizeof(header)+bytes;status.flushBytes+=sizeof(header)+bytes;}
    for(size_t i=0;i<buffer.count;){const auto& r=buffer.slots[i].record;if(static_cast<uint64_t>(r.timestamp)+r.intervalSeconds<=now&&dayPath(r.timestamp)==relative)buffer.erase(i);else ++i;}
    return true;
}
void EnergyDetailArchive::inventory(fs::FS& fs,uint32_t now){auto& storage=StorageService::instance();File root=fs.open(storage.sdUserDataPath("energy/detail"),FILE_READ);if(!root||!root.isDirectory()){if(root)root.close();return;}
    uint32_t cutoff=0;if(status.retentionDays){tm local{};if(TimeService::instance().localFromEpoch(now,local)){local.tm_hour=local.tm_min=local.tm_sec=0;local.tm_mday-=status.retentionDays;local.tm_isdst=-1;const time_t epoch=mktime(&local);if(epoch>0)cutoff=epoch;}}
    uint32_t days=0;uint64_t bytes=0;
    for(File year=root.openNextFile();year;year=root.openNextFile()){if(!year.isDirectory()){year.close();continue;}
        for(File month=year.openNextFile();month;month=year.openNextFile()){if(!month.isDirectory()){month.close();continue;}
            for(File day=month.openNextFile();day;day=month.openNextFile()){if(day.isDirectory()){day.close();continue;}String name=day.name();const int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);
                int y=0,m=0,d=0;if(name.length()!=14||sscanf(name.c_str(),"%4d-%2d-%2d.bin",&y,&m,&d)!=3||y<2020||y>2106||m<1||m>12||d<1||d>31){day.close();continue;}
                DetailFileHeader h;const bool owned=day.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))==sizeof(h)&&fileHeaderValid(h);const size_t size=day.size();String path=day.path();day.close();if(!owned)continue;
                tm local{};local.tm_year=y-1900;local.tm_mon=m-1;local.tm_mday=d;local.tm_isdst=-1;const time_t epoch=mktime(&local);
                if(epoch<=0||dayPath(epoch).length()==0||storage.sdUserDataPath(dayPath(epoch))!=path)continue;
                if(cutoff&&static_cast<uint64_t>(epoch)<cutoff){if(fs.remove(path)){fs.remove(path+".redo");continue;}++status.writeErrors;}
                ++days;bytes+=size;
            }month.close();
        }year.close();
    }root.close();status.storedDays=days;status.bytesUsed=bytes;
}
void EnergyDetailArchive::maintain(bool enabled,uint8_t interval,uint16_t retention,bool preferBackfill,EnergyHistoryProvider* provider){
    const bool previousSd=status.sdAvailable;
    status.enabled=enabled;status.intervalMinutes=detailInterval(interval);status.retentionDays=retention;attach();preferBackfill_=preferBackfill;
    if(!previousSd&&status.sdAvailable)lastInventory_=0;
    status.backfillSupported=provider&&provider->supportsHistoryBackfill();strlcpy(status.backfillSource,status.backfillSupported?provider->historySource():"",sizeof(status.backfillSource));
    if(!enabled)return;if(!started_){started_=true;lastAttempt_=millis();}
    if(!TimeService::instance().valid())return;const time_t epoch=TimeService::instance().nowUtc();if(epoch<1577836800LL||static_cast<uint64_t>(epoch)>UINT32_MAX)return;const uint32_t now=epoch;
    auto* fs=fs_;if(!fs)return;
    if(!lastInventory_){inventory(*fs,now);lastInventory_=now;status.freeBytes=freeBytes();status.lowSpace=status.freeBytes<16*1024*1024;}
    if(static_cast<uint32_t>(millis()-lastAttempt_)<1800000)return;lastAttempt_=millis();const int64_t start=esp_timer_get_time();
    if(preferBackfill_&&status.backfillSupported){bool accepted=false;const bool fetched=provider->fetchHistory(now-2400,now,status.intervalMinutes*60,[&](const EnergyHistoryPoint& p){
            if(p.timestamp<static_cast<int64_t>(now)-2400||p.timestamp>now||!p.resolutionSeconds||p.resolutionSeconds>3600)return true;
            const int64_t sampleStart=esp_timer_get_time();buffer.add(static_cast<uint32_t>(p.timestamp),p.values,p.quality,p.resolutionSeconds,status.intervalMinutes,true);status.aggregationUs+=esp_timer_get_time()-sampleStart;++status.aggregationSamples;accepted=true;return true;});
        if(fetched&&accepted)status.lastBackfill=now;
    }
    uint32_t handled[96]{};size_t handledCount=0;bool ok=true,flushed=false;
    for(size_t i=0;i<buffer.count;){const auto& r=buffer.slots[i].record;if(static_cast<uint64_t>(r.timestamp)+r.intervalSeconds>now){++i;continue;}const String path=dayPath(r.timestamp);if(!path.length()){++i;continue;}
        tm local{};if(!TimeService::instance().localFromEpoch(r.timestamp,local)){++i;continue;}const uint32_t dayKey=(local.tm_year+1900)*512+local.tm_yday;
        bool done=false;for(size_t j=0;j<handledCount;++j)if(handled[j]==dayKey)done=true;if(done){++i;continue;}handled[handledCount++]=dayKey;
        if(!flushDay(*fs,path,now)){++status.writeErrors;ok=false;++i;}else {flushed=true;i=0;}
    }
    if(ok&&flushed){status.lastFlush=now;++status.flushes;}
    status.maxFlushUs=std::max<uint32_t>(status.maxFlushUs,std::min<int64_t>(UINT32_MAX,esp_timer_get_time()-start));
    if(status.retentionDays||now-lastInventory_>=86400){inventory(*fs,now);lastInventory_=now;}
    status.freeBytes=freeBytes();status.lowSpace=status.freeBytes<16*1024*1024;
}
bool EnergyDetailArchive::visit(uint32_t from,uint32_t to,const std::function<bool(const DetailRecord&)>& visitor){if(!attach())return false;auto* fs=fs_;
    for(uint32_t day=from;day<to;day=nextLocalDay(day)){const String relative=dayPath(day);const String path=StorageService::instance().sdUserDataPath(relative);if(!fs->exists(path))continue;if(!scan(*fs,relative,false))return false;
        std::sort(index_,index_+indexCount_,[](const Index& a,const Index& b){return a.time<b.time;});File file=fs->open(path,FILE_READ);if(!file)return false;
        for(size_t i=0;i<indexCount_;++i){const auto& item=index_[i];if(item.time<from||item.time>=to)continue;DetailRecord record;if(!file.seek(item.offset+sizeof(DetailBlockHeader)+item.slot*sizeof(DetailRecord))||file.read(reinterpret_cast<uint8_t*>(&record),sizeof(record))!=sizeof(record)){file.close();return false;}if(!visitor(record)){file.close();return true;}}
        file.close();
    }return true;
}
}

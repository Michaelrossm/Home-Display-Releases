#include "EventLog.h"
#include "../services/StorageService.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <ctime>
namespace hp {
static constexpr char kPath[]="/events.bin";
EventLog& EventLog::instance(){static EventLog e;return e;}
uint32_t EventLog::checksum(const Header&h)const{return h.magic^h.writeIndex^h.count^h.generation^0xE71A49C3u;}
bool EventLog::valid(const Header&h)const{return h.magic==kMagic&&h.writeIndex<kCapacity&&h.count<=kCapacity&&h.checksum==checksum(h);}
bool EventLog::validRecord(const Record&r)const{return memchr(r.level,0,sizeof(r.level))&&memchr(r.code,0,sizeof(r.code))&&memchr(r.message,0,sizeof(r.message));}
void EventLog::appendRam(const Record&r){if(!records_)return;portENTER_CRITICAL(&ringMux_);records_[writeIndex_]=r;writeIndex_=(writeIndex_+1)%kCapacity;count_=std::min<uint32_t>(count_+1,kCapacity);portEXIT_CRITICAL(&ringMux_);}
bool EventLog::initializeFile(){auto*fs=StorageService::instance().fsFor(StorageArea::Recovery);if(!fs)return false;const String p=StorageService::instance().pathFor(StorageArea::Recovery,kPath);StorageService::instance().ensureParentDirs(StorageArea::Recovery,kPath);fileHeader_={kMagic,0,0,0,0};fileHeader_.checksum=checksum(fileHeader_);File f=fs->open(p,FILE_WRITE);if(!f)return false;bool ok=f.write((const uint8_t*)&fileHeader_,sizeof(fileHeader_))==sizeof(fileHeader_);ok&=f.write((const uint8_t*)&fileHeader_,sizeof(fileHeader_))==sizeof(fileHeader_);f.flush();f.close();return ok;}
bool EventLog::loadPersistent(){auto*fs=StorageService::instance().fsFor(StorageArea::Recovery);if(!fs)return false;const String p=StorageService::instance().pathFor(StorageArea::Recovery,kPath);File f=fs->open(p,FILE_READ);if(f&&f.size()>=2*sizeof(Header)){Header c[2];bool rd=f.read((uint8_t*)&c[0],sizeof(Header))==sizeof(Header)&&f.read((uint8_t*)&c[1],sizeof(Header))==sizeof(Header);if(rd&&(valid(c[0])||valid(c[1]))){if(!valid(c[0]))fileHeader_=c[1];else if(!valid(c[1]))fileHeader_=c[0];else fileHeader_=(int32_t)(c[0].generation-c[1].generation)>=0?c[0]:c[1];size_t need=2*sizeof(Header)+(size_t)std::min<uint32_t>(fileHeader_.count,kCapacity)*sizeof(Record);if(f.size()<need){f.close();return false;}uint32_t first=fileHeader_.count<kCapacity?0:fileHeader_.writeIndex;for(uint32_t i=0;i<fileHeader_.count;++i){uint32_t slot=(first+i)%kCapacity;if(!f.seek(2*sizeof(Header)+slot*sizeof(Record),SeekSet)){f.close();return false;}Record r{};if(f.read((uint8_t*)&r,sizeof(r))!=sizeof(r)){f.close();return false;}if(validRecord(r))appendRam(r);}f.close();return true;}}if(f)f.close();return initializeFile();}
bool EventLog::begin(bool persistent){if(!fileLock_)fileLock_=xSemaphoreCreateMutex();if(!fileLock_)return false;if(!records_){records_=(Record*)heap_caps_calloc(kCapacity,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!records_)records_=(Record*)calloc(kCapacity,sizeof(Record));}if(!records_)return false;writeIndex_=count_=0;persistent_=persistent;ready_=persistent?loadPersistent():true;return ready_;}
bool EventLog::setPersistence(bool p){if(!ready_||!fileLock_||xSemaphoreTake(fileLock_,0)!=pdTRUE)return false;if(p==persistent_){xSemaphoreGive(fileLock_);return true;}if(pending_)xQueueReset(pending_);const bool ok=!p||initializeFile();if(ok)persistent_=p;xSemaphoreGive(fileLock_);return ok;}
bool EventLog::deferPersistence(){
 if(pending_)return true;if(!ready_)return false;
 const auto queue=xQueueCreate(16,sizeof(Record));if(!queue)return false;
 if(BackgroundScheduler::instance().add(this)){pending_.store(queue,std::memory_order_release);return true;}
 vQueueDelete(queue);return false;
}
void EventLog::backgroundTick(){
 if(!pending_||!persistent_||!fileLock_||xSemaphoreTake(fileLock_,0)!=pdTRUE)return;
 Record r{};
 // Bound a logging burst so radio/energy jobs retain worker turns.
 for(unsigned i=0;i<2&&xQueueReceive(pending_,&r,0)==pdTRUE;++i)
  if(!writePersistent(r))persistenceFailures_.fetch_add(1);
 xSemaphoreGive(fileLock_);
}
bool EventLog::add(const char*level,const char*code,const String&m){return record(level,code,m,true);}
bool EventLog::addTransient(const char*level,const char*code,const String&m){return record(level,code,m,false);}
bool EventLog::record(const char*level,const char*code,const String&m,bool durable){if(!ready_)return false;Record r{};r.timestamp=time(nullptr);r.uptimeSeconds=millis()/1000;strlcpy(r.level,level?level:"INFO",sizeof(r.level));strlcpy(r.code,code?code:"EVENT",sizeof(r.code));strlcpy(r.message,m.c_str(),sizeof(r.message));appendRam(r);if(!durable||!persistent_)return true;
 if(pending_){if(xQueueSend(pending_,&r,0)==pdTRUE)return true;persistenceDrops_.fetch_add(1);return false;}
 // Before UI/tasks start, preserve the existing synchronous boot diagnostics.
 if(!fileLock_||xSemaphoreTake(fileLock_,portMAX_DELAY)!=pdTRUE)return false;
 const bool ok=writePersistent(r);xSemaphoreGive(fileLock_);return ok;
}
bool EventLog::writePersistent(const Record&r){auto*fs=StorageService::instance().fsFor(StorageArea::Recovery);if(!fs)return false;File f=fs->open(StorageService::instance().pathFor(StorageArea::Recovery,kPath),"r+");if(!f)return false;size_t off=2*sizeof(Header)+fileHeader_.writeIndex*sizeof(Record);if(!f.seek(off,SeekSet)||f.write((const uint8_t*)&r,sizeof(r))!=sizeof(r)){f.close();return false;}f.flush();Header next=fileHeader_;next.writeIndex=(next.writeIndex+1)%kCapacity;next.count=std::min<uint32_t>(next.count+1,kCapacity);++next.generation;next.checksum=checksum(next);size_t ho=(next.generation&1u)?sizeof(Header):0;bool ok=f.seek(ho,SeekSet)&&f.write((const uint8_t*)&next,sizeof(next))==sizeof(next);f.flush();f.close();if(ok)fileHeader_=next;return ok;}
size_t EventLog::count()const{portENTER_CRITICAL(&ringMux_);const auto n=count_;portEXIT_CRITICAL(&ringMux_);return n;}
bool EventLog::forEach(const Callback&cb){if(!ready_||!records_)return false;portENTER_CRITICAL(&ringMux_);const uint32_t n=count_,first=n<kCapacity?0:writeIndex_;portEXIT_CRITICAL(&ringMux_);for(uint32_t i=0;i<n;++i){Record r{};portENTER_CRITICAL(&ringMux_);r=records_[(first+i)%kCapacity];portEXIT_CRITICAL(&ringMux_);if(validRecord(r)&&!cb(r))break;}return true;}
String EventLog::recentText(size_t maxItems, bool warningsAndErrorsOnly) {
    String out;
    if (!ready_ || !records_ || !maxItems) return out;
    size_t emitted = 0;
    portENTER_CRITICAL(&ringMux_);const uint32_t n=count_,head=writeIndex_;portEXIT_CRITICAL(&ringMux_);
    for (uint32_t back = 0; back < n && emitted < maxItems; ++back) {
        const uint32_t index = (head + kCapacity - 1 - back) % kCapacity;
        Record r{};portENTER_CRITICAL(&ringMux_);r=records_[index];portEXIT_CRITICAL(&ringMux_);
        if (!validRecord(r)) continue;
        const bool important =
            strcmp(r.level, "WARN") == 0 ||
            strcmp(r.level, "ERROR") == 0;
        if (warningsAndErrorsOnly && !important) continue;
        if (out.length()) out += "\n";
        out += String(r.level) + " | " + r.code + " | " + r.message;
        ++emitted;
    }
    return out;
}

uint32_t EventLog::warningCount() {
    uint32_t result = 0;
    forEach([&result](const Record& r) {
        if (strcmp(r.level, "WARN") == 0) ++result;
        return true;
    });
    return result;
}

uint32_t EventLog::errorCount() {
    uint32_t result = 0;
    forEach([&result](const Record& r) {
        if (strcmp(r.level, "ERROR") == 0) ++result;
        return true;
    });
    return result;
}

bool EventLog::clear(){if(!ready_||!fileLock_||xSemaphoreTake(fileLock_,0)!=pdTRUE)return false;if(pending_)xQueueReset(pending_);portENTER_CRITICAL(&ringMux_);writeIndex_=count_=0;portEXIT_CRITICAL(&ringMux_);auto*fs=StorageService::instance().fsFor(StorageArea::Recovery);if(fs)fs->remove(StorageService::instance().pathFor(StorageArea::Recovery,kPath));const bool ok=!persistent_||initializeFile();xSemaphoreGive(fileLock_);return ok;}
}

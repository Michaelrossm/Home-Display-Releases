#pragma once
#include "EnergyDetailCore.h"
#include "EnergyModel.h"
#include <Arduino.h>
#include <FS.h>
#include <memory>
namespace hp {
struct EnergyDetailStatus {
    bool enabled=false,sdAvailable=false,usbAvailable=false,backfillSupported=false,lowSpace=false;
    uint8_t intervalMinutes=5;uint16_t retentionDays=0;
    uint32_t bufferPoints=0,bufferedMinutes=0,lastFlush=0,lastBackfill=0,storedDays=0;
    uint64_t bytesUsed=0,flushBytes=0,aggregationUs=0,freeBytes=0;
    uint32_t droppedPoints=0,writeErrors=0,recoveredBlocks=0,flushes=0,aggregationSamples=0,maxFlushUs=0;
    char backfillSource[32]{};
};
// This entire instance, including bounded scratch/index storage, lives in PSRAM.
class EnergyDetailArchive {
public:
    DetailBuffer buffer;
    EnergyDetailStatus status;
    void sample(const EnergySnapshot& snapshot);
    void maintain(bool enabled,uint8_t interval,uint16_t retention,bool preferBackfill,EnergyHistoryProvider* provider);
    bool visit(uint32_t from,uint32_t to,const std::function<bool(const DetailRecord&)>& visitor);
private:
    fs::FS* fs_=nullptr;std::unique_ptr<fs::FS> ownedFs_;bool usb_=false;uint32_t generation_=0;
    bool attach();bool parents(const String&);bool truncate(const String&,size_t);uint64_t freeBytes();
    struct Index {uint32_t time=0,offset=0;uint16_t slot=0;};
    Index index_[2048]{};size_t indexCount_=0;
    DetailRecord scratch_[96]{},append_[96]{};
    uint32_t lastAttempt_=0,lastInventory_=0;bool started_=false,preferBackfill_=true;
    uint32_t latest_=0;
    String dayPath(uint32_t time)const;
    bool recoverJournal(fs::FS& fs,const String& relative);
    bool scan(fs::FS& fs,const String& relative,bool repair);
    bool writeBlock(fs::FS& fs,const String& relative,uint32_t offset,const DetailBlockHeader& header);
    bool flushDay(fs::FS& fs,const String& relative,uint32_t now);
    void inventory(fs::FS& fs,uint32_t now);
};
}

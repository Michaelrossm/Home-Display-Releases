#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <functional>
#include <atomic>
#include <mutex>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "StorageUsage.h"
namespace hp {
enum class StorageClass:uint8_t{SystemData,AppPackage,UserData,Media,Logs,VoiceModels,UsbAppPackage};
enum class StorageArea:uint8_t{UserData,AppData,SystemData,Cache,Config,Recovery};
class StorageService{
public:
 struct ArchiveVolume {fs::FS* fs=nullptr;std::unique_ptr<fs::FS> owned;bool usb=false;uint32_t generation=0;};
 bool archiveVolume(ArchiveVolume&); // Worker-only, external media only, SD then USB.
 bool archiveVolumeValid(const ArchiveVolume&)const;
 bool archiveParents(ArchiveVolume&,const String& relative);
 bool archiveTruncate(ArchiveVolume&,const String& relative,size_t);
 uint64_t archiveFreeBytes(const ArchiveVolume&);
 using Validator=std::function<bool(fs::FS&,const String&)>;
 static StorageService& instance();
 static std::mutex& mutationMutex(); // Shared native/Web file mutations; never held by UI callbacks.
 uint32_t contentRevision()const{return contentRevision_.load();}
 void notifyMutation(){++contentRevision_;} // Also for generation-bound native FS transactions.
 bool begin();
 bool prepareExternalDirectories(fs::FS&); // Native mount workers only; never formats/overwrites.
 bool internalReady()const{return internalReady_;} bool sdAvailable()const{return sdReady_;} uint64_t sdBytes()const{return sdBytes_;}
 bool appFsAvailable()const{return appFsReady_;}
 StorageClass newAppStorage()const; // Reads legacy SD preference when unset.
 bool setNewAppStorage(StorageClass);
 bool canStore(StorageClass)const; bool usesSd(StorageClass)const; fs::FS* fsFor(StorageClass); String pathFor(StorageClass,const String&)const;
 fs::FS* fsFor(StorageArea); String pathFor(StorageArea,const String&)const;
 bool validateRelativePath(const String&)const; bool ensureParentDirs(StorageClass,const String&); bool ensureParentDirs(StorageArea,const String&);
 bool read(StorageArea,const String&,void*,size_t,size_t*actual=nullptr); bool write(StorageArea,const String&,const void*,size_t);
 bool writeAtomic(StorageArea,const String&,const void*,size_t,const Validator& validator=Validator());
 bool recoverAtomicWrite(StorageArea,const String&,const Validator& validator=Validator());
 bool remove(StorageArea,const String&); bool removeTree(StorageArea,const String&); bool rename(StorageArea,const String&,const String&); bool exists(StorageArea,const String&);
 bool writeTextAtomic(StorageClass,const String&,const String&); bool writeBinaryAtomic(StorageClass,const String&,const void*,size_t);
 size_t readBinary(StorageClass,const String&,void*,size_t); bool exists(StorageClass,const String&); bool sdAbsoluteExists(const String&);
 File openSdRead(const String&); File openSdDirectory(const String&);
 bool ensureSdDirectory(const String&); // Native service only, never formats storage.
 // Native detail archives must never use the UserData internal fallback.
 fs::FS* sdUserDataFs();
 uint64_t sdFreeBytes(); // Worker only.
 bool requestUsageReport(); // Existing worker; no filesystem scans on LVGL.
 String usageReport();
 bool usageSnapshot(StorageUsageSnapshot&);
 bool usageBusy()const{return usagePending_.load();}
 String sdUserDataPath(const String&) const;
 bool ensureSdUserDataParents(const String&);
 bool truncateSdUserData(const String&,size_t);
 bool touchSdUserData(const String&); // Worker-only cache recency, no file content changes.
 void reportSdMounted(bool,uint64_t capacityBytes=0);
 bool formatSystemDataForRecovery();
 bool requestSdFormat(); // Only after two native UI confirmations; prepare on existing worker.
 void applyBootSdFormat(bool bootHealthy); // Before app/media/archive workers start.
 const char* sdFormatStatus()const;
    bool suspendInternalForUpdate();
    bool resumeInternalAfterUpdate();
 uint32_t readErrors()const{return readErrors_;} uint32_t writeErrors()const{return writeErrors_;}
    uint32_t maxReadUs() const { return maxReadUs_; } uint32_t maxWriteUs() const { return maxWriteUs_; }
private:
 std::atomic<uint32_t> contentRevision_{1};
 bool mountInternalWithoutDestructiveRecovery(); bool mountAppFs(); bool partitionLooksBlank(const char* label)const; String normalize(StorageArea,const String&)const;
 bool internalReady_=false,sdReady_=false,appFsReady_=false;uint64_t sdBytes_=0;uint32_t readErrors_=0,writeErrors_=0,maxReadUs_=0,maxWriteUs_=0;
 fs::LittleFSFS appFs_;
 std::atomic<uint8_t> sdFormatState_{0};
 static void prepareSdFormat(void*);
 static void collectUsage(void*);
 SemaphoreHandle_t usageMutex_ = nullptr;
 String usageReport_;
 StorageUsageSnapshot usageSnapshot_;
 std::atomic<bool> usagePending_{false};
};
}

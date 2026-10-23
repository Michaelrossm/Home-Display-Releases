#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

namespace hp {

enum class NasProtocol : uint8_t { WebDav, Dlna, Smb2 };

struct NasProfile {
    uint32_t id = 0;
    NasProtocol protocol = NasProtocol::WebDav;
    bool enabled = true;
    char name[40]{};
    char host[96]{};
    uint16_t port = 0;
    char path[128]{};
    char username[64]{};
};

struct NasBrowseEntry {
    char name[96]{};
    char path[192]{}; // Relative path within the configured share.
    uint64_t size = 0;
    bool directory = false;
};

struct NasBrowseState {
    bool busy = false;
    bool success = false;
    uint32_t generation = 0;
    uint32_t profileId = 0;
    uint8_t count = 0;
    char message[160]{};
    char folder[192]{};
};


class NasService {
public:
    struct FileInfo {String name;uint64_t size=0,modified=0;bool directory=false;};
    using Visitor=bool(*)(const FileInfo&,void*);
    // Worker-only shared provider operations; never expose credentials to apps.
    bool visit(uint32_t id,const String& folder,Visitor visitor,void* context,const std::atomic<bool>* cancel=nullptr);
    bool readFile(uint32_t id,const String& path,uint8_t* bytes,size_t capacity,size_t& size,const std::atomic<bool>* cancel=nullptr);
    bool readRange(uint32_t id,const String& path,uint64_t offset,uint8_t* bytes,size_t capacity,size_t& size,uint64_t& total,const std::atomic<bool>* cancel=nullptr);
    bool statFile(uint32_t id,const String& path,FileInfo& info);
    bool copyProfile(size_t index,NasProfile& out)const;
    int providerStatus(uint32_t id)const; // -1 unknown, 0 offline, 1 confirmed online.
    void reportProvider(uint32_t id,bool online);
    static NasService& instance();
    uint32_t contentRevision()const{return contentRevision_.load();}
    bool begin();

    bool addProfile(const NasProfile& profile, const String& password);
    bool removeProfile(size_t index);
    size_t profileCount() const { return profileCount_; }
    const NasProfile* profile(size_t i) const { return i < profileCount_ ? &profiles_[i] : nullptr; }

    // SMB operations run on a dedicated worker; UI only reads snapshots.
    bool startSmbBrowse(size_t profileIndex, const String& folder = String());
    bool startSmbDownload(uint8_t entryIndex,uint32_t expectedProfileId=0,const String& expectedFolder=String());
    NasBrowseState browseState() const;
    bool browseEntry(uint8_t entryIndex, NasBrowseEntry& out) const;

private:
    std::atomic<uint32_t> contentRevision_{1};
    bool load();
    bool save();

    struct NasJob;
    static void smbWorkerThunk(void* arg);
    void runSmbJob(NasJob& job);
    void finishSmbJob(bool ok, const String& message);
    static constexpr size_t kMaxBrowseEntries = 80;
    mutable SemaphoreHandle_t browseLock_ = nullptr;
    NasBrowseState browseState_{};
    NasBrowseEntry* browseEntries_ = nullptr;
    uint8_t browseProfile_ = 0;
    static constexpr size_t kMaxProfiles = 8;
    NasProfile* profiles_ = nullptr;
    size_t profileCount_ = 0;
    SemaphoreHandle_t lock_ = nullptr;
    mutable portMUX_TYPE profilesMux_=portMUX_INITIALIZER_UNLOCKED;
    uint8_t providerKnown_=0,providerOnline_=0;
};

}

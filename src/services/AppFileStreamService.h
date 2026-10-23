#pragma once
#include <Arduino.h>
#include <FS.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
namespace hp {
// All filesystem methods are worker-only; cancelOwner only marks bounded metadata.
class AppFileStreamService {
public:
    static constexpr size_t kSessions=3,kChunk=8192,kSdFile=16*1024*1024,kInternalFile=1024*1024;
    static AppFileStreamService& instance();
    static bool validName(const String&);
    static bool validOwner(const String&);
    uint32_t begin(const String& owner,const String& name,size_t expected,String& error);
    bool write(const String& owner,uint32_t id,size_t offset,const uint8_t*,size_t,size_t& written,String& error);
    bool commit(const String& owner,uint32_t id,size_t& written,String& error);
    bool abort(const String& owner,uint32_t id);
    File open(const String& owner,const String& name);
    void cancelOwner(const char*);
    void cleanup();
private:
    struct Session {char owner[32]{};uint32_t id=0,last=0;bool active=false,sd=false;
        std::atomic<bool> cancelled{false};String name,path;File file;size_t written=0,expected=0,reserved=0;};
    Session slots_[kSessions];portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;uint32_t sequence_=0;
    Session* find(const String&,uint32_t);
    void discard(Session&);
};
}

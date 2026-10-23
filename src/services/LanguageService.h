#pragma once
#include <Arduino.h>
#include <atomic>
#include <memory>
namespace hp {
class LanguageService {
public:
    static LanguageService& instance();
    bool begin(); // Boot-only, before LVGL starts. No per-frame unzip or storage reads.
    void loadExternalAtBoot(); // After storage startup, before constructing the UI.
    size_t count()const;
    const char* id(size_t index)const;
    const char* name(size_t index)const;
    const char* active()const{return activeId_;}
    const char* requested()const{return requested_;}
    bool ready()const{return ready_;}
    bool busy()const;
    bool request(const char* locale); // Existing local transfer worker; no UI mutation.
    void pollUi(); // Apply completion under the existing display guard.
    const char* status()const;
    const char* tr(const char* key,const char* germanFallback=nullptr)const;
    const char* text(const char* german)const; // Compatibility for shared legacy labels.
    String catalogJson(const char* keys=nullptr)const;
    static bool validId(const char* locale);
private:
    struct Pack;struct Change;
    static std::shared_ptr<Pack> load(size_t index);
    static std::shared_ptr<Pack> decode(const uint8_t* zip,size_t size,size_t index);
    static void saveWorker(void*);
    std::shared_ptr<Pack> german_,selected_;std::shared_ptr<Change> change_;
    char activeId_[16]="de-DE",requested_[16]="de-DE";bool ready_=false;
    const char* statusKey_="language.unchanged";
};
inline const char* tr(const char* key,const char* fallback=nullptr){return LanguageService::instance().tr(key,fallback);}
}

#pragma once
#include "../../core/AppManager.h"
#include "../../services/PhonebookService.h"
#include <atomic>
namespace hp {
struct PhonebookPage;
class PhonebookApp final:public IApp {
public:
    const char* id()const override{return "phonebook";}
    const char* name()const override{return "Telefonbuch";}
    const char* icon()const override{return "@contacts";}
    const char* version()const override{return "1.0.0";}
    AppKind kind()const override{return AppKind::Native;}
    bool launcherVisible()const override{return true;}
    const char* webPath()const override{return "/phonebook";}
    bool begin()override{return true;}
    void open(lv_obj_t*)override;
    void close()override;
private:
    friend struct PhonebookPage;
    enum class Operation {Load,SaveContact,RemoveContact,SaveUser,ImportVcard,ExportVcard};
    struct Work {Operation operation=Operation::Load;uint32_t generation=0,revision=0,expectedRevision=0;PhonebookContact contact;PhonebookUser user;LocalPhonebook data;String filename;bool ok=false,conflict=false;}work_;
    bool queue(Operation,const PhonebookContact* =nullptr,const PhonebookUser* =nullptr,const String& filename=String());
    static void worker(void*);
    PhonebookPage* page_=nullptr;
    std::atomic<uint32_t> generation_{0};
    std::atomic<bool> busy_{false},done_{false};
};
}

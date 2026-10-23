#pragma once
#include <Arduino.h>
#include <vector>
#include "ContactProtocol.h"
#include <atomic>
#include <ArduinoJson.h>
namespace hp {
struct PhonebookContact {
    String id,name,email,phone,mobile,street,houseNumber,postalCode,city,country,notes,userId;
    bool readOnly=false;
    String company,jobTitle,firstName,lastName;
    ContactProtocol::Birthday birthday;
    std::vector<ContactProtocol::Value> phones,emails;
    std::vector<ContactProtocol::Address> addresses;
};
struct PhonebookUser {String id,name,firstName,lastName;};
struct LocalPhonebook {std::vector<PhonebookContact> contacts;std::vector<PhonebookUser> users;};
// Per-account phonebook: local records and read-only provider snapshots stay separate.
// This storage layer never requests provider permissions or network access.
// Disk operations are worker-only; apps must not call these from LVGL callbacks.
class PhonebookService {
public:
    static PhonebookService& instance();
    bool load(const String& accountId,std::vector<PhonebookContact>& output);
    bool save(const String& accountId,const std::vector<PhonebookContact>& contacts);
    bool loadLocal(LocalPhonebook& output);
    bool loadDirectory(LocalPhonebook& output); // Local + explicitly enabled provider snapshots.
    bool saveLocal(const LocalPhonebook& data);
    enum class LocalAction { SaveContact, RemoveContact, SaveUser };
    bool changeLocal(LocalAction,const PhonebookContact&,const PhonebookUser&,LocalPhonebook& result);
    uint32_t revision()const;
    bool importVcard(const String&,const String& userId,LocalPhonebook& result);
    bool exportVcard(const PhonebookContact&,String& output)const;
    // Native exchange folder in UserData; no arbitrary filesystem/secret paths.
    bool importVcardFile(const String& filename,const String& userId,LocalPhonebook& result);
    bool exportVcardFile(const String& filename,const PhonebookContact&,LocalPhonebook& result);
    // Shared JSON codec for native storage and the bounded web API.
    static bool decodeContact(JsonObjectConst,PhonebookContact&);
    static void encodeContact(JsonObject,const PhonebookContact&);
    static bool decodeLocal(JsonVariantConst,LocalPhonebook&);
    static bool encodeLocal(const LocalPhonebook&,String&);
    static bool validateLocalText(const void*,size_t);
    void notifyLocalChanged(){++revision_;}
private:
    std::atomic<uint32_t> revision_{1};
    friend class CalendarSyncService;
    bool loadFile(const String&,bool remote,std::vector<PhonebookContact>&);
    bool saveFile(const String&,bool remote,const std::vector<PhonebookContact>&,bool truncated=false);
    bool sharedUsers(LocalPhonebook&);
};
}

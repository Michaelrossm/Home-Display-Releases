#include "PhonebookWebApi.h"
#include "PhonebookService.h"
#include "NetworkWorker.h"
#include "SecurityService.h"
#include "PostalLookupJob.h"
#include "../apps/calendar/CalendarService.h"
#include "../apps/phonebook/PhonebookFilter.h"
#include <ArduinoJson.h>
#include <algorithm>
namespace hp {
void executePhonebookWebJob(const LuaJobService::Request& request,LuaJobService::Result& result){
    if(!NetworkWorker::instance().isWorkerContext()){result.error="worker_required";return;}
    if(request.context=="postal_lookup"){
        {SecurityWriteGuard guard;if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){result.error="settings_locked";return;}}
        executePostalLookupJob(request,result);return;
    }
    std::lock_guard<std::recursive_mutex> profileLock(CalendarService::profileMutex());
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){result.error="settings_locked";return;}
    if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return;}
    JsonDocument args,out;if(deserializeJson(args,request.body,DeserializationOption::NestingLimit(8))){result.error="phonebook_json";return;}
    auto& service=PhonebookService::instance();LocalPhonebook data;
    if(!service.loadDirectory(data)){result.error="phonebook_storage_or_user_migration";return;}
    const String op=request.context;const String id=args["id"]|"";
    if(op=="list"){
        const String query=args["query"]|"",user=args["user"]|"";
        const unsigned offset=args["offset"]|0u;if(offset>2048||query.length()>64||user.length()>32){result.error="phonebook_query";return;}
        auto rows=out["items"].to<JsonArray>();unsigned matches=0;
        for(const auto& c:data.contacts)if(phonebookMatches(c,user.c_str(),query.c_str())){
            if(matches>=offset&&rows.size()<16){auto row=rows.add<JsonObject>();row["id"]=c.id;row["name"]=c.name;row["company"]=c.company;row["phone"]=c.mobile.length()?c.mobile:c.phone;row["email"]=c.email;row["userId"]=c.userId;row["readOnly"]=c.readOnly;}
            ++matches;
        }
        out["total"]=matches;out["more"]=offset+rows.size()<matches;out["next_offset"]=offset+rows.size();
    }else if(op=="details"||op=="export"){
        auto found=std::find_if(data.contacts.begin(),data.contacts.end(),[&](const auto& c){return c.id==id;});
        if(found==data.contacts.end()){result.error="contact_not_found";return;}
        if(op=="details")service.encodeContact(out["contact"].to<JsonObject>(),*found);
        else{String vcard;if(!service.exportVcard(*found,vcard)){result.error="contact_export_failed";return;}out["vcard"]=vcard;}
    }else if(op=="save"||op=="remove"||op=="import"){
        if(!args["revision"].is<uint32_t>()||args["revision"].as<uint32_t>()!=service.revision()){result.error="revision_conflict";return;}
        bool ok=false;
        if(op=="save"){PhonebookContact contact;if(!service.decodeContact(args["contact"].as<JsonObjectConst>(),contact)){result.error="contact_invalid";return;}ok=service.changeLocal(PhonebookService::LocalAction::SaveContact,contact,{},data);}
        else if(op=="remove"){PhonebookContact contact;contact.id=id;ok=service.changeLocal(PhonebookService::LocalAction::RemoveContact,contact,{},data);}
        else{const String text=args["vcard"]|"",user=args["user"]|"";ok=service.importVcard(text,user,data);}
        if(!ok){result.error="phonebook_write_failed";return;}out["saved"]=true;
    }else{result.error="phonebook_operation";return;}
    auto users=out["users"].to<JsonArray>();for(const auto& user:data.users){auto row=users.add<JsonObject>();row["id"]=user.id;row["name"]=user.name;row["firstName"]=user.firstName;row["lastName"]=user.lastName;}
    out["revision"]=service.revision();out["limit"]=128;
    if(out.overflowed()||measureJson(out)>LuaJobService::kResponseBytes){result.error="phonebook_response_limit";return;}
    serializeJson(out,result.body);result.ok=true;
}
}

#pragma once
#include "PrinterSafety.h"
#include <Arduino.h>
#include <ArduinoJson.h>
namespace hp {
inline constexpr size_t kPrinterWebProfiles=5;
inline bool printerCompleteText(JsonVariantConst v){
    return v.is<const char*>()&&v.as<JsonString>().size()==strlen(v.as<const char*>());
}
inline bool printerRuntimeField(const String& key){
    return key=="id"||key=="mode"||key=="background"||key=="password_secret"||key=="enabled"||key=="calibration"||key=="alignment";
}
inline bool normalizePrinterWebProfiles(JsonArray profiles,String& error){
    if(profiles.size()>kPrinterWebProfiles){error="Maximal 5 Druckerprofile erlaubt";return false;}
    String reserved[kPrinterWebProfiles],used[kPrinterWebProfiles];size_t count=0;
    for(JsonVariant v:profiles){
        if(!v.is<JsonObject>()){error="Druckerprofil muss ein Objekt sein";return false;}
        const String id=v["id"]|"";
        if(printerCompleteText(v["id"])&&printerIdentity(id.c_str(),36))reserved[count]=id;
        ++count;
    }
    size_t at=0;
    for(JsonObject p:profiles){
        String id=p["id"]|"";bool valid=printerCompleteText(p["id"])&&printerIdentity(id.c_str(),36);
        for(size_t j=0;j<at;++j)if(used[j]==id)valid=false;
        if(!valid){
            for(unsigned n=1;;++n){id=String("printer_")+String(n);bool collision=false;
                for(size_t j=0;j<count;++j)if(reserved[j]==id)collision=true;
                for(size_t j=0;j<at;++j)if(used[j]==id)collision=true;
                if(!collision)break;
            }
        }
        p["id"]=id;used[at++]=id;
        if(!p["protocol"].isUnbound()&&!printerCompleteText(p["protocol"])){
            error="Druckerprotokoll muss Text sein";return false;
        }
        String protocol=p["protocol"]|"reprap";
        if(protocol!="reprap"&&protocol!="rrf"&&protocol!="moonraker"&&protocol!="klipper"&&protocol!="octoprint"&&protocol!="marlin_octoprint"){
            error="Unbekanntes Druckerprotokoll";return false;
        }
        p["protocol"]=protocol;
        for(const char* field:{"calibration","alignment"})if(p[field].isUnbound())p[field]="none";
        if(!printerCompleteText(p["calibration"])||!printerCalibrationProfile(p["calibration"].as<const char*>())||
           !printerCompleteText(p["alignment"])||!printerAlignmentProfile(p["alignment"].as<const char*>())){
            error="Ungueltiges Kalibrierungsprofil";return false;
        }
        const int port=p["port"]|0;
        if(port<1||port>65535)p["port"]=printerDefaultPort(protocol.c_str());
        if(p["mode"].isUnbound())p["mode"]="auto";
        const String mode=p["mode"]|"";
        if(mode!="auto"&&mode!="standalone"&&mode!="sbc"){error="Ungueltiger Druckermodus";return false;}
        if(p["background"].isUnbound())p["background"]=true;
        if(!p["background"].is<bool>()){error="Drucker-Hintergrundwert muss Boolean sein";return false;}
        if(p["enabled"].isUnbound())p["enabled"]=true;
        if(!p["enabled"].is<bool>()){error="Drucker-Aktivierung muss Boolean sein";return false;}
        const String secret=p["password_secret"]|"";
        if(!p["password_secret"].isUnbound()&&(!printerCompleteText(p["password_secret"])||
           (secret.length()&&!printerIdentity(secret.c_str(),48)))){error="Ungueltige Secret-Referenz";return false;}
        String name=p["name"]|"";if(!name.length())p["name"]=String("Drucker ")+String(static_cast<unsigned>(at));
        else if(name.length()>48){error="Druckername zu lang";return false;}
        const String host=p["host"]|"";
        if(!printerCompleteText(p["host"])||!host.length()||host.length()>120){error="Drucker-IP oder Hostname fehlt/ist zu lang";return false;}
        for(size_t i=0;i<host.length();++i)if(!isalnum(static_cast<unsigned char>(host[i]))&&host[i]!='.'&&host[i]!='-'){
            error="Ungueltige Drucker-IP oder Hostname";return false;
        }
    }
    return true;
}
}

#include "PostalLookupService.h"
#include "NetworkService.h"
#include "NetworkWorker.h"
#include "TimeService.h"
#include "RegionService.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <cmath>
#include <cstdlib>
namespace hp {namespace {
struct Response {String body;bool failed=false;const std::atomic<bool>* cancelled=nullptr;uint32_t started=0;};
esp_err_t receive(esp_http_client_event_t* event){
    auto& response=*static_cast<Response*>(event->user_data);
    if((response.cancelled&&response.cancelled->load())||uint32_t(millis()-response.started)>5000){response.failed=true;return ESP_FAIL;}
    if(event->event_id==HTTP_EVENT_ON_DATA&&event->data_len>0){
        if(response.body.length()+size_t(event->data_len)>8192||!response.body.concat(static_cast<const char*>(event->data),event->data_len)){response.failed=true;return ESP_FAIL;}
    }
    return ESP_OK;
}
}
bool lookupPostalCode(JsonVariantConst args,JsonDocument& result,String& error,const std::atomic<bool>* cancelled){
    if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
    if(cancelled&&cancelled->load()){error="cancelled";return false;}
    String country=args["country"]|"";country.trim();country.toUpperCase();
    if(!country.length())country=RegionService::instance().country();
    else{const int i=RegionService::find(country.c_str());if(i>=0)country=RegionService::code(i);}
    if(country=="DEUTSCHLAND"||country=="GERMANY"||!country.length())country="DE";
    if(country=="ÖSTERREICH"||country=="AUSTRIA")country="AT";
    if(country=="SCHWEIZ"||country=="SWITZERLAND")country="CH";
    const char* postal=args["postal_code"].as<const char*>();
    const size_t length=postal?strlen(postal):0;
    if(country.length()!=2||country[0]<'A'||country[0]>'Z'||country[1]<'A'||country[1]>'Z'||length<2||length>16){error="Land als zweistelligen ISO-Code und vollständige PLZ eingeben.";return false;}
    const bool numeric=country=="DE"||country=="AT"||country=="CH";
    if(numeric&&length!=(country=="DE"?5u:4u)){error="PLZ: Deutschland 5, Österreich/Schweiz 4 Ziffern; Land auswählen.";return false;}
    String encoded;
    for(size_t i=0;i<length;++i){const char c=postal[i];
        if(c>='0'&&c<='9'){encoded+=c;continue;}
        if(!numeric&&((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-')){encoded+=c;continue;}
        if(!numeric&&c==' '){encoded+="%20";continue;}
        error="PLZ enthält ungültige Zeichen.";return false;
    }
    if(!NetworkService::instance().online()||!TimeService::instance().valid()){error="PLZ-Suche benötigt Netzwerk und gültige Gerätezeit. Adresse manuell eingeben.";return false;}
    const String url="https://api.zippopotam.us/"+country+"/"+encoded;
    Response response;response.cancelled=cancelled;response.started=millis();
    esp_http_client_config_t config{};config.url=url.c_str();config.crt_bundle_attach=esp_crt_bundle_attach;
    config.timeout_ms=4000;config.disable_auto_redirect=true;config.event_handler=receive;config.user_data=&response;
    auto client=esp_http_client_init(&config);if(!client){error="PLZ-Suche: zu wenig Speicher.";return false;}
    const auto performed=esp_http_client_perform(client);const int status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
    if(cancelled&&cancelled->load()){error="cancelled";return false;}
    if(performed!=ESP_OK||response.failed||status!=200||uint32_t(millis()-response.started)>5000){error=status==404?"PLZ nicht im Onlinedienst gefunden. Manuelle Eingabe bleibt möglich.":"PLZ-Suche nicht verfügbar. Manuelle Eingabe bleibt möglich.";return false;}
    JsonDocument data;if(deserializeJson(data,static_cast<const String&>(response.body),DeserializationOption::NestingLimit(5))||!data["places"].is<JsonArray>()||data["places"].size()>15){error="PLZ-Antwort ungültig oder zu viele Orte. Manuell eingeben.";return false;}
    result["postal_code"]=String(postal);result["country_code"]=country;
    auto places=result["places"].to<JsonArray>();
    for(JsonObjectConst place:data["places"].as<JsonArrayConst>()){
        const char* city=place["place name"],*region=place["state"];
        if(!city||!region||strlen(city)>127||strlen(region)>127){error="PLZ-Antwort ungültig.";result.clear();return false;}
        auto row=places.add<JsonObject>();row["city"]=String(city);row["region"]=String(region);
        const char* countryName=data["country"]|"";
        if(strlen(countryName)>96){error="PLZ-Antwort ungültig.";result.clear();return false;}
        row["country"]=String(country=="DE"?"Deutschland":country=="AT"?"Österreich":country=="CH"?"Schweiz":countryName);
        row["country_code"]=country;
        for(const char* key:{"latitude","longitude"}){
            const char* coordinate=place[key].as<const char*>();
            if(!coordinate)continue; // Address clients do not require coordinates.
            char* end=nullptr;const double value=strtod(coordinate,&end);const double bound=!strcmp(key,"latitude")?90:180;
            if(end==coordinate||*end||!std::isfinite(value)||value < -bound||value > bound){error="PLZ-Koordinaten ungültig.";result.clear();return false;}
            row[key]=value;
        }
    }
    if(!places.size()){error="Keine Ortsdaten gefunden. Adresse manuell eingeben.";return false;}
    return !result.overflowed();
}
}

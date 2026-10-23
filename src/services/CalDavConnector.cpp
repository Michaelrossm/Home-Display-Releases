#include "CalDavConnector.h"
#include "CalDavCore.h"
#include "NetworkService.h"
#include "TimeService.h"
#include "ContactProtocol.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <algorithm>
namespace hp {namespace {
struct Reply {String body,location;int status=0;CloudAccountState failure=CloudAccountState::ServerUnavailable;bool overflow=false;CalDavControl* control=nullptr;};
esp_err_t receive(esp_http_client_event_t* event){
 auto& reply=*static_cast<Reply*>(event->user_data);
 if(reply.control&&(reply.control->stopped()||!NetworkService::instance().online()))return ESP_FAIL;
 if(event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"Location"))reply.location=event->header_value;
 if(event->event_id==HTTP_EVENT_ON_DATA&&event->data_len>0){if(reply.body.length()+event->data_len>65536||!reply.body.concat(static_cast<const char*>(event->data),event->data_len)){reply.overflow=true;return ESP_FAIL;}}
 return ESP_OK;
}
bool propfind(String& url,const String& root,const char* user,const char* password,bool icloud,bool depthOne,Reply& reply,bool contacts=false,const char* report=nullptr){
 auto* control=reply.control;
 const char* query=report?report:contacts?
 "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:current-user-principal/><c:addressbook-home-set/><d:resourcetype/><d:displayname/></d:prop></d:propfind>":
 "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:current-user-principal/><c:calendar-home-set/><d:resourcetype/><d:displayname/></d:prop></d:propfind>";
 for(unsigned redirects=0;redirects<=3;++redirects){
  reply=Reply{};reply.control=control;
  const uint32_t timeout=control?control->nextTimeout():8000;
  if(!timeout||!NetworkService::instance().online())return false;
  if(!CalDavDiscovery::allowedTarget(root.c_str(),url.c_str(),icloud)){reply.failure=CloudAccountState::Error;return false;}
  esp_http_client_config_t cfg{};cfg.url=url.c_str();cfg.username=user;cfg.password=password;cfg.auth_type=HTTP_AUTH_TYPE_BASIC;
  cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.timeout_ms=timeout;cfg.disable_auto_redirect=true;cfg.event_handler=receive;cfg.user_data=&reply;
  auto client=esp_http_client_init(&cfg);if(!client)return false;
  esp_http_client_set_method(client,report?HTTP_METHOD_REPORT:HTTP_METHOD_PROPFIND);esp_http_client_set_header(client,"Depth",depthOne?"1":"0");
  esp_http_client_set_header(client,"Content-Type","application/xml; charset=utf-8");esp_http_client_set_post_field(client,query,strlen(query));
  auto error=esp_http_client_perform(client);reply.status=esp_http_client_get_status_code(client);
  int tlsError=0,tlsFlags=0;esp_http_client_get_and_clear_last_tls_error(client,&tlsError,&tlsFlags);esp_http_client_cleanup(client);
  if(error!=ESP_OK||reply.overflow){reply.failure=tlsError||tlsFlags?CloudAccountState::TlsError:CloudAccountState::ServerUnavailable;return false;}
  if(reply.status==401){reply.failure=CloudAccountState::InvalidCredentials;return false;}
  if(reply.status==403){reply.failure=CloudAccountState::PermissionMissing;return false;}
  if(reply.status==301||reply.status==302||reply.status==303||reply.status==307||reply.status==308){
   const auto next=CalDavDiscovery::resolve(url.c_str(),reply.location.c_str());if(next.empty()||redirects==3||!CalDavDiscovery::allowedTarget(root.c_str(),next,icloud)){reply.failure=CloudAccountState::Error;return false;}url=next.c_str();continue;
  }
  if(reply.status!=207){reply.failure=reply.status>=500?CloudAccountState::ServerUnavailable:CloudAccountState::Error;return false;}return true;
 }return false;
}
const char* failureMessage(CloudAccountState state){switch(state){case CloudAccountState::InvalidCredentials:return "Benutzername oder App-Passwort ungültig.";case CloudAccountState::TlsError:return "TLS-Zertifikat konnte nicht geprüft werden.";case CloudAccountState::PermissionMissing:return "Kalenderberechtigung fehlt.";case CloudAccountState::ServerUnavailable:return "Kalenderserver nicht erreichbar.";default:return "Ungültige Serverantwort oder unsichere Rückleitung abgelehnt.";}}
}
CalDavConnectionResult CalDavConnector::discover(const char* server,const char* user,const char* password,bool icloud,CalDavControl* control){
 CalDavControl localControl;if(!control)control=&localControl;
 CalDavConnectionResult result;if(!NetworkService::instance().online()||!TimeService::instance().valid()){
 result.state=CloudAccountState::ServerUnavailable;result.message="Netzwerk und gültige Uhrzeit erforderlich.";return result;}
 String root=server,url=root;Reply reply;reply.control=control;DavDiscovery discovery;
 if(!CalDavDiscovery::httpsUrl(root.c_str())||!user||!*user||!password||!*password){result.state=CloudAccountState::InvalidCredentials;result.message="Server, Benutzername und geschütztes Passwort einrichten.";return result;}
 if(!propfind(url,root,user,password,icloud,false,reply)){
  // A configured explicit base path is the fallback if well-known is absent.
  if(reply.status==404){url=(CalDavDiscovery::origin(root.c_str())+"/.well-known/caldav").c_str();
   if(!propfind(url,root,user,password,icloud,false,reply)){result.state=reply.failure;result.message=failureMessage(result.state);return result;}}
  else {result.state=reply.failure;result.message=failureMessage(result.state);return result;}
 }
 if(!CalDavCore::instance().parseDiscovery(reply.body.c_str(),reply.body.length(),discovery)){result.message="Ungültige CalDAV-Discovery-Antwort.";return result;}
 if(discovery.homes.empty()&&!discovery.principal.empty()){
  const auto principal=CalDavDiscovery::resolve(url.c_str(),discovery.principal);if(principal.empty()){result.message="Ungültige Principal-Adresse.";return result;}
  url=principal.c_str();if(!propfind(url,root,user,password,icloud,false,reply)){result.state=reply.failure;result.message=failureMessage(result.state);return result;}
  if(!CalDavCore::instance().parseDiscovery(reply.body.c_str(),reply.body.length(),discovery)){result.message="Ungültige Calendar-Home-Set-Antwort.";return result;}
 }
 auto homes=discovery.homes;if(homes.empty()&&!discovery.calendars.empty())homes.push_back(url.c_str());
 if(homes.empty()){result.state=CloudAccountState::PermissionMissing;result.message="Kein Kalender-Home-Set verfügbar.";return result;}
 const String homeBase=url;
 for(const auto& home:homes){const auto target=CalDavDiscovery::resolve(homeBase.c_str(),home);if(target.empty()){result.message="Ungültige Kalenderadresse.";return result;}url=target.c_str();
  if(!propfind(url,root,user,password,icloud,true,reply)){result.state=reply.failure;result.message=failureMessage(result.state);return result;}
  if(!CalDavCore::instance().parseDiscovery(reply.body.c_str(),reply.body.length(),discovery)){result.message="Ungültige Kalenderliste.";return result;}result.truncated|=discovery.truncated;
  for(const auto& collection:discovery.calendars){auto href=CalDavDiscovery::resolve(url.c_str(),collection.href);
   if(!CalDavDiscovery::allowedTarget(root.c_str(),href,icloud)){result.message="Kalender auf fremdem Server abgelehnt.";return result;}
   bool duplicate=false;for(const auto& c:result.calendars)if(c.id==href.c_str())duplicate=true;
   if(!duplicate){if(result.calendars.size()<16)result.calendars.push_back({String(href.c_str()),String(collection.name.c_str()),true});else result.truncated=true;}
  }
 }
 result.state=CloudAccountState::Connected;result.message=result.truncated?"Verbunden. Kalenderliste auf 16 Einträge begrenzt.":"CalDAV-Verbindung geprüft. Kalender verfügbar.";return result;
}
CalDavEventsResult CalDavConnector::events(const char* server,const char* collection,const char* user,const char* password,bool icloud,int64_t from,int64_t to,CalDavControl* control){
 CalDavEventsResult result;CalDavControl local;if(!control)control=&local;
 if(from<=0||to<=from||to-from>366LL*86400||!server||!collection||!user||!password){result.message="Ungültiger CalDAV-Zeitraum.";return result;}
 auto stamp=[](int64_t epoch){char text[17];time_t value=epoch;tm utc{};gmtime_r(&value,&utc);strftime(text,sizeof(text),"%Y%m%dT%H%M%SZ",&utc);return String(text);};
 const String first=stamp(from),last=stamp(to);
 const String report=String("<?xml version=\"1.0\" encoding=\"utf-8\"?><c:calendar-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag/><c:calendar-data><c:expand start=\"")+first+"\" end=\""+last+"\"/></c:calendar-data></d:prop><c:filter><c:comp-filter name=\"VCALENDAR\"><c:comp-filter name=\"VEVENT\"><c:time-range start=\""+first+"\" end=\""+last+"\"/></c:comp-filter></c:comp-filter></c:filter></c:calendar-query>";
 String url=collection,root=server;Reply reply;reply.control=control;
 if(!propfind(url,root,user,password,icloud,true,reply,false,report.c_str())){result.state=reply.failure;result.message=failureMessage(result.state);return result;}
 DavDiscovery discovery;if(!CalDavDiscovery::parse(reply.body.c_str(),reply.body.length(),discovery)){result.message="Ungültige CalDAV-Terminantwort.";return result;}
 if(discovery.truncated){result.message="CalDAV-Antwort unvollständig; letzten Cache behalten.";return result;}
 for(const auto& resource:discovery.resources){std::string error;
  if(!calendar::parseExpandedCalendar(resource.data,result.events,error,128)){result.events.clear();result.message=String("CalDAV-Daten abgelehnt: ")+error.c_str();return result;}}
 result.state=CloudAccountState::Connected;result.message="CalDAV-Zeitraum geladen.";return result;
}
CardDavConnectionResult CalDavConnector::contacts(const char* server,const char* user,const char* password,bool icloud,CalDavControl* control){
 CalDavControl localControl;if(!control)control=&localControl;
 CardDavConnectionResult result;if(!NetworkService::instance().online()||!TimeService::instance().valid()){result.state=CloudAccountState::ServerUnavailable;result.message="Kontaktserver: Netzwerk und Uhrzeit erforderlich.";return result;}
 String root=icloud?String("https://contacts.icloud.com/"):String((CalDavDiscovery::origin(server)+"/.well-known/carddav").c_str()),url=root;Reply reply;reply.control=control;DavDiscovery discovery;
 auto fail=[&](){result.state=reply.failure;result.message=String("CardDAV: ")+failureMessage(result.state);return result;};
 if(!CalDavDiscovery::httpsUrl(root.c_str())||!user||!*user||!password||!*password){result.state=CloudAccountState::InvalidCredentials;result.message="CardDAV-Zugangsdaten fehlen.";return result;}
 if(!propfind(url,root,user,password,icloud,false,reply,true)){
   if(!icloud&&reply.status==404){url=server;if(!propfind(url,root,user,password,icloud,false,reply,true))return fail();}else return fail();}
 if(!CalDavDiscovery::parse(reply.body.c_str(),reply.body.length(),discovery,true)){result.message="Ungültige CardDAV-Discovery.";return result;}
 if(discovery.homes.empty()&&!discovery.principal.empty()){
   auto principal=CalDavDiscovery::resolve(url.c_str(),discovery.principal);if(principal.empty()){result.message="Ungültiger Kontakt-Principal.";return result;}url=principal.c_str();
   if(!propfind(url,root,user,password,icloud,false,reply,true))return fail();
   if(!CalDavDiscovery::parse(reply.body.c_str(),reply.body.length(),discovery,true)){result.message="Ungültiges Addressbook-Home-Set.";return result;}}
 auto homes=discovery.homes;if(homes.empty()&&!discovery.calendars.empty())homes.push_back(url.c_str());
 if(homes.empty()){result.state=CloudAccountState::PermissionMissing;result.message="Kein CardDAV-Adressbuch freigegeben.";return result;}
 std::vector<std::string> books;const String homeBase=url;
 for(const auto& home:homes){auto target=CalDavDiscovery::resolve(homeBase.c_str(),home);if(target.empty()){result.message="Ungültige Adressbuchadresse.";return result;}url=target.c_str();
   if(!propfind(url,root,user,password,icloud,true,reply,true))return fail();
   if(!CalDavDiscovery::parse(reply.body.c_str(),reply.body.length(),discovery,true)){result.message="Ungültige CardDAV-Liste.";return result;}
   result.truncated|=discovery.truncated;for(const auto& book:discovery.calendars){auto href=CalDavDiscovery::resolve(url.c_str(),book.href);
      if(!CalDavDiscovery::allowedTarget(root.c_str(),href,icloud)){result.message="Fremdes Adressbuch abgelehnt.";return result;}
      if(std::find(books.begin(),books.end(),href)==books.end()){if(books.size()<16)books.push_back(href);else result.truncated=true;}}}
 constexpr const char* query="<?xml version=\"1.0\" encoding=\"utf-8\"?><c:addressbook-query xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag/><c:address-data/></d:prop><c:filter><c:prop-filter name=\"FN\"/></c:filter><c:limit><c:nresults>128</c:nresults></c:limit></c:addressbook-query>";
 if(books.empty()){result.state=CloudAccountState::PermissionMissing;result.message="Kein CardDAV-Adressbuch verfügbar.";return result;}
 for(const auto& book:books){url=book.c_str();if(!propfind(url,root,user,password,icloud,true,reply,true,query))return fail();
   if(!CalDavDiscovery::parse(reply.body.c_str(),reply.body.length(),discovery,true)){result.message="Ungültige CardDAV-Kontaktantwort.";return result;}
   result.truncated|=discovery.truncated;
   for(const auto& resource:discovery.cards){if(result.contacts.size()>=128){result.truncated=true;break;}ContactProtocol::Card card;
     if(!ContactProtocol::parseVcard(resource.data,card)){result.message="Nicht unterstützte oder beschädigte vCard; Offline-Stand bleibt erhalten.";return result;}
     PhonebookContact contact;uint64_t hash=14695981039346656037ULL;for(unsigned char c:book+resource.href){hash^=c;hash*=1099511628211ULL;}
     char key[40];snprintf(key,sizeof(key),"card-%08lx%08lx",(unsigned long)(hash>>32),(unsigned long)hash);contact.id=key;contact.readOnly=true;
     auto copy=[&](const std::string& value,size_t cap){size_t length=std::min(value.size(),cap);if(length<value.size()){while(length&&(uint8_t(value[length])&0xc0)==0x80)--length;result.truncated=true;}return String(value.substr(0,length).c_str());};
     contact.name=copy(card.name,256);contact.email=copy(card.email,254);contact.phone=copy(card.phone,96);contact.mobile=copy(card.mobile,96);
     contact.street=copy(card.street,192);contact.city=copy(card.city,128);contact.postalCode=copy(card.postalCode,32);contact.country=copy(card.country,96);contact.notes=copy(card.notes,1024);
     contact.company=copy(card.company,192);contact.jobTitle=copy(card.jobTitle,128);contact.birthday=card.birthday;
     contact.phones=std::move(card.phones);contact.emails=std::move(card.emails);contact.addresses=std::move(card.addresses);
     for(auto& a:contact.addresses){a.street=copy(a.street,192).c_str();a.postalCode=copy(a.postalCode,32).c_str();a.city=copy(a.city,128).c_str();a.region=copy(a.region,96).c_str();a.country=copy(a.country,96).c_str();}
     bool duplicate=false;for(const auto& old:result.contacts)if(old.id==contact.id)duplicate=true;if(!duplicate)result.contacts.push_back(std::move(contact));
   }if(result.contacts.size()>=128){result.truncated=true;break;}}
 result.state=CloudAccountState::Connected;result.message=result.truncated?"CardDAV-Kontakte synchronisiert; auf 128 / Feldlängen begrenzt.":"CardDAV-Kontakte synchronisiert.";return result;
}
}

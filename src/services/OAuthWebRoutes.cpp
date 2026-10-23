#include "OAuthWebRoutes.h"
#include "WebServerService.h"
#include "SettingsService.h"
#include "FileTransferService.h"
#include "CalendarSyncService.h"
#include "CloudAccountService.h"
#include "OAuthProtocol.h"
#include "../core/EventLog.h"
#include <ArduinoJson.h>
#include <lvgl.h>
#include "QrGenerator.h"
#include "NetworkService.h"
#include <mbedtls/platform_util.h>
namespace hp {
namespace {
String csrf;
void headers(httpd_req_t* req){
 httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");
 httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(req,"X-Frame-Options","DENY");
}
esp_err_t json(httpd_req_t* req,JsonDocument& doc){String body;serializeJson(doc,body);httpd_resp_set_type(req,"application/json");return httpd_resp_send(req,body.c_str(),body.length());}
bool postBody(httpd_req_t* req,JsonDocument& doc){
 char token[65]{},type[40]{};
 if(csrf.length()!=64||httpd_req_get_hdr_value_str(req,"X-HP-CSRF",token,sizeof(token))!=ESP_OK||
 !OAuthProtocol::constantEqual(token,csrf)||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||
 strcmp(type,"application/json")||req->content_len<2||req->content_len>1536)return false;
 char body[1537]{};int read=0;while(read<req->content_len){int n=httpd_req_recv(req,body+read,req->content_len-read);if(n<=0){mbedtls_platform_zeroize(body,sizeof(body));return false;}read+=n;}
 // Mutable input enables zero-copy parsing: wiping the scratch buffer would
 // invalidate every JSON string before the caller uses it. Force owned copies.
 bool ok=!deserializeJson(doc,static_cast<const char*>(body),read)&&doc.is<JsonObject>();mbedtls_platform_zeroize(body,sizeof(body));return ok;
}
EmailProvider provider(const char* name){return !strcmp(name,"microsoft")?EmailProvider::Microsoft:!strcmp(name,"google")?EmailProvider::Google:EmailProvider::Count;}
const char* cloudProvider(AccountProvider p){return p==AccountProvider::Microsoft?"microsoft":p==AccountProvider::Google?"google":p==AccountProvider::ICloud?"icloud":"caldav";}
void registry(JsonDocument& doc,const char* key){auto rows=doc[key].to<JsonArray>();
 for(const auto& account:CloudAccountService::instance().accounts()){auto row=rows.add<JsonObject>();row["id"]=account.id;row["provider"]=cloudProvider(account.provider);row["displayName"]=account.displayName;row["email"]=account.email;
 row["state"]=cloudStateName(account.state);row["message"]=account.message;row["connected"]=account.connected;row["enabled"]=account.enabled;row["phonebookEnabled"]=account.phonebookEnabled;
 const auto contacts=CloudAccountService::instance().info(uint8_t(account.id[8]-'0'));row["contactsMessage"]=contacts.contactsMessage;row["contactCount"]=contacts.contactCount;row["contactsTruncated"]=contacts.contactsTruncated;
 auto caps=row["capabilities"].to<JsonArray>();for(auto capability:{AccountCapability::Profile,AccountCapability::Calendar,AccountCapability::Mail,AccountCapability::Contacts,AccountCapability::Files})
 if(account.capabilities&capabilityBit(capability))caps.add(capability==AccountCapability::Profile?"Profile":capability==AccountCapability::Calendar?"Calendar":capability==AccountCapability::Mail?"Mail":capability==AccountCapability::Contacts?"Contacts":"Files");}
}
esp_err_t accountsApi(httpd_req_t* req){if(!FileTransferService::authorize(req))return ESP_OK;headers(req);JsonDocument doc;registry(doc,"accounts");return json(req,doc);}
esp_err_t loginQr(httpd_req_t* req){
 if(!WebServerService::instance().secureRequest(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"HTTPS required");
 if(!FileTransferService::authorize(req))return ESP_OK;headers(req);
 char query[64]{},name[16]{};if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK||httpd_query_key_value(query,"provider",name,sizeof(name))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid provider");
 String url;if(!strcmp(name,"apple")){const String ip=NetworkService::instance().ip();if(!ip.length()||ip=="0.0.0.0")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"No LAN address");url="https://"+ip+"/accounts?setup=icloud";}
 else {const auto p=provider(name);if(p==EmailProvider::Count)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid provider");url=CalendarSyncService::instance().getAccountState(p).authorizationUrl;}
 if(!url.startsWith("https://")||url.length()>1024)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Start login first; no active HTTPS link");
 int size=0;String bits;if(!QrGenerator::matrix(url,size,bits))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Use the login link; QR too long");
 JsonDocument doc;doc["size"]=size;doc["bits"]=bits;return json(req,doc);
}
esp_err_t status(httpd_req_t* req){
 if(!FileTransferService::authorize(req))return ESP_OK;headers(req);JsonDocument doc;auto rows=doc["accounts"].to<JsonArray>();
 for(auto p:{EmailProvider::Microsoft,EmailProvider::Google}){const auto info=CalendarSyncService::instance().getAccountState(p);
 auto row=rows.add<JsonObject>();row["provider"]=p==EmailProvider::Microsoft?"microsoft":"google";row["state"]=AccountAuthPolicy::stateName(info.authState);
 row["email"]=info.email;row["name"]=info.name;row["message"]=info.message;row["connected"]=info.connected;row["working"]=info.working;
 row["url"]=info.authorizationUrl;row["code"]=info.userCode;row["expiresIn"]=info.expiresIn;
 }registry(doc,"registry");auto setup=doc["setupStatus"].to<JsonArray>();for(uint8_t slot=0;slot<5;++slot){auto info=CloudAccountService::instance().accountInfo(String("account-")+slot);auto row=setup.add<JsonObject>();row["slot"]=slot;row["message"]=info.message;row["state"]=cloudStateName(info.state);}return json(req,doc);
}
esp_err_t config(httpd_req_t* req){
 if(!FileTransferService::authorize(req))return ESP_OK;headers(req);JsonDocument doc;auto& service=CalendarSyncService::instance();
 if(req->method==HTTP_POST){
 if(!postBody(req,doc)||!doc["provider"].is<const char*>()||!doc["own"].is<bool>())
 return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Nur öffentliche OAuth-Daten erlaubt");
 for(JsonPair item:doc.as<JsonObject>()){
 const String key=item.key().c_str();if(key!="provider"&&key!="purpose"&&key!="own"&&key!="clientId"&&key!="redirect")
 return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Nur öffentliche OAuth-Daten erlaubt");}
 const auto p=provider(doc["provider"]);const String purpose=doc["purpose"]|"identity";
 if(p==EmailProvider::Count||(purpose!="identity"&&purpose!="calendar"))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiger Anbieter");
 if(!service.configurePublic(p,purpose=="calendar"?OAuthPurpose::Calendar:OAuthPurpose::Identity,doc["own"],doc["clientId"]|"",doc["redirect"]|""))
 return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Gesperrt oder Angaben ungültig");
 httpd_resp_set_status(req,"202 Accepted");return httpd_resp_sendstr(req,"queued");}
 auto rows=doc["setups"].to<JsonArray>();
 for(uint8_t i=0;i<3;++i){auto p=i==0?EmailProvider::Microsoft:EmailProvider::Google;auto purpose=i==2?OAuthPurpose::Calendar:OAuthPurpose::Identity;
 auto setup=service.setup(p,purpose);auto row=rows.add<JsonObject>();row["provider"]=p==EmailProvider::Microsoft?"microsoft":"google";
 row["purpose"]=purpose==OAuthPurpose::Calendar?"calendar":"identity";row["own"]=setup.own;row["clientId"]=setup.clientId;row["redirect"]=setup.redirect;
 row["standardAvailable"]=setup.standardAvailable;row["secretStored"]=setup.secretStored;row["busy"]=setup.busy;row["message"]=setup.message;
 }return json(req,doc);
}
esp_err_t action(httpd_req_t* req){
 if(!WebServerService::instance().secureRequest(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Kontenanmeldung nur ueber HTTPS");
 if(!FileTransferService::authorize(req))return ESP_OK;headers(req);JsonDocument doc;
 if(!postBody(req,doc))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Anfrage");
 auto& service=CalendarSyncService::instance();bool ok=false;const String uri=req->uri;
 if(uri=="/accounts/action"){
 if(!doc["slot"].is<unsigned>()||doc["slot"].as<unsigned>()>=5)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiges Konto");
 const uint8_t slot=doc["slot"];const String op=doc["action"]|"";
 ok=op=="connect"?service.connect(slot):op=="sync"?service.sync(slot):op=="disconnect"?service.disconnect(slot):false;
 }else if(uri=="/api/accounts/icloud/connect"||uri=="/api/accounts/caldav/connect"){
  // Wipe JSON-owned secret bytes even on validation failures; never echo them.
  struct SecretWipe {const char* p;~SecretWipe(){if(p)mbedtls_platform_zeroize(const_cast<char*>(p),strlen(p));}} wipe{doc["password"].is<const char*>()?doc["password"].as<const char*>():nullptr};
  for(JsonPair item:doc.as<JsonObject>()){const String key=item.key().c_str();if(key!="slot"&&key!="serverUrl"&&key!="username"&&key!="test"&&key!="password")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Unbekanntes Feld");}
  if(!doc["password"].isNull()&&!doc["password"].is<const char*>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungueltiges Passwortfeld");
  if(!doc["slot"].is<unsigned>()||doc["slot"].as<unsigned>()>=5)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiger Kontoplatz");
  const uint8_t slot=doc["slot"];ok=(doc["test"]|false)?service.testCalDav(slot):service.requestCalDavConfiguration(slot,uri=="/api/accounts/icloud/connect"?AccountProvider::ICloud:AccountProvider::CalDav,doc["serverUrl"]|"",doc["username"]|"",doc["password"]|"");
 }else if(uri=="/api/accounts/disconnect")ok=service.disconnectAccount(String(doc["id"]|""));
 else if(uri=="/api/accounts/authorize"){
  const String capability=doc["capability"]|"Calendar";if(capability!="Calendar"&&capability!="Profile"&&capability!="Contacts")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Connector nicht verfügbar");
  ok=service.authorizeCapability(doc["id"]|"",capability=="Calendar"?AccountCapability::Calendar:capability=="Contacts"?AccountCapability::Contacts:AccountCapability::Profile);
 }else if(uri=="/api/accounts/enabled"){
  if(!doc["enabled"].is<bool>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiger Zustand");
  const String feature=doc["feature"]|"account";if(feature!="account"&&feature!="phonebook")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Funktion");
  ok=feature=="phonebook"?service.setPhonebookEnabled(doc["id"]|"",doc["enabled"]):service.setAccountEnabled(doc["id"]|"",doc["enabled"]);
 }else{
 auto p=uri.startsWith("/api/accounts/microsoft/")?EmailProvider::Microsoft:uri.startsWith("/api/accounts/google/")?EmailProvider::Google:provider(doc["provider"]|"");
 if(p==EmailProvider::Count)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiger Anbieter");
 const String purpose=doc["purpose"]|"identity";
 if(purpose!="identity"&&purpose!="calendar")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültiger Zweck");
 if(uri.endsWith("/start"))ok=p==EmailProvider::Microsoft?service.startMicrosoftLogin():service.startGoogleLogin(purpose=="calendar"?OAuthPurpose::Calendar:OAuthPurpose::Identity);
 else if(uri.endsWith("/disconnect"))ok=service.disconnectAccount(p);
 else if(uri=="/api/accounts/login/cancel")ok=service.cancelLogin(p);
 }
 if(!ok)return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Einstellungen entsperren; Dienst beschäftigt oder Konto nicht verfügbar");
 httpd_resp_set_status(req,"202 Accepted");return httpd_resp_sendstr(req,"queued");
}
esp_err_t portal(httpd_req_t* req){
 if(!WebServerService::instance().secureRequest(req)){const String target=String("https://")+SettingsService::instance().snapshot().hostname+".local/accounts";httpd_resp_set_status(req,"302 Found");httpd_resp_set_hdr(req,"Location",target.c_str());return httpd_resp_sendstr(req,"HTTPS required");}
 if(!FileTransferService::authorize(req))return ESP_OK;headers(req);
 const String csp="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-"+csrf+"'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'";
 httpd_resp_set_hdr(req,"Content-Security-Policy",csp.c_str());httpd_resp_set_type(req,"text/html; charset=utf-8");
 String page=R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Home-Display · Konten</title>
<style>body{font:16px system-ui;background:#101925;color:#eff6ff;max-width:950px;margin:30px auto;padding:20px}article,details{background:#1d2a3c;padding:22px;margin:18px 0;border-radius:16px}canvas.login-qr{display:block;max-width:100%;width:240px;height:240px;image-rendering:pixelated;border:10px solid white;border-radius:8px;margin:12px 0}button,input,select{font:inherit;padding:11px;border-radius:8px;margin:6px;border:1px solid #6886a3}button{background:#283e57;color:white}a{color:#9dddff}code{display:block;font-size:30px;letter-spacing:3px;margin:15px 0}small{color:#becbd9}label{display:block}input{max-width:90%;width:600px}</style>
<h1>Kalender verbinden</h1><p>Wähle deinen Anbieter. Die Anmeldung erfolgt beim Anbieter; deine Zugangsdaten werden nicht an Apps weitergegeben.</p>
<article><h2>Einrichtung in drei Schritten</h2><label>Anbieter <select id="quickProvider"><option value="microsoft">Microsoft / Outlook</option><option value="google">Google Kalender</option><option value="icloud">Apple iCloud</option><option value="caldav">Anderer CalDAV-Anbieter</option></select></label>
<p id="quickHelp" role="status"></p><button id="quickNext">Einrichtung anzeigen</button><p id="quickReady" role="status"></p>
<div id="googleGuide" hidden><h3>Google Kalender mit eigener OAuth-Anwendung</h3>
<ol><li><a href="https://console.cloud.google.com/projectcreate" target="_blank" rel="noopener noreferrer">Eigenes Google-Cloud-Projekt anlegen</a> (oder vorhandenes auswählen).</li>
<li><a href="https://console.cloud.google.com/apis/library/calendar-json.googleapis.com" target="_blank" rel="noopener noreferrer">Google Calendar API aktivieren</a>.</li>
<li><a href="https://console.cloud.google.com/auth/overview" target="_blank" rel="noopener noreferrer">OAuth-Zustimmungsbildschirm einrichten</a>; bei Testbetrieb das eigene Google-Konto als Testnutzer eintragen.</li>
<li><a href="https://console.cloud.google.com/auth/clients" target="_blank" rel="noopener noreferrer">OAuth-Client des Typs <strong>Webanwendung</strong> erstellen</a>. Die exakt registrierte HTTPS-Callback-Adresse muss auf <code>/oauth/callback</code> enden und erreichbar sein.</li>
<li>Client-ID und HTTPS-Rueckleitung unten speichern. Das zugehörige <strong>Client-Secret ausschließlich am entsperrten Display</strong> unter Konten → OAuth eingeben. Danach „Kalender freigeben“ auswählen und den QR-Code scannen.</li></ol>
<p><strong>Konfigurationsprüfung:</strong> <span id="googleReady" role="status">Wird geprüft …</span></p>
<p>Ohne eigene, vom Besitzer registrierte Google-OAuth-Anwendung und gültige HTTPS-Rückleitung kann die Firmware den Kalenderzugriff nicht starten. Die lokale Display-HTTPS-Adresse ist nicht automatisch eine öffentlich erreichbare Google-Callback-Adresse.</p></div>
<div id="icloudGuide" hidden><h3>iCloud am Smartphone verbinden</h3>
<ol><li>Auf dem iPhone oder Smartphone bei <a href="https://account.apple.com/" target="_blank" rel="noopener noreferrer">account.apple.com</a> ein <strong>app-spezifisches Passwort</strong> erstellen (falls für das Konto verfügbar).</li>
<li>Hier einen freien Kontoplatz und die Apple-Account-Adresse angeben; das App-Passwort unten auf dieser <strong>HTTPS-Seite</strong> eintragen und speichern.</li>
<li>Danach „Verbindung testen“. Das Display übernimmt die synchronisierten Kalender.</li></ol>
<p>Das normale Apple-Account-Passwort nicht verwenden. Der QR-Code öffnet nur die lokale Einrichtung; im QR selbst stehen keine Zugangsdaten. Am Display PIN-Schutz freigeben und nur das eigene vertrauenswürdige WLAN/LAN nutzen.</p></div>
</article>
<p>Google und Microsoft: Anmeldung auf der offiziellen Anbieterseite. iCloud: ausschließlich ein app-spezifisches Passwort verwenden.</p>
<p><small>HTTPS schützt die Übertragung. Zertifikat vor der ersten Anmeldung prüfen; Einstellungen am Display müssen entsperrt sein.</small></p>
<div id="accounts"></div><p id="notice" role="status"></p>
<h2>Verwaltete Konten</h2><div id="registry"></div>
<details id="davExpert"><summary>iCloud / CalDAV einrichten</summary><p>Für iCloud wird ein app-spezifisches Passwort benötigt. Das normale Apple-ID-Passwort wird nicht gespeichert.</p>
<p>Kontoplatz auswählen und App-Passwort über HTTPS hinterlegen. Leer lassen, um das bereits gespeicherte Passwort zu verwenden.</p>
<label>App-Passwort <input id="davPassword" type="password" maxlength="192" autocomplete="new-password"></label>
<label>Anbieter <select id="davProvider"><option value="icloud">iCloud</option><option value="caldav">Eigener CalDAV-Server</option></select></label>
<label>Kontoplatz <select id="davSlot"><option value="0">1</option><option value="1">2</option><option value="2">3</option><option value="3">4</option><option value="4">5</option></select></label>
<label>Apple-ID / Benutzername <input id="davUser" maxlength="254"></label><label id="davServerField" hidden>HTTPS-Server mit optionalem Port und Basis-Pfad <input id="davServer" maxlength="512" placeholder="https://server.example:8443/remote.php/dav/"></label>
<button id="davSave">Speichern und verbinden</button><button id="davTest">Verbindung testen</button><p id="davStatus" role="status"></p></details>
<details id="oauthExpert"><summary>Expertenmodus · Eigene OAuth-App verwenden</summary><p>Microsoft: öffentlicher Gerätecode-Client. Google: Geräteclient für Profil, Webclient für Kalender. Erforderliches Google-Client-Secret ausschließlich am Display eingeben.</p>
<label>Anbieter / Zweck <select id="kind"><option value="microsoft:identity">Microsoft</option><option value="google:identity">Google Profil (Gerätecode)</option><option value="google:calendar">Google Kalender (HTTPS-Web-Flow)</option></select></label>
<label><input id="own" type="checkbox" style="width:auto"> Eigene OAuth-App verwenden</label>
<label>Client-ID <input id="clientId" maxlength="192"></label><label id="redirectField">Google HTTPS-Rückleitung <input id="redirect" maxlength="254"></label>
<button id="save">Einstellungen speichern</button><p id="configNote"></p></details>
<script src="/app-web-lifecycle.js" nonce=")HTML"+csrf+R"HTML("></script><script nonce=")HTML"+csrf+R"HTML(">
const token=")HTML"+csrf+R"HTML(";
const $=id=>document.getElementById(id);let setups=[],busy=false,lastRegistry=[];const qrCache=new Map();
const help={
 microsoft:'Microsoft: Kalender über Gerätecode freigeben. Du benötigst eine registrierte öffentliche Microsoft-Client-ID (Expertenmodus). Danach den angezeigten Code auf der offiziellen Microsoft-Seite eingeben.',
 google:'Google: Kalender benötigen eine eigene OAuth-Web-Anwendung mit Calendar API und einer gültigen HTTPS-Rückrufadresse. Diese Angaben zuerst im Expertenmodus einrichten; der Gerätecode alleine berechtigt nicht zum Kalenderlesen.',
 icloud:'iCloud: Mit Apple-ID und einem APP-SPEZIFISCHEN Passwort verbinden, nicht mit deinem normalen Apple-Passwort. Einen freien Kontoplatz auswählen, speichern, danach Verbindung testen.',
 caldav:'CalDAV: HTTPS-Serveradresse, Benutzername und App-Passwort eintragen, speichern und Verbindung testen. Nur HTTPS mit gültigem Zertifikat.'};
function quickHelp(){const type=$('quickProvider').value;$('quickHelp').textContent=help[type];$('quickReady').textContent='';$('googleGuide').hidden=type!=='google';$('icloudGuide').hidden=type!=='icloud';if(type==='google')updateGoogleReadiness();}
$('quickProvider').onchange=quickHelp;
$('quickNext').onclick=()=>{const type=$('quickProvider').value;
 if(type==='icloud'||type==='caldav'){$('davExpert').open=true;$('davProvider').value=type;$('davProvider').onchange();let used=new Set(lastRegistry.map(a=>Number(a.id.slice(8))));let free=[0,1,2,3,4].find(n=>!used.has(n));if(free!==undefined)$('davSlot').value=String(free);$('davExpert').scrollIntoView({behavior:'smooth',block:'start'});$('quickReady').textContent='Jetzt Kontoplatz, Benutzername und App-Passwort eintragen.';return;}
 const key=type==='google'?'google:calendar':'microsoft:identity';$('kind').value=key;$('oauthExpert').open=true;showConfig();
 $('oauthExpert').scrollIntoView({behavior:'smooth',block:'start'});$('quickReady').textContent=type==='google'?'Eigene Google-OAuth-Webanwendung konfigurieren, Client-Secret am Display eingeben und anschließend Kalender freigeben.':'Microsoft-Client-ID kontrollieren und danach die Kalenderfreigabe starten.';
};const requestedSetup=new URLSearchParams(location.search).get('setup');if(['microsoft','google','icloud','caldav'].includes(requestedSetup))$('quickProvider').value=requestedSetup;quickHelp();if(requestedSetup==='icloud'||requestedSetup==='caldav')$('quickNext').click();
async function command(path,data){try{let r=await hpWeb.fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-HP-CSRF':token},body:JSON.stringify(data)});let message=r.ok?'Anfrage angenommen. Verbindungsstatus wird aktualisiert.':'Anfrage abgelehnt: '+(await r.text()).slice(0,240);$('notice').textContent=message;await update();}catch(e){$('notice').textContent=e.message;}}
function updateGoogleReadiness(){const target=$('googleReady');if(!target)return;const s=setups.find(s=>s.provider==='google'&&s.purpose==='calendar');
 if(!s){target.textContent='OAuth-Einstellungen werden geladen.';return;}
 let needs=[];if(!s.own||!s.clientId)needs.push('eigene Client-ID');
 if(!s.redirect||!s.redirect.startsWith('https://')||!s.redirect.endsWith('/oauth/callback'))needs.push('registrierte HTTPS-Rückleitung');
 if(!s.secretStored)needs.push('Client-Secret am Display');
 target.textContent=needs.length?'Noch erforderlich: '+needs.join(', ')+'.':'Lokale OAuth-Konfiguration vorhanden. Jetzt Google Kalender freigeben; die Erreichbarkeit der Rückleitung und die Berechtigungen werden erst beim echten Login geprüft.';
}
function showConfig(){let [provider,purpose]=$('kind').value.split(':'),s=setups.find(s=>s.provider===provider&&s.purpose===purpose);$('redirectField').hidden=provider!=='google'||purpose!=='calendar';if(!s)return;$('own').checked=s.own;$('clientId').value=s.clientId;$('redirect').value=s.redirect;$('configNote').textContent=s.message||(!s.standardAvailable?'Keine Projekt-Standard-ID vorhanden. Eigene Anbieterregistrierung erforderlich.':'Projekt-Standard verfügbar.');}
$('kind').onchange=showConfig;
$('davProvider').onchange=()=>{$('davServerField').hidden=$('davProvider').value!=='caldav';};
$('davSave').onclick=()=>{if(location.protocol!=='https:')return;const slot=Number($('davSlot').value);const existing=lastRegistry.find(a=>Number(a.id.slice(8))===slot);if(existing&&!confirm('Kontoplatz '+(slot+1)+' ist bereits mit '+existing.provider+' belegt. Vorhandene Konfiguration wirklich ersetzen?'))return;if(!$('davUser').value.trim()){ $('davStatus').textContent='Apple-Account-Adresse / Benutzername fehlt.';return;}if($('davProvider').value==='icloud'&&!$('davPassword').value.trim()&&!existing){$('davStatus').textContent='Für ein neues iCloud-Konto wird ein App-spezifisches Passwort benötigt.';return;}const args={slot:Number($('davSlot').value),username:$('davUser').value,serverUrl:$('davServer').value,password:$('davPassword').value};$('davPassword').value='';command('/api/accounts/'+$('davProvider').value+'/connect',args);};
$('davTest').onclick=()=>command('/api/accounts/'+$('davProvider').value+'/connect',{slot:Number($('davSlot').value),test:true});
$('save').onclick=()=>{let [provider,purpose]=$('kind').value.split(':');command('/api/accounts/config',{provider,purpose,own:$('own').checked,clientId:$('clientId').value,redirect:$('redirect').value});};
// Reuse the firmware's existing QrGenerator matrix endpoint, never a JS QR library.
async function showLoginQr(card,provider,url){
 if(!url||!url.startsWith('https://'))return;
 let box=document.createElement('div'),hint=document.createElement('p');
 hint.textContent='Mit dem Handy scannen und beim Anbieter anmelden.';box.append(hint);
 let canvas=document.createElement('canvas');canvas.className='login-qr';canvas.setAttribute('aria-label','QR-Code zur Anbieteranmeldung');box.append(canvas);card.append(box);
 try{
  let matrix=qrCache.get(provider+':'+url);
  if(!matrix){const r=await hpWeb.fetch('/api/accounts/login/qr?provider='+encodeURIComponent(provider));if(!r.ok)throw Error('QR derzeit nicht verfügbar');matrix=await r.json();qrCache.set(provider+':'+url,matrix);}
  if(!box.isConnected)return;
  const n=matrix.size,hex=matrix.bits;
  if(!Number.isInteger(n)||n<21||n>149||typeof hex!=='string'||hex.length!==Math.ceil(n*n/4))throw Error('QR-Daten ungültig');
  const quiet=4,scale=4;canvas.width=canvas.height=(n+2*quiet)*scale;
  const ctx=canvas.getContext('2d');ctx.fillStyle='#fff';ctx.fillRect(0,0,canvas.width,canvas.height);ctx.fillStyle='#000';
  for(let i=0;i<n*n;i++){const nibble=parseInt(hex[Math.floor(i/4)],16);if(!Number.isFinite(nibble))throw Error('QR-Daten ungültig');if(nibble&(8>>(i%4)))ctx.fillRect((quiet+i%n)*scale,(quiet+Math.floor(i/n))*scale,scale,scale);}
 }catch(e){box.replaceChildren(document.createTextNode('QR momentan nicht verfügbar. Bitte den offiziellen Anmeldelink öffnen.'));}
}
function button(card,text,path,data,disabled=false){let b=document.createElement('button');b.textContent=text;b.disabled=disabled;b.onclick=()=>command(path,data);card.append(b);}
async function update(){if(busy)return;busy=true;try{
 let r=await hpWeb.fetch('/api/accounts/login/status');if(!r.ok)throw Error();let root=$('accounts');root.replaceChildren();
 const data=await r.json();lastRegistry=data.registry||[];for(let a of data.accounts){let card=document.createElement('article'),title=document.createElement('h2');title.textContent=a.provider==='microsoft'?'Microsoft / Outlook':'Google / Gmail';card.append(title);
 let p=document.createElement('p');p.textContent=(a.email?a.email+' · ':'')+a.message+' ['+a.state+']';card.append(p);
 let active=['RequestingCode','AwaitingUser','Polling'].includes(a.state);
 if(a.code){let code=document.createElement('code');code.textContent=a.code;card.append(code);}
 if(a.url){let link=document.createElement('a');link.href=a.url;link.target='_blank';link.rel='noopener noreferrer';link.textContent='Offizielle Anmeldeseite öffnen ↗';card.append(link);showLoginQr(card,a.provider,a.url);if(a.code){let tip=document.createElement('p');tip.textContent='Nach dem Scannen den angezeigten Gerätecode eingeben.';card.append(tip);}}
 if(a.expiresIn){let ttl=document.createElement('p');ttl.textContent='Noch '+a.expiresIn+' Sekunden. Nach der Anmeldung hier warten.';card.append(ttl);}
 button(card,['Expired','Error'].includes(a.state)?'Erneut versuchen':'Profil anmelden','/api/accounts/'+a.provider+'/start',{purpose:'identity'},active||a.working);
 if(a.provider==='microsoft'){let cal=document.createElement('p');cal.textContent='Für Kalenderzugriff muss das Konto zusätzlich Calendars.Read erhalten. Nach erfolgreicher Anmeldung unten bei deinem Konto „Kalender freigeben“ verwenden.';card.append(cal);}
 if(a.provider==='google'){let hint=document.createElement('p');hint.textContent='Gerätecode: nur Profil. Kalender braucht zusätzliche Web-OAuth-/HTTPS-Einrichtung.';card.append(hint);button(card,'Kalender freigeben','/api/accounts/google/start',{purpose:'calendar'},active||a.working);}
 if(active)button(card,'Anmeldung abbrechen','/api/accounts/login/cancel',{provider:a.provider});
 if(a.connected){let b=document.createElement('button');b.textContent='Verbindung entfernen';b.onclick=()=>{if(confirm('Lokale Autorisierung und Kalendercache entfernen?'))command('/api/accounts/'+a.provider+'/disconnect',{});};card.append(b);}root.append(card);
 }
 let registry=$('registry');registry.replaceChildren();for(let a of data.registry){let card=document.createElement('article'),title=document.createElement('h3');title.textContent=a.displayName+' · '+a.provider;card.append(title);
 let p=document.createElement('p');p.textContent=a.state+' · '+a.message+' · Freigaben: '+(a.capabilities.join(', ')||'noch keine');card.append(p);
 button(card,a.enabled?'Deaktivieren':'Aktivieren','/api/accounts/enabled',{id:a.id,enabled:!a.enabled});
 button(card,a.phonebookEnabled?'Telefonbuch ausschalten':'Telefonbuch einschalten','/api/accounts/enabled',{id:a.id,feature:'phonebook',enabled:!a.phonebookEnabled},!a.enabled);
 if(a.enabled&&a.phonebookEnabled)button(card,'Kontakte freigeben / verbinden','/api/accounts/authorize',{id:a.id,capability:'Contacts'});
 if(a.enabled&&a.phonebookEnabled){let note=document.createElement('p');note.textContent=a.contactsMessage||'';card.append(note);button(card,'Kontakte synchronisieren','/accounts/action',{slot:Number(a.id.slice(8)),action:'sync'});}
 if(a.enabled&&!a.capabilities.includes('Calendar'))button(card,'Kalender freigeben / verbinden','/api/accounts/authorize',{id:a.id,capability:'Calendar'});
 let b=document.createElement('button');b.textContent='Verbindung trennen';b.onclick=()=>{if(confirm('Lokale Zugangsdaten und Cache entfernen?'))command('/api/accounts/disconnect',{id:a.id});};card.append(b);registry.append(card);}
 let dav=data.setupStatus.find(s=>s.slot===Number($('davSlot').value));if(dav)$('davStatus').textContent=dav.state+' · '+dav.message;
 let c=await hpWeb.fetch('/api/accounts/config');if(c.ok){setups=(await c.json()).setups;if(!$('oauthExpert').open)showConfig();else{let [provider,purpose]=$('kind').value.split(':'),s=setups.find(s=>s.provider===provider&&s.purpose===purpose);if(s&&s.message)$('configNote').textContent=s.message;}updateGoogleReadiness();}
 }catch(e){$('notice').textContent='Display nicht erreichbar oder Anmeldung erforderlich.';}finally{busy=false;}}
hpWeb.whenVisible(update);hpWeb.onResume(update);hpWeb.every(update,1500);</script></html>)HTML";
 return httpd_resp_send(req,page.c_str(),page.length());
}
// Intentionally not Basic-authenticated: provider redirects arrive cross-site.
// Only a live, one-use random state plus PKCE authorizes this narrowly scoped route.
esp_err_t callback(httpd_req_t* req){
    headers(req);httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; frame-ancestors 'none'");
    const size_t length=httpd_req_get_url_query_len(req);if(!length||length>2300)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Rückleitung");
    char query[2301]{},state[193]{},code[2049]{};
    if(httpd_req_get_url_query_str(req,query,length+1)!=ESP_OK||httpd_query_key_value(query,"state",state,sizeof(state))!=ESP_OK||
        httpd_query_key_value(query,"code",code,sizeof(code))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Anmeldung nicht abgeschlossen");
    auto decode=[](const char* input){String output;for(size_t i=0;input[i];++i){if(input[i]=='%'&&input[i+1]&&input[i+2]){
        auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
        const int a=digit(input[i+1]),b=digit(input[i+2]);if(a<0||b<0||!(a*16+b))return String();output+=char(a*16+b);i+=2;
    }else output+=input[i]=='+'?' ':input[i];}return output;};
    String decoded=decode(code);const bool ok=CalendarSyncService::instance().completeGoogle(decode(state),decoded);
    if(decoded.length())mbedtls_platform_zeroize(&decoded[0],decoded.length());mbedtls_platform_zeroize(query,sizeof(query));mbedtls_platform_zeroize(code,sizeof(code));
    if(!ok)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Anmeldung abgelaufen oder bereits verwendet");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");return httpd_resp_sendstr(req,"Rückleitung angenommen. Das Display prüft jetzt Konto und Kalenderberechtigung. Zur Kontoseite zurückkehren.");
}

}
const httpd_uri_t* oauthWebRoutes(size_t& count){
 static const httpd_uri_t routes[]={httpd_uri_t{"/accounts",HTTP_GET,portal,nullptr},httpd_uri_t{"/accounts/status",HTTP_GET,status,nullptr},
 httpd_uri_t{"/accounts/action",HTTP_POST,action,nullptr},httpd_uri_t{"/oauth/callback",HTTP_GET,callback,nullptr},
 httpd_uri_t{"/api/accounts",HTTP_GET,accountsApi,nullptr},httpd_uri_t{"/api/accounts/login/status",HTTP_GET,status,nullptr},
 httpd_uri_t{"/api/accounts/login/qr",HTTP_GET,loginQr,nullptr},
 httpd_uri_t{"/api/accounts/login/cancel",HTTP_POST,action,nullptr},httpd_uri_t{"/api/accounts/config",HTTP_GET,config,nullptr},
 httpd_uri_t{"/api/accounts/config",HTTP_POST,config,nullptr},httpd_uri_t{"/api/accounts/microsoft/start",HTTP_POST,action,nullptr},
 httpd_uri_t{"/api/accounts/google/start",HTTP_POST,action,nullptr},httpd_uri_t{"/api/accounts/microsoft/disconnect",HTTP_POST,action,nullptr},
 httpd_uri_t{"/api/accounts/google/disconnect",HTTP_POST,action,nullptr},httpd_uri_t{"/api/accounts/icloud/connect",HTTP_POST,action,nullptr},
 httpd_uri_t{"/api/accounts/caldav/connect",HTTP_POST,action,nullptr},httpd_uri_t{"/api/accounts/disconnect",HTTP_POST,action,nullptr},
 httpd_uri_t{"/api/accounts/authorize",HTTP_POST,action,nullptr},httpd_uri_t{"/api/accounts/enabled",HTTP_POST,action,nullptr}};
 count=sizeof(routes)/sizeof(routes[0]);return routes;
}
bool registerOAuthWebRoutes(httpd_handle_t server){
 // Starting the optional maintenance server must not invalidate open account forms.
 if(csrf.length()!=64)csrf=OAuthProtocol::randomToken();if(csrf.length()!=64)return false;
 size_t count=0;const auto* routes=oauthWebRoutes(count);
 for(size_t i=0;i<count;++i){
  const esp_err_t error=httpd_register_uri_handler(server,&routes[i]);
  if(error!=ESP_OK){EventLog::instance().add("ERROR","WEB_ROUTE_FAILED",String(routes[i].uri)+" method="+String((unsigned)routes[i].method)+" "+esp_err_to_name(error));return false;}
 }
 return true;
}
}

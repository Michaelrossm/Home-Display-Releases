#include "WebServerService.h"
#include "../ui/LicenseUi.h"
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include "WebTlsIdentity.h"
#include <esp_https_server.h>
#include "IntercomService.h"
#include "IntercomWebPage.h"
#include "AppWebLifecycleScript.h"
#include "AppWebRoute.h"
#include "../apps/intercom/IntercomSettingsApi.h"
#include "../core/PsramArray.h"
#include "../core/PerformancePolicy.h"
#include "../apps/calendar/CalendarApi.h"
#include "../apps/lua/LuaJobService.h"
#include "SecurityService.h"
#include "CalendarWebPage.h"
#include "PhonebookWebPage.h"
#include "PhonebookWebApi.h"
#include "PhotoWebApi.h"
#include "PhotoWebPage.h"
#include "AccountConnectScript.h"
#include "BuildInfo.h"
#include "ConfigService.h"
#include "SettingsService.h"
#include "LanguageService.h"
#include "RegionService.h"
#include "NetworkService.h"
#include "StorageService.h"
#include "UsbStorageService.h"
#include "DiagnosticsService.h"
#include "PowerManager.h"
#include "FileTransferService.h"
#include "../apps/calendar/CalendarService.h"
#include "PhonebookService.h"
#include "OtaWriteSession.h"
#include "AppWebSettingsService.h"
#include "LuaAppDataCleanupService.h"
#include "WebCsrfService.h"
#include <ArduinoJson.h>
#include "OAuthWebRoutes.h"
#include "../core/AppRegistry.h"
#include "../core/EventLog.h"
#include "../ui/UiShell.h"
#include "../ui/ThemeController.h"
#include "../ui/ThemeCatalog.h"
#include "../hardware/DisplayManager.h"
#include "WebAdminAuth.h"
#include "WebLoginService.h"
#include <esp_system.h>
#include <cstring>
#include <limits>

namespace hp {
namespace {

String jsonEscape(const String& in) {
    String out; out.reserve(in.length() + 12);
    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c >= 0x20) out += c;
                break;
        }
    }
    return out;
}


String urlDecode(const String& in) {
    String out; out.reserve(in.length());
    for (size_t i=0;i<in.length();++i) {
        const char c=in[i];
        if(c=='+') out+=' ';
        else if(c=='%' && i+2<in.length()) { char h[3]={in[i+1],in[i+2],0}; out+=(char)strtol(h,nullptr,16); i+=2; }
        else out+=c;
    }
    return out;
}

void commonHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

bool authorized(httpd_req_t* req) {
    char fetchSite[32]{};
    if (httpd_req_get_hdr_value_str(req, "Sec-Fetch-Site", fetchSite, sizeof(fetchSite)) == ESP_OK &&
        strcmp(fetchSite, "same-origin") != 0 && strcmp(fetchSite, "none") != 0) {
        httpd_resp_set_status(req, "403 Forbidden");
        commonHeaders(req);
        httpd_resp_sendstr(req, "Cross-origin administration denied");
        return false;
    }

    if (WebAdminAuth::authorized(req)) return true;

    // deny() owns its response headers. IDF appends headers instead of
    // replacing duplicates; adding them twice exhausts the header table and
    // drops Location, leaving the browser on a bare "Bitte anmelden" page.
    WebLoginService::deny(req);
    return false;
}

bool writeAuthorized(httpd_req_t* req) {
    return authorized(req) && WebCsrfService::instance().validate(req);
}
void calendarWebJob(const LuaJobService::Request& request,LuaJobService::Result& result){
 SecurityWriteGuard security;
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){result.error="settings_locked";return;}
 executeCalendarJob(request,result);
}
void intercomWebJob(const LuaJobService::Request& request,LuaJobService::Result& result){
    SecurityWriteGuard guard;
    if(SecurityService::instance().tabletLocked()||!SecurityService::instance().settingsAllowed()){result.error="pin_locked";return;}
    if(request.cancelled&&request.cancelled->load()){result.error="cancelled";return;}
    if(request.context=="list"){
        auto* profiles=allocatePsramArray<IntercomDevice>(5);if(!profiles){result.error="memory";return;}
        const auto count=nativeIntercomDevices(profiles,5);JsonDocument doc;auto rows=doc["devices"].to<JsonArray>();
        for(size_t i=0;i<count;++i){const auto& d=profiles[i];auto row=rows.add<JsonObject>();
            row["id"]=d.id;row["name"]=d.name;row["provider"]=d.provider;row["host"]=d.host;
            row["username"]=d.username;row["participant"]=d.participant;row["port"]=d.port;
            row["videoPort"]=d.videoPort;row["videoPath"]=d.videoPath;row["fps"]=d.fps;row["enabled"]=d.enabled;
            row["httpVideo"]=d.httpVideo;
            row["ringTone"]=d.ringTone;
        }
        const auto written=serializeJson(doc,result.body);heap_caps_free(profiles);
        if(doc.overflowed()||written!=measureJson(doc)||result.body.length()!=written){result.body="";result.error="memory";return;}
        result.ok=true;return;
    }
    JsonDocument doc;if(deserializeJson(doc,request.body,DeserializationOption::NestingLimit(3))||!doc.is<JsonObject>()){result.error="settings_json";return;}
    IntercomDevice d;strlcpy(d.owner,"intercom",sizeof(d.owner));
    auto copy=[&](const char* key,char* target,size_t capacity,bool optional=false){
        if(optional&&doc[key].isNull()){target[0]=0;return true;}
        if(!doc[key].is<const char*>())return false;const char* value=doc[key];
        if(strlen(value)>=capacity)return false;strlcpy(target,value,capacity);return true;
    };
    if(!copy("id",d.id,sizeof(d.id))||!d.id[0]){result.error="device_id";return;}
    if(request.context=="remove"){result.ok=removeNativeIntercomDevice(d.id,result.error);result.body="{}";return;}
    if(!copy("name",d.name,sizeof(d.name))||!copy("provider",d.provider,sizeof(d.provider))||
        !copy("host",d.host,sizeof(d.host))||!copy("username",d.username,sizeof(d.username),true)||
        !copy("participant",d.participant,sizeof(d.participant),true)||!copy("videoPath",d.videoPath,sizeof(d.videoPath),true)||
        !doc["port"].is<uint16_t>()||!doc["videoPort"].is<uint16_t>()||!doc["fps"].is<uint8_t>()||!doc["enabled"].is<bool>()){
        result.error="device_fields";return;
    }
    d.port=doc["port"];d.videoPort=doc["videoPort"];d.fps=doc["fps"];d.enabled=doc["enabled"];
    if(!doc["httpVideo"].isNull()&&!doc["httpVideo"].is<bool>()){result.error="video_transport";return;}
    d.httpVideo=doc["httpVideo"] | false;
    if(!doc["ringTone"].isNull()&&!doc["ringTone"].is<bool>()){result.error="ring_tone";return;}
    d.ringTone=doc["ringTone"] | true;
    const bool change=doc.containsKey("password");
    if(change&&(!doc["password"].is<const char*>()||strlen(doc["password"].as<const char*>())>192)){result.error="password_length";return;}
    String password=change?doc["password"].as<String>():String();
    result.ok=saveNativeIntercomDevice(d,change?&password:nullptr,result.error);result.body="{}";
}

bool queryValue(httpd_req_t* req, const char* key, char* out, size_t outLen) {
    char query[768]{};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
    return httpd_query_key_value(query, key, out, outLen) == ESP_OK;
}

void sendJson(httpd_req_t* req, const String& body, const char* status = nullptr) {
    commonHeaders(req);
    if (status) httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_send(req, body.c_str(), body.length());
}

const char* boolJson(bool v) { return v ? "true" : "false"; }

String kindName(AppKind kind) {
    switch (kind) {
        case AppKind::Lua: return "Lua";
        case AppKind::Native: return "Native";
        case AppKind::System: return "System";
        case AppKind::Web: return "Web";
    }
    return "Unbekannt";
}

String stateName(AppRegistryState state) {
    switch (state) {
        case AppRegistryState::Enabled: return "enabled";
        case AppRegistryState::Disabled: return "disabled";
        case AppRegistryState::Error: return "error";
    }
    return "error";
}

const char kIndex[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Home-Display</title><style>
:root{--bg:#0d141d;--panel:#172231;--panel2:#1e2d3f;--text:#f2f6fb;--muted:#9fb0c4;--line:#2d4056;--accent:#3a95ff;--ok:#4fc27b;--warn:#f4b84a;--err:#ef6969}
*{box-sizing:border-box}body{margin:0;font:15px system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text)}
header{position:sticky;top:0;z-index:5;background:rgba(13,20,29,.94);backdrop-filter:blur(12px);border-bottom:1px solid var(--line)}
.head{max-width:1240px;margin:auto;padding:18px 20px;display:flex;justify-content:space-between;align-items:center;gap:16px}.brand{font-size:21px;font-weight:750}.muted{color:var(--muted)}
main{max-width:1240px;margin:auto;padding:20px}.tabs{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:18px}.tabs button,.btn{border:1px solid var(--line);background:var(--panel);color:var(--text);border-radius:11px;padding:10px 13px;cursor:pointer;text-decoration:none}.tabs button.active,.btn.primary{background:var(--accent);border-color:var(--accent);color:white}
.page{display:none}.page.active{display:block}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}.grid2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:17px;min-width:0}.card h2{font-size:17px;margin:0 0 12px}.metric{font-size:25px;font-weight:700;overflow-wrap:anywhere}.label{color:var(--muted);font-size:12px;margin-bottom:5px}.status{display:inline-flex;align-items:center;gap:7px}.dot{width:9px;height:9px;border-radius:50%;background:var(--muted)}.ok .dot{background:var(--ok)}.warn .dot{background:var(--warn)}.err .dot{background:var(--err)}
table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:10px 8px;border-bottom:1px solid var(--line);vertical-align:middle}th{font-size:12px;color:var(--muted)}input,select{width:100%;background:var(--panel2);color:var(--text);border:1px solid var(--line);border-radius:9px;padding:10px}.row{display:flex;gap:10px;align-items:center}.row>*{flex:1}.actions{display:flex;gap:8px;flex-wrap:wrap}.pre{white-space:pre-wrap;background:#0a1017;border:1px solid var(--line);border-radius:12px;padding:14px;color:#cfe0f1;max-height:460px;overflow:auto;font:12px ui-monospace,SFMono-Regular,Consolas,monospace}.small{font-size:12px;color:var(--muted)}.drop{border:1px dashed var(--line);border-radius:13px;padding:18px;text-align:center;background:var(--panel2);cursor:pointer}.drop.drag{border-color:var(--accent);background:#17304d}.progress{height:10px;background:#0a1017;border:1px solid var(--line);border-radius:99px;overflow:hidden}.bar{height:100%;width:0;background:var(--accent);transition:width .12s linear}.btn.danger{border-color:#7a3b45;color:#ffb7c0}
@media(max-width:900px){.grid{grid-template-columns:repeat(2,1fr)}.grid2{grid-template-columns:1fr}}@media(max-width:560px){.grid{grid-template-columns:1fr}.head{align-items:flex-start;flex-direction:column}main{padding:14px}.hide-mobile{display:none}}
button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.tabs{align-items:center}.tabs button{font-weight:600}.card{box-shadow:0 5px 22px #00000012}.card h2{letter-spacing:.1px}.metric{font-variant-numeric:tabular-nums}.small{line-height:1.5}.storage-warning{color:var(--warn)}.memory-warning{color:var(--warn)}.memory-critical{color:var(--err)}
.settings-pane{display:none}.settings-pane.active{display:block}[data-settings].active{border-color:var(--accent)}
</style></head><body><header><div class="head"><div><div class="brand">Home-Display</div><div id="headline" class="muted">Verbindung wird aufgebaut …</div></div><div class="actions"><button class="btn" onclick="refreshAll()">Aktualisieren</button></div></div></header>
<main><nav class="tabs"><button data-page="home" class="active">Übersicht</button><button data-page="apps">Apps</button><button data-page="settings">Einstellungen</button><a class="btn" href="/login">Anmeldung / Abmelden</a></nav>
<section id="home" class="page active"><div class="grid"><div class="card"><div class="label">Netzwerk</div><div id="mNet" class="metric">–</div><div id="mIp" class="small">–</div></div><div class="card"><div class="label">Firmware</div><div id="mFw" class="metric">–</div><div id="mUptime" class="small">–</div></div><div class="card"><div class="label">Interner RAM frei</div><div id="mRam" class="metric">–</div><div id="mLargest" class="small">–</div></div><div class="card"><div class="label">PSRAM frei</div><div id="mPsram" class="metric">–</div><div id="mStorage" class="small">–</div></div></div>
<div class="card" style="margin-top:14px"><h2>Externe Speicher</h2><p class="small">Status wird ohne Datenträgerzugriffe angezeigt. Die Speichergröße ist die beim Einbinden ermittelte Kapazität.</p><div class="grid2"><div><div class="label">USB-Laufwerk</div><div id="mUsb" class="metric">–</div><div id="mUsbDetail" class="small">–</div></div><div><div class="label">SD-Karte</div><div id="mSd" class="metric">–</div><div id="mSdDetail" class="small">–</div></div></div></div>
<div class="card" style="margin-top:14px"><h2>Apps öffnen</h2><div id="webApps" class="grid2"></div><p class="small">Gemeinsamer geschützter Server. Externe Web-Apps öffnen erst beim Anklicken.</p></div><div class="grid2" style="margin-top:14px"><div class="card"><h2>Häufig verwendete Funktionen</h2><div class="actions"><button class="btn primary" id="appsAdvanced">App installieren</button><button class="btn" id="filesAdvanced" onclick="location.href='/files'">Dateien</button><button class="btn" onclick="location.href='/users'">Benutzer</button><button class="btn" id="accountsAdvanced" onclick="location.href='/accounts'">Konten</button><button class="btn" onclick="location.href='/calendar'">Kalender</button></div><p class="small">Die vorhandenen ausgereiften Upload- und Kontenfunktionen bleiben als Serviceoberfläche erhalten und verwenden dieselben Backends.</p></div><div class="card"><h2>Status</h2><div id="health">–</div></div></div></section>
<section id="apps" class="page"><div class="card"><h2>Installierte Apps</h2><div style="overflow:auto"><table><thead><tr><th>Name</th><th>Typ</th><th>Version</th><th>Status</th><th>Einstellungen</th><th>Aktion</th></tr></thead><tbody id="appsBody"></tbody></table></div></div>
<div class="card" id="appInstallerCard" style="margin-top:14px"><div class="row"><div><h2>App installieren / aktualisieren</h2><div class="small">Lua-App als ZIP direkt hier installieren. Die vorhandene App bleibt bis zum erfolgreichen Commit erhalten.</div></div><button class="btn" id="legacyInstaller" onclick="location.href='/apps'">Erweiterte Service-Seite</button></div><div id="appZipDrop" class="drop" style="margin-top:12px">App-ZIPs hier ablegen oder auswählen (maximal 32)</div><input id="appZipFile" type="file" multiple accept=".zip,application/zip" style="margin-top:10px"><div id="appZipMeta" class="small" style="margin-top:10px"></div><label id="appDowngradeLabel" class="small" style="display:none;margin-top:10px"><input id="appDowngrade" type="checkbox" style="width:auto"> Downgrade ausdrücklich erlauben</label><div class="actions" style="margin-top:12px"><button id="appZipInstall" class="btn primary" disabled>Ausgewählte Apps installieren</button><button id="appZipStop" class="btn" disabled>Stapel abbrechen</button></div><div class="progress" style="margin-top:12px"><div id="appZipBar" class="bar"></div></div><div id="appZipOverall" class="small" aria-live="polite"></div><ol id="appZipResults" class="small"></ol><div id="appZipStatus" class="small" style="margin-top:8px">Noch keine ZIP-Datei ausgewählt.</div></div>
<div id="appSettingsCard" class="card" style="display:none;margin-top:14px">
<div class="row"><div><h2 id="appSettingsTitle">App-Einstellungen</h2><div id="appSettingsNote" class="small"></div></div><button class="btn" onclick="closeAppSettings()">Schließen</button></div>
<div id="appSettingsFields"></div>
<div class="actions" style="margin-top:14px"><button class="btn primary" onclick="saveAppWebSettings()">Speichern</button><button class="btn" id="appSettingsDisplayBtn">Am Display öffnen</button></div>
<div id="appSettingsStatus" class="small" style="margin-top:10px"></div>
</div></section>
<section id="settings" class="page"><nav class="actions"><button class="btn" data-settings="network">Netzwerk</button><button class="btn" data-settings="system">System</button><button class="btn" data-settings="diagnostics">Diagnose</button><button class="btn" onclick="location.href=\'/users\'">Benutzer</button></nav><p id="settingsStatus" class="small" role="status"></p><section id="network" class="settings-pane active"><div class="grid2"><div class="card"><h2>LAN</h2><div id="lanState">–</div></div><div class="card"><h2>WLAN</h2><div id="wifiState">–</div><div class="actions" style="margin-top:12px"><button class="btn" onclick="wifiEnable(true)">WLAN ein</button><button class="btn" onclick="wifiEnable(false)">WLAN aus</button><button class="btn primary" onclick="scanWifi()">Netze suchen</button></div></div></div><div class="card" style="margin-top:14px"><h2>WLAN-Profile</h2><div id="profiles"></div><div id="scanResults" class="small" style="margin-top:12px"></div></div></section>
<section id="system" class="settings-pane"><div class="grid2"><div class="card"><h2>Region</h2><label class="small">Standardland für alle Apps</label><select id="regionCountry"></select><button class="btn" onclick="saveRegion()">Region speichern</button><p id="regionStatus" class="small"></p><h2>Darstellung</h2><label class="small">Theme</label><select id="themeMode"></select><label class="small">Akzent</label><select id="accent"></select><label class="small">Helligkeit <span id="brightnessValue"></span></label><input id="brightness" type="range" min="1" max="255"><div class="actions" style="margin-top:12px"><button class="btn primary" onclick="saveTheme()">Theme speichern</button><button class="btn" onclick="saveBrightness()">Helligkeit übernehmen</button></div></div><div class="card"><h2>Wartung</h2><div class="actions"><button class="btn" onclick="restartDevice()">Neu starten</button><button class="btn" id="firmwareAdvanced" onclick="location.href='/apps'">Firmware / OTA</button><button class="btn" id="storageAdvanced" onclick="location.href='/files'">Speicher / Dateien</button></div><div id="service8088Status" class="small" style="margin-top:12px">Service-Port 8088 ist aus.</div><div class="actions" style="margin-top:10px"><button class="btn" onclick="startService8088()">8088 für max. 30 Min starten</button><button class="btn" onclick="openService8088()">8088 öffnen</button><button class="btn danger" onclick="stopService8088()">8088 stoppen</button></div><p class="small">Recovery bleibt unabhängig davon erreichbar. Im Normalbetrieb läuft 8088 nur bei Bedarf und maximal 30 Minuten.</p></div></div></section>
<section id="diagnostics" class="settings-pane"><div class="card"><div class="row"><div><h2>Diagnose</h2><div class="small">Aktueller Gerätezustand und letzte Warnungen.</div></div><button class="btn" onclick="loadDiagnostics()">Neu laden</button></div><div id="diagSummary" class="grid" style="margin-top:14px"></div><div id="diagEvents" class="card" style="margin-top:14px"><b>Letzte Ereignisse</b><div class="small" style="margin-top:8px">–</div></div><details style="margin-top:14px"><summary>Rohbericht anzeigen</summary><pre id="diag" class="pre">–</pre></details></div></section></section></main>
<script src="/app-web-lifecycle.js"></script><script>
const $=id=>document.getElementById(id),serviceBase=location.protocol+'//'+location.hostname+':8088';let service8088={running:false,remaining:0};

let activePage='home',activeSettings='network';const settingsPending=new Map();
function settingsVisible(section){return activePage==='settings'&&activeSettings===section&&!document.hidden}
async function refreshSettingsSection(section=activeSettings){
 if(!settingsVisible(section))return;
 if(settingsPending.has(section))return settingsPending.get(section);
 $('settingsStatus').textContent='Wird geladen …';
 const task=safeLoad('Einstellungen',async()=>{
  if(section==='network')await loadNetwork();
  else if(section==='system'){
   if(!$('licensesLink')){const link=document.createElement('a');link.id='licensesLink';link.className='btn';link.href='/licenses';link.textContent='Über Home-Display · Lizenzen';$('system').append(link)}
   await loadSettings();await loadLanguage();if(settingsVisible(section))await loadService8088()
  }
  else if(section==='diagnostics')await loadDiagnostics();
 }).finally(()=>{settingsPending.delete(section);if(settingsVisible(section))$('settingsStatus').textContent=''});
 settingsPending.set(section,task);return task;
}
async function showPage(page){
 activePage=page;
 document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('active',b.dataset.page===page));
 document.querySelectorAll('.page').forEach(p=>p.classList.toggle('active',p.id===page));
 if(page==='settings')await refreshSettingsSection();
 else if(page==='apps')await safeLoad('Apps',loadApps);
}
function showSettings(section){
 activeSettings=section;
 document.querySelectorAll('[data-settings]').forEach(b=>b.classList.toggle('active',b.dataset.settings===section));
 document.querySelectorAll('.settings-pane').forEach(p=>p.classList.toggle('active',p.id===section));
 return refreshSettingsSection(section);
}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>showPage(b.dataset.page));
document.querySelectorAll('[data-settings]').forEach(b=>b.onclick=()=>showSettings(b.dataset.settings));
$('appsAdvanced').onclick=async e=>{e.preventDefault();await showPage('apps');$('appInstallerCard').scrollIntoView({behavior:'smooth',block:'start'})};
function renderWebApps(apps){
 const host=$('webApps');host.replaceChildren();
 for(const app of apps){
  if(!app.webPath)continue;
  let url;try{url=new URL(app.webPath,location.origin)}catch(e){continue}
  if(!['http:','https:'].includes(url.protocol))continue;
  if(app.kind!=='Web'&&(url.origin!==location.origin||!app.webPath.startsWith('/')||app.webPath.startsWith('//')))continue;
  const card=document.createElement('div');card.className='card';
  const name=document.createElement('h3');name.textContent=app.name;card.appendChild(name);
  const note=document.createElement('p');note.className='small';note.textContent=(app.kind==='Web'?'Externe Web-App':'App-Weboberfläche')+(app.state==='disabled'?' · deaktiviert':'');card.appendChild(note);
  const link=document.createElement('a');link.className='btn primary';link.textContent='Öffnen';link.href=url.href;
  if(app.kind==='Web'){link.target='_blank';link.rel='noopener noreferrer'}
  card.appendChild(link);host.appendChild(card);
 }
 if(!host.childElementCount)host.textContent='Keine App-Weboberflächen verfügbar.';
}
let csrfToken='';async function ensureCsrf(){if(csrfToken)return csrfToken;let r=await hpWeb.fetch('/api/csrf',{cache:'no-store'});if(!r.ok)throw Error('CSRF-Token konnte nicht geladen werden');csrfToken=(await r.json()).token||'';return csrfToken}async function api(url,opt={}){opt.cache='no-store';let method=(opt.method||'GET').toUpperCase();if(method!=='GET'&&method!=='HEAD'){await ensureCsrf();opt.headers=Object.assign({},opt.headers||{}, {'X-HP-CSRF':csrfToken})}let r=await hpWeb.fetch(url,opt);if(!r.ok)throw Error((await r.text())||('HTTP '+r.status));let ct=r.headers.get('content-type')||'';return ct.includes('json')?r.json():r.text()}
async function safeLoad(name,fn){try{await fn();return true}catch(e){$('headline').textContent=name+': '+e.message;return false}}
// Compact KiB display avoids rounding e.g. 32 KiB largest block to "0.0 MB".
const memText=n=>n==null||!Number.isFinite(Number(n))?'–':Number(n)<1048576?(Number(n)/1024).toFixed(1)+' KiB':(Number(n)/1048576).toFixed(1)+' MiB';
const diskText=n=>n>0?(n>=1073741824?(n/1073741824).toFixed(1)+' GiB':(n/1048576).toFixed(1)+' MiB'):'Größe nicht ermittelt';
async function loadStatus(){
 const s=await api('/api/status'),m=s.memory||{},d=s.storage||{},usb=d.usb||{};
 $('headline').textContent=s.network.path+' · '+(s.network.ip||'keine IP');
 $('mNet').textContent=s.network.path;$('mIp').textContent=s.network.ip||'Offline';
 $('mFw').textContent=s.version;$('mUptime').textContent='Laufzeit '+Math.floor(s.uptime/3600)+' h';
 $('mRam').textContent=memText(m.freeInternal);$('mLargest').textContent='größter Block '+memText(m.largestInternal);
 $('mPsram').textContent=memText(m.freePsram);$('mStorage').textContent='AppFS '+(d.appfs?'bereit':'nicht verfügbar');
 $('mUsb').textContent=usb.mounted?'Bereit':usb.present?'Erkannt':'Nicht angeschlossen';
 $('mUsbDetail').textContent=usb.mounted?diskText(usb.capacity):usb.present?'Datenträger nicht eingebunden':'Kein USB-Massenspeicher';
 $('mSd').textContent=d.sd?'Bereit':'Nicht vorhanden';$('mSdDetail').textContent=d.sd?diskText(d.sdCapacity):'Keine SD-Karte eingebunden';
 const free=Number(m.freeInternal),largest=Number(m.largestInternal);
 const low=Number.isFinite(free)&&Number.isFinite(largest)&&(free<65536||largest<16384);
 const critical=Number.isFinite(free)&&Number.isFinite(largest)&&(free<32768||largest<8192);
 $('mRam').classList.toggle('memory-warning',low&&!critical);$('mRam').classList.toggle('memory-critical',critical);
 const problems=[];if(s.recoveryRequired)problems.push('Recovery erforderlich');if(low)problems.push('Wenig interner RAM – Diagnose prüfen');
 if(usb.present&&!usb.mounted)problems.push('USB erkannt, aber nicht eingebunden');
 if(!d.appfs)problems.push('AppFS nicht verfügbar');
 $('health').replaceChildren();let status=document.createElement('div');status.className='status '+(problems.length?'warn':'ok');
 const dot=document.createElement('span');dot.className='dot';const message=document.createElement('span');message.textContent=problems.length?problems.join(' · '):'System meldet keine bekannten Warnungen';
 status.append(dot,message);$('health').append(status);
}

let installedApps={};
async function loadApps(){let a=await api('/api/apps'),b=$('appsBody');b.innerHTML='';installedApps={};renderWebApps(a.apps||[]);if(!a.apps||!a.apps.length){b.innerHTML='<tr><td colspan="6" class="small">Keine Apps im Registry-Snapshot. Erneut laden oder Gerät neu starten.</td></tr>';return}a.apps.forEach(x=>{installedApps[x.id]=x;let tr=document.createElement('tr');let settings='<span class="small">–</span>';if(x.kind==='Lua')settings='<button class="btn primary" onclick="editAppSettings(\''+js(x.id)+'\')">Im Web bearbeiten</button> ';if(x.hasSettings)settings+='<button class="btn" onclick="openAppSettings(\''+js(x.id)+'\')">Am Display öffnen</button>';let actions=x.canDisable?'<button class="btn" onclick="toggleApp(\''+js(x.id)+'\','+(x.state!=='enabled')+')">'+(x.state==='enabled'?'Deaktivieren':'Aktivieren')+'</button>':'<span class="small">System-App</span>';if(x.removable){if(x.kind==='Lua'){actions+=' <button class="btn" onclick="backupApp(\''+js(x.id)+'\')">Daten sichern</button>';actions+=' <button class="btn danger" onclick="removeApp(\''+js(x.id)+'\',\''+js(x.kind)+'\',false)">App löschen</button>';actions+=' <button class="btn danger" onclick="removeApp(\''+js(x.id)+'\',\''+js(x.kind)+'\',true)">Komplett löschen</button>'}else actions+=' <button class="btn danger" onclick="removeApp(\''+js(x.id)+'\',\''+js(x.kind)+'\',true)">Löschen</button>'}tr.innerHTML='<td><b>'+esc(x.name)+'</b><div class="small">'+esc(x.id)+'</div></td><td>'+esc(x.kind)+'</td><td>'+esc(x.version||'–')+'</td><td>'+esc(x.state)+'</td><td>'+settings+'</td><td>'+actions+'</td>';b.appendChild(tr)})}
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}function js(s){return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'")}
async function toggleApp(id,on){await api('/api/apps/toggle?id='+encodeURIComponent(id)+'&on='+(on?1:0),{method:'POST'});await loadApps()}
async function backupApp(id,silent=false){try{let r=await fetch('/api/apps/export?id='+encodeURIComponent(id),{cache:'no-store'});if(!r.ok)throw Error((await r.text())||('HTTP '+r.status));let b=await r.blob(),u=URL.createObjectURL(b),a=document.createElement('a');a.href=u;a.download=id+'-backup.json';document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(u),1000);if(!silent)$('headline').textContent='Backup für '+id+' wurde erstellt.';return true}catch(e){alert('Backup fehlgeschlagen: '+e.message+'\nKomplett-Löschen wurde nicht gestartet.');return false}}
async function removeApp(id,kind,purge){let msg=purge&&kind==='Lua'?'App „'+id+'“ KOMPLETT löschen?\n\nVorher wird automatisch ein Backup der normalen Einstellungen und App-Dateien heruntergeladen. Secret-Werte werden aus Sicherheitsgründen nicht exportiert.':'App „'+id+'“ löschen?\n\nEinstellungen und App-Daten bleiben erhalten und können bei einer späteren Neuinstallation wieder verwendet werden.';if(!confirm(msg))return;if(purge&&kind==='Lua'){if(!confirm('Letzte Bestätigung:\n\nAlle gespeicherten Daten von „'+id+'“ wirklich endgültig löschen?'))return;if(!await backupApp(id,true))return}try{let url=kind==='Lua'?'/apps/lua/remove?id='+encodeURIComponent(id)+'&purge='+(purge?1:0):'/apps/remove?id='+encodeURIComponent(id);await api(url,{method:'POST'});closeAppSettings();await loadApps();$('headline').textContent=purge&&kind==='Lua'?'App '+id+' inklusive Daten wurde vollständig gelöscht.':'App '+id+' wurde gelöscht; Einstellungen und Daten bleiben erhalten.'}catch(e){alert('Löschen fehlgeschlagen: '+e.message)}}
function zipU16(v,o){return v.getUint16(o,true)}function zipU32(v,o){return v.getUint32(o,true)}
async function zipInflateRaw(b){if(!('DecompressionStream'in window))throw Error('Dieser Browser kann ZIP nicht entpacken');let d=new DecompressionStream('deflate-raw');return new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(d)).arrayBuffer())}
async function readAppZip(file){let b=await file.arrayBuffer(),v=new DataView(b),u=new Uint8Array(b),e=-1;for(let i=b.byteLength-22;i>=Math.max(0,b.byteLength-65557);--i){if(zipU32(v,i)===0x06054b50){e=i;break}}if(e<0)throw Error('Ungültige ZIP-Datei');let n=zipU16(v,e+10),o=zipU32(v,e+16),out={};for(let k=0;k<n;k++){if(zipU32(v,o)!==0x02014b50)throw Error('ZIP-Verzeichnis ungültig');let m=zipU16(v,o+10),cs=zipU32(v,o+20),nl=zipU16(v,o+28),xl=zipU16(v,o+30),cl=zipU16(v,o+32),lo=zipU32(v,o+42),name=new TextDecoder().decode(u.slice(o+46,o+46+nl)).replace(/\\/g,'/'),ln=zipU16(v,lo+26),lx=zipU16(v,lo+28),st=lo+30+ln+lx,data=u.slice(st,st+cs),plain=m===0?data:(m===8?await zipInflateRaw(data):null);if(!plain)throw Error('ZIP-Kompression '+m+' nicht unterstützt');let base=name.split('/').pop().toLowerCase();if(/^assets\/[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(png|jpe?g|bmp|gif|webp|tga)$/.test(name)){if(name.length>63||name.includes('..')||out[name]||plain.byteLength>131072)throw Error('Ungültiges Bild-Asset');let assets=Object.keys(out).filter(x=>x.startsWith('assets/'));if(assets.length>=16||assets.reduce((sum,x)=>sum+out[x].byteLength,0)+plain.byteLength>524288)throw Error('Zu viele oder zu große Assets');out[name]=plain}else if(base==='main.lua'||base==='settings.lua'||base==='web_settings.json'||base==='app.json'||base==='manifest.json'){if(name!==base||out[base])throw Error('Paketdateien müssen eindeutig im ZIP-Hauptverzeichnis liegen');out[base]=plain}o+=46+nl+xl+cl}return out}
function appVersionCmp(a,b){const parts=v=>{let pos=0,out=[];v=String(v||'');for(let i=0;i<4;i++){while(pos<v.length&&!/[0-9]/.test(v[pos]))pos++;let n=0;while(pos<v.length&&/[0-9]/.test(v[pos]))n=Math.min(2147483647,n*10+Number(v[pos++]));out.push(n);while(pos<v.length&&v[pos]!=='.')pos++;if(pos<v.length)pos++}return out};const A=parts(a),B=parts(b);for(let i=0;i<4;i++){if(A[i]<B[i])return-1;if(A[i]>B[i])return 1}return 0}
function validAppId(x){return /^[a-z0-9_-]{1,31}$/.test(x)}
function validateAppManifest(o,firmwareVersion){
  const fail=reason=>{throw Error('app.json: '+reason)};
  const own=k=>Object.prototype.hasOwnProperty.call(o,k);
  const bytes=s=>new TextEncoder().encode(s).length;
  const trim=s=>s.replace(/^[\x09-\x0d\x20]+|[\x09-\x0d\x20]+$/g,'');
  const version=s=>bytes(s)<=24&&/^[A-Za-z0-9._+\-]+$/.test(s)&&/[0-9]/.test(s);
  if(!o||typeof o!=='object'||Array.isArray(o)||o.schema!==3)fail('Schema 3 erforderlich');
  for(const k of ['id','name','version','type','entry']){
    if(typeof o[k]!=='string')fail(k+' fehlt');
    o[k]=trim(o[k]);
  }
  if(!validAppId(o.id))fail('ungueltige ID');
  if(!bytes(o.name)||bytes(o.name)>48)fail('Name muss 1 bis 48 UTF-8-Bytes haben');
  if(!version(o.version))fail('ungueltige Version');
  if(o.type!=='lua'||o.entry!=='main.lua')fail('type/entry ungueltig');
  if(typeof o.settings!=='boolean')fail('settings muss true/false sein');
  if(o.settings ? o.settingsEntry!=='settings.lua' : own('settingsEntry'))fail('settingsEntry ungueltig');
  if(own('enabled')&&typeof o.enabled!=='boolean')fail('enabled muss true/false sein');
  const icon=trim(typeof o.icon==='string'?o.icon:'home');
  if(bytes(icon)>31)fail('Icon zu lang');
  const minimum=trim(typeof o.minFirmware==='string'?o.minFirmware:'');
  if(minimum&&(!version(minimum)||typeof firmwareVersion!=='string'||appVersionCmp(firmwareVersion,minimum)<0))fail('minFirmware ungueltig oder Firmware zu alt');
  const allowed=new Set(['network.local','network.internet','storage','secrets','discovery','mqtt','websocket','home_assistant','energy','media','storage.read','storage.writeAppData','storage.nasRead','system.wallpaper','power.idleIntegration','calendar.read','calendar.write','camera','microphone','intercom','door.open']);
  if(!Array.isArray(o.permissions)||o.permissions.length>16)fail('permissions ungueltig');
  const seen=new Set();
  for(const p of o.permissions){
    const bit=p==='storage.writeAppData'?'storage':p;
    if(typeof p!=='string'||!allowed.has(p)||seen.has(bit))fail('permissions unbekannt oder doppelt');
    seen.add(bit);
  }
  if(own('background')){
    const b=o.background;
    if(!b||typeof b!=='object'||Array.isArray(b))fail('background muss ein Objekt sein');
    const keys=new Set(['enabled','priority','preferredIntervalMinutes','maximumDelayMinutes','requiresNetwork','allowDuringHighLoad']);
    if(Object.keys(b).some(k=>!keys.has(k)))fail('background: unbekanntes Feld');
    if(typeof b.enabled!=='boolean')fail('background.enabled muss true/false sein');
    const p=typeof b.priority==='string'?b.priority:'none';
    if(!['none','low','normal','important'].includes(p)||b.enabled&&p==='none')fail('background.priority ungueltig');
    for(const k of ['preferredIntervalMinutes','maximumDelayMinutes'])
      if(Object.prototype.hasOwnProperty.call(b,k)&&(!Number.isInteger(b[k])||b[k]<0||b[k]>10080))fail('background: Intervall ungueltig');
    for(const k of ['requiresNetwork','allowDuringHighLoad'])
      if(Object.prototype.hasOwnProperty.call(b,k)&&typeof b[k]!=='boolean')fail('background: Boolean erforderlich');
    const interval=b.preferredIntervalMinutes===undefined?30:b.preferredIntervalMinutes;
    const delay=b.maximumDelayMinutes===undefined?60:b.maximumDelayMinutes;
    if(interval<(p==='important'?5:p==='normal'?15:30)||delay<interval)fail('background: Intervallgrenzen verletzt');
  }
}
function uploadPart(url,data,headers,onProgress){return hpWeb.upload(url,data,{headers:{'X-HP-CSRF':csrfToken,'X-Install-Session':installSession,...headers},signal:url==='/apps/lua/commit'?undefined:appZipController?.signal,onProgress})}
let preparedAppZip=null,appZipFiles=[],appZipBatchRunning=false,appZipController=null,lastAppZipError='',prepareSequence=0;
const installSession=[...crypto.getRandomValues(new Uint8Array(16))].map(x=>x.toString(16).padStart(2,'0')).join('');
function appInstallProgress(v,t){$('appZipBar').style.width=Math.max(0,Math.min(100,v))+'%';$('appZipStatus').textContent=t}
function validateWebSettingsBytes(bytes){if(!bytes)return;if(bytes.byteLength===0||bytes.byteLength>16384)throw Error('web_settings.json muss 1 Byte bis 16 KiB groß sein');let w;try{w=JSON.parse(new TextDecoder().decode(bytes))}catch(e){throw Error('web_settings.json ist kein gültiges JSON: '+e.message)}if(w.schema!==1)throw Error('web_settings.json: schema muss 1 sein');if(!Array.isArray(w.tabs)||w.tabs.length<1||w.tabs.length>12)throw Error('web_settings.json: 1 bis 12 Tabs erforderlich');const idOk=x=>typeof x==='string'&&/^[A-Za-z0-9_-]{1,48}$/.test(x),txt=(x,n)=>typeof x==='string'&&x.length>0&&x.length<=n,simple=new Set(['text','password','bool','integer','number','select','host','url']),tabs=new Set(),keys=new Set();let top=0,total=0;function options(f){if(!Array.isArray(f.options)||f.options.length<1||f.options.length>24)throw Error('web_settings.json: Auswahlfeld benötigt 1 bis 24 Optionen');let seen=new Set();for(let o of f.options){let v,l;if(typeof o==='string'){v=o;l=o}else if(o&&typeof o==='object'&&typeof o.value==='string'&&typeof o.label==='string'){v=o.value;l=o.label}else throw Error('web_settings.json: Auswahloption ist ungültig');if(!txt(v,96)||!txt(l,96))throw Error('web_settings.json: Auswahloption ist zu lang');if(seen.has(v))throw Error('web_settings.json: Auswahlwert ist doppelt');seen.add(v)}}function sf(f,n){if(!f||!idOk(f.key)||!txt(f.label,72)||!simple.has(f.type))throw Error('web_settings.json: Feld-Key, Beschriftung oder Typ ist ungültig');if(n&&f.type==='password')throw Error('web_settings.json: Passwort im Repeater nicht erlaubt');if(f.help!==undefined&&!txt(f.help,220))throw Error('web_settings.json: Hilfetext ist ungültig');if(f.placeholder!==undefined&&(typeof f.placeholder!=='string'||f.placeholder.length>120))throw Error('web_settings.json: Platzhalter ist zu lang');if(f.maxLength!==undefined&&(!Number.isInteger(f.maxLength)||f.maxLength<1||f.maxLength>2048))throw Error('web_settings.json: maxLength muss 1 bis 2048 sein');if(f.type==='select')options(f)}for(let t of w.tabs){if(!t||!idOk(t.id)||!txt(t.label,48)||!Array.isArray(t.fields))throw Error('web_settings.json: Tab ist ungültig');if(tabs.has(t.id))throw Error('web_settings.json: Tab-ID ist doppelt');tabs.add(t.id);if(top+t.fields.length>48)throw Error('web_settings.json: maximal 48 Hauptfelder erlaubt');for(let f of t.fields){top++;if(++total>96)throw Error('web_settings.json: maximal 96 Felder inklusive Repeater');if(!f||!idOk(f.key))throw Error('web_settings.json: Feld-Key ist ungültig');if(keys.has(f.key))throw Error('web_settings.json: Einstellungs-Key '+f.key+' ist doppelt');keys.add(f.key);if(f.type==='repeater'){if(!txt(f.label,72)||!Array.isArray(f.fields)||f.fields.length<1||f.fields.length>16)throw Error('web_settings.json: Repeater ist ungültig');let mi=f.maxItems===undefined?8:f.maxItems;if(!Number.isInteger(mi)||mi<1||mi>16)throw Error('web_settings.json: Repeater maxItems muss 1 bis 16 sein');validateDiscoverySchema(f);let ck=new Set();for(let c of f.fields){sf(c,true);if(c.key===f.key)throw Error('web_settings.json: Repeater-Key kollidiert mit Unterfeld');if(ck.has(c.key))throw Error('web_settings.json: Repeater-Unterfeld '+c.key+' ist doppelt');ck.add(c.key);if(++total>96)throw Error('web_settings.json: maximal 96 Felder inklusive Repeater')}}else {if(f.discovery!==undefined)throw Error('Discovery nur im Repeater erlaubt');sf(f,false)}}}}
async function prepareAppZip(file){const sequence=++prepareSequence;lastAppZipError='';preparedAppZip=null;$('appZipInstall').disabled=true;$('appZipMeta').textContent='';$('appDowngradeLabel').style.display='none';$('appDowngrade').checked=false;if(!file)return;try{appInstallProgress(5,'ZIP wird geprüft …');if(!/\.zip$/i.test(file.name)||file.size<=0||file.size>8388608)throw Error('ZIP: maximal 8 MiB');let z=await readAppZip(file);if(sequence!==prepareSequence)return null;if(!z['main.lua'])throw Error('main.lua fehlt');if(z['web_settings.json'])validateWebSettingsBytes(z['web_settings.json']);let manifest=z['app.json']||z['manifest.json'];if(!manifest)throw Error('app.json fehlt');if(manifest.length>8192)throw Error('app.json: maximal 8192 Bytes');let obj=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(manifest));const depth=v=>v&&typeof v==='object'?1+Math.max(0,...Object.values(v).map(depth)):0;if(depth(obj)>4)throw Error('app.json: maximale Verschachtelung 4');let firmwareVersion;if(typeof obj?.minFirmware==='string'&&obj.minFirmware.trim())firmwareVersion=(await api('/api/status')).version;if(sequence!==prepareSequence)return null;validateAppManifest(obj,firmwareVersion);if(obj.settings&&!z['settings.lua'])throw Error('settings.lua fehlt');if(!obj.settings&&z['settings.lua'])throw Error('settings.lua ist laut Manifest nicht vorgesehen');let current=installedApps[obj.id]?.version,mode='Neuinstallation';if(current!==undefined){let c=appVersionCmp(obj.version,current);mode=c>0?'Update':c===0?'Neuinstallation gleicher Version':'Downgrade';if(c<0)$('appDowngradeLabel').style.display='block'}preparedAppZip={id:obj.id,name:obj.name,version:obj.version,current,mode,manifest,manifestName:z['app.json']?'app.json':'manifest.json',main:z['main.lua'],settings:z['settings.lua'],webSettings:z['web_settings.json'],assets:Object.keys(z).filter(x=>x.startsWith('assets/')).map(name=>({name,data:z[name]}))};$('appZipMeta').innerHTML='<b>'+esc(mode)+'</b> · '+esc(obj.name)+' · '+esc(obj.version)+(current!==undefined?' · installiert '+esc(current):'')+(preparedAppZip.webSettings?' · Web-Einstellungen enthalten':'');$('appZipInstall').disabled=false;appInstallProgress(15,'Paket geprüft. Noch nichts verändert.');return preparedAppZip}catch(e){lastAppZipError=e.message;appInstallProgress(0,'Fehler: '+e.message);return null}}
async function installPreparedApp(batch=false){
 const p=preparedAppZip;if(!p)throw Error('Kein geprüftes Paket.');await ensureCsrf();
 const reloadSettings=appSettingsModel&&appSettingsModel.id===p.id;
 if(p.mode==='Downgrade'&&!$('appDowngrade').checked)throw Error('Downgrade muss ausdrücklich bestätigt werden.');
 $('appZipInstall').disabled=true;$('appZipFile').disabled=true;
 try{
  await uploadPart('/apps/lua/cancel',new Uint8Array(),{'X-App-Id':p.id});
  await uploadPart('/apps/lua/file',p.manifest,{'X-App-Id':p.id,'X-App-File':p.manifestName},q=>appInstallProgress(20+q*15,'Manifest wird übertragen …'));
  await uploadPart('/apps/lua/file',p.main,{'X-App-Id':p.id,'X-App-File':'main.lua'},q=>appInstallProgress(35+q*30,'main.lua wird übertragen …'));
  if(p.settings)await uploadPart('/apps/lua/file',p.settings,{'X-App-Id':p.id,'X-App-File':'settings.lua'},q=>appInstallProgress(65+q*6,'settings.lua wird übertragen …'));
  if(p.webSettings)await uploadPart('/apps/lua/file',p.webSettings,{'X-App-Id':p.id,'X-App-File':'web_settings.json'},q=>appInstallProgress(71+q*5,'web_settings.json wird übertragen …'));
  for(const a of p.assets)await uploadPart('/apps/lua/file',a.data,{'X-App-Id':p.id,'X-App-File':a.name},()=>appInstallProgress(78,'Assets werden übertragen …'));
  if(appZipController?.signal.aborted)throw new DOMException('Stapel abgebrochen','AbortError');
  const result=await uploadPart('/apps/lua/commit',new Uint8Array(),{'X-App-Id':p.id,'X-Allow-Downgrade':$('appDowngrade').checked?'1':'0'},q=>appInstallProgress(82+q*15,'Paket wird geprüft und aktiviert …'));
  appInstallProgress(100,result||'App erfolgreich installiert.');
  try{await loadApps();if(reloadSettings)await editAppSettings(p.id)}catch(e){$('appZipStatus').textContent+=' Übersicht später neu laden.'}
  preparedAppZip=null;return result||'Installiert';
 }catch(e){
  // Idempotent staging cleanup is separate from the cancelled data transfer.
  // Never repeat a commit whose HTTP result was lost.
  for(let attempt=0;attempt<3;++attempt){try{await hpWeb.upload('/apps/lua/cancel',new Uint8Array(),{headers:{'X-HP-CSRF':csrfToken,'X-Install-Session':installSession,'X-App-Id':p.id,'X-Install-Release':'1'}});break}catch(cleanup){if(cleanup.status!==503)break;await new Promise(resolve=>setTimeout(resolve,200))}}
  throw e;
 }finally{preparedAppZip=null;if(!batch)$('appZipFile').disabled=false}
}
async function chooseAppZips(files){
 if(appZipBatchRunning)return;const list=[...files];if(!list.length||list.length>32){appInstallProgress(0,'1 bis 32 App-ZIPs auswählen.');return}
 appZipFiles=list;preparedAppZip=null;$('appZipResults').replaceChildren();$('appZipMeta').textContent=list.map(f=>f.name).join(' · ');$('appZipOverall').textContent=list.length+' Paket(e), Installation nacheinander.';
 if(list.length===1)await prepareAppZip(list[0]);else{$('appDowngradeLabel').style.display='none';$('appDowngrade').checked=false}
 $('appZipInstall').disabled=false;
}
async function installAppBatch(){
 if(appZipBatchRunning||!appZipFiles.length)return;
 appZipBatchRunning=true;appZipController=new AbortController();const files=appZipFiles.slice(),approved=$('appDowngrade').checked;
 $('appZipFile').disabled=true;$('appZipStop').disabled=false;$('appZipResults').replaceChildren();$('appZipInstall').disabled=true;
 try{
  const batch=await hpWeb.batch(files,async(file,index,total)=>{
   $('appZipOverall').textContent=(index+1)+' / '+total+' · '+file.name;
   const p=await prepareAppZip(file);$('appZipInstall').disabled=true;if(!p)throw Error(lastAppZipError||'ZIP-Prüfung fehlgeschlagen');
   if(p.mode==='Downgrade'){$('appDowngrade').checked=(files.length===1&&approved)||confirm(p.name+': ausdrücklich von '+p.current+' auf '+p.version+' zurückstufen?');if(!$('appDowngrade').checked)throw Error('Downgrade nicht bestätigt.')}
   return installPreparedApp(true);
  },{signal:appZipController.signal,max:32,onResult:(result,total)=>{
   const row=document.createElement('li');row.textContent=files[result.index].name+' — '+(result.ok?result.value:'Fehler: '+result.error.message);$('appZipResults').append(row);
   $('appZipOverall').textContent=(result.index+1)+' / '+total+' abgeschlossen';
  }});
  appZipFiles=files.slice(batch.results.length);$('appZipStatus').textContent=(batch.stopped?'Stapel gestoppt. ':'Stapel abgeschlossen. ')+batch.results.filter(r=>r.ok).length+' erfolgreich; '+batch.results.filter(r=>!r.ok).length+' fehlgeschlagen; '+appZipFiles.length+' nicht gestartet. Bereits installierte Apps bleiben erhalten.';
 }catch(e){appInstallProgress(0,e.message)}finally{appZipBatchRunning=false;appZipController=null;$('appZipFile').disabled=false;$('appZipStop').disabled=true;$('appZipInstall').disabled=!appZipFiles.length;$('appZipFile').value=''}
}
function bindAppInstaller(){
 const d=$('appZipDrop'),f=$('appZipFile');d.onclick=()=>{if(!appZipBatchRunning)f.click()};
 d.ondragover=e=>{e.preventDefault();d.classList.add('drag')};d.ondragleave=()=>d.classList.remove('drag');
 d.ondrop=e=>{e.preventDefault();d.classList.remove('drag');chooseAppZips(e.dataTransfer.files)};f.onchange=()=>chooseAppZips(f.files);$('appZipInstall').onclick=installAppBatch;
 $('appZipStop').onclick=()=>{appZipController?.abort();$('appZipStop').disabled=true;$('appZipStatus').textContent='Abbruch angefordert. Eine bereits laufende Aktivierung wird sicher beendet; keine weiteren Apps werden gestartet.'};
}

async function openAppSettings(id){let r=await api('/api/apps/settings/open?id='+encodeURIComponent(id),{method:'POST'});$('headline').textContent=r.message||'App-Einstellungen am Display geöffnet.'}
let appSettingsModel=null;
let appSettingsTab='';
function closeAppSettings(){$('appSettingsCard').style.display='none';appSettingsModel=null;appSettingsTab='';appDiscoveryEpoch++;appDiscoveryResults.clear()}
function simpleFieldInput(f,prefix='aws_'){
  let id=prefix+f.key,help=f.help?'<div class="small">'+esc(f.help)+'</div>':'';
  let label='<label class="label" for="'+id+'">'+esc(f.label)+'</label>';
  if(f.type==='bool')return '<div class="card" style="margin:8px 0"><label><input id="'+id+'" data-key="'+esc(f.key)+'" data-kind="bool" type="checkbox" style="width:auto;margin-right:8px" '+(f.value?'checked':'')+'> '+esc(f.label)+'</label>'+help+'</div>';
  if(f.type==='select'){let opts=(f.options||[]).map(o=>'<option value="'+esc(o.value)+'" '+(String(o.value)===String(f.value)?'selected':'')+'>'+esc(o.label)+'</option>').join('');return '<div style="margin:12px 0">'+label+'<select id="'+id+'" data-key="'+esc(f.key)+'" data-kind="select">'+opts+'</select>'+help+'</div>'}
  let type=f.type==='password'?'password':(f.type==='integer'||f.type==='number'?'number':(f.type==='url'?'url':'text'));
  let attrs='';if(f.min!==undefined)attrs+=' min="'+f.min+'"';if(f.max!==undefined)attrs+=' max="'+f.max+'"';if(f.step!==undefined)attrs+=' step="'+f.step+'"';if(f.maxLength)attrs+=' maxlength="'+f.maxLength+'"';
  let placeholder=f.type==='password'?(f.configured?'Gesetzt – leer lassen zum Behalten':'Noch nicht gesetzt'):(f.placeholder||'');
  let value=f.type==='password'?(f.draftSecret??''):(f.value??'');
  let clear=f.type==='password'&&f.configured?'<label class="small"><input id="'+id+'_clear" type="checkbox" style="width:auto" '+(f.draftClear?'checked':'')+'> Gespeichertes Passwort löschen</label>':'';
  return '<div style="margin:12px 0">'+label+'<input id="'+id+'" data-key="'+esc(f.key)+'" data-kind="'+esc(f.type)+'" type="'+type+'" value="'+esc(value)+'" placeholder="'+esc(placeholder)+'"'+attrs+'>'+clear+help+'</div>';
}
function repeaterField(f){
  let discovery=f.discovery?'<button class="btn" onclick="startAppDiscovery(\''+js(f.key)+'\')">LAN-Geräte suchen</button><div id="discovery_'+esc(f.key)+'" class="small" style="margin-top:10px"></div>':'';
  let status=f.key==='printer_profiles'?'<div class="small">'+(f.items||[]).length+' eingerichtet · '+(f.items||[]).filter(item=>printerStateFor(f,item)?.online).length+' verbunden (Cache)</div><button class="btn" onclick="refreshPrinterStatus()">Status aktualisieren</button>':'';
  let html='<div class="card" style="margin:12px 0"><div class="row"><div><b>'+esc(f.label)+'</b>'+(f.help?'<div class="small">'+esc(f.help)+'</div>':'')+'</div><button class="btn" onclick="addRepeaterItem(\''+js(f.key)+'\')">+ Hinzufügen</button></div><div id="rep_'+esc(f.key)+'">';
  (f.items||[]).forEach((item,i)=>{html+=repeaterItemHtml(f,item,i)});
  return html+'</div>'+status+discovery+'</div>';
}
function validateDiscoverySchema(f){
  if(f.discovery===undefined)return;
  const d=f.discovery,m=d?.map;
  if(f.type!=='repeater'||!d||d.type!=='printers'||!m||Array.isArray(m)||typeof m!=='object'||typeof m.host!=='string')throw Error('Discovery benötigt Repeater, type=printers und map.host');
  const entries=Object.entries(m),targets=new Set();if(entries.length<1||entries.length>4)throw Error('Discovery-Zuordnung ungültig');
  for(let [source,target] of entries){
    const child=(f.fields||[]).find(c=>c.key===target);
    const types=source==='port'?['integer','number']:source==='host'?['host','text','url']:['text','select'];
    if(!['host','hostname','port','protocol'].includes(source)||typeof target!=='string'||!child||!types.includes(child.type)||targets.has(target))throw Error('Discovery-Zuordnung ungültig');targets.add(target);
  }
  if(d.protocolValues!==undefined){
    if(!d.protocolValues||typeof d.protocolValues!=='object'||Array.isArray(d.protocolValues)||Object.keys(d.protocolValues).length>3)throw Error('Discovery-Protokollwerte ungültig');
    for(let [key,value] of Object.entries(d.protocolValues))if(!['moonraker','rrf','octoprint'].includes(key)||typeof value!=='string'||!value.length||value.length>96)throw Error('Discovery-Protokollwerte ungültig');
  }
}
let appDiscoveryResults=new Map(),appDiscoveryEpoch=0;
function preserveAppSettings(){
  if(!appSettingsModel)return;
  for(let t of appSettingsModel.tabs||[])for(let f of t.fields||[]){
    if(f.type==='repeater'){
      for(let i=0;i<(f.items||[]).length;i++){
        for(let ch of f.fields||[]){let value=readSimpleValue(ch,'rep_'+f.key+'_'+i+'_');if(value!==undefined)f.items[i][ch.key]=value}
        if(f.key==='printer_profiles'&&!(f.fields||[]).some(ch=>ch.key==='enabled')){let toggle=$('printer_enabled_'+i);if(toggle)f.items[i].enabled=toggle.checked}
      }
    }else {
      let value=readSimpleValue(f);
      if(f.type==='password'){if(value!==undefined)f.draftSecret=value;let clear=$('aws_'+f.key+'_clear');if(clear)f.draftClear=clear.checked}
      else if(value!==undefined)f.value=value;
    }
  }
}
async function startAppDiscovery(key){
  const model=appSettingsModel,f=findRepeater(key);if(!model||!f?.discovery)return;
  const epoch=++appDiscoveryEpoch,deadline=Date.now()+20000;appDiscoveryResults.delete(key);
  const active=()=>appSettingsModel===model&&epoch===appDiscoveryEpoch;
  const status=text=>{let el=$('discovery_'+key);if(active()&&el)el.textContent=text};
  status('Suche wird gestartet …');
  try{
    let run=0,result;
    do{
      result=await api('/api/apps/settings/discovery',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:model.id,field:key,run})});
      if(!active())return;run=result.run;status(result.message);
      if(result.done)break;if(Date.now()>=deadline)throw Error('Suche dauert zu lange. Netzwerk ist möglicherweise ausgelastet; bitte später erneut versuchen.');
      await new Promise(resolve=>setTimeout(resolve,500));
    }while(active());
    if(!active())return;appDiscoveryResults.set(key,{model,devices:result.devices||[]});
    let root=$('discovery_'+key);if(!root)return;root.textContent=result.message;
    (result.devices||[]).forEach((device,index)=>{
      const row=document.createElement('div');row.style.marginTop='8px';
      const label=document.createElement('span');label.textContent=device.host+' · '+device.hostname+' · Port '+device.port+' · '+({moonraker:'Klipper / Moonraker',rrf:'RepRapFirmware / Duet',octoprint:'OctoPrint'}[device.protocol]||'Protokoll unbekannt')+' (mDNS) ';row.appendChild(label);
      const button=document.createElement('button');button.className='btn';button.textContent='Als Profil übernehmen';button.disabled=device.protocol==='unknown';button.onclick=()=>useAppDiscovery(key,index);row.appendChild(button);root.appendChild(row);
    });
  }catch(error){status('Fehler: '+error.message)}
}
function useAppDiscovery(key,index){
  const result=appDiscoveryResults.get(key),f=findRepeater(key),device=result?.devices[index];
  if(!f||result?.model!==appSettingsModel||!device||device.protocol==='unknown')return;
  if((f.items||[]).length>=(f.maxItems||8)){alert('Profil-Limit erreicht. Vorhandenes Profil bearbeiten oder entfernen.');return}
  const profile={};
  for(let [source,target] of Object.entries(f.discovery.map||{})){
    const child=(f.fields||[]).find(c=>c.key===target);if(!child)return;
    let value=device[source];if(source==='protocol')value=f.discovery.protocolValues?.[value]??value;
    if(source==='host'&&child.type==='url')value='http://'+device.host+':'+device.port;
    if(child.type==='select'&&!(child.options||[]).some(o=>(typeof o==='string'?o:o.value)===value)){alert('Erkanntes Protokoll passt nicht zur Profilauswahl. Bitte manuell eintragen.');return}
    profile[target]=value;
  }
  const inputs=Array.from($('appSettingsFields').querySelectorAll('input,select')).map(el=>({id:el.id,value:el.value,checked:el.checked}));
  preserveAppSettings();f.items=f.items||[];
  const hostKey=f.discovery.map.host;
  if(f.items.some(item=>item[hostKey]===profile[hostKey]&&(!f.discovery.map.port||item[f.discovery.map.port]===profile[f.discovery.map.port]))){alert('Dieses Gerät ist bereits als Profil eingetragen.');return}
  f.items.push(profile);renderAppSettings();
  for(let input of inputs){let el=$(input.id);if(el){el.value=input.value;el.checked=input.checked}}
  $('appSettingsStatus').textContent='Profil übernommen, noch nicht gespeichert. Angaben prüfen und Speichern wählen; keine Verbindung wurde aufgebaut.';
}
function printerStateFor(f,item){
  const state=f.printer_status?.[item?.id];
  return state&&state.host===item.host&&state.port===item.port&&state.protocol===item.protocol&&state.mode===(item.mode||'auto')&&(state.enabled??true)===(item.enabled??true)?state:null;
}
function printerDefaultPort(protocol){return ['moonraker','klipper'].includes(protocol)?7125:['octoprint','marlin_octoprint'].includes(protocol)?5000:80}
function repeaterItemHtml(f,item,index){
  let title=(f.itemLabel||'Eintrag')+' '+(index+1)+(item?.name?' · '+item.name:'');
  let html='<div class="card" data-repeater="'+esc(f.key)+'" data-index="'+index+'" style="margin-top:10px"><div class="row"><b>'+esc(title)+'</b><button class="btn danger" onclick="removeRepeaterItem(\''+js(f.key)+'\','+index+')">Entfernen</button></div>';
  (f.fields||[]).forEach(ch=>{let c=Object.assign({},ch,{value:item&&item[ch.key]!==undefined?item[ch.key]:(ch.default??(ch.type==='bool'?false:''))});html+=simpleFieldInput(c,'rep_'+f.key+'_'+index+'_')});
  if(f.key==='printer_profiles'){
    if(!(f.fields||[]).some(ch=>ch.key==='enabled'))html+='<label class="small"><input id="printer_enabled_'+index+'" type="checkbox" style="width:auto" '+(item.enabled!==false?'checked':'')+'> Drucker aktivieren (nach Speichern)</label>';
    const state=printerStateFor(f,item);
    html+='<div class="small">'+(state?(state.stale?'Veralteter Status · ':state.online?'Verbunden · ':'Offline · ')+esc(state.message||state.state||'')+' · '+esc(state.firmware||state.protocol)+' · '+(state.sensor_count||0)+' Temperatursensoren'+(state.truncated?' (Liste begrenzt)':'')+' · vor '+Math.floor((state.age_ms||0)/1000)+' s':'Noch keine Statusdaten; Drucker-App öffnen')+'</div>';
  }
  return html+'</div>';
}
function findRepeater(key){for(let t of appSettingsModel.tabs||[])for(let f of t.fields||[])if(f.key===key&&f.type==='repeater')return f;return null}
function addRepeaterItem(key){
  let f=findRepeater(key);if(!f)return;preserveAppSettings();f.items=f.items||[];
  const limit=key==='printer_profiles'?Math.min(f.maxItems||8,5):(f.maxItems||8);
  if(f.items.length>=limit){alert('Maximal '+limit+' Einträge');return}
  let item={};for(let ch of f.fields||[])if(ch.default!==undefined)item[ch.key]=ch.default;
  if(key==='printer_profiles'){
    let n=1;const used=new Set(f.items.map(p=>p.id));while(used.has('printer_'+n))n++;
    item={...item,id:'printer_'+n,name:item.name||'Drucker '+n,protocol:item.protocol||'reprap',mode:'auto',background:true,enabled:true};
    item.port=item.port||printerDefaultPort(item.protocol);
  }
  f.items.push(item);renderAppSettings();
}
function removeRepeaterItem(key,index){let f=findRepeater(key);if(!f||!f.items)return;preserveAppSettings();f.items.splice(index,1);renderAppSettings()}
function showAppSettingsTab(id){preserveAppSettings();appSettingsTab=id;renderAppSettings()}
async function refreshPrinterStatus(){
  const model=appSettingsModel;if(!model)return;preserveAppSettings();
  try{
    const fresh=await api('/api/apps/settings?id='+encodeURIComponent(model.id));if(appSettingsModel!==model)return;
    for(let tab of model.tabs||[])for(let field of tab.fields||[]){
      if(field.key!=='printer_profiles'||field.type!=='repeater')continue;
      const updated=(fresh.tabs||[]).flatMap(t=>t.fields||[]).find(f=>f.key===field.key&&f.type==='repeater');
      field.printer_status=updated?.printer_status||{};
      field.printer_online=(field.items||[]).filter(item=>printerStateFor(field,item)?.online).length;
    }
    preserveAppSettings();renderAppSettings();
  }catch(e){if(appSettingsModel===model)$('appSettingsStatus').textContent='Status: '+e.message}
}
function renderAppSettings(){
  if(!appSettingsModel)return;
  let tabs=appSettingsModel.tabs||[];
  if(!appSettingsTab&&tabs.length)appSettingsTab=tabs[0].id;
  let root=$('appSettingsFields');root.innerHTML='';
  if(tabs.length>1){
    let nav='<div class="actions" style="margin-bottom:14px">';
    tabs.forEach(t=>nav+='<button class="btn '+(t.id===appSettingsTab?'primary':'')+'" onclick="showAppSettingsTab(\''+js(t.id)+'\')">'+esc(t.label)+'</button>');
    root.insertAdjacentHTML('beforeend',nav+'</div>');
  }
  let tab=tabs.find(t=>t.id===appSettingsTab)||tabs[0];
  if(!tab){root.innerHTML='<p class="small">Keine Web-Einstellungen vorhanden.</p>';return}
  if(tabs.length===1)root.insertAdjacentHTML('beforeend','<h3>'+esc(tab.label)+'</h3>');
  (tab.fields||[]).forEach(f=>root.insertAdjacentHTML('beforeend',f.type==='repeater'?repeaterField(f):simpleFieldInput(f)));
  if(!(tab.fields||[]).length)root.insertAdjacentHTML('beforeend','<p class="small">Dieser Bereich enthält keine Felder.</p>');
}
async function editAppSettings(id){
  if(id==='intercom'){location.href='/intercom';return}
  try{
    let d=await api('/api/apps/settings?id='+encodeURIComponent(id));appSettingsModel=d;appSettingsTab='';
    $('appSettingsTitle').textContent=d.name+' · Web-Einstellungen';
    let note=d.declared?'Eigene web_settings.json der App. Display- und Web-Einstellungen verwenden dieselben gespeicherten Werte.':'Kompatibilitätsmodus für ältere Apps. Nur einfache Werte werden automatisch angezeigt.';
    if(!d.declared&&d.complexHidden)note+=' '+d.complexHidden+' komplexe Einstellung(en) werden zum Schutz nicht als Roh-JSON bearbeitet.';
    $('appSettingsNote').textContent=note;
    $('appSettingsDisplayBtn').onclick=()=>openAppSettings(id);
    $('appSettingsStatus').textContent='';
    $('appSettingsCard').style.display='block';renderAppSettings();$('appSettingsCard').scrollIntoView({behavior:'smooth',block:'start'});
  }catch(e){$('headline').textContent='App-Einstellungen: '+e.message}
}
function readSimpleValue(f,prefix='aws_'){
  let el=$(prefix+f.key);if(!el)return undefined;
  if(f.type==='bool')return el.checked;
  if(f.type==='integer'){const n=Number(el.value||'0');return Number.isInteger(n)?n:NaN}
  if(f.type==='number')return Number(el.value||'0');
  return el.value;
}
async function saveAppWebSettings(){
  if(!appSettingsModel)return;preserveAppSettings();let values={},clearSecrets=[];
  for(let tab of appSettingsModel.tabs||[])for(let f of tab.fields||[]){
    if(f.type==='repeater'){
      let items=[];for(let item of f.items||[]){let obj={};for(let ch of f.fields||[]){if(item[ch.key]!==undefined)obj[ch.key]=item[ch.key]}
        if(f.key==='printer_profiles')for(let key of ['id','mode','background','password_secret','enabled','calibration','alignment'])if(item[key]!==undefined)obj[key]=item[key];
        items.push(obj)}values[f.key]=items;
    }else{
      let v=f.type==='password'?(f.draftSecret??''):f.value;if(v!==undefined)values[f.key]=v;
      if(f.type==='password'&&f.draftClear)clearSecrets.push(f.key);
    }
  }
  try{
    let r=await api('/api/apps/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:appSettingsModel.id,values,clearSecrets})});
    $('appSettingsStatus').textContent='Gespeichert: '+(r.changed||0)+' Änderung(en).';await editAppSettings(appSettingsModel.id);
  }catch(e){$('appSettingsStatus').textContent='Fehler: '+e.message}
}
async function loadNetwork(){if(!settingsVisible('network'))return;try{let n=await api('/api/network');$('lanState').innerHTML='<b>'+(n.ethernet.connected?'Verbunden':'Nicht verbunden')+'</b><br>'+esc(n.ethernet.ip||'Keine IP')+'<br><span class="small">'+esc(n.ethernet.mac||'')+(n.ethernet.gateway?' · Gateway '+esc(n.ethernet.gateway):'')+(n.ethernet.linkSpeedMbps?' · '+n.ethernet.linkSpeedMbps+' Mbit/s':'')+'</span>';$('wifiState').innerHTML='<b>'+(n.wifi.connected?'Verbunden':(n.wifi.enabled?'Bereit':'Aus'))+'</b><br>'+esc(n.wifi.ssid||'Kein aktives WLAN')+(n.wifi.rssi>-126?' ('+n.wifi.rssi+' dBm)':'')+'<br><span class="small">'+esc(n.wifi.ip||n.wifi.text||n.wifi.policy||'')+'</span>';let p=$('profiles');p.innerHTML='';(n.profiles||[]).forEach(x=>{let d=document.createElement('div');d.className='card';d.style.margin='8px 0';d.innerHTML='<div class="row"><div><b>Profil '+(x.slot+1)+'</b> '+(x.preferred?'· bevorzugt':'')+'<div class="small">'+(x.configured?'Gespeichert':'Nicht eingerichtet')+'</div></div><div><button class="btn" onclick="preferProfile('+x.slot+')" '+(!x.configured?'disabled':'')+'>Bevorzugen</button> <button class="btn danger" onclick="forgetProfile('+x.slot+')" '+(!x.configured?'disabled':'')+'>Löschen</button></div></div><div class="grid" style="margin-top:10px"><div><label class="label">SSID</label><input id="ssid'+x.slot+'" placeholder="SSID" value="'+esc(x.ssid||'')+'"></div><div><label class="label">Passwort</label><input id="pwd'+x.slot+'" type="password" placeholder="Leer = unverändert"></div><div><label class="label">Statische IP (leer = DHCP)</label><input id="ip'+x.slot+'" placeholder="192.168.178.50" value="'+esc(x.ip||'')+'"></div><div><label class="label">Gateway</label><input id="gw'+x.slot+'" placeholder="192.168.178.1" value="'+esc(x.gateway||'')+'"></div><div><label class="label">Subnetz</label><input id="mask'+x.slot+'" placeholder="255.255.255.0" value="'+esc(x.subnet||'')+'"></div><div><label class="label">DNS 1</label><input id="dns1_'+x.slot+'" placeholder="optional" value="'+esc(x.dns1||'')+'"></div><div><label class="label">DNS 2</label><input id="dns2_'+x.slot+'" placeholder="optional" value="'+esc(x.dns2||'')+'"></div></div><button class="btn primary" style="margin-top:10px" onclick="saveProfile('+x.slot+')">Profil speichern / verbinden</button>';p.appendChild(d)});let sr=(n.scan&&n.scan.results)||[];$('scanResults').innerHTML='<b>'+esc(n.scan?.status||'Noch keine Suche')+'</b><br>'+sr.map(x=>'<button class="btn" onclick="useScannedSsid(\''+js(x.ssid)+'\')">'+esc(x.ssid)+' · '+x.rssi+' dBm'+(x.secure?' · 🔒':'')+'</button>').join(' ')}catch(e){$('lanState').innerHTML='<span class="small">Fehler: '+esc(e.message)+'</span>';$('wifiState').innerHTML='<span class="small">Fehler: '+esc(e.message)+'</span>';throw e}}
async function scanWifi(){let d=await api('/api/network/wifi/scan',{method:'POST'});$('scanResults').textContent=d.status||'Suche gestartet';setTimeout(()=>refreshSettingsSection('network'),1800);setTimeout(()=>refreshSettingsSection('network'),4500)}
function useScannedSsid(ssid){let target=[0,1,2].find(i=>!$('ssid'+i)?.value)||0;if($('ssid'+target)){$('ssid'+target).value=ssid;$('ssid'+target).focus()}}
async function saveProfile(slot){let q=new URLSearchParams({slot:String(slot),ssid:$('ssid'+slot).value,password:$('pwd'+slot).value,ip:$('ip'+slot).value,gateway:$('gw'+slot).value,subnet:$('mask'+slot).value,dns1:$('dns1_'+slot).value,dns2:$('dns2_'+slot).value});await api('/api/network/wifi/profile?'+q.toString(),{method:'POST'});await loadNetwork()}
async function preferProfile(slot){await api('/api/network/wifi/preferred?slot='+slot,{method:'POST'});setTimeout(()=>refreshSettingsSection('network'),500)}
async function forgetProfile(slot){if(!confirm('WLAN-Profil '+(slot+1)+' wirklich löschen?'))return;await api('/api/network/wifi/forget?slot='+slot,{method:'POST'});await loadNetwork()}
async function wifiEnable(on){await api('/api/network/wifi/enabled?on='+(on?1:0),{method:'POST'});setTimeout(()=>refreshSettingsSection('network'),500)}
async function loadSettings(){if(!settingsVisible('system'))return;let s=await api('/api/settings');$('themeMode').replaceChildren(...s.themes.map(t=>new Option(t.name,t.id)));$('accent').replaceChildren(...s.accents.map(t=>new Option(t.name,t.id)));$('themeMode').value=s.themeMode;$('accent').value=s.accent;$('brightness').value=s.brightness;$('brightnessValue').textContent=s.brightness;$('brightness').oninput=()=>$('brightnessValue').textContent=$('brightness').value;const region=await api('/api/region');$('regionCountry').replaceChildren(...region.countries.map(c=>new Option(c.name,c.code)));$('regionCountry').value=region.country;$('regionStatus').textContent=region.status}
async function saveRegion(){await api('/api/region?country='+encodeURIComponent($('regionCountry').value),{method:'POST'});$('regionStatus').textContent='Region wird gespeichert …';for(let i=0;i<12&&settingsVisible('system');i++){await new Promise(r=>setTimeout(r,250));const s=await api('/api/region');$('regionStatus').textContent=s.status;if(!s.busy){$('regionCountry').value=s.country;break}}}
async function saveTheme(){await api('/api/settings/theme?mode='+$('themeMode').value+'&accent='+$('accent').value,{method:'POST'});await loadSettings()}
async function saveBrightness(){await api('/api/settings/brightness?value='+$('brightness').value,{method:'POST'});await loadSettings()}
let serviceStatusPending=false;
async function loadService8088(){
  if(serviceStatusPending||!settingsVisible('system'))return;
  serviceStatusPending=true;
  try{
    service8088=await api('/api/service/status');
    let running=!!service8088.running,remaining=Number(service8088.remainingSeconds||0);
    let m=Math.floor(remaining/60),sec=remaining%60;
    let text=running?'Service-Port 8088 aktiv · Restzeit '+m+':'+String(sec).padStart(2,'0'):'Service-Port 8088 ist aus.';
    if($('service8088Status'))$('service8088Status').textContent=text;
    if($('serviceToggle'))$('serviceToggle').textContent=running?'Service :8088 stoppen':'Service :8088 starten';
  }catch(e){
    if($('service8088Status'))$('service8088Status').textContent='Service-Status nicht verfügbar.';
  }finally{serviceStatusPending=false}
}
async function startService8088(){
  service8088=await api('/api/service/start',{method:'POST'});
  await loadService8088();
  return !!service8088.running;
}
async function stopService8088(){
  try{await api('/api/service/stop',{method:'POST'})}finally{await loadService8088()}
}
async function toggleService8088(){
  await loadService8088();
  if(service8088.running)await stopService8088();else await startService8088();
}
async function openService8088(){
  await loadService8088();
  if(!service8088.running){
    alert('Service-Port 8088 ist aus. Bitte zuerst für maximal 30 Minuten starten.');
    return;
  }
  window.open(serviceBase+'/apps','_blank','noopener');
}
hpWeb.every(()=>{if(settingsVisible('system')&&service8088.running)return loadService8088()},5000);
// Native intercom settings share the authenticated Port-80 server.
// App-owned declarative settings, still authenticated and CSRF protected.
const appRoute=/^\/apps\/([A-Za-z0-9_-]{1,31})\/$/.exec(location.pathname);
const settingsOwner=appRoute?appRoute[1]:new URLSearchParams(location.search).get('app_settings');
async function openLinkedAppSettings(){if(settingsOwner&&/^[A-Za-z0-9_-]{1,31}$/.test(settingsOwner)){
  activePage='apps';document.querySelectorAll('.tabs button,.page').forEach(x=>x.classList.remove('active'));
  const appsTab=document.querySelector('.tabs button[data-page="apps"]');if(appsTab)appsTab.classList.add('active');$('apps').classList.add('active');
  await safeLoad('App-Einstellungen',()=>editAppSettings(settingsOwner));
}}

async function loadDiagnostics(){if(!settingsVisible('diagnostics'))return;let report=await api('/api/diagnostics');$('diag').textContent=report;let lines=String(report).split(/\r?\n/),pairs={};let events=[],inEvents=false;for(let line of lines){if(line==='recent_events:'){inEvents=true;continue}if(inEvents){if(line)events.push(line);continue}for(let token of line.split(' ')){let p=token.indexOf('=');if(p>0)pairs[token.slice(0,p)]=token.slice(p+1)}}let items=[['Firmware',pairs.version||'–'],['Laufzeit',pairs.uptime_s?pairs.uptime_s+' s':'–'],['Heap frei',pairs.heap_free?Math.round(Number(pairs.heap_free)/1024)+' KiB':'–'],['PSRAM frei',pairs.psram_free?Math.round(Number(pairs.psram_free)/1048576*10)/10+' MiB':'–'],['UI FPS',pairs.ui_fps||'–'],['HTTP-Fehler',pairs.http_failures||'0'],['Web :80',pairs.web80_active==='1'?'aktiv':'aus'],['Service :8088',pairs.web8088_active==='1'?'aktiv':'aus'],['8088 Restzeit',pairs.web8088_active==='1'?(Math.ceil(Number(pairs.web8088_remaining_s||0)/60)+' min'):'–'],['CSRF-Fehler',pairs.csrf_failures||'0'],['Sockets frei',((pairs.web80_free_sockets||'–')+' / '+(pairs.web8088_free_sockets||'–'))],['Storage-Lesefehler',pairs.storage_read_errors||'0'],['Supervisor',pairs.supervisor_events||'0']];$('diagSummary').innerHTML=items.map(x=>'<div class="metric"><span>'+esc(x[0])+'</span><b>'+esc(x[1])+'</b></div>').join('');$('diagEvents').innerHTML='<b>Letzte Ereignisse</b><div class="small" style="margin-top:8px">'+(events.length?events.map(esc).join('<br>'):'Keine aktuellen Ereignisse im Bericht.')+'</div>'}
async function loadLanguage(){const data=await api('/api/language?keys=language.title,language.german_only,language.restart,language.save');let card=$('languageCard');if(!card){card=document.createElement('div');card.id='languageCard';card.className='card';card.style.marginTop='14px';$('system').append(card)}card.replaceChildren();const title=document.createElement('h2');title.textContent=data.strings['language.title'];const select=document.createElement('select');select.id='languageSelect';select.setAttribute('aria-label',data.strings['language.title']);for(const entry of data.languages){const option=document.createElement('option');option.value=entry.id;option.textContent=entry.name;option.selected=entry.id===data.active;select.append(option)}const button=document.createElement('button');button.className='btn primary';button.textContent=data.strings['language.save'];button.onclick=async()=>{button.disabled=true;try{const v=await api('/api/language?id='+encodeURIComponent(select.value),{method:'POST'});note.textContent=v.message}catch(e){note.textContent=e.message}finally{button.disabled=false}};const note=document.createElement('p');note.className='small';note.textContent=(data.languages.length===1?data.strings['language.german_only']+' ':'')+data.strings['language.restart'];card.append(title,select,button,note)}
async function restartDevice(){if(!confirm('Home-Display wirklich neu starten?'))return;try{await api('/api/restart',{method:'POST'})}catch(e){}}
async function refreshAll(){await safeLoad('CSRF',ensureCsrf);await safeLoad('Status',loadStatus);await safeLoad('Apps',loadApps);if(activePage==='settings')await refreshSettingsSection()}
bindAppInstaller();hpWeb.whenVisible(()=>appRoute?safeLoad('CSRF',ensureCsrf).then(openLinkedAppSettings):refreshAll().then(openLinkedAppSettings));
// Refresh only the lightweight status endpoint while the overview is visible.
// No extra HTTP requests when the user switches tabs or the browser is hidden.
hpWeb.every(()=>{if(activePage==='home')return safeLoad('Status',loadStatus)},15000);
hpWeb.onResume(()=>appSettingsModel?refreshPrinterStatus():safeLoad('Status',loadStatus));
</script></body></html>)HTML";

} // namespace

WebServerService& WebServerService::instance() {
    static WebServerService service;
    return service;
}

bool WebServerService::begin() {
    if (server_) return true;
    static const httpd_uri_t routes[] = {
        {"/login", HTTP_GET, WebLoginService::page, nullptr},
        {"/login.js", HTTP_GET, WebLoginService::script, nullptr},
        {"/api/auth/login", HTTP_POST, WebLoginService::login, nullptr},
        {"/api/auth/logout", HTTP_POST, WebLoginService::logout, nullptr},
        {"/", HTTP_GET, rootHandler, nullptr},
        {"/api/status", HTTP_GET, statusHandler, nullptr},
        {"/api/csrf", HTTP_GET, csrfHandler, nullptr},
        {"/api/users/revision", HTTP_GET, userRevisionHandler, nullptr},
        {"/calendar", HTTP_GET, calendarPageHandler, nullptr},
        {"/users", HTTP_GET, calendarPageHandler, nullptr},
        {"/users/", HTTP_GET, calendarPageHandler, nullptr},
        {"/phonebook", HTTP_GET, phonebookPageHandler, nullptr},
        {"/photos", HTTP_GET, photosPageHandler, nullptr},
        {"/apps/photos/", HTTP_GET, photosPageHandler, nullptr},
        {"/apps/photos", HTTP_GET, photosPageHandler, nullptr},
        {"/api/photos/job", HTTP_POST, photosJobHandler, nullptr},
        {"/api/photos/result", HTTP_GET, photosResultHandler, nullptr},
        {"/photos/upload", HTTP_POST, FileTransferService::filesOperationHandler, nullptr},
        {"/api/phonebook/job", HTTP_POST, phonebookJobHandler, nullptr},
        {"/api/phonebook/result", HTTP_GET, phonebookResultHandler, nullptr},
        {"/files/download", HTTP_GET, FileTransferService::filesOperationHandler, nullptr},
        {"/files/mkdir", HTTP_POST, FileTransferService::filesOperationHandler, nullptr},
        {"/files/rename", HTTP_POST, FileTransferService::filesOperationHandler, nullptr},
        {"/files/upload", HTTP_POST, FileTransferService::filesOperationHandler, nullptr},
        {"/app-web-lifecycle.js", HTTP_GET, appWebLifecycleHandler, nullptr},
        {"/account-connect.js", HTTP_GET, accountConnectScriptHandler, nullptr},
        {"/api/calendar/job", HTTP_POST, calendarJobHandler, nullptr},
        {"/api/calendar/result", HTTP_GET, calendarResultHandler, nullptr},
        {"/api/diagnostics", HTTP_GET, diagnosticsHandler, nullptr},
        {"/licenses", HTTP_GET, licenseHandler, nullptr},
        {"/licenses/project", HTTP_GET, licenseHandler, nullptr},
        {"/licenses/third-party", HTTP_GET, licenseHandler, nullptr},
#if HOMEPANEL_FEATURE_INTERCOM
        {"/api/intercom/diagnostics", HTTP_GET, intercomDiagnosticsHandler, nullptr},
        {"/intercom", HTTP_GET, intercomPageHandler, nullptr},
        {"/api/intercom/settings", HTTP_POST, intercomSettingsHandler, nullptr},
        {"/api/intercom/result", HTTP_GET, intercomResultHandler, nullptr},
        {"/api/intercom/event", HTTP_POST, intercomEventHandler, nullptr},
#endif
        {"/api/service/status", HTTP_GET, serviceStatusHandler, nullptr},
        {"/api/service/start", HTTP_POST, serviceStartHandler, nullptr},
        {"/api/service/stop", HTTP_POST, serviceStopHandler, nullptr},
        {"/api/apps", HTTP_GET, appsHandler, nullptr},
        {"/api/apps/toggle", HTTP_POST, appToggleHandler, nullptr},
        {"/api/apps/settings/open", HTTP_POST, appSettingsOpenHandler, nullptr},
        {"/api/apps/settings", HTTP_GET, appWebSettingsGetHandler, nullptr},
        {"/api/apps/settings", HTTP_POST, appWebSettingsSaveHandler, nullptr},
        {"/api/apps/settings/discovery", HTTP_POST, appWebSettingsDiscoveryHandler, nullptr},
        {"/api/apps/export", HTTP_GET, appDataExportHandler, nullptr},
        {"/api/network", HTTP_GET, networkHandler, nullptr},
        {"/api/network/wifi/scan", HTTP_POST, wifiScanHandler, nullptr},
        {"/api/network/wifi/profile", HTTP_POST, wifiProfileHandler, nullptr},
        {"/api/network/wifi/enabled", HTTP_POST, wifiEnabledHandler, nullptr},
        {"/api/network/wifi/preferred", HTTP_POST, wifiPreferredHandler, nullptr},
        {"/api/network/wifi/forget", HTTP_POST, wifiForgetHandler, nullptr},
        {"/api/settings", HTTP_GET, settingsHandler, nullptr},
        {"/api/language", HTTP_GET, languageHandler, nullptr},
        {"/api/language", HTTP_POST, languageHandler, nullptr},
        {"/api/region", HTTP_GET, regionHandler, nullptr},
        {"/api/region", HTTP_POST, regionHandler, nullptr},
        {"/api/settings/theme", HTTP_POST, themeHandler, nullptr},
        {"/api/settings/brightness", HTTP_POST, brightnessHandler, nullptr},
        {"/api/restart", HTTP_POST, restartHandler, nullptr}
    };
    // Reuse the existing mature service handlers on port 80. This keeps one
    // OTA/App/Storage implementation while the :8088 instance remains a fallback.
    static const httpd_uri_t serviceRoutes[] = {
        {"/upload", HTTP_POST, FileTransferService::uploadHandler, nullptr},
        {"/files", HTTP_GET, FileTransferService::filesIndexHandler, nullptr},
        {"/files/delete", HTTP_POST, FileTransferService::filesDeleteHandler, nullptr},
        {"/apps", HTTP_GET, FileTransferService::appsIndexHandler, nullptr},
        {"/apps/add", HTTP_POST, FileTransferService::appsAddHandler, nullptr},
        {"/apps/remove", HTTP_POST, FileTransferService::appsRemoveHandler, nullptr},
        {"/apps/toggle", HTTP_POST, FileTransferService::appsToggleHandler, nullptr},
        {"/apps/native", HTTP_POST, FileTransferService::appsNativeHandler, nullptr},
        {"/apps/web/toggle", HTTP_POST, FileTransferService::appsWebToggleHandler, nullptr},
        {"/apps/lua/remove", HTTP_POST, FileTransferService::appsLuaRemoveHandler, nullptr},
        {"/apps/export", HTTP_GET, FileTransferService::appsExportHandler, nullptr},
        {"/apps/lua/file", HTTP_POST, FileTransferService::appsLuaFileHandler, nullptr},
        {"/apps/lua/commit", HTTP_POST, FileTransferService::appsLuaCommitHandler, nullptr},
        {"/apps/lua/cancel", HTTP_POST, FileTransferService::appsLuaCancelHandler, nullptr},
        {"/apps/upload", HTTP_POST, FileTransferService::appsUploadHandler, nullptr},
        {"/firmware/inspect", HTTP_POST, FileTransferService::firmwareInspectHandler, nullptr},
        {"/firmware/upload", HTTP_POST, FileTransferService::firmwareUploadHandler, nullptr},
        {"/firmware/reboot", HTTP_POST, FileTransferService::firmwareRebootHandler, nullptr}
    };
    size_t oauthCount=0;oauthWebRoutes(oauthCount);
    // All apps share ONE wildcard route; installing apps does not grow this table.
    const size_t routeCount=sizeof(routes)/sizeof(routes[0])+sizeof(serviceRoutes)/sizeof(serviceRoutes[0])+oauthCount+1;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if(routeCount>std::numeric_limits<decltype(cfg.max_uri_handlers)>::max()){
        EventLog::instance().add("ERROR","WEB80_START_FAILED","Route capacity exceeds HTTP server limit");return false;
    }
    cfg.server_port=443;
    cfg.core_id = performance::kIoCore<0 ? tskNO_AFFINITY : performance::kIoCore;
    cfg.ctrl_port=32769; // Separate from the optional port-8088 server control socket.
    cfg.max_open_sockets=socketCapacity(); // Three browsers with bounded request concurrency.
    cfg.recv_wait_timeout=5;cfg.send_wait_timeout=5;
    cfg.max_uri_handlers=static_cast<decltype(cfg.max_uri_handlers)>(routeCount);
    cfg.stack_size=12288;cfg.lru_purge_enable=true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    auto& identity=WebTlsIdentity::instance();
    if(!identity.load()){EventLog::instance().add("ERROR","WEB_TLS_IDENTITY","Cannot load encrypted device identity");return false;}
    httpd_ssl_config_t tls=HTTPD_SSL_CONFIG_DEFAULT();tls.httpd=cfg;
    tls.servercert=identity.certificate();tls.servercert_len=identity.certificateSize();
    tls.prvtkey_pem=identity.key();tls.prvtkey_len=identity.keySize();
    tls.port_secure=443;tls.tls_handshake_timeout_ms=5000;
    const esp_err_t startError=httpd_ssl_start(&server_,&tls);
    if(startError!=ESP_OK){server_=nullptr;EventLog::instance().add("ERROR","WEB80_START_FAILED",String(esp_err_to_name(startError))+" routes="+String((unsigned long)routeCount));return false;}
    const auto registerRoute=[this](const httpd_uri_t& route){
        const esp_err_t error=httpd_register_uri_handler(server_,&route);
        if(error==ESP_OK)return true;
        EventLog::instance().add("ERROR","WEB80_ROUTE_FAILED",String(route.uri)+" method="+String((unsigned)route.method)+" "+esp_err_to_name(error));
        stop();return false;
    };
    for(const auto& route:routes)if(!registerRoute(route))return false;
    for(const auto& route:serviceRoutes)if(!registerRoute(route))return false;
    if (!registerOAuthWebRoutes(server_)) { EventLog::instance().add("ERROR","WEB80_ROUTE_FAILED","OAuth route registration/token initialization failed");stop(); return false; }
    // Register last: concrete service/installer routes keep their existing handlers.
    const httpd_uri_t appRoute={"/apps/*",HTTP_GET,appPageHandler,nullptr};
    if(!registerRoute(appRoute))return false;

    // No HTTP credentials, API handlers or uploads: only a fixed-target GET redirect.
    httpd_config_t redirectCfg=HTTPD_DEFAULT_CONFIG();redirectCfg.server_port=80;
    redirectCfg.ctrl_port=32770;redirectCfg.max_open_sockets=1;redirectCfg.max_uri_handlers=1;
    redirectCfg.uri_match_fn=httpd_uri_match_wildcard;
    if(httpd_start(&redirect_,&redirectCfg)!=ESP_OK){stop();return false;}
    const httpd_uri_t redirectRoute={"/*",HTTP_GET,[](httpd_req_t* req)->esp_err_t{
        httpd_resp_set_status(req,"302 Found");
        // Never trust the client Host header for redirects.
        // The accepted socket's local IP is trusted and keeps IP-only clients
        // working even when their OS/router cannot resolve multicast DNS.
        sockaddr_in local{};socklen_t length=sizeof(local);char address[INET_ADDRSTRLEN]{};
        String host=NetworkService::instance().ip();
        if(getsockname(httpd_req_to_sockfd(req),reinterpret_cast<sockaddr*>(&local),&length)==0&&local.sin_family==AF_INET&&inet_ntop(AF_INET,&local.sin_addr,address,sizeof(address)))host=address;
        if(!host.length()){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Network address unavailable");}
        String target=String("https://")+host;
        const String path=req->uri;
        if(path.length()>2048||path.indexOf('\r')>=0||path.indexOf('\n')>=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid URL");
        target+=path;
        httpd_resp_set_hdr(req,"Location",target.c_str());
        httpd_resp_set_hdr(req,"Cache-Control","no-store");
        return httpd_resp_sendstr(req,"Use HTTPS at the configured device hostname.");
    },nullptr};
    if(httpd_register_uri_handler(redirect_,&redirectRoute)!=ESP_OK){stop();return false;}
    EventLog::instance().add("INFO", "WEB80_READY", String("HTTPS UI on 443; HTTP redirect only; routes=")+String((unsigned long)routeCount));
    return true;
}

size_t WebServerService::activeClients() const {
    if (!server_) return 0;
    int fds[socketCapacity()]{};
    size_t count = socketCapacity();
    if (httpd_get_client_list(server_, &count, fds) != ESP_OK) return 0;
    return count;
}

void WebServerService::stop() {
    if (redirect_) { httpd_stop(redirect_); redirect_ = nullptr; }
    if (server_) { httpd_ssl_stop(server_); server_ = nullptr; }
}

esp_err_t WebServerService::rootHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    commonHeaders(req);
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; style-src 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kIndex, HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebServerService::appPageHandler(httpd_req_t* req) {
    if(!authorized(req))return ESP_OK;
    char id[32]{};bool slash=false;
    if(!parseAppWebRoute(req->uri,id,slash))return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"App-Weboberfläche nicht vorhanden");
    AppRegistryEntry entry;
    const String path=String("/apps/")+id+"/";
    // Cached descriptors only; never touch Lua/LVGL or enumerate package storage here.
    if(!AppRegistry::instance().lookup(id,entry)||entry.kind!=AppKind::Lua||entry.webPath!=path)
        return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"App-Weboberfläche nicht vorhanden");
    if(!slash){commonHeaders(req);httpd_resp_set_status(req,"308 Permanent Redirect");httpd_resp_set_hdr(req,"Location",path.c_str());return httpd_resp_send(req,nullptr,0);}
    return rootHandler(req);
}

esp_err_t WebServerService::statusHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    const auto mem = DiagnosticsService::instance().memory();
    const auto net = NetworkService::instance().overview();
    auto& storage = StorageService::instance();
    const auto& usb = UsbStorageService::instance();
    const bool usbPresent = usb.present();
    const bool usbMounted = usb.available();
    char sdSize[24], usbSize[24];
    snprintf(sdSize,sizeof(sdSize),"%llu",static_cast<unsigned long long>(storage.sdBytes()));
    snprintf(usbSize,sizeof(usbSize),"%llu",static_cast<unsigned long long>(usb.capacity()));
    String body; body.reserve(1100);
    body = "{\"version\":\"" + jsonEscape(build::kVersion) + "\",\"uptime\":" + String(millis()/1000) +
           ",\"network\":{\"path\":\"" + String(NetworkService::instance().pathName()) + "\",\"ip\":\"" + jsonEscape(NetworkService::instance().ip()) + "\"}" +
           ",\"memory\":{\"freeInternal\":" + String(mem.freeInternal) + ",\"largestInternal\":" + String(mem.largestInternal) + ",\"freePsram\":" + String(mem.freePsram) + "}" +
           ",\"storage\":{\"sd\":" + String(boolJson(storage.sdAvailable())) + ",\"sdCapacity\":" + String(sdSize) +
           ",\"usb\":{\"present\":" + String(boolJson(usbPresent)) + ",\"mounted\":" + String(boolJson(usbMounted)) + ",\"capacity\":" + String(usbSize) + "},\"appfs\":" + String(boolJson(storage.appFsAvailable())) + "}" +
           ",\"recoveryRequired\":false}";
    sendJson(req, body); return ESP_OK;
}

esp_err_t WebServerService::csrfHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    sendJson(req, "{\"token\":\"" + jsonEscape(WebCsrfService::instance().token()) + "\"}");
    return ESP_OK;
}
esp_err_t WebServerService::userRevisionHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;
    sendJson(req,String("{\"revision\":")+PhonebookService::instance().revision()+",\"calendar_revision\":"+CalendarService::instance().syncRevision()+"}");return ESP_OK;
}
esp_err_t WebServerService::appWebLifecycleHandler(httpd_req_t* req){
 if(!authorized(req))return ESP_OK;
 commonHeaders(req);httpd_resp_set_type(req,"application/javascript; charset=utf-8");
 return httpd_resp_send(req,appWebLifecycleScript,sizeof(appWebLifecycleScript)-1);
}
esp_err_t WebServerService::calendarPageHandler(httpd_req_t* req){
 if(!authorized(req))return ESP_OK;
 commonHeaders(req);httpd_resp_set_type(req,"text/html; charset=utf-8");return httpd_resp_send(req,calendarWebPage,sizeof(calendarWebPage)-1);
}
esp_err_t WebServerService::accountConnectScriptHandler(httpd_req_t* req){
 if(!authorized(req))return ESP_OK;commonHeaders(req);httpd_resp_set_type(req,"application/javascript; charset=utf-8");return httpd_resp_send(req,accountConnectScript,sizeof(accountConnectScript)-1);
}
esp_err_t WebServerService::calendarJobHandler(httpd_req_t* req){
 return dataJobHandler(req,false);
}
esp_err_t WebServerService::phonebookJobHandler(httpd_req_t* req){return dataJobHandler(req,true);}
esp_err_t WebServerService::phonebookPageHandler(httpd_req_t* req){
 if(!authorized(req))return ESP_OK;commonHeaders(req);httpd_resp_set_type(req,"text/html; charset=utf-8");return httpd_resp_send(req,phonebookWebPage,sizeof(phonebookWebPage)-1);
}
esp_err_t WebServerService::photosPageHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;commonHeaders(req);httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; style-src 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    return httpd_resp_send(req,photoWebPage,sizeof(photoWebPage)-1);
}
esp_err_t WebServerService::photosJobHandler(httpd_req_t* req){
    if(!writeAuthorized(req))return ESP_OK;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");
    if(req->content_len<=0||req->content_len>LuaJobService::kBodyBytes)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"photos_size");
    String body;if(!body.reserve(req->content_len+1))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");
    char buffer[512];int remaining=req->content_len;const uint32_t started=millis();
    while(remaining>0){if(uint32_t(millis()-started)>10000)return httpd_resp_send_err(req,HTTPD_408_REQ_TIMEOUT,"photos_timeout");
        const int n=httpd_req_recv(req,buffer,std::min<int>(remaining,sizeof(buffer)));if(n<=0||!body.concat(buffer,n))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"photos_body");remaining-=n;}
    JsonDocument input;if(deserializeJson(input,body,DeserializationOption::NestingLimit(6))||!input["op"].is<const char*>()||!input["args"].is<JsonObject>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"photos_json");
    const String op=input["op"].as<String>();if(op!="get"&&op!="save"&&op!="pause"&&op!="resume"&&op!="start")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"photos_operation");
    LuaJobService::Request job;job.owner="@photos-web";job.kind=1;job.context=op;serializeJson(input["args"],job.body);job.handler=executePhotoWebJob;
    const uint32_t id=LuaJobService::instance().submit(job);if(!id){sendJson(req,"{\"error\":\"photos_busy\"}","503 Service Unavailable");return ESP_OK;}
    sendJson(req,String("{\"job\":")+id+"}","202 Accepted");return ESP_OK;
}
esp_err_t WebServerService::photosResultHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;SecurityWriteGuard guard;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");
    char value[16]{};if(!queryValue(req,"id",value,sizeof(value)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"photos_job_id");
    char* end=nullptr;const unsigned long id=strtoul(value,&end,10);if(!*value||!end||*end||!id||id>UINT32_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"photos_job_id");
    LuaJobService::Result result;if(!LuaJobService::instance().result("@photos-web",id,result)){sendJson(req,"{\"error\":\"photos_busy\"}","503 Service Unavailable");return ESP_OK;}
    String response=String("{\"done\":")+boolJson(result.done)+",\"ok\":"+boolJson(result.ok)+",\"error\":\""+jsonEscape(result.error)+"\"";
    if(result.done&&result.ok)response+=",\"value\":"+(result.body.length()?result.body:String("{}"));response+="}";sendJson(req,response);
    if(result.done)LuaJobService::instance().release("@photos-web",id);return ESP_OK;
}
esp_err_t WebServerService::dataJobHandler(httpd_req_t* req,bool phonebook){
 if(!writeAuthorized(req))return ESP_OK;
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");
 if(req->content_len<=0||req->content_len>LuaJobService::kBodyBytes)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"calendar_size");
 String body;if(!body.reserve(req->content_len+1))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");
 char buffer[512];int remaining=req->content_len;while(remaining>0){const int n=httpd_req_recv(req,buffer,std::min<int>(remaining,sizeof(buffer)));if(n<=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"calendar_body");if(!body.concat(buffer,n))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");remaining-=n;}
 JsonDocument input;if(deserializeJson(input,body,DeserializationOption::NestingLimit(8))||!input["op"].is<const char*>()||!input["args"].is<JsonObject>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"calendar_json");
 const String op=input["op"].as<String>();bool allowed=false;for(const char* candidate:{"query","details","save","delete","people","calendars","visibility","appearance","person_save","person_remove","user_profile","calendar_create","region","status","sync","collection_selection","account_person","person_preferences","person_contacts","postal_lookup"})if(op==candidate)allowed=true;
 if(phonebook){allowed=false;for(const char* candidate:{"list","details","save","remove","import","export","postal_lookup"})if(op==candidate)allowed=true;}
 if(!allowed)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"calendar_operation");
 LuaJobService::Request job;job.owner="@calendar-web";job.kind=5;job.context=op;serializeJson(input["args"],job.body);job.handler=calendarWebJob;
 if(phonebook||op=="people"||op=="person_contacts"||op=="person_preferences"||op=="person_save"||op=="person_remove"||op=="user_profile"||op=="account_person")job.lane=LuaJobService::Request::Lane::LocalTransfer;
 if(phonebook){job.owner="@phonebook-web";job.handler=executePhonebookWebJob;}
 if(op=="query")job.fileName="calendar_query";
 if(op=="postal_lookup")job.lane=LuaJobService::Request::Lane::Transfer;
 job.required=job.permissions=perm(AppPermission::CalendarRead)|perm(AppPermission::CalendarWrite);
 const uint32_t id=LuaJobService::instance().submit(job);if(!id){sendJson(req,"{\"error\":\"calendar_busy\"}","503 Service Unavailable");return ESP_OK;}
 sendJson(req,String("{\"job\":")+id+"}","202 Accepted");return ESP_OK;
}
esp_err_t WebServerService::calendarResultHandler(httpd_req_t* req){
 return dataResultHandler(req,false);
}
esp_err_t WebServerService::phonebookResultHandler(httpd_req_t* req){return dataResultHandler(req,true);}
esp_err_t WebServerService::dataResultHandler(httpd_req_t* req,bool phonebook){
 if(!authorized(req))return ESP_OK;SecurityWriteGuard security;
 if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"settings_locked");
 char value[16]{};if(!queryValue(req,"id",value,sizeof(value)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"calendar_job_id");
 char* end=nullptr;const unsigned long id=strtoul(value,&end,10);if(!*value||!end||*end||!id||id>UINT32_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"calendar_job_id");
 const char* owner=phonebook?"@phonebook-web":"@calendar-web";
 LuaJobService::Result result;if(!LuaJobService::instance().result(owner,id,result)){sendJson(req,"{\"error\":\"calendar_job_missing_or_busy\"}","503 Service Unavailable");return ESP_OK;}
 String response=String("{\"done\":")+boolJson(result.done)+",\"ok\":"+boolJson(result.ok)+",\"error\":\""+jsonEscape(result.error)+"\"";
 if(result.done&&result.ok)response+=",\"value\":"+(result.body.length()?result.body:String("{}"));response+="}";sendJson(req,response);
 if(result.done)LuaJobService::instance().release(owner,id);return ESP_OK;
}

esp_err_t WebServerService::diagnosticsHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    commonHeaders(req); httpd_resp_set_type(req, "text/plain; charset=utf-8");
    const String report = DiagnosticsService::instance().supportReportText();
    return httpd_resp_send(req, report.c_str(), report.length());
}

esp_err_t WebServerService::licenseHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    if(!strcmp(req->uri,"/licenses/project")||!strcmp(req->uri,"/licenses/third-party")){
        httpd_resp_set_type(req,"text/plain; charset=utf-8");
        return httpd_resp_sendstr(req,!strcmp(req->uri,"/licenses/project")?LicenseUi::projectText():LicenseUi::thirdPartyText());
    }
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'");
    return httpd_resp_sendstr(req,"<!doctype html><html lang='de'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Home-Display · Lizenzen</title><style>body{font:18px system-ui;background:#101b25;color:#edf4fa;max-width:800px;margin:40px auto;padding:20px}a{display:block;color:#8dd5ff;margin:24px 0}</style><h1>Home-Display · Lizenzen</h1><p>Copyright 2026 Michael Roßmann</p><a href='/licenses/project'>Projektlizenz · vollständiger Originaltext</a><a href='/licenses/third-party'>Drittanbieter und Lizenzhinweise</a><a href='/'>Zurück zu Einstellungen</a></html>");
}
esp_err_t WebServerService::intercomDiagnosticsHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;const String report=IntercomService::instance().diagnostics();sendJson(req,report);return ESP_OK;
}
esp_err_t WebServerService::intercomPageHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;commonHeaders(req);
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; style-src 'unsafe-inline'; script-src 'self' 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    return httpd_resp_send(req,intercomWebPage,sizeof(intercomWebPage)-1);
}
esp_err_t WebServerService::intercomSettingsHandler(httpd_req_t* req){
    if(!writeAuthorized(req))return ESP_OK;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"pin_locked");
    if(req->content_len<=0||req->content_len>2048)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"settings_size");
    String body;if(!body.reserve(req->content_len+1))return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"memory");
    char buffer[256];int remaining=req->content_len;
    while(remaining){const int n=httpd_req_recv(req,buffer,std::min<int>(remaining,sizeof(buffer)));
        if(n<=0||!body.concat(buffer,n))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"settings_body");remaining-=n;}
    JsonDocument doc;if(deserializeJson(doc,body,DeserializationOption::NestingLimit(4))||!doc["op"].is<const char*>()||!doc["args"].is<JsonObject>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"settings_json");
    const String op=doc["op"].as<String>();if(op!="list"&&op!="save"&&op!="remove")return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"settings_operation");
    LuaJobService::Request job;job.owner="@intercom-web";job.kind=5;job.context=op;job.handler=intercomWebJob;
    job.required=job.permissions=perm(AppPermission::Network)|perm(AppPermission::Intercom);
    serializeJson(doc["args"],job.body);const auto id=LuaJobService::instance().submit(job);
    if(!id){sendJson(req,"{\"error\":\"queue_full\"}","503 Service Unavailable");return ESP_OK;}
    sendJson(req,String("{\"job\":")+id+"}","202 Accepted");return ESP_OK;
}
esp_err_t WebServerService::intercomResultHandler(httpd_req_t* req){
    if(!authorized(req))return ESP_OK;SecurityWriteGuard guard;
    if(!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"pin_locked");
    char value[16]{};if(!queryValue(req,"id",value,sizeof(value)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"job_id");
    char* end=nullptr;const auto id=strtoul(value,&end,10);if(!*value||!end||*end||!id||id>UINT32_MAX)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"job_id");
    LuaJobService::Result result;if(!LuaJobService::instance().result("@intercom-web",id,result)){sendJson(req,"{\"error\":\"job_missing_or_busy\"}","503 Service Unavailable");return ESP_OK;}
    String out=String("{\"done\":")+boolJson(result.done)+",\"ok\":"+boolJson(result.ok)+",\"error\":\""+jsonEscape(result.error)+"\"";
    if(result.done&&result.ok)out+=",\"value\":"+(result.body.length()?result.body:String("{}"));out+="}";sendJson(req,out);
    if(result.done)LuaJobService::instance().release("@intercom-web",id);return ESP_OK;
}
esp_err_t WebServerService::intercomEventHandler(httpd_req_t* req){
    if(!writeAuthorized(req))return ESP_OK;
    if(req->content_len<=0||req->content_len>256)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"event size");
    char bytes[257]{};int used=0;while(used<req->content_len){int n=httpd_req_recv(req,bytes+used,req->content_len-used);if(n<=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"event body");used+=n;}
    JsonDocument doc;if(deserializeJson(doc,bytes,used,DeserializationOption::NestingLimit(2))||!doc["device"].is<const char*>()||!doc["call"].is<uint32_t>()||!doc["event"].is<const char*>())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"event invalid");
    IntercomDevice device;const char* id=doc["device"];if(!IntercomService::instance().device(id,device)||strcmp(device.provider,"lan_event"))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"LAN event device required");
    const char* kind=doc["event"];bool ok=false;if(!strcmp(kind,"ring"))ok=IntercomService::instance().incoming(id,doc["call"]);else if(!strcmp(kind,"end"))ok=IntercomService::instance().finished(id,doc["call"]);
    sendJson(req,ok?"{\"accepted\":true}":"{\"accepted\":false}",ok?"200 OK":"409 Conflict");return ESP_OK;
}

esp_err_t WebServerService::appsHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    auto* entries=allocatePsramArray<AppRegistryEntry>(48);
    if(!entries){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"registry_memory");}
    const size_t count = AppRegistry::instance().snapshot(entries, 48);
    String body = "{\"apps\":["; body.reserve(5000);
    bool first = true;
    for (size_t i=0; i<count; ++i) {
        const auto& e = entries[i];
        if (!first) body += ','; first = false;
        body += "{\"id\":\"" + jsonEscape(e.id) + "\",\"name\":\"" + jsonEscape(e.name) +
                "\",\"version\":\"" + jsonEscape(e.version) + "\",\"kind\":\"" + kindName(e.kind) +
                "\",\"state\":\"" + stateName(e.state) + "\",\"removable\":" + String(boolJson(e.removable)) + ",\"hasSettings\":" + String(boolJson(e.hasSettings)) + ",\"canDisable\":" + String(boolJson(e.userInstallable)) + ",\"launcherVisible\":" + String(boolJson(e.launcherVisible)) + "}";
        body.remove(body.length()-1);
        body += ",\"webPath\":\"" + jsonEscape(e.webPath) + "\"}";
    }
    body += "]}";
    for(size_t i=0;i<48;++i)entries[i].~AppRegistryEntry();
    heap_caps_free(entries);
    sendJson(req, body); return ESP_OK;
}

esp_err_t WebServerService::appToggleHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char id[48]{}, on[8]{};
    if (!queryValue(req,"id",id,sizeof(id)) || !queryValue(req,"on",on,sizeof(on))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id/on fehlt");
    }
    if (!AppRegistry::instance().setEnabled(id, atoi(on) != 0))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "App-Status konnte nicht geändert werden");
    UiShell::instance().requestAppsRefresh();
    sendJson(req, "{\"ok\":true}"); return ESP_OK;
}


esp_err_t WebServerService::appSettingsOpenHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;

    char rawId[48]{};
    if (!queryValue(req, "id", rawId, sizeof(rawId)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id fehlt");

    const String id = urlDecode(rawId);
    AppRegistryEntry entry;
    if (!AppRegistry::instance().lookup(id.c_str(), entry) ||
        !entry.hasSettings || !entry.app)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,
                                   "Diese App hat keine eigene Einstellungsseite");

    // Queue only: native/Lua settings stay on the UI task under the central
    // display lock. The HTTP task never touches LVGL or executes settings.lua.
    if (!UiShell::instance().requestAppSettings(id.c_str())) {
        sendJson(req,
                 "{\"ok\":false,\"message\":\"Einstellungsseite konnte nicht angefordert werden\"}",
                 "503 Service Unavailable");
        return ESP_OK;
    }

    sendJson(req,
             "{\"ok\":true,\"message\":\"App-Einstellungen wurden am Display geöffnet\"}",
             "202 Accepted");
    return ESP_OK;
}


esp_err_t WebServerService::appWebSettingsGetHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    char rawId[48]{};
    if (!queryValue(req, "id", rawId, sizeof(rawId)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id fehlt");

    String result, error;
    if (!AppWebSettingsService::instance().describe(urlDecode(rawId), result, error)) {
        sendJson(req,
                 "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}",
                 "400 Bad Request");
        return ESP_OK;
    }
    sendJson(req, result);
    return ESP_OK;
}

esp_err_t WebServerService::appWebSettingsDiscoveryHandler(httpd_req_t* req){
    if(!writeAuthorized(req))return ESP_OK;
    if(req->content_len<=0||req->content_len>512)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Suchanfrage");
    char body[513]{};int offset=0;
    while(offset<req->content_len){const int got=httpd_req_recv(req,body+offset,req->content_len-offset);if(got<=0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Anfrage unvollständig");offset+=got;}
    JsonDocument input;
    if(deserializeJson(input,body,offset,DeserializationOption::NestingLimit(3))||!input["id"].is<const char*>()||!input["field"].is<const char*>()||(!input["run"].isUnbound()&&!input["run"].is<uint32_t>()))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Ungültige Suchanfrage");
    String output,error;
    if(!AppWebSettingsService::instance().discover(input["id"].as<String>(),input["field"].as<String>(),input["run"]|0u,output,error)){
        sendJson(req,"{\"ok\":false,\"error\":\""+jsonEscape(error)+"\"}","400 Bad Request");return ESP_OK;
    }
    sendJson(req,output);return ESP_OK;
}

esp_err_t WebServerService::appWebSettingsSaveHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    if (req->content_len <= 0 || req->content_len > 16384)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Ungültige Größe");

    String body;
    if (!body.reserve(req->content_len + 1))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speicher");

    char buffer[512];
    int remaining = req->content_len;
    while (remaining > 0) {
        const int want = min<int>(remaining, sizeof(buffer));
        const int got = httpd_req_recv(req, buffer, want);
        if (got <= 0)
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Anfrage unvollständig");
        if (!body.concat(buffer, got))
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Speicher");
        remaining -= got;
    }

    String result, error;
    if (!AppWebSettingsService::instance().apply(body, result, error)) {
        sendJson(req,
                 "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}",
                 "400 Bad Request");
        return ESP_OK;
    }
    sendJson(req, result);
    return ESP_OK;
}


esp_err_t WebServerService::appDataExportHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    char rawId[48]{};
    if (!queryValue(req, "id", rawId, sizeof(rawId)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id fehlt");
    return LuaAppDataCleanupService::instance().sendExport(
        req, urlDecode(rawId));
}

esp_err_t WebServerService::networkHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    const auto n = NetworkService::instance().overview();

    JsonDocument doc;
    doc["path"] = NetworkService::instance().pathName();

    auto eth = doc["ethernet"].to<JsonObject>();
    eth["connected"] = n.ethernet.connected;
    eth["ip"] = n.ethernet.ip;
    eth["gateway"] = n.ethernet.gateway;
    eth["subnet"] = n.ethernet.subnet;
    eth["dns1"] = n.ethernet.dns1;
    eth["dns2"] = n.ethernet.dns2;
    eth["mac"] = n.ethernet.mac;
    eth["linkSpeedMbps"] = n.ethernet.linkSpeedMbps;

    auto wifi = doc["wifi"].to<JsonObject>();
    wifi["enabled"] = n.wifi.enabled;
    wifi["connected"] = n.wifi.connected;
    wifi["configured"] = n.wifi.configured;
    wifi["c6Ready"] = n.wifi.c6Ready;
    wifi["ssid"] = n.wifi.ssid;
    wifi["ip"] = n.wifi.ip;
    wifi["mac"] = n.wifi.mac;
    wifi["rssi"] = n.wifi.rssi;
    wifi["channel"] = n.wifi.channel;
    wifi["text"] = n.wifi.text;
    wifi["policy"] = NetworkService::wifiPolicyReasonName(n.wifiPolicyReason);

    auto profiles = doc["profiles"].to<JsonArray>();
    for (uint8_t i = 0; i < NetworkService::kMaxWifiProfiles; ++i) {
        String ssid, password;
        WifiProfileAddressing addressing;
        NetworkService::instance().getWifiProfile(i, ssid, password, addressing);
        auto p = profiles.add<JsonObject>();
        p["slot"] = i;
        p["ssid"] = ssid;
        p["configured"] = NetworkService::instance().wifiProfileConfigured(i);
        p["preferred"] = NetworkService::instance().preferredWifiProfile() == i;
        p["ip"] = addressing.ip;
        p["gateway"] = addressing.gateway;
        p["subnet"] = addressing.subnet;
        p["dns1"] = addressing.dns1;
        p["dns2"] = addressing.dns2;
    }

    auto scan = doc["scan"].to<JsonObject>();
    scan["active"] = NetworkService::instance().wifiScanInProgress();
    scan["status"] = NetworkService::instance().wifiScanStatus();
    auto results = scan["results"].to<JsonArray>();
    WifiNetwork cached[12];
    const int count = NetworkService::instance().scanNetworks(cached, 12);
    for (int i = 0; i < count; ++i) {
        auto item = results.add<JsonObject>();
        item["ssid"] = cached[i].ssid;
        item["rssi"] = cached[i].rssi;
        item["secure"] = cached[i].secure;
    }

    String body;
    body.reserve(4096);
    serializeJson(doc, body);
    sendJson(req, body);
    return ESP_OK;
}

esp_err_t WebServerService::wifiScanHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    if (!NetworkService::instance().requestWifiScan()) {
        sendJson(req, "{\"ok\":false,\"status\":\"WLAN-Suche konnte nicht gestartet werden\"}", "503 Service Unavailable");
        return ESP_OK;
    }
    sendJson(req, "{\"ok\":true,\"status\":\"WLAN-Suche gestartet …\"}", "202 Accepted"); return ESP_OK;
}

esp_err_t WebServerService::wifiProfileHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char slotText[8]{}, ssid[96]{}, password[128]{};
    char ip[32]{}, gateway[32]{}, subnet[32]{}, dns1[32]{}, dns2[32]{};
    if (!queryValue(req,"slot",slotText,sizeof(slotText)) ||
        !queryValue(req,"ssid",ssid,sizeof(ssid)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slot/ssid fehlt");
    queryValue(req,"password",password,sizeof(password));
    queryValue(req,"ip",ip,sizeof(ip));
    queryValue(req,"gateway",gateway,sizeof(gateway));
    queryValue(req,"subnet",subnet,sizeof(subnet));
    queryValue(req,"dns1",dns1,sizeof(dns1));
    queryValue(req,"dns2",dns2,sizeof(dns2));

    const int slot = atoi(slotText);
    if (slot < 0 || slot >= NetworkService::kMaxWifiProfiles)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ungültiges Profil");

    String oldSsid, oldPassword;
    WifiProfileAddressing oldAddressing;
    NetworkService::instance().getWifiProfile((uint8_t)slot, oldSsid, oldPassword, oldAddressing);

    const String decodedSsid = urlDecode(ssid);
    const String decodedPassword = password[0] ? urlDecode(password) : oldPassword;
    WifiProfileAddressing addressing;
    addressing.ip = urlDecode(ip);
    addressing.gateway = urlDecode(gateway);
    addressing.subnet = urlDecode(subnet);
    addressing.dns1 = urlDecode(dns1);
    addressing.dns2 = urlDecode(dns2);

    if (!NetworkService::instance().connectWifiProfile(
            (uint8_t)slot, decodedSsid, decodedPassword, addressing))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "WLAN-Profil/IP-Konfiguration konnte nicht gespeichert werden");
    sendJson(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::wifiEnabledHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char on[8]{}; if (!queryValue(req,"on",on,sizeof(on))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "on fehlt");
    NetworkService::instance().setWifiEnabled(atoi(on)!=0);
    sendJson(req, "{\"ok\":true}"); return ESP_OK;
}

esp_err_t WebServerService::wifiPreferredHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char slotText[8]{};
    if (!queryValue(req, "slot", slotText, sizeof(slotText)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slot fehlt");
    const int slot = atoi(slotText);
    if (slot < 0 || slot >= NetworkService::kMaxWifiProfiles ||
        !NetworkService::instance().setPreferredWifiProfile((uint8_t)slot))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Bevorzugtes WLAN-Profil konnte nicht gesetzt werden");
    sendJson(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::wifiForgetHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char slotText[8]{};
    if (!queryValue(req, "slot", slotText, sizeof(slotText)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "slot fehlt");
    const int slot = atoi(slotText);
    if (slot < 0 || slot >= NetworkService::kMaxWifiProfiles ||
        !NetworkService::instance().forgetWifiProfile((uint8_t)slot))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "WLAN-Profil konnte nicht gelöscht werden");
    sendJson(req, "{\"ok\":true}");
    return ESP_OK;
}

esp_err_t WebServerService::regionHandler(httpd_req_t* req){
    if(req->method==HTTP_GET){if(!authorized(req))return ESP_OK;sendJson(req,RegionService::instance().catalogJson());return ESP_OK;}
    if(!writeAuthorized(req))return ESP_OK;char code[4]{};if(!queryValue(req,"country",code,sizeof(code)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"region_country");
    const int index=RegionService::find(code);if(index<0)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"region_country");
    if(!RegionService::instance().request(index)){sendJson(req,"{\"error\":\"region_busy_or_locked\"}","409 Conflict");return ESP_OK;}
    sendJson(req,"{\"pending\":true}","202 Accepted");return ESP_OK;
}
esp_err_t WebServerService::languageHandler(httpd_req_t* req){
    if(req->method==HTTP_GET){if(!authorized(req))return ESP_OK;char keys[1552]{},query[1600]{};
        if(httpd_req_get_url_query_len(req)>=sizeof(query))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"language_key_limit");
        if(httpd_req_get_url_query_len(req)){if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"language_query");
            const auto rc=httpd_query_key_value(query,"keys",keys,sizeof(keys));if(rc!=ESP_OK&&rc!=ESP_ERR_NOT_FOUND)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"language_key_limit");}
        const String decoded=urlDecode(keys);if(decoded.length()!=strlen(decoded.c_str()))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"language_key_invalid");
        const String response=LanguageService::instance().catalogJson(decoded.length()?decoded.c_str():nullptr);sendJson(req,response,response.startsWith("{\"error\":")?"400 Bad Request":nullptr);return ESP_OK;}
    if(!writeAuthorized(req))return ESP_OK;char locale[16]{};if(!queryValue(req,"id",locale,sizeof(locale))||!LanguageService::validId(locale))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"language_id");
    DisplayUiGuard guard(100);if(!guard){sendJson(req,"{\"error\":\"ui_busy\"}","503 Service Unavailable");return ESP_OK;}auto& service=LanguageService::instance();
    if(!service.request(locale))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"language_unavailable_locked_or_busy");
    JsonDocument out;out["queued"]=service.busy();out["message"]=service.status();out["restartRequired"]=strcmp(service.active(),locale)!=0;String response;serializeJson(out,response);sendJson(req,response);return ESP_OK;
}
esp_err_t WebServerService::settingsHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    String body;
    {
        DisplayUiGuard guard;
        if (!guard) {
            commonHeaders(req);
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "Anzeige beschaeftigt");
        }
        auto& catalog = ThemeCatalog::instance();
        catalog.begin();
        catalog.refreshSd();
        const auto s = SettingsService::instance().snapshot();
        JsonDocument output;
        output["brightness"] = s.brightness;
        output["themeMode"] = catalog.normalizeMode(s.ui.themeMode);
        output["accent"] = s.ui.accent;
        auto themes = output["themes"].to<JsonArray>();
        for (uint8_t id = 0; id < catalog.count(); ++id) {
            auto theme = themes.add<JsonObject>();
            theme["id"] = id;
            theme["name"] = catalog.name(id);
        }
        auto accents = output["accents"].to<JsonArray>();
        for (uint8_t id = 0; id < ThemeCatalog::kAccentCount; ++id) {
            auto accent = accents.add<JsonObject>();
            accent["id"] = id;
            accent["name"] = ThemeCatalog::accentName(id);
        }
        serializeJson(output, body);
    }
    sendJson(req, body); return ESP_OK;
}

esp_err_t WebServerService::themeHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char mode[8]{}, accent[8]{};
    if (!queryValue(req,"mode",mode,sizeof(mode)) || !queryValue(req,"accent",accent,sizeof(accent)))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mode/accent fehlt");
    char *modeEnd = nullptr, *accentEnd = nullptr;
    const long m=strtol(mode,&modeEnd,10), a=strtol(accent,&accentEnd,10);
    if (!mode[0] || *modeEnd || !accent[0] || *accentEnd || m<0 || m>ThemeCatalog::kMaxMode || a<0 || a>5)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Theme ungueltig");
    {
        DisplayUiGuard guard;
        if (!guard) {
            commonHeaders(req);
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "Anzeige beschaeftigt");
        }
        if (!ThemeController::instance().apply((uint8_t)m,(uint8_t)a))
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Theme konnte nicht gesetzt werden");
    }
    sendJson(req, "{\"ok\":true}"); return ESP_OK;
}

esp_err_t WebServerService::brightnessHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    char value[8]{}; if (!queryValue(req,"value",value,sizeof(value))) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "value fehlt");
    const int v=atoi(value); if(v<1||v>255||!SettingsService::instance().setBrightness((uint8_t)v)) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Helligkeit ungültig");
    sendJson(req, "{\"ok\":true}"); return ESP_OK;
}

esp_err_t WebServerService::restartHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    std::unique_lock<std::mutex> updateLock(OtaWriteSession::mutex(), std::try_to_lock);
    if (!updateLock.owns_lock()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "OTA still in progress");
    }
    EventLog::instance().add("WARN","WEB_RESTART","Restart requested from port 80");
    sendJson(req, "{\"ok\":true}"); delay(200); esp_restart(); return ESP_OK;
}

esp_err_t WebServerService::serviceStatusHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    auto& service = FileTransferService::instance();
    const uint32_t remaining = service.remainingSeconds();
    const String body =
        "{\"running\":" + String(service.running() ? "true" : "false") +
        ",\"timed\":" + String(service.timedSession() ? "true" : "false") +
        ",\"remainingSeconds\":" + String((unsigned long)remaining) +
        ",\"maxSeconds\":1800}";
    sendJson(req, body);
    return ESP_OK;
}

esp_err_t WebServerService::serviceStartHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    auto& service = FileTransferService::instance();
    if (!service.startTimed(30)) {
        DiagnosticsService::instance().recordHttpFailure();
        return httpd_resp_send_err(
            req, HTTPD_500_INTERNAL_SERVER_ERROR,
            "Service-Port 8088 konnte nicht gestartet werden");
    }
    const String body =
        "{\"running\":true,\"remainingSeconds\":" +
        String((unsigned long)service.remainingSeconds()) +
        ",\"maxSeconds\":1800}";
    sendJson(req, body);
    return ESP_OK;
}

esp_err_t WebServerService::serviceStopHandler(httpd_req_t* req) {
    if (!writeAuthorized(req)) return ESP_OK;
    FileTransferService::instance().stop();
    EventLog::instance().add(
        "INFO", "SERVICE_WEB_STOP",
        "Port 8088 manuell beendet");
    sendJson(req, "{\"running\":false,\"remainingSeconds\":0,\"maxSeconds\":1800}");
    return ESP_OK;
}

esp_err_t WebServerService::serviceRedirectHandler(httpd_req_t* req) {
    if (!authorized(req)) return ESP_OK;
    String location = "https://" + SettingsService::instance().snapshot().hostname + ".local:8088" + String(req->uri);
    commonHeaders(req); httpd_resp_set_status(req,"302 Found"); httpd_resp_set_hdr(req,"Location",location.c_str());
    return httpd_resp_sendstr(req, "");
}

} // namespace hp

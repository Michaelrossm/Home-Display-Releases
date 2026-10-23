#include "RecoveryWebService.h"
#include "BuildInfo.h"
#include "ConfigService.h"
#include "DiagnosticsService.h"
#include "NetworkService.h"
#include "../core/RecoveryManager.h"
#include "../core/EventLog.h"
#include "WebAdminAuth.h"
#include <esp_system.h>
#include <cstring>

namespace hp {
namespace {

void headers(httpd_req_t* req) {
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(req,"Referrer-Policy","no-referrer");
}

bool authorized(httpd_req_t* req) {
    if(WebAdminAuth::authorized(req))return true;
    headers(req);httpd_resp_set_status(req,"401 Unauthorized");httpd_resp_set_hdr(req,"WWW-Authenticate","Basic realm=\"Home-Display Recovery\"");
    httpd_resp_sendstr(req,"Anmeldung erforderlich");return false;
}

bool query(httpd_req_t* req,const char*key,char*out,size_t n){char q[256]{};return httpd_req_get_url_query_str(req,q,sizeof(q))==ESP_OK&&httpd_query_key_value(q,key,out,n)==ESP_OK;}

const char kRecoveryHtml[] PROGMEM=R"HTML(<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Home-Display Recovery</title><style>
*{box-sizing:border-box}body{margin:0;background:#10151c;color:#edf3fa;font:15px system-ui,-apple-system,Segoe UI,Roboto,sans-serif}.wrap{max-width:900px;margin:28px auto;padding:0 16px}.hero,.card{background:#19222e;border:1px solid #344354;border-radius:14px;padding:18px;margin:12px 0}.hero h1{margin:0 0 6px}.warn{color:#ffcf70}.ok{color:#74d89a}.btn{display:inline-block;border:1px solid #42566b;background:#243346;color:white;padding:10px 12px;border-radius:9px;margin:5px 6px 5px 0;text-decoration:none;cursor:pointer}.danger{background:#6e2d33;border-color:#9e444d}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.pre{white-space:pre-wrap;background:#0b1016;padding:12px;border-radius:9px;max-height:360px;overflow:auto;font:12px ui-monospace,Consolas,monospace}@media(max-width:650px){.grid{grid-template-columns:1fr}}</style></head><body><div class="wrap"><div class="hero"><h1>Home-Display Recovery</h1><div id="reason" class="warn">Status wird geladen …</div><p>Diese Oberfläche ist absichtlich klein und unabhängig von AppFS, Lua-Apps und der normalen Benutzeroberfläche.</p></div><div class="grid"><div class="card"><h2>Reparatur</h2><button class="btn" onclick="act('repair-assets')">Assets prüfen / reparieren</button><button class="btn" onclick="act('reset-network')">Netzwerk zurücksetzen</button><button class="btn" onclick="act('reset-ui')">UI zurücksetzen</button><button class="btn" onclick="act('reset-webapps')">Web-Apps zurücksetzen</button><button class="btn" onclick="act('reset-calendar')">Kalenderkonten zurücksetzen</button><button class="btn" onclick="act('reset-media')">Media zurücksetzen</button><button class="btn" onclick="act('reset-user')">Benutzereinstellungen zurücksetzen</button></div><div class="card"><h2>Boot / Firmware</h2><button class="btn" onclick="act('restart')">Neu starten</button><button class="btn" onclick="rollback()">Vorherige Firmware / Rollback</button><p class="warn">Rollback startet das Gerät sofort neu, wenn ein gültiger vorheriger OTA-Slot vorhanden ist.</p></div></div><div class="card"><h2>Diagnose</h2><button class="btn" onclick="diag()">Supportbericht aktualisieren</button><pre id="diag" class="pre">–</pre></div><div class="card"><h2>Werksreset</h2><p class="warn">Löscht Benutzereinstellungen, Web-Apps und Kalenderdaten. Firmware und Recovery bleiben erhalten.</p><button class="btn danger" onclick="factoryReset()">Werksreset ausführen</button></div></div><script>
async function api(u,o){let r=await fetch(u,o);let t=await r.text();if(!r.ok)throw Error(t||('HTTP '+r.status));try{return JSON.parse(t)}catch(e){return t}}
async function status(){try{let s=await api('/api/status');document.getElementById('reason').textContent=s.required?'Recovery erforderlich: '+s.reason:'Recovery-Service bereit · '+s.ip;document.getElementById('reason').className=s.required?'warn':'ok'}catch(e){document.getElementById('reason').textContent='Statusfehler: '+e.message}}
async function act(a){if(!confirm('Aktion wirklich ausführen?\n'+a))return;try{await api('/api/action?action='+encodeURIComponent(a),{method:'POST'});alert('Aktion ausgeführt. Bei Bedarf Gerät neu starten.');status()}catch(e){alert('Fehler: '+e.message)}}
async function rollback(){if(!confirm('Vorherige Firmware markieren und neu starten?'))return;try{await api('/api/action?action=rollback',{method:'POST'})}catch(e){}}
async function factoryReset(){if(!confirm('WERKSRESET: Alle Benutzereinstellungen wirklich löschen?'))return;if(prompt('Zur Bestätigung RESET eingeben')!=='RESET')return;try{await api('/api/action?action=factory-reset&confirm=RESET',{method:'POST'});alert('Werksreset ausgeführt. Gerät wird neu gestartet.')}catch(e){alert('Fehler: '+e.message)}}
async function diag(){try{document.getElementById('diag').textContent=await api('/api/diagnostics')}catch(e){document.getElementById('diag').textContent=e.message}}
status();diag();</script></body></html>)HTML";

RecoveryAction decodeAction(const String& action,bool&valid,bool&reboots){valid=true;reboots=false;
    if(action=="restart"){reboots=true;return RecoveryAction::Restart;}
    if(action=="rollback"){reboots=true;return RecoveryAction::PreviousFirmware;}
    if(action=="repair-assets")return RecoveryAction::RepairAssets;
    if(action=="reset-network")return RecoveryAction::ResetNetwork;
    if(action=="reset-ui")return RecoveryAction::ResetUi;
    if(action=="reset-webapps")return RecoveryAction::ResetWebApps;
    if(action=="reset-calendar")return RecoveryAction::ResetCalendarAccounts;
    if(action=="reset-media")return RecoveryAction::ResetMedia;
    if(action=="reset-user")return RecoveryAction::ResetUserSettings;
    if(action=="factory-reset")return RecoveryAction::FullFactoryReset;
    valid=false;return RecoveryAction::Restart;
}

}

RecoveryWebService& RecoveryWebService::instance(){static RecoveryWebService s;return s;}

bool RecoveryWebService::begin(){
    if(server_)return true;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.server_port=8088;cfg.ctrl_port=32770;cfg.max_open_sockets=2;cfg.max_uri_handlers=8;cfg.stack_size=7168;cfg.lru_purge_enable=true;
    if(httpd_start(&server_,&cfg)!=ESP_OK){server_=nullptr;return false;}
    const httpd_uri_t routes[]={
        {"/",HTTP_GET,rootHandler,nullptr},
        {"/api/status",HTTP_GET,statusHandler,nullptr},
        {"/api/action",HTTP_POST,actionHandler,nullptr},
        {"/api/diagnostics",HTTP_GET,diagnosticsHandler,nullptr},
        {"/restart",HTTP_POST,restartHandler,nullptr}
    };
    for(const auto&r:routes)if(httpd_register_uri_handler(server_,&r)!=ESP_OK){stop();return false;}
    EventLog::instance().add("WARN","RECOVERY_WEB","Recovery web service listening on port 8088");return true;
}

void RecoveryWebService::stop(){if(server_){httpd_stop(server_);server_=nullptr;}}

esp_err_t RecoveryWebService::rootHandler(httpd_req_t*req){if(!authorized(req))return ESP_OK;headers(req);httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");httpd_resp_set_type(req,"text/html; charset=utf-8");return httpd_resp_send(req,kRecoveryHtml,HTTPD_RESP_USE_STRLEN);}

esp_err_t RecoveryWebService::statusHandler(httpd_req_t*req){if(!authorized(req))return ESP_OK;const bool required=RecoveryManager::instance().required();String reason=RecoveryManager::instance().reason();String body="{\"required\":"+String(required?"true":"false")+",\"reason\":\""+reason+"\",\"version\":\""+String(build::kVersion)+"\",\"ip\":\""+NetworkService::instance().ip()+"\"}";headers(req);httpd_resp_set_type(req,"application/json");return httpd_resp_send(req,body.c_str(),body.length());}

esp_err_t RecoveryWebService::actionHandler(httpd_req_t*req){
    if(!authorized(req))return ESP_OK;char actionText[48]{},confirm[16]{};if(!query(req,"action",actionText,sizeof(actionText)))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"action fehlt");query(req,"confirm",confirm,sizeof(confirm));
    bool valid=false,reboots=false;RecoveryAction action=decodeAction(actionText,valid,reboots);if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"unbekannte Aktion");
    const bool confirmed=action==RecoveryAction::FullFactoryReset&&strcmp(confirm,"RESET")==0;if(action==RecoveryAction::FullFactoryReset&&!confirmed)return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Factory Reset nicht bestätigt");
    EventLog::instance().add("WARN","RECOVERY_WEB_ACTION",actionText);
    if(reboots){headers(req);httpd_resp_set_type(req,"application/json");httpd_resp_sendstr(req,"{\"ok\":true,\"rebooting\":true}");delay(200);RecoveryManager::instance().execute(action,confirmed);return ESP_OK;}
    const bool ok=RecoveryManager::instance().execute(action,confirmed);if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Recovery-Aktion fehlgeschlagen");
    headers(req);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}

esp_err_t RecoveryWebService::diagnosticsHandler(httpd_req_t*req){if(!authorized(req))return ESP_OK;headers(req);httpd_resp_set_type(req,"text/plain; charset=utf-8");const String r=DiagnosticsService::instance().supportReportText();return httpd_resp_send(req,r.c_str(),r.length());}

esp_err_t RecoveryWebService::restartHandler(httpd_req_t*req){if(!authorized(req))return ESP_OK;headers(req);httpd_resp_sendstr(req,"rebooting");delay(200);esp_restart();return ESP_OK;}

}

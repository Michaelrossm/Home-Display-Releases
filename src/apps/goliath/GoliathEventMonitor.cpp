#include "GoliathEventMonitor.h"
#include "GoliathSettings.h"
#include "GoliathDiagnostics.h"
#include "../../services/NetworkService.h"
#include "../../services/NetworkWorker.h"
#include "../../services/IntercomService.h"
#include <mbedtls/md5.h>
#include <esp_system.h>
#include <cstring>
namespace hp {
namespace {
const char* const path = "/cgi-bin/eventManager.cgi?action=attach&codes=%5BAll%5D";
String param(const String &src, const char *name) {
    const String needle = String(name) + "=";
    int p = 0;
    while (p < int(src.length())) {
        while (p < int(src.length()) && (src[p] == ' ' || src[p] == ',')) ++p;
        const int first = p;
        while (p < int(src.length()) && src[p] != '=' && src[p] != ',') ++p;
        if (p >= int(src.length()) || src[p] != '=') { ++p; continue; }
        String key = src.substring(first, p++); key.trim();
        String value;
        if (p < int(src.length()) && src[p] == '"') {
            ++p;
            while (p < int(src.length()) && src[p] != '"') {
                if (src[p] == '\\' && p + 1 < int(src.length())) ++p;
                value += src[p++];
            }
            if (p < int(src.length())) ++p;
        } else {
            int end = src.indexOf(',', p);
            value = src.substring(p, end < 0 ? src.length() : end);
            value.trim(); p = end < 0 ? src.length() : end;
        }
        if (key.equalsIgnoreCase(name)) return value;
    }
    return {};
}
String hash(const String& s) {
    uint8_t data[16];
    if (mbedtls_md5(reinterpret_cast<const unsigned char*>(s.c_str()), s.length(), data)) return {};
    char result[33];
    constexpr char h[] = "0123456789abcdef";
    for (unsigned i=0;i<16;i++){ result[i*2]=h[data[i]>>4];result[i*2+1]=h[data[i]&15]; }
    result[32]=0; return String(result);
}
void log(const char *code, const char *message) {
    GoliathDiagnostics::instance().add(code, message);
    IntercomService::instance().diagnostic(code);
}
}
GoliathEventMonitor& GoliathEventMonitor::instance() { static GoliathEventMonitor m; return m; }
bool GoliathEventMonitor::begin() {
    if (registered_) return true;
    registered_ = NetworkWorker::instance().addMaintenance(maintenance, this);
    if (registered_) settingsChanged();
    return registered_;
}
void GoliathEventMonitor::maintenance(void *ctx) { static_cast<GoliathEventMonitor*>(ctx)->poll(); }
void GoliathEventMonitor::settingsChanged() {
    // This method is called outside NetworkWorker. Avoid changing active socket here.
    retryAt_ = 0; nextConfigAt_ = 0;
}
const char* GoliathEventMonitor::stateName() const { return state_; }
void GoliathEventMonitor::disconnect(const char *reason, uint32_t backoff) {
    socket_.stop();
headers_ = ""; line_ = ""; headersDone_ = false;
    retryAt_ = millis() + backoff;
    strlcpy(state_, reason, sizeof(state_));
    // Never log raw CGI events, host, credentials, phone numbers or full URLs.
    if (strcmp(reason, "reconnect") && strcmp(reason, "disabled")) log(reason, "Dahua Ereigniskanal Statusaenderung");
}
void GoliathEventMonitor::sendRequest() {
    String req = String("GET ") + path + " HTTP/1.1\r\nHost: " + host_ +
                 "\r\nAccept: text/plain\r\nConnection: keep-alive\r\n";
    if (authorized_) {
        const String nc = "00000001";
        char randomHex[9]; snprintf(randomHex, sizeof(randomHex), "%08lx", (unsigned long)esp_random());
        const String cnonce(randomHex);
        const String a1=hash(user_+":"+realm_+":"+password_);
        const String a2=hash(String("GET:")+path);
        const String response = qop_.length() ? hash(a1+":"+nonce_+":"+nc+":"+cnonce+":auth:"+a2)
                                               : hash(a1+":"+nonce_+":"+a2);
        req += "Authorization: Digest username=\""+user_+"\", realm=\""+realm_+
               "\", nonce=\""+nonce_+"\", uri=\""+path+"\", response=\""+response+"\"";
        if (qop_.length()) req += ", qop=auth, nc="+nc+", cnonce=\""+cnonce+"\"";
        if (opaque_.length()) req += ", opaque=\""+opaque_+"\"";
        req += "\r\n";
    }
    req += "\r\n";
    socket_.write(reinterpret_cast<const uint8_t*>(req.c_str()), req.length());
    lastByte_=millis();
    strlcpy(state_, "connecting", sizeof(state_));
}
void GoliathEventMonitor::handleLine(const String& line) {
    if (!line.startsWith("Code=") || line.length()>256) return;
    const int semi=line.indexOf(';');
    const String code=line.substring(5,semi<0?line.length():semi);
    if (code.length()>48) return;
    String action;
    int a=line.indexOf("action=");
    if (a>=0) { int end=line.indexOf(';',a); action=line.substring(a+7,end<0?line.length():end); action.trim(); }
    // Different Dahua generations emit Invite / Call / CallNoAnswered; do NOT
    // infer an apartment from generic alarm or call events.
    if (code.equalsIgnoreCase("Invite") || code.equalsIgnoreCase("Call") ||
        code.equalsIgnoreCase("CallNoAnswered") || code.equalsIgnoreCase("VideoTalk")) {
        ++observed_;
        // No room-specific proof yet. Conservative diagnostic-only behavior.
        log(action.equalsIgnoreCase("Stop") ? "GOL_CALL_END" : "GOL_CALL_SEEN",
            "Rufereignis erkannt; Wohnungszuordnung noch nicht bestaetigt");
    } else if (code.equalsIgnoreCase("KeepAlive")) {
        // Silent heartbeat: no log storm.
    }
}
void GoliathEventMonitor::poll() {
    if (!NetworkWorker::instance().backgroundWindow()) return;
    static GoliathAppSettings cfg;
    if (int32_t(millis()-nextConfigAt_)>=0) {
        cfg = GoliathSettings::instance().load();
        nextConfigAt_ = millis()+2000;
    }
    const bool canRun = cfg.enabled && cfg.diagnosticsEnabled && cfg.host.length() &&
                        cfg.username.length() && cfg.password.length() &&
                        NetworkService::instance().online();
    if (!canRun) {
        if (socket_.connected()) disconnect("disabled", 0);
        user_ = ""; password_ = ""; realm_ = ""; nonce_ = ""; opaque_ = ""; qop_ = "";
        enabled_=false; strlcpy(state_, "disabled", sizeof(state_)); return;
    }
    if (!enabled_ || host_ != cfg.host || port_ != cfg.httpPort || user_ != cfg.username ||
        apartment_ != cfg.apartment || password_ != cfg.password) {
        socket_.stop(); headers_= "";line_= "";headersDone_=false;authorized_=false;attempts_=0;
        host_=cfg.host;port_=cfg.httpPort;user_=cfg.username;password_=cfg.password;
        apartment_=cfg.apartment;enabled_=true;retryAt_=0;
    }
    if (!socket_.connected()) {
        if (int32_t(millis()-retryAt_)<0) return;
        IPAddress ip;
        if (!ip.fromString(host_)) { disconnect("invalid_ip", 30000); return; }
        // Existing NetworkWorker maintenance calls can share the stream with video.
        if (!socket_.connect(ip,port_,250)) { disconnect("event_connect_fail",10000);return; }
        socket_.setTimeout(20);sendRequest();return;
    }
    if (uint32_t(millis()-lastByte_)>30000) { disconnect("event_timeout");return; }
    uint32_t start=micros(); unsigned bytes=0;
    while(socket_.available() && bytes<1024 && uint32_t(micros()-start)<1500) {
        int ch=socket_.read(); if(ch<0)break; ++bytes;lastByte_=millis();
        if (!headersDone_) {
            headers_ += char(ch);
            if (headers_.length()>4096) { disconnect("event_header_limit");return; }
            if (!headers_.endsWith("\r\n\r\n")) continue;
            if (headers_.startsWith("HTTP/1.1 401") || headers_.startsWith("HTTP/1.0 401")) {
                if (attempts_++) { disconnect("event_auth_fail",60000);return; }
                int pos=headers_.indexOf("WWW-Authenticate:");
                if(pos<0)pos=headers_.indexOf("Www-Authenticate:");
                if(pos<0) {disconnect("event_auth_unknown",60000);return;}
                int end=headers_.indexOf("\r\n",pos);
                String chall=headers_.substring(pos,end);int digest=chall.indexOf("Digest ");
                if(digest<0){disconnect("event_digest_required",60000);return;}
                chall=chall.substring(digest+7);
                realm_=param(chall,"realm");nonce_=param(chall,"nonce");
                opaque_=param(chall,"opaque");qop_=param(chall,"qop");
                const String alg=param(chall,"algorithm");
                if (!realm_.length()||!nonce_.length()||(alg.length()&&!alg.equalsIgnoreCase("MD5"))||
                    (qop_.length()&&qop_.indexOf("auth")<0)) {
                    disconnect("event_digest_unsupported",60000);return;
                }
                authorized_=true;socket_.stop();headers_= "";headersDone_=false;
                retryAt_=millis()+150; return;
            }
            if (!headers_.startsWith("HTTP/1.1 200") && !headers_.startsWith("HTTP/1.0 200")) {
                disconnect("event_http_status",30000);return;
            }
            headersDone_=true;headers_= "";strlcpy(state_,"listening",sizeof(state_));
            log("GOL_EVENT_READY","Dahua Ereigniskanal verbunden (nur Diagnose)");
        } else if (ch=='\n') {
            line_.trim();handleLine(line_);line_= "";
        } else if (ch!='\r') {
            if (line_.length()<256) line_+=char(ch);
            else {line_= "";disconnect("event_line_limit");return;}
        }
    }
}
}

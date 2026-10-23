#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HX711.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <WiFiUdp.h>
#include <Update.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <ArduinoJson.h>
#include "device_config.h"
#include "index_html.h"
#include "setup_html.h"
#include "license_text.h"

WebServer server(80);
DNSServer captiveDns;
WiFiUDP ntpUdp;
bool ntpServerRunning=false, hxSleeping=false, otaRunning=false, otaSucceeded=false, otaError=false;
String otaMessage;
String ntpHost="pool.ntp.org";
unsigned long otaRestartAt=0;
constexpr uint32_t LIGHT_SLEEP_US=500000;
constexpr uint32_t NTP_UNIX_OFFSET=2208988800UL;
constexpr uint8_t CAPTIVE_DNS_PORT=53;
bool wifiConnecting=false;
Preferences preferences;
HX711 hx;
String ssid, wifiPass, deviceName, adminPassword;
String staticIp="192.168.178.178", staticGateway="192.168.178.1", staticSubnet="255.255.255.0", staticDns="192.168.178.1";
bool staticIpEnabled=false;
// Standard-Nennfuellmenge: 2 kg CO2. Leergewicht und Anbauteile
// muessen an der realen, komplett angeschlossenen Mechanik ermittelt werden.
float emptyG=0, regulatorG=0, capacityG=2000, factor=0;
long rawOffset=0;
bool hasTare=false, scheduleEnabled=false;
int offHour=22, onHour=6;
bool stationActive=false, apActive=false, ntpConfigured=false;
unsigned long wifiStartedAt=0, nextSample=0, deferredSwitch=0, lastSampleAt=0;
unsigned long nextStationRetry=0;
// Boot-Schutz und Rettungsmodus: Zeitplan darf die Wiederherstellung nie verhindern.
constexpr uint32_t BOOT_WIFI_GRACE_MS=120000UL;
constexpr uint32_t AP_RETRY_MS=15000UL;
constexpr uint32_t STA_CONNECT_TIMEOUT_MS=30000UL;
unsigned long bootStartedAt=0, nextApRetry=0;
bool wifiEverConnected=false, otaRecoveryPending=false, radioStoppedForSchedule=false;
uint32_t wifiFailures=0, apFailures=0;
String wifiLastError="";

// 1 Messzyklus pro Minute. Pro Zyklus werden 8 Rohwerte gemittelt.
constexpr uint32_t SAMPLE_PERIOD_MS=60000;
constexpr uint8_t RAW_READINGS_PER_CYCLE=8;
constexpr uint32_t SAMPLE_TIMEOUT_MS=5000;
constexpr uint32_t WIFI_TICK_MS=1000;
constexpr uint32_t IDLE_CPU_MHZ=80;
constexpr uint32_t ACTIVE_CPU_MHZ=160;
bool sampling=false, sampleAttempted=false;
uint8_t rawReadings=0;
int64_t rawTotal=0;
long cachedRaw=0; bool cachedRawValid=false;
unsigned long sampleStartedAt=0, lastWifiTick=0;
void idleCpu(){if(getCpuFrequencyMhz()!=IDLE_CPU_MHZ)setCpuFrequencyMhz(IDLE_CPU_MHZ);}
void activeCpu(){if(getCpuFrequencyMhz()!=ACTIVE_CPU_MHZ)setCpuFrequencyMhz(ACTIVE_CPU_MHZ);}
float samples[12]={0}; int sampleCount=0,samplePos=0;
String apName;
String csrfToken;
// Das Board muss mindestens zwei OTA-App-Partitionen besitzen.
constexpr uint32_t OTA_HEALTH_MS=120000UL;
constexpr uint32_t BOOTLOOP_THRESHOLD=3;
constexpr uint32_t BACKUP_MAX_BYTES=4096;
constexpr const char* FIRMWARE_BUILD="1.0.13-recovery-2";
RTC_DATA_ATTR uint32_t rtcBootMagic=0;
RTC_DATA_ATTR uint32_t rtcUnconfirmedBoots=0;
constexpr uint32_t RTC_BOOT_MAGIC=0xC02A2026UL;
bool safeMode=false, firmwareConfirmed=false;
String rollbackInfo="";
esp_reset_reason_t lastResetReason=ESP_RST_UNKNOWN;
String diagLog[12];uint8_t diagNext=0,diagCount=0;
void logDiag(const String& msg){
  diagLog[diagNext]=String(millis()/1000)+"s "+msg;
  diagNext=(diagNext+1)%12;
  if(diagCount<12)diagCount++;
  Serial.println("[DIAG] "+msg);
}
const char* resetReasonText(esp_reset_reason_t r){
  switch(r){
    case ESP_RST_POWERON:return "POWERON";
    case ESP_RST_SW:return "SOFTWARE";
    case ESP_RST_PANIC:return "PANIC";
    case ESP_RST_INT_WDT:return "INT_WDT";
    case ESP_RST_TASK_WDT:return "TASK_WDT";
    case ESP_RST_WDT:return "WDT";
    case ESP_RST_BROWNOUT:return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    default:return "OTHER";
  }
}
bool otaLayoutOk(){
  const esp_partition_t* running=esp_ota_get_running_partition();
  const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
  return running && target && running->type==ESP_PARTITION_TYPE_APP &&
         running->subtype>=ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
         running->subtype<=ESP_PARTITION_SUBTYPE_APP_OTA_15 &&
         target->type==ESP_PARTITION_TYPE_APP && target!=running;
}
size_t otaSlotSize(){
  const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
  return target?target->size:0;
}
// Software-Rollback: Der fruehere OTA-Slot enthaelt vor dem naechsten Update
// normalerweise die bisherige Firmware. Nur bei vorhandener gueltiger App wechseln.
// Ein Absturz vor setup() ist damit NICHT abfangbar (dafuer braucht es den
// gesondert mit Rollback kompilierten ESP-IDF-Bootloader).
bool rollbackToPrevious(){
  if(!otaLayoutOk())return false;
  const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
  esp_app_desc_t desc={};
  if(esp_ota_get_partition_description(target,&desc)!=ESP_OK || desc.version[0]=='\0')return false;
  esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
  if(esp_ota_get_state_partition(target,&state)==ESP_OK &&
     (state==ESP_OTA_IMG_INVALID || state==ESP_OTA_IMG_ABORTED))return false;
  if(esp_ota_set_boot_partition(target)!=ESP_OK)return false;
  rollbackInfo=String("Rueckwechsel zu ")+desc.version;
  logDiag(rollbackInfo);
  preferences.putBool("otarescue",false);
  delay(150);ESP.restart();return true;
}
void evaluateBootHealth(){
  lastResetReason=esp_reset_reason();
  if(rtcBootMagic!=RTC_BOOT_MAGIC || lastResetReason==ESP_RST_POWERON){
    rtcBootMagic=RTC_BOOT_MAGIC;rtcUnconfirmedBoots=0;
  }
  otaRecoveryPending=preferences.getBool("otarescue",false);
  if(otaRecoveryPending){
    rtcUnconfirmedBoots++;
    if(rtcUnconfirmedBoots>=BOOTLOOP_THRESHOLD){
      logDiag("OTA-Bootloop erkannt; pruefe vorherige Firmware");
      if(!rollbackToPrevious()){
        safeMode=true;
        rollbackInfo="Rollback nicht moeglich; Rettungs-Hotspot erzwungen";
        logDiag(rollbackInfo);
      }
    }
  }else rtcUnconfirmedBoots=0;
  logDiag(String("Start: ")+resetReasonText(lastResetReason)+
          ", unbestaetigte Boots="+String(rtcUnconfirmedBoots));
}
void confirmHealthyBoot(){
  if(firmwareConfirmed || (uint32_t)(millis()-bootStartedAt)<OTA_HEALTH_MS)return;
  // Ohne STA/AP ist die Firmware nicht ueber WLAN verwaltbar: nicht bestaetigen.
  if(!stationActive && !apActive)return;
  if(stationActive && WiFi.status()!=WL_CONNECTED)return;
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  // Nur bei eigens mit Rollback gebauten Bootloader wirksam.
  esp_ota_img_states_t imageState=ESP_OTA_IMG_UNDEFINED;
  if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&imageState)==ESP_OK &&
     imageState==ESP_OTA_IMG_PENDING_VERIFY){
    if(esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK){
      logDiag("Bootloader-OTA konnte nicht bestaetigt werden");return;
    }
  }
#endif
  firmwareConfirmed=true;
  rtcUnconfirmedBoots=0;
  if(otaRecoveryPending){
    preferences.putBool("otarescue",false);
    otaRecoveryPending=false;
  }
  logDiag("Boot nach 120 Sekunden und aktiver Netzwerkverbindung bestaetigt");
}



bool validClock() { return time(nullptr) > 1700000000; }
bool nightBlock() {
  if(!scheduleEnabled || !validClock() || offHour==onHour) return false;
  time_t now=time(nullptr); struct tm tmnow={}; localtime_r(&now,&tmnow);
  int h=tmnow.tm_hour;
  return offHour>onHour ? (h>=offHour || h<onHour) : (h>=offHour && h<onHour);
}
// Eine neue Firmware und ein Neustart bekommen immer Zeit fuer WLAN-Recovery.
// Solange kein erfolgreicher WLAN-Start stattgefunden hat, niemals alles abschalten.
bool allowScheduledWifiOff(){
 if(!nightBlock() || otaRunning || otaRestartAt) return false;
 if((uint32_t)(millis()-bootStartedAt)<BOOT_WIFI_GRACE_MS)return false;
 if(otaRecoveryPending || safeMode || !wifiEverConnected)return false;
 return true;
}
String jsonStr(const String& in) {
  String s="\"";
  for(size_t i=0;i<in.length();i++){ char c=in[i]; if(c=='"'||c=='\\')s+='\\';if(c=='\n'||c=='\r')s+=' ';else if((uint8_t)c>=32)s+=c; }
  return s+'"';
}
String numOrNull(float n,int places=2){return isfinite(n)?String(n,places):String("null");}
uint8_t calCount=0;
float avgG(){if(sampleCount==0 || (factor==0 && calCount==0))return NAN;float sum=0;for(int i=0;i<sampleCount;i++)sum+=samples[i];return sum/sampleCount;}
void wakeHx(){if(hxSleeping){hx.power_up();hxSleeping=false;}}
void sleepHx(){if(!hxSleeping){hx.power_down();hxSleeping=true;}}
// Mehrpunkt-Kalibrierung: 1 bis 8 Referenzgewichte plus Nullpunkt.
// Punkte werden nach Gewicht aufsteigend gehalten. HX711 kann je nach
// Anschluss eine positive oder negative Rohwert-Steigung besitzen.
constexpr uint8_t MAX_CAL_POINTS=8;
struct CalPoint {float grams; long raw;};
CalPoint calPoints[MAX_CAL_POINTS] = {};
float gramsFromRaw(double raw){
 if(!hasTare || (calCount==0 && factor==0))return NAN;
 const double delta=raw-(double)rawOffset;
 if(calCount==0)return (float)(delta/factor); // alte 1-Punkt-Kalibrierung
 // Nullpunkt (0 g) und Referenzgewichte bilden stueckweise lineare Abschnitte.
 double x0=0,y0=0,x1=(double)calPoints[0].raw-rawOffset,y1=calPoints[0].grams;
 const double sign=x1>=0?1.0:-1.0;
 const double position=delta*sign;
 // Unterhalb des ersten Punkts den Abschnitt 0 -> erster Punkt verwenden.
 // Oberhalb des letzten Punkts den letzten Abschnitt extrapolieren.
 if(position>x1*sign){
   for(uint8_t i=1;i<calCount;i++){
     x0=x1;y0=y1;
     x1=(double)calPoints[i].raw-rawOffset;y1=calPoints[i].grams;
     if(position<=x1*sign)break;
   }
 }
 if(fabs(x1-x0)<1.0)return NAN;
 return (float)(y0+(delta-x0)*(y1-y0)/(x1-x0));
}
void saveCalibration(){
 preferences.putUChar("caln",calCount);
 for(uint8_t i=0;i<MAX_CAL_POINTS;i++){
   String w="cw"+String(i),r="cr"+String(i);
   if(i<calCount){preferences.putFloat(w.c_str(),calPoints[i].grams);preferences.putLong(r.c_str(),calPoints[i].raw);}
   else {preferences.remove(w.c_str());preferences.remove(r.c_str());}
 }
}
void loadCalibration(){
 calCount=0;const uint8_t n=preferences.getUChar("caln",0);
 if(n>MAX_CAL_POINTS)return;
 for(uint8_t i=0;i<n;i++){
   String w="cw"+String(i),r="cr"+String(i);
   float g=preferences.getFloat(w.c_str(),0);long raw=preferences.getLong(r.c_str(),0);
   if(!isfinite(g)||g<50||g>10000||raw==rawOffset) {calCount=0;return;}
   if(i && (g<=calPoints[i-1].grams ||
     ((double)raw-calPoints[i-1].raw)*((double)calPoints[0].raw-rawOffset)<=0)) {calCount=0;return;}
   calPoints[i]={g,raw};calCount++;
 }
}

void clearSamples(){cachedRawValid=false;sampleCount=0;samplePos=0;lastSampleAt=0;nextSample=millis()-SAMPLE_PERIOD_MS;sampling=false;sampleAttempted=false;sleepHx();idleCpu();}
void saveScale(){preferences.putLong("offset",rawOffset);preferences.putBool("tare",hasTare);preferences.putFloat("factor",factor);saveCalibration();clearSamples();}
void loadSettings(){
 preferences.begin("co2scale",false);
 ssid=preferences.getString("ssid","");wifiPass=preferences.getString("pass","");
 deviceName=preferences.getString("name","CO2 Waage");
 adminPassword=preferences.getString("adminpass", "");
 emptyG=preferences.getFloat("empty",0);regulatorG=preferences.getFloat("reg",0);
 capacityG=preferences.getFloat("cap",2000);factor=preferences.getFloat("factor",0);
 rawOffset=preferences.getLong("offset",0);hasTare=preferences.getBool("tare",false);loadCalibration();
 scheduleEnabled=preferences.getBool("sched",false);
 staticIpEnabled=preferences.getBool("staticip",false);
 staticIp=preferences.getString("ip","192.168.178.178");
 staticGateway=preferences.getString("gateway","192.168.178.1");
 staticSubnet=preferences.getString("subnet","255.255.255.0");
 staticDns=preferences.getString("dns","192.168.178.1");
 offHour=preferences.getInt("off",22);onHour=preferences.getInt("on",6);
 ntpHost=preferences.getString("ntphost","pool.ntp.org");ntpHost.trim();if(ntpHost.isEmpty()||ntpHost.length()>100)ntpHost="pool.ntp.org";
 if(!isfinite(factor)||fabsf(factor)<0.00001f)factor=0;
 if(!isfinite(emptyG)||emptyG<0)emptyG=0;
 if(!isfinite(regulatorG)||regulatorG<0)regulatorG=0;
 if(!isfinite(capacityG)||capacityG<=0)capacityG=2000;
 if(offHour<0||offHour>23)offHour=22;
 if(onHour<0||onHour>23)onHour=6;
}
bool parseIp(const String& text,IPAddress& result){return result.fromString(text) && result!=(uint32_t)0;}
bool applyIpConfig(){
 if(!staticIpEnabled)return WiFi.config(INADDR_NONE,INADDR_NONE,INADDR_NONE);
 IPAddress ip,gateway,subnet,dns;
 if(!parseIp(staticIp,ip)||!parseIp(staticGateway,gateway)||!parseIp(staticSubnet,subnet)||!parseIp(staticDns,dns))return false;
 return WiFi.config(ip,gateway,subnet,dns);
}
void stopRadio(){ntpUdp.stop();ntpServerRunning=false;captiveDns.stop();wifiConnecting=false;server.stop();WiFi.disconnect(true,false);WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);stationActive=false;apActive=false;ntpConfigured=false;Serial.println("WLAN AUS (Zeitplan)");}
bool setupAccessPoint(){
 ntpUdp.stop();ntpServerRunning=false;ntpConfigured=false;
 captiveDns.stop();
 // Vorherige STA-Verbindung fuer einen sauberen AP-Neustart beenden.
 WiFi.disconnect(false,false);
 WiFi.mode(WIFI_AP_STA);
 WiFi.setHostname(WIFI_HOSTNAME);
 WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
 if(!WiFi.softAP(apName.c_str())){
   apFailures++;wifiLastError="Einrichtungs-Hotspot konnte nicht gestartet werden";
   apActive=false;stationActive=false;wifiConnecting=false;
   nextApRetry=millis()+AP_RETRY_MS;
   logDiag("AP-Start fehlgeschlagen; Wiederholung in 15 Sekunden");
   return false;
 }
 captiveDns.start(CAPTIVE_DNS_PORT,"*",WiFi.softAPIP());
 apActive=true;stationActive=false;wifiConnecting=false;nextApRetry=0;
 nextStationRetry=ssid.length()?millis()+60000UL:0;
 Serial.printf("Einrichtungs-WLAN: %s, IP %s\n",apName.c_str(),WiFi.softAPIP().toString().c_str());
 return true;
}
void beginRadio(){
 radioStoppedForSchedule=false;
 // Wichtig: beginRadio startet IMMER WLAN, der Zeitplan wird nur in wifiTick
 // nach erfolgreichem Boot/WLAN und einer Schutzfrist angewendet.
 WiFi.persistent(false);WiFi.setSleep(true);
 if(ssid.length()){
   WiFi.mode(WIFI_STA);WiFi.setHostname(WIFI_HOSTNAME);
   if(!applyIpConfig()){
     Serial.println("Statische IP ungueltig: DHCP-Fallback");
     wifiLastError="Statische IP ungueltig, DHCP verwendet";
     WiFi.config(INADDR_NONE,INADDR_NONE,INADDR_NONE);
   }
   WiFi.setAutoReconnect(true);
   WiFi.begin(ssid.c_str(),wifiPass.c_str());
   wifiStartedAt=millis();stationActive=true;apActive=false;ntpConfigured=false;
   Serial.printf("Verbinde mit WLAN: %s\n",ssid.c_str());
 }else setupAccessPoint();
 server.begin();
}
bool authorized(){
 if(adminPassword.isEmpty())return true;
 if(server.authenticate(WEB_ADMIN_USER,adminPassword.c_str()))return true;
 server.requestAuthentication(BASIC_AUTH,"CO2-Waage Einstellungen");return false;
}
bool safeMutation(){
  // Zufallstoken gegen Cross-Site-Requests und DNS-Rebinding-ähnliche Form-POSTs.
  if(!server.hasHeader("X-CSRF-Token") || server.header("X-CSRF-Token")!=csrfToken){
    server.send(403,"text/plain; charset=utf-8","Sicherheitstoken fehlt oder ist ungueltig. Seite neu laden.");return false;
  }
  if(server.hasHeader("Origin")){
    String origin=server.header("Origin");
    String host=server.hostHeader();
    if(origin!=String("http://")+host && origin!=String("https://")+host){
      server.send(403,"text/plain; charset=utf-8","Ungueltiger Seitenursprung");return false;
    }
  }
  return true;
}
void reply(int code,const String& msg){server.sendHeader("Cache-Control","no-store");server.send(code,"text/plain; charset=utf-8",msg);}
void jsonReply(const String& msg){server.sendHeader("Cache-Control","no-store");server.send(200,"application/json; charset=utf-8",msg);}
void handleStatus(){
 float gross=avgG(), gas=gross-emptyG-regulatorG;
 bool calibrated=hasTare && (factor!=0 || calCount>0);
 uint32_t sampleAge=lastSampleAt ? (uint32_t)(millis()-lastSampleAt) : 0;
 bool valid=calibrated && sampleCount>=1 && lastSampleAt && sampleAge<=SAMPLE_PERIOD_MS*3 && isfinite(gross);
 float level=valid?constrain(gas,0.0f,capacityG):NAN;
 float pct=valid?(level/capacityG*100.0f):NAN;
 String warn="";
 if(!hxSleeping && !sampling && !hx.is_ready())warn="HX711 nicht bereit: Stromversorgung und Leitungen DOUT/SCK pruefen";
 else if(!hasTare)warn="Nullpunkt fehlt: Plattform leer tarieren";
 else if(factor==0 && calCount==0)warn="Kalibrierung fehlt: Referenzgewicht auflegen";
 else if(sampleCount<1)warn="Warte auf erste Messung";
 else if(lastSampleAt && sampleAge>SAMPLE_PERIOD_MS*3)warn="Messwert veraltet: Sensor pruefen";
 else if(gas<0)warn="Gewicht unter Leergewicht: Eingaben, Nullpunkt und Schlauch pruefen";
 else if(gas>capacityG+150)warn="Mehr als Nennfuellmenge: Eingaben oder Kalibrierung pruefen";
 time_t now=time(nullptr);char dateBuf[42]="";if(validClock()){struct tm t={};localtime_r(&now,&t);strftime(dateBuf,sizeof(dateBuf),"%Y-%m-%d %H:%M:%S",&t);}
 String mode=apActive?"access_point":stationActive?"station":"off";
 String ip=apActive?WiFi.softAPIP().toString():stationActive?WiFi.localIP().toString():"";
 String j="{";
 j+="\"schema\":1,\"name\":"+jsonStr(deviceName)+",\"version\":"+jsonStr(FIRMWARE_VERSION)+",";
 j+="\"measurement\":{\"valid\":"+String(valid?"true":"false")+",\"sensor_ready\":"+String((!hxSleeping && hx.is_ready())?"true":"false")+",\"calibrated\":"+String(calibrated?"true":"false")+",";
 j+="\"calibration_points\":"+String(calCount)+",\"gross_g\":"+(valid?numOrNull(gross):"null")+",\"gas_g\":"+(valid?numOrNull(level):"null")+",\"gas_unclamped_g\":"+(valid?numOrNull(gas):"null")+",\"percent\":"+numOrNull(pct)+",\"empty_g\":"+numOrNull(emptyG)+",\"regulator_g\":"+numOrNull(regulatorG)+",\"capacity_g\":"+numOrNull(capacityG)+",\"empty_total_g\":"+numOrNull(emptyG+regulatorG)+",\"full_bottle_g\":"+numOrNull(emptyG+capacityG)+",\"full_total_g\":"+numOrNull(emptyG+regulatorG+capacityG)+",\"warning\":"+jsonStr(warn)+"},";
 j+="\"power\":{\"cpu_mhz\":"+String(getCpuFrequencyMhz())+",\"wifi_modem_sleep\":true,\"hx711_power_down\":"+String(hxSleeping?"true":"false")+",\"sample_period_seconds\":60,\"sample_age_seconds\":"+(lastSampleAt?String(sampleAge/1000):String("null"))+",\"sampling\":"+String(sampling?"true":"false")+"},";
 // Zeitplan auch vor der Abschaltzeit ausgeben, damit Clients offline nicht pollen.
 // Off-Zeitfenster ist lokal Europe/Berlin; Start inklusiv, Ende exklusiv.
 String offText=String(offHour<10?"0":"")+String(offHour)+":00";
 String onText=String(onHour<10?"0":"")+String(onHour)+":00";
 j+="\"wifi_schedule\":{\"enabled\":"+String(scheduleEnabled?"true":"false")+",\"timezone\":\"Europe/Berlin\",\"off_start_local\":"+jsonStr(offText)+",\"off_end_local\":"+jsonStr(onText)+",\"off_start_hour\":"+String(offHour)+",\"off_end_hour\":"+String(onHour)+",\"crosses_midnight\":"+String(offHour>onHour?"true":"false")+",\"currently_off\":"+String(nightBlock()?"true":"false")+",\"effective_off\":"+String(radioStoppedForSchedule?"true":"false")+",\"clock_valid\":"+String(validClock()?"true":"false")+",\"start_inclusive\":true,\"end_exclusive\":true},";
 j+="\"wifi\":{\"mode\":"+jsonStr(mode)+",\"ip\":"+jsonStr(ip)+",\"rssi\":"+String(stationActive&&WiFi.status()==WL_CONNECTED?WiFi.RSSI():0)+",\"scheduled_off\":"+String(nightBlock()?"true":"false")+"},";
 j+="\"firmware_build\":"+jsonStr(FIRMWARE_BUILD)+",";
 j+="\"recovery\":{\"ota_pending\":"+String(otaRecoveryPending?"true":"false")+",\"scheduled_radio_off\":"+String(radioStoppedForSchedule?"true":"false")+",\"boot_grace_active\":"+String((uint32_t)(millis()-bootStartedAt)<BOOT_WIFI_GRACE_MS?"true":"false")+",\"wifi_ever_connected\":"+String(wifiEverConnected?"true":"false")+",\"wifi_failures\":"+String(wifiFailures)+",\"ap_failures\":"+String(apFailures)+",\"last_error\":"+jsonStr(wifiLastError)+",\"safe_mode\":"+String(safeMode?"true":"false")+",\"boot_attempts\":"+String(rtcUnconfirmedBoots)+"},";
 j+="\"time\":{\"valid\":"+String(validClock()?"true":"false")+",\"epoch\":"+String((unsigned long)(validClock()?now:0))+",\"local\":"+jsonStr(dateBuf)+",\"ntp_host\":"+jsonStr(ntpHost)+",\"ntp_server\":"+String(ntpServerRunning?"true":"false")+"},\"uptime_ms\":"+String(millis())+"}";
 jsonReply(j);
}
void handleLive(){
 // Keine Zusatzmessung: auch die Live-API liefert nur den letzten Minutenwert.
 const float gross=avgG();
 String j="{\"ready\":"+String(cachedRawValid?"true":"false")+",\"raw\":"+(cachedRawValid?String(cachedRaw):String("null"))+",\"delta\":"+(cachedRawValid&&hasTare?String(cachedRaw-rawOffset):String("null"))+",\"gross_g\":"+numOrNull(gross)+",\"cached\":true,\"sample_age_seconds\":"+(lastSampleAt?String((millis()-lastSampleAt)/1000):String("null"))+"}";
 jsonReply(j);
}
void handleDiagnostics(){
 if(!authorized())return;
 String j="{\"version\":"+jsonStr(FIRMWARE_VERSION)+",\"build\":"+jsonStr(FIRMWARE_BUILD);
 j+=",\"reset_reason\":"+jsonStr(resetReasonText(lastResetReason));
 j+=",\"uptime_s\":"+String(millis()/1000)+",\"free_heap\":"+String(ESP.getFreeHeap());
 j+=",\"min_free_heap\":"+String(ESP.getMinFreeHeap());
 j+=",\"ota_slots_valid\":"+String(otaLayoutOk()?"true":"false");
 j+=",\"ota_target_bytes\":"+String(otaSlotSize());
 j+=",\"boot_attempts\":"+String(rtcUnconfirmedBoots);
 j+=",\"ota_pending\":"+String(otaRecoveryPending?"true":"false");
 j+=",\"boot_confirmed\":"+String(firmwareConfirmed?"true":"false");
 j+=",\"safe_mode\":"+String(safeMode?"true":"false");
 j+=",\"rollback_info\":"+jsonStr(rollbackInfo);
 j+=",\"wifi_failures\":"+String(wifiFailures)+",\"ap_failures\":"+String(apFailures);
 j+=",\"last_wifi_error\":"+jsonStr(wifiLastError);
 j+=",\"ota_error\":"+jsonStr(otaMessage)+",\"events\":[";
 for(uint8_t i=0;i<diagCount;i++){
   if(i)j+=",";
   j+=jsonStr(diagLog[(diagNext+12-diagCount+i)%12]);
 }
 j+="]}";jsonReply(j);
}
void handleTimeGet(){
 time_t now=time(nullptr);struct tm local={};char stamp[40]="";
 if(validClock()){localtime_r(&now,&local);strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",&local);}
 jsonReply("{\"valid\":"+String(validClock()?"true":"false")+",\"epoch\":"+String((unsigned long)(validClock()?now:0))+",\"local\":"+jsonStr(stamp)+",\"timezone\":\"Europe/Berlin\",\"ntp_host\":"+jsonStr(ntpHost)+",\"ntp_server\":"+String(ntpServerRunning?"true":"false")+"}");
}
void handleNtpHost(){if(!safeMutation())return;
 if(!authorized())return;
 String host=server.arg("host");host.trim();
 if(host.isEmpty()||host.length()>100||host.indexOf(' ')>=0||host.indexOf('/')>=0||host.indexOf('\\')>=0){reply(400,"Ungueltiger NTP-Servername");return;}
 ntpHost=host;preferences.putString("ntphost",ntpHost);
 ntpConfigured=false;reply(200,"Zeitserver gespeichert. Synchronisierung wird neu gestartet.");
}
void writeNtpTimestamp(uint8_t* out,const timeval& tv){
 uint32_t sec=(uint32_t)tv.tv_sec+NTP_UNIX_OFFSET;
 uint32_t frac=(uint32_t)((uint64_t)tv.tv_usec*4294967296ULL/1000000ULL);
 for(int i=0;i<4;i++){out[i]=(uint8_t)(sec>>(24-i*8));out[4+i]=(uint8_t)(frac>>(24-i*8));}
}
void ntpServerTick(){
 if(!ntpServerRunning)return;
 int len=ntpUdp.parsePacket();if(len<=0)return;
 uint8_t req[48]={};int count=ntpUdp.read(req,sizeof(req));
 while(ntpUdp.available())ntpUdp.read();
 if(count<48 || (req[0]&7)!=3 || !validClock())return;
 uint8_t resp[48]={};
 resp[0]=(req[0]&0x38)|0x04;resp[1]=2;resp[2]=6;resp[3]=0xEC;
 resp[12]='C';resp[13]='O';resp[14]='2';resp[15]='W';
 memcpy(resp+24,req+40,8); // Originate = client transmit
 struct timeval tv={};gettimeofday(&tv,nullptr);
 writeNtpTimestamp(resp+16,tv);writeNtpTimestamp(resp+32,tv);
 gettimeofday(&tv,nullptr);writeNtpTimestamp(resp+40,tv);
 ntpUdp.beginPacket(ntpUdp.remoteIP(),ntpUdp.remotePort());ntpUdp.write(resp,sizeof(resp));ntpUdp.endPacket();
}
void handleOtaUpload(){
 HTTPUpload& up=server.upload();
 if(up.status==UPLOAD_FILE_START){
   if(!safeMutation()){otaError=true;otaMessage="Sicherheitstoken ungueltig";return;}
   otaSucceeded=false;otaError=false;otaMessage="";
   if(!authorized()){otaError=true;otaMessage="Anmeldung erforderlich";return;}
   if(!up.filename.endsWith(".bin")){otaError=true;otaMessage="Nur Firmware-Dateien (.bin)";return;}
   if(!otaLayoutOk()){otaError=true;otaMessage="Kein sicheres Dual-OTA-Partitionslayout vorhanden";return;}
   if(otaSlotSize()<0x10000){otaError=true;otaMessage="OTA-Zielpartition ungueltig";return;}
   logDiag("OTA gestartet: Zielpartition "+String(otaSlotSize())+" Bytes");
   if(!Update.begin(otaSlotSize(),U_FLASH)){otaError=true;otaMessage=String("OTA-Start fehlgeschlagen: ")+Update.errorString();return;}
   otaRunning=true;activeCpu();WiFi.setSleep(false);sleepHx();
 }else if(up.status==UPLOAD_FILE_WRITE){
   if(!otaError && otaRunning && (up.totalSize>otaSlotSize() || Update.write(up.buf,up.currentSize)!=up.currentSize)){otaError=true;otaMessage="Firmware zu gross oder Schreibfehler";Update.abort();otaRunning=false;WiFi.setSleep(true);idleCpu();}
 }else if(up.status==UPLOAD_FILE_END){
   if(!otaError && otaRunning && Update.end(true)){
     otaSucceeded=true;otaMessage="Update geschrieben. Neustart und Bootpruefung folgen.";logDiag("OTA geschrieben; Teststart folgt");
     // Nach dem OTA Boot nicht blind die WLAN-Nachtabschaltung anwenden.
     preferences.putBool("otarescue",true);
   }
   else if(!otaError){otaError=true;otaMessage=String("OTA ungueltig: ")+Update.errorString();Update.abort();logDiag(otaMessage);}
   otaRunning=false;WiFi.setSleep(true);idleCpu();
 }else if(up.status==UPLOAD_FILE_ABORTED){
   Update.abort();otaRunning=false;otaError=true;otaMessage="Upload abgebrochen";logDiag(otaMessage);WiFi.setSleep(true);idleCpu();
 }
}
void handleOtaFinish(){if(!safeMutation())return;
 if(!authorized())return;
 if(otaSucceeded){reply(200,otaMessage);otaRestartAt=millis()+2500;}
 else reply(400,otaMessage.isEmpty()?"Kein gueltiges Update empfangen":otaMessage);
}
void handleConfigGet(){if(!authorized())return;
 String j="{\"name\":"+jsonStr(deviceName)+",\"empty_g\":"+numOrNull(emptyG)+",\"regulator_g\":"+numOrNull(regulatorG)+",\"capacity_g\":"+numOrNull(capacityG)+",\"ssid\":"+jsonStr(ssid)+",\"static_ip_enabled\":"+String(staticIpEnabled?"true":"false")+",\"static_ip\":"+jsonStr(staticIp)+",\"gateway\":"+jsonStr(staticGateway)+",\"subnet\":"+jsonStr(staticSubnet)+",\"dns\":"+jsonStr(staticDns)+",\"schedule_enabled\":"+String(scheduleEnabled?"true":"false")+",\"off_hour\":"+String(offHour)+",\"on_hour\":"+String(onHour)+",\"web_password_enabled\":"+String(adminPassword.isEmpty()?"false":"true")+",\"ntp_host\":"+jsonStr(ntpHost)+"}";
 jsonReply(j);
}
bool parseFloatField(const char* key,float& dest,float minVal,float maxVal){if(!server.hasArg(key))return false;String s=server.arg(key);if(!s.length())return false;char* end=nullptr;float n=strtof(s.c_str(),&end);if(end==s.c_str()||*end!='\0'||!isfinite(n)||n<minVal||n>maxVal)return false;dest=n;return true;}
bool parseHour(const char* key,int& val){float n;if(!parseFloatField(key,n,0,23)||floorf(n)!=n)return false;val=(int)n;return true;}
void handleConfigPost(){if(!safeMutation())return;if(!authorized())return;
 float a,b,c;int off,on;
 if(!parseFloatField("empty_g",a,0,100000)||!parseFloatField("regulator_g",b,0,100000)||!parseFloatField("capacity_g",c,1,20000)||!parseHour("off_hour",off)||!parseHour("on_hour",on)){reply(400,"Ungueltige Gewichts- oder Stundenangabe");return;}
 if(a+b>100000){reply(400,"Leergewicht zu gross");return;}
 String n=server.arg("name");n.trim();if(n.length()==0||n.length()>40){reply(400,"Name muss 1 bis 40 Zeichen enthalten");return;}
 bool newStaticEnabled=server.arg("static_ip_enabled")=="1";
 String newIp=server.arg("static_ip"),newGateway=server.arg("gateway"),newSubnet=server.arg("subnet"),newDns=server.arg("dns");
 if(newStaticEnabled){IPAddress ip,gw,mask,dns;if(!parseIp(newIp,ip)||!parseIp(newGateway,gw)||!parseIp(newSubnet,mask)||!parseIp(newDns,dns)){reply(400,"Ungueltige feste IP-Konfiguration");return;}}
 emptyG=a;regulatorG=b;capacityG=c;offHour=off;onHour=on;deviceName=n;
 scheduleEnabled=server.arg("schedule_enabled")=="1";
 staticIpEnabled=newStaticEnabled;staticIp=newIp;staticGateway=newGateway;staticSubnet=newSubnet;staticDns=newDns;
 preferences.putFloat("empty",emptyG);preferences.putFloat("reg",regulatorG);preferences.putFloat("cap",capacityG);
 preferences.putInt("off",offHour);preferences.putInt("on",onHour);preferences.putBool("sched",scheduleEnabled);preferences.putString("name",deviceName);
 preferences.putBool("staticip",staticIpEnabled);preferences.putString("ip",staticIp);preferences.putString("gateway",staticGateway);preferences.putString("subnet",staticSubnet);preferences.putString("dns",staticDns);
 reply(200,"Einstellungen gespeichert. Der WLAN-Zeitplan wird angewendet.");deferredSwitch=millis()+1800;
}
void handleWebPassword(){if(!safeMutation())return;
 if(!authorized())return;
 if(!server.hasArg("password")){reply(400,"Passwortparameter fehlt");return;}
 String candidate=server.arg("password");
 if(candidate.length()>64 || (!candidate.isEmpty() && candidate.length()<12)){reply(400,"Passwort muss 12 bis 64 Zeichen haben (oder leer zum Entfernen)");return;}
 // Leer = Anmeldung abschalten. Zugang zum lokalen Netz vorausgesetzt.
 adminPassword=candidate;
 preferences.putString("adminpass",adminPassword);
 reply(200,adminPassword.isEmpty()?"Webserver jetzt ohne Passwort":"Webserver-Passwort gespeichert. Benutzer: admin");
}
// Backup enthaelt keine Zugangsdaten, keine feste IP und keinen CSRF-Token.
// So kann ein Backup keine neue WLAN-Aussperrung verursachen.
void handleBackupExport(){
 if(!authorized())return;
 DynamicJsonDocument doc(3072);
 doc["schema"]=1;doc["kind"]="co2-waage-settings";doc["firmware"]=FIRMWARE_VERSION;
 JsonObject cfg=doc.createNestedObject("settings");
 cfg["name"]=deviceName;cfg["empty_g"]=emptyG;cfg["regulator_g"]=regulatorG;
 cfg["capacity_g"]=capacityG;cfg["schedule_enabled"]=scheduleEnabled;
 cfg["off_hour"]=offHour;cfg["on_hour"]=onHour;cfg["ntp_host"]=ntpHost;
 JsonObject cal=doc.createNestedObject("calibration");
 cal["has_tare"]=hasTare;cal["raw_offset"]=rawOffset;cal["factor"]=factor;
 JsonArray points=cal.createNestedArray("points");
 for(uint8_t i=0;i<calCount;i++){
   JsonObject point=points.createNestedObject();
   point["grams"]=calPoints[i].grams;point["raw"]=calPoints[i].raw;
 }
 String json;serializeJsonPretty(doc,json);
 server.sendHeader("Cache-Control","no-store");
 server.sendHeader("Content-Disposition","attachment; filename=co2-waage-backup.json");
 server.send(200,"application/json; charset=utf-8",json);
}
void handleBackupImport(){
 if(!safeMutation())return;
 if(!authorized())return;
 if(sampling||otaRunning){reply(409,"Messung oder OTA laeuft");return;}
 if(!server.hasArg("plain")){reply(400,"JSON-Datei fehlt");return;}
 const String payload=server.arg("plain");
 if(payload.length()>BACKUP_MAX_BYTES){reply(413,"Backup zu gross (maximal 4096 Bytes)");return;}
 DynamicJsonDocument doc(4096);
 DeserializationError err=deserializeJson(doc,payload);
 if(err){reply(400,"JSON nicht lesbar");return;}
 if(doc["schema"].as<int>()!=1 || doc["kind"].as<String>()!="co2-waage-settings"){
   reply(400,"Unbekanntes Backup-Format");return;
 }
 JsonObject cfg=doc["settings"].as<JsonObject>();
 JsonObject cal=doc["calibration"].as<JsonObject>();
 if(cfg.isNull()||cal.isNull()){reply(400,"Einstellungen oder Kalibrierung fehlen");return;}
 String n=cfg["name"].as<String>();n.trim();
 String ntp=cfg["ntp_host"].as<String>();ntp.trim();
 if(n.isEmpty()||n.length()>40||ntp.isEmpty()||ntp.length()>100||
    ntp.indexOf(' ')>=0||ntp.indexOf('/')>=0||ntp.indexOf('\\')>=0){
   reply(400,"Ungueltiger Name oder Zeitserver");return;
 }
 float empty=cfg["empty_g"].as<float>();
 float reg=cfg["regulator_g"].as<float>();
 float cap=cfg["capacity_g"].as<float>();
 int off=cfg["off_hour"].as<int>(),on=cfg["on_hour"].as<int>();
 bool schedule=cfg["schedule_enabled"].as<bool>();
 if(cfg["empty_g"].isNull()||cfg["regulator_g"].isNull()||cfg["capacity_g"].isNull()||
    cfg["off_hour"].isNull()||cfg["on_hour"].isNull()||cfg["schedule_enabled"].isNull()||
    !isfinite(empty)||!isfinite(reg)||!isfinite(cap)||empty<0||reg<0||
    empty+reg>100000||cap<1||cap>20000||off<0||off>23||on<0||on>23){
   reply(400,"Gewichts- oder Zeitplanwerte ungueltig");return;
 }
 if(cal["has_tare"].isNull()||cal["raw_offset"].isNull()||cal["factor"].isNull()){
   reply(400,"Kalibrierungsgrunddaten fehlen");return;
 }
 bool tare=cal["has_tare"].as<bool>();
 double raw0d=cal["raw_offset"].as<double>();
 float f=cal["factor"].as<float>();
 if(!isfinite(raw0d)||raw0d<-8388608||raw0d>8388607||floor(raw0d)!=raw0d||
    !isfinite(f)||fabsf(f)>1000000){reply(400,"Kalibrierungsdaten ungueltig");return;}
 long offset=(long)raw0d;
 JsonArray arr=cal["points"].as<JsonArray>();
 if(arr.isNull()||arr.size()>MAX_CAL_POINTS||(!tare && (arr.size()||f!=0))){
   reply(400,"Kalibrierpunkte ungueltig");return;
 }
 CalPoint newPoints[MAX_CAL_POINTS]={};uint8_t count=0;
 double sign=0;
 for(JsonVariant item:arr){
   if(!item.is<JsonObject>()){reply(400,"Kalibrierpunkt ungueltig");return;}
   if(item["grams"].isNull()||item["raw"].isNull()){
     reply(400,"Kalibrierpunkt unvollstaendig");return;
   }
   double g=item["grams"].as<double>();double r=item["raw"].as<double>();
   if(!isfinite(g)||!isfinite(r)||g<50||g>10000||r<-8388608||r>8388607||floor(r)!=r){
     reply(400,"Kalibrierpunkt ausserhalb des Messbereichs");return;
   }
   if(count==0){sign=r-offset;if(fabs(sign)<100){reply(400,"Erster Kalibrierpunkt zu nah am Nullpunkt");return;}}
   else if(g<=newPoints[count-1].grams ||
           (r-newPoints[count-1].raw)*sign<=0 || fabs(r-newPoints[count-1].raw)<100){
     reply(400,"Kalibrierpunkte nicht streng monoton");return;
   }
   newPoints[count++]={(float)g,(long)r};
 }
 if(count)f=(float)(((double)newPoints[0].raw-offset)/newPoints[0].grams);
 if(!tare)f=0;
 // Erst NACH kompletter Pruefung werden die Einstellungen geaendert.
 deviceName=n;emptyG=empty;regulatorG=reg;capacityG=cap;
 scheduleEnabled=schedule;offHour=off;onHour=on;ntpHost=ntp;
 hasTare=tare;rawOffset=offset;factor=f;calCount=count;
 for(uint8_t i=0;i<count;i++)calPoints[i]=newPoints[i];
 preferences.putString("name",deviceName);
 preferences.putFloat("empty",emptyG);preferences.putFloat("reg",regulatorG);
 preferences.putFloat("cap",capacityG);preferences.putBool("sched",scheduleEnabled);
 preferences.putInt("off",offHour);preferences.putInt("on",onHour);
 preferences.putString("ntphost",ntpHost);
 saveScale();ntpConfigured=false;
 logDiag("Einstellungen und "+String(calCount)+" Kalibrierpunkte importiert");
 reply(200,"Backup eingelesen. WLAN und Passwoerter wurden nicht veraendert.");
}
void handleTare(){if(!safeMutation())return;
 if(!authorized())return;
 if(sampling||otaRunning){reply(409,"Messung oder Update laeuft, bitte warten");return;}
 wakeHx();activeCpu();
 if(!hx.wait_ready_timeout(2000)){sleepHx();idleCpu();reply(503,"HX711 nicht bereit: Verdrahtung pruefen");return;}
 long raw=hx.read_average(15);sleepHx();idleCpu();
 rawOffset=raw;hasTare=true;factor=0;calCount=0;saveScale();
 reply(200,"Nullpunkt gesetzt. Bisherige Kalibrierpunkte geloescht. Jetzt nacheinander Pruefgewichte erfassen.");
}
// Eine Aufnahme ist eine explizite Benutzeraktion und zaehlt nicht als periodische
// 60-Sekunden-Gewichtsmessung. Sie nutzt 15 gemittelte Rohwerte.
void handleCalibrate(){if(!safeMutation())return;
 if(!authorized())return;
 if(sampling||otaRunning){reply(409,"Messung oder Update laeuft, bitte warten");return;}
 if(!hasTare){reply(409,"Zuerst Nullpunkt mit leerer Plattform setzen");return;}
 float grams;
 if(!parseFloatField("grams",grams,50,10000)){reply(400,"Pruefgewicht ungueltig (50 bis 10000 g)");return;}
 wakeHx();activeCpu();
 if(!hx.wait_ready_timeout(2500)){sleepHx();idleCpu();reply(503,"HX711 nicht bereit");return;}
 long raw=hx.read_average(15);sleepHx();idleCpu();
 const double diff=(double)raw-(double)rawOffset;
 if(fabs(diff)<100){reply(409,"Messunterschied zu klein. Gewicht korrekt aufgelegt?");return;}
 // Vorhandenen Punkt mit gleichem Gewicht ersetzen, sonst neu einfuegen.
 CalPoint candidate[MAX_CAL_POINTS]={};uint8_t count=0;bool replaced=false;
 for(uint8_t i=0;i<calCount;i++){
   if(fabs(calPoints[i].grams-grams)<0.5f){candidate[count++]={grams,raw};replaced=true;}
   else candidate[count++]=calPoints[i];
 }
 if(!replaced){if(count>=MAX_CAL_POINTS){reply(409,"Maximal 8 Pruefgewichte. Erst einen Punkt entfernen.");return;}candidate[count++]={grams,raw};}
 for(uint8_t i=1;i<count;i++){
   CalPoint key=candidate[i];int j=i-1;
   while(j>=0 && candidate[j].grams>key.grams){candidate[j+1]=candidate[j];j--;}
   candidate[j+1]=key;
 }
 // Nur streng monotone Rohwerte, gleiche Richtung und ausreichender Abstand.
 const double sign=(double)candidate[0].raw-rawOffset;
 if(fabs(sign)<100){reply(409,"Zu kleiner Rohwert am ersten Punkt");return;}
 for(uint8_t i=1;i<count;i++){
   double step=(double)candidate[i].raw-candidate[i-1].raw;
   if(step*sign<=0 || fabs(step)<100){reply(409,"Messwerte nicht monoton: Gewichte einzeln und mittig auflegen. Kein Punkt gespeichert.");return;}
 }
 for(uint8_t i=0;i<count;i++)calPoints[i]=candidate[i];
 calCount=count;
 factor=(float)(((double)calPoints[0].raw-rawOffset)/calPoints[0].grams);
 saveScale();
 reply(200,String(replaced?"Pruefgewicht aktualisiert. ":"Pruefgewicht gespeichert. ")+String(calCount)+" von 8 Punkten vorhanden.");
}
void handleCalibrationGet(){
 if(!authorized())return;
 String j="{\"zero_set\":"+String(hasTare?"true":"false")+",\"points\":[";
 for(uint8_t i=0;i<calCount;i++){
   if(i)j+=",";
   j+="{\"grams\":"+numOrNull(calPoints[i].grams,1)+",\"raw\":"+String(calPoints[i].raw)+"}";
 }
 j+="]}";jsonReply(j);
}
void handleCalibrationDelete(){if(!safeMutation())return;
 if(!authorized())return;
 int idx=server.arg("index").toInt();
 if(!server.hasArg("index")||server.arg("index")!=String(idx)||idx<0||idx>=calCount){reply(400,"Ungueltiger Kalibrierpunkt");return;}
 for(uint8_t i=idx;i+1<calCount;i++)calPoints[i]=calPoints[i+1];
 calCount--;
 if(calCount)factor=(float)(((double)calPoints[0].raw-rawOffset)/calPoints[0].grams);
 else factor=0;
 saveScale();reply(200,"Kalibrierpunkt entfernt.");
}
bool setupClient(){return apActive && server.client().localIP()==WiFi.softAPIP();}
void handleScan(){
 if(!authorized())return;
 if(!apActive){reply(403,"WLAN-Suche nur im Einrichtungsmodus");return;}
 int count=WiFi.scanNetworks(false,true,false,160);
 if(count<0){reply(503,"Netzwerksuche fehlgeschlagen");return;}
 String out="{\"networks\":[";
 for(int i=0;i<count && i<40;i++){
   if(i)out+=",";
   out+="{\"ssid\":"+jsonStr(WiFi.SSID(i))+",\"rssi\":"+String(WiFi.RSSI(i));
   out+=",\"secure\":"+String(WiFi.encryptionType(i)==WIFI_AUTH_OPEN?"false":"true")+"}";
 }
 out+="]}";WiFi.scanDelete();jsonReply(out);
}
void handlePortal(){
 if(!authorized())return;
 if(!apActive){server.sendHeader("Location","/");server.send(302,"text/plain","");return;}
 server.send_P(200,"text/html; charset=utf-8",SETUP_HTML);
}
void handleWiFi(){if(!safeMutation())return;if(!authorized())return;
 String n=server.arg("ssid");n.trim();if(n.length()==0||n.length()>32){reply(400,"SSID muss 1 bis 32 Zeichen haben");return;}
 String p=server.arg("password");
 if(p.length()>0&&(p.length()<8||p.length()>63)){reply(400,"WLAN-Passwort muss 8 bis 63 Zeichen haben");return;}
 if(n!=ssid && p.length()==0){reply(400,"Bei neuer SSID auch WLAN-Passwort eingeben");return;}
 ssid=n;if(p.length())wifiPass=p;
 preferences.putString("ssid",ssid);preferences.putString("pass",wifiPass);
 reply(200,"WLAN gespeichert. Verbindung wird hergestellt. Bei Erfolg lautet die neue IP-Adresse die vom Router zugewiesene Adresse.");
 if(apActive){WiFi.begin(ssid.c_str(),wifiPass.c_str());wifiStartedAt=millis();wifiConnecting=true;}
 else deferredSwitch=millis()+1800;
}
void handleTime(){if(!safeMutation())return;if(!authorized())return;
 String epoch=server.arg("epoch");char* end=nullptr;unsigned long n=strtoul(epoch.c_str(),&end,10);
 if(epoch.isEmpty()||end==epoch.c_str()||*end!=0||n<1700000000UL||n>4102444800UL){reply(400,"Ungueltige Unix-Zeit");return;}
 struct timeval t={};t.tv_sec=n;settimeofday(&t,nullptr);
 reply(200,"Uhrzeit uebernommen (Europe/Berlin).");deferredSwitch=millis()+1800;
}
void setupRoutes(){
 server.on("/",HTTP_GET,[]{if(!authorized())return;if(apActive)handlePortal();else server.send_P(200,"text/html; charset=utf-8",INDEX_HTML);});
 server.on("/scale",HTTP_GET,[]{if(!authorized())return;server.send_P(200,"text/html; charset=utf-8",INDEX_HTML);});
 server.on("/setup",HTTP_GET,handlePortal);
 server.on("/api/wifi/scan",HTTP_GET,handleScan);
 server.on("/generate_204",HTTP_GET,handlePortal);
 server.on("/hotspot-detect.html",HTTP_GET,handlePortal);
 server.on("/connecttest.txt",HTTP_GET,handlePortal);
 server.on("/fwlink",HTTP_GET,handlePortal);
 server.on("/api/security",HTTP_GET,[]{if(!authorized())return;jsonReply("{\"csrf\":"+jsonStr(csrfToken)+"}");});
 server.on("/api/v1/status",HTTP_GET,handleStatus);
 server.on("/api/v1/live",HTTP_GET,handleLive);
 server.on("/api/v1/time",HTTP_GET,handleTimeGet);
 server.on("/api/diagnostics",HTTP_GET,handleDiagnostics);
 server.on("/api/backup",HTTP_GET,handleBackupExport);
 server.on("/api/backup",HTTP_POST,handleBackupImport);
 server.on("/api/ntp-host",HTTP_POST,handleNtpHost);
 server.on("/api/ota",HTTP_POST,handleOtaFinish,handleOtaUpload);
 server.on("/api/status",HTTP_GET,handleStatus);
 server.on("/api/config",HTTP_GET,handleConfigGet);
 server.on("/api/config",HTTP_POST,handleConfigPost);
 server.on("/api/web-password",HTTP_POST,handleWebPassword);
 server.on("/api/tare",HTTP_POST,handleTare);
 server.on("/api/calibrate",HTTP_POST,handleCalibrate);
 server.on("/api/calibration",HTTP_GET,handleCalibrationGet);
 server.on("/api/calibration/delete",HTTP_POST,handleCalibrationDelete);
 server.on("/license",HTTP_GET,[]{if(!authorized())return;server.send_P(200,"text/plain; charset=utf-8",LICENSE_TEXT);});
 server.on("/third-party",HTTP_GET,[]{if(!authorized())return;server.send_P(200,"text/plain; charset=utf-8",THIRD_PARTY_TEXT);});
 server.on("/api/wifi",HTTP_POST,handleWiFi);
 server.on("/api/time",HTTP_POST,handleTime);
 server.onNotFound([]{if(apActive)handlePortal();else reply(404,"Nicht gefunden");});
}
void sampleScale(){
 const uint32_t now=millis();
 if(!sampling){
   if(sampleAttempted && (uint32_t)(now-nextSample)<SAMPLE_PERIOD_MS)return;
   nextSample=now;sampleAttempted=true;
   if(!hasTare || (factor==0 && calCount==0)){sleepHx();return;}
   wakeHx();activeCpu();sampling=true;rawReadings=0;rawTotal=0;sampleStartedAt=now;
   return; // HX711 braucht nach Power-Up Zeit fuer den ersten Messwert
 }
 if((uint32_t)(now-sampleStartedAt)>SAMPLE_TIMEOUT_MS){
   sampling=false;sleepHx();idleCpu();return;
 }
 if(!hx.is_ready())return;
 rawTotal+=hx.read();rawReadings++;
 if(rawReadings<RAW_READINGS_PER_CYCLE)return;
 cachedRaw=(long)(rawTotal/rawReadings);cachedRawValid=true;
 const float grams=gramsFromRaw((double)rawTotal/rawReadings);
 sampling=false;sleepHx();idleCpu();
 if(!isfinite(grams)||fabsf(grams)>100000)return;
 samples[samplePos]=grams;samplePos=(samplePos+1)%12;
 if(sampleCount<12)sampleCount++;
 lastSampleAt=millis();
}
void wifiTick(){
 const uint32_t now=millis();
 // Waehrend OTA-Upload und vor dem geplanten OTA-Neustart WLAN nie abschalten.
 if(otaRunning || otaRestartAt)return;
 // Ist das Funkmodul absichtlich aus, bleibt es bis zum Ende der Abschaltzeit aus.
 if(radioStoppedForSchedule){
   if(nightBlock())return;
   radioStoppedForSchedule=false;
 }
 if(deferredSwitch && (int32_t)(now-deferredSwitch)>=0){
   deferredSwitch=0;stopRadio();beginRadio();return;
 }
 // WLAN nur dann planmaessig abschalten, wenn das Geraet nach diesem Boot
 // bereits nachweislich im Heim-WLAN war. Der AP bleibt ein Rettungszugang.
 if(allowScheduledWifiOff()){
   if(stationActive && WiFi.status()==WL_CONNECTED){stopRadio();radioStoppedForSchedule=true;return;}
 }
 if(!stationActive && !apActive){
   if(nextApRetry && (int32_t)(now-nextApRetry)<0)return;
   if(ssid.isEmpty() || nextApRetry){
     setupAccessPoint();server.begin();
   }else beginRadio();
   return;
 }
 if(apActive){
   // Wenn der AP-Treiber nach einem Fehler nicht mehr aktiv ist, automatisch neu starten.
   if(WiFi.softAPIP()==IPAddress((uint32_t)0)){
     apFailures++;wifiLastError="Einrichtungs-Hotspot verloren; Neustart wird versucht";
     apActive=false;nextApRetry=now+AP_RETRY_MS;
     Serial.println("AP nicht mehr aktiv, automatischer Neustart folgt");
     return;
   }
   if(wifiConnecting && WiFi.status()==WL_CONNECTED){
     Serial.printf("WLAN verbunden. Neue IP: %s\n",WiFi.localIP().toString().c_str());
     wifiEverConnected=true;
     /* OTA wird erst nach Health-Check bestaetigt */
     wifiConnecting=false;deferredSwitch=millis()+10000;
   }else if(wifiConnecting && (uint32_t)(now-wifiStartedAt)>STA_CONNECT_TIMEOUT_MS){
     WiFi.disconnect(false,false);wifiConnecting=false;
     wifiFailures++;wifiLastError="Heim-WLAN nicht erreichbar, Rettungs-Hotspot aktiv";
     nextStationRetry=now+60000;
     logDiag("WLAN-Verbindung fehlgeschlagen. Einrichtungs-AP bleibt aktiv.");
   }
   if(!wifiConnecting && ssid.length() && nextStationRetry && (int32_t)(now-nextStationRetry)>=0){
     Serial.printf("Neuer WLAN-Verbindungsversuch: %s\n",ssid.c_str());
     WiFi.begin(ssid.c_str(),wifiPass.c_str());
     wifiStartedAt=now;wifiConnecting=true;nextStationRetry=now+60000;
   }
   return;
 }
 if(stationActive && WiFi.status()==WL_CONNECTED){
   wifiStartedAt=now;wifiEverConnected=true;
   // OTA-Recovery erst nach 120s stabiler Laufzeit abschliessen.
   if(!ntpConfigured){
     configTzTime(NTP_TZ,ntpHost.c_str(),"time.google.com");
     ntpConfigured=true;Serial.print("WLAN IP: ");Serial.println(WiFi.localIP());
   }
   if(!ntpServerRunning && ntpUdp.begin(123)){
     ntpServerRunning=true;Serial.println("Lokaler NTP-Uhrserver auf UDP 123 gestartet");
   }
 }else if(stationActive && (uint32_t)(now-wifiStartedAt)>STA_CONNECT_TIMEOUT_MS){
   wifiFailures++;wifiLastError="Heim-WLAN nicht erreichbar, starte Rettungs-Hotspot";
   logDiag("WLAN nicht erreichbar -> Einrichtungs-AP");
   server.stop();WiFi.disconnect(true,false);
   setupAccessPoint();server.begin();
 }
}
void setup(){
 Serial.begin(115200);delay(250);Serial.println("\nCO2-Waage " FIRMWARE_VERSION);
 bootStartedAt=millis();
 Serial.printf("Reset-Ursache: %d\n",(int)esp_reset_reason());
 idleCpu();
 setenv("TZ",NTP_TZ,1);tzset();loadSettings();
 evaluateBootHealth();
 if(otaRecoveryPending)Serial.println("OTA-Recovery aktiv: WLAN-Abschaltung bis erfolgreicher Verbindung gesperrt");
 hx.begin(PIN_HX711_DOUT,PIN_HX711_SCK);hx.set_gain(128);sleepHx();
 uint64_t mac=ESP.getEfuseMac();char suffix[7];snprintf(suffix,sizeof(suffix),"%06X",(unsigned int)(mac&0xFFFFFF));
 apName="ESP32-CO2-"+String(suffix);
 char csrfBuf[33];snprintf(csrfBuf,sizeof(csrfBuf),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());csrfToken=csrfBuf;
 const char* headers[]={"X-CSRF-Token","Origin"};server.collectHeaders(headers,2);
 setupRoutes();if(safeMode){setupAccessPoint();server.begin();}else beginRadio();
}
void loop(){
 if(otaRestartAt && (int32_t)(millis()-otaRestartAt)>=0){ESP.restart();}
 if(!otaRunning)sampleScale();
 confirmHealthyBoot();
 if((uint32_t)(millis()-lastWifiTick)>=WIFI_TICK_MS){lastWifiTick=millis();wifiTick();}
 if(apActive)captiveDns.processNextRequest();
 if(stationActive||apActive)server.handleClient();
 if(stationActive && WiFi.status()==WL_CONNECTED)ntpServerTick();
 // Echte Light-Sleep-Phasen nur bei ausgeschaltetem WLAN: sonst waere der
 // HTTP-/NTP-Server waehrend der Schlafphase nicht erreichbar.
 if(!stationActive && !apActive && !sampling && !otaRunning){
   esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_US);
   esp_light_sleep_start();
   esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
 }else delay(25);
}

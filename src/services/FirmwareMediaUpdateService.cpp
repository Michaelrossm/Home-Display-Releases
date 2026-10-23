#include "FirmwareMediaUpdateService.h"
#include "StorageService.h"
#include "UsbStorageService.h"
#include "NetworkWorker.h"
#include "OtaWriteSession.h"
#include "OtaService.h"
#include "NetworkService.h"
#include "C6UpdateService.h"
#include "../core/EventLog.h"
#include <esp32-hal-hosted.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_partition.h>
#include <bootloader_common.h>
#include <mbedtls/sha256.h>
#include <memory>
#include <algorithm>
#include <cstring>
namespace hp {
namespace {
constexpr char kC6Sha[]="f8509949826c197a4fa3c7958943b4e8985cffdfdf4204c2847d8953905121c9";
const char* path(bool c6){return c6?FirmwareMediaUpdateService::c6Path():FirmwareMediaUpdateService::p4Path();}
String digestHex(const uint8_t d[32]){char str[65]{};for(int i=0;i<32;i++)snprintf(str+i*2,3,"%02x",d[i]);return str;}
bool fileDigest(File& f,uint8_t digest[32]){
 if(!f||!f.seek(0))return false;
 uint8_t buffer[4096];mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
 bool ok=mbedtls_sha256_starts(&ctx,0)==0;
 while(ok&&f.available()){
  size_t n=f.read(buffer,sizeof(buffer));if(!n){ok=false;break;}
  ok=mbedtls_sha256_update(&ctx,buffer,n)==0;vTaskDelay(1);
 }
 if(ok)ok=mbedtls_sha256_finish(&ctx,digest)==0;
 mbedtls_sha256_free(&ctx);return ok&&f.seek(0);
}
bool compatibleP4(File& file,size_t size){
 constexpr size_t kOff=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t);
 uint8_t head[1024]{};
 if(size<kOff+sizeof(esp_app_desc_t)||file.read(head,sizeof(head))!=sizeof(head)||!file.seek(0))return false;
 esp_image_header_t image{};memcpy(&image,head,sizeof(image));
 esp_app_desc_t app{};memcpy(&app,head+kOff,sizeof(app));
 esp_image_header_t runningHeader{};
 const auto* running=esp_ota_get_running_partition();const auto* runningDesc=esp_app_get_description();
 return image.magic==ESP_IMAGE_HEADER_MAGIC&&app.magic_word==ESP_APP_DESC_MAGIC_WORD&&
  memchr(app.project_name,0,sizeof(app.project_name))&&memchr(app.version,0,sizeof(app.version))&&
  running&&runningDesc&&esp_partition_read(running,0,&runningHeader,sizeof(runningHeader))==ESP_OK&&
  image.chip_id==runningHeader.chip_id&&
  strncmp(app.project_name,runningDesc->project_name,sizeof(app.project_name))==0&&
  bootloader_common_check_chip_revision_validity(&image,true);
}
}
FirmwareMediaUpdateService& FirmwareMediaUpdateService::instance(){static FirmwareMediaUpdateService s;return s;}
bool FirmwareMediaUpdateService::validC6Hash(const uint8_t* digest){return digest&&digestHex(digest)==kC6Sha;}
bool FirmwareMediaUpdateService::available()const{return StorageService::instance().sdAvailable()||UsbStorageService::instance().available();}
String FirmwareMediaUpdateService::storedInfo()const{
 auto& storage=StorageService::instance();StorageService::ArchiveVolume vol;
 if(!storage.archiveVolume(vol))return "Keine SD-Karte oder kein USB-Stick angeschlossen.";
 String info=vol.usb?"USB: ":"SD: ";
 for(bool c6:{false,true}){
  auto f=vol.fs->open(path(c6),FILE_READ);
  info+=String(c6?"C6 ":"P4 ");
  if(!f){info+="nicht vorhanden; ";continue;}
  info+=String(f.size())+" Bytes";
  if(!c6){
    constexpr size_t offset=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t);
    uint8_t head[1024]{};
    if(f.read(head,sizeof(head))==sizeof(head)){
      esp_app_desc_t app{};memcpy(&app,head+offset,sizeof(app));
      if(app.magic_word==ESP_APP_DESC_MAGIC_WORD&&memchr(app.version,0,sizeof(app.version)))
        info+=" Version "+String(app.version);
    }
  }else info+=" (ESP-Hosted; SHA-Prüfung vor Installation)";
  info+="; ";f.close();
 }
 return info;
}
FirmwareMediaUpdateService::Progress FirmwareMediaUpdateService::progress()const{
 std::lock_guard<std::mutex> g(mutex_);return progress_;
}
bool FirmwareMediaUpdateService::beginWebUpload(const String& previousVersion,const String& nextVersion){
 std::lock_guard<std::mutex> g(mutex_);
 if(busy_)return false;
 busy_=true;c6_=false;
 ++progress_.generation;
 progress_.phase=Phase::Checking;progress_.percent=0;progress_.c6=false;
 progress_.previousVersion=previousVersion;
 progress_.nextVersion=nextVersion;
 progress_.message="Webserver-Update wird vorbereitet ...";
 status_=progress_.message;
 return true;
}
void FirmwareMediaUpdateService::webUploadProgress(Phase phase,uint8_t percent,const String& message){
 publish(phase,percent,message);
}
void FirmwareMediaUpdateService::endWebUpload(bool success,const String& message){
 std::lock_guard<std::mutex> g(mutex_);
 busy_=false;
 progress_.phase=success?Phase::Ready:Phase::Failed;
 progress_.percent=success?100:progress_.percent;
 progress_.message=message;
 status_=message;
}
void FirmwareMediaUpdateService::webRebooting(){
 std::lock_guard<std::mutex> g(mutex_);
 if(progress_.phase!=Phase::Ready||progress_.c6)return;
 progress_.phase=Phase::Rebooting;
 progress_.message="Neustart läuft ...";
 status_=progress_.message;
}
void FirmwareMediaUpdateService::publish(Phase phase,uint8_t percent,const String& message){
 std::lock_guard<std::mutex> g(mutex_);
 if(progress_.phase==phase&&progress_.percent==percent&&progress_.message==message)return;
 progress_.phase=phase;progress_.percent=percent;progress_.message=message;
 status_=message;
}
bool FirmwareMediaUpdateService::busy()const{std::lock_guard<std::mutex> g(mutex_);return busy_;}
String FirmwareMediaUpdateService::status()const{std::lock_guard<std::mutex> g(mutex_);return status_;}
void FirmwareMediaUpdateService::setState(const String& s,bool b){std::lock_guard<std::mutex> g(mutex_);status_=s;busy_=b;}
bool FirmwareMediaUpdateService::start(bool c6){
 {std::lock_guard<std::mutex> g(mutex_);if(busy_)return false;busy_=true;c6_=c6;status_=String(c6?"C6":"P4")+"-Datei wird lokal geprüft ...";
  ++progress_.generation;progress_.phase=Phase::Checking;progress_.percent=0;
  progress_.c6=c6;progress_.message=status_;
  const auto* running=esp_app_get_description();
  if(c6&&hostedIsInitialized()){
   uint32_t major=0,minor=0,patch=0;
   hostedGetSlaveVersion(&major,&minor,&patch);
   progress_.previousVersion=String(major)+"."+String(minor)+"."+String(patch);
  }else progress_.previousVersion=c6?"Unbekannt (C6 offline)":(running?String(running->version):"Unbekannt");
  progress_.nextVersion=c6?"ESP-Hosted 2.12.13":"Wird geprüft ...";}
 if(NetworkWorker::instance().submitLocalTransfer(worker,this,"fw-media"))return true;
 setState("Transfer-Worker ist ausgelastet.",false);publish(Phase::Failed,0,"Transfer-Worker ist ausgelastet.");return false;
}
void FirmwareMediaUpdateService::worker(void* ctx){static_cast<FirmwareMediaUpdateService*>(ctx)->install();}
void FirmwareMediaUpdateService::install(){
 const bool c6=c6_;
 auto fail=[&](const String& msg){EventLog::instance().add("ERROR","FW_MEDIA",msg);setState(msg,false);publish(Phase::Failed,0,msg);};
 auto& storage=StorageService::instance();StorageService::ArchiveVolume vol;
 if(!storage.archiveVolume(vol)){fail("SD/USB nicht verfügbar.");return;}
 // Keep this lease for the entire read/write operation, so the staged image
 // cannot be replaced while it is being installed.
 std::unique_lock<std::mutex> mediaLock(StorageService::mutationMutex(),std::try_to_lock);
 if(!mediaLock.owns_lock()){fail("Speicher wird gerade bearbeitet. Bitte erneut versuchen.");return;}
 File file=vol.fs->open(path(c6),FILE_READ);
 if(!file){fail("Keine Firmware-Datei im /Firmware-Ordner gefunden.");return;}
 const size_t bytes=file.size();
 const esp_partition_t* target=c6?nullptr:esp_ota_get_next_update_partition(nullptr);
 if(c6?(!validC6Size(bytes)):(bytes<1024||!target||bytes>target->size||OtaService::instance().pendingValidation())){
  fail("Firmware-Größe oder OTA-Zustand ungültig.");return;
 }
 if(!c6&&!compatibleP4(file,bytes)){fail("P4-Firmware passt nicht zu diesem Gerät.");return;}
 if(!c6){
  constexpr size_t off=sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t);
  uint8_t head[1024]{};esp_app_desc_t desc{};
  if(!file.read(head,sizeof(head))||!file.seek(0)){fail("P4-Versionsinformation nicht lesbar.");return;}
  memcpy(&desc,head+off,sizeof(desc));
  {std::lock_guard<std::mutex> g(mutex_);progress_.nextVersion=String(desc.version);}
 }
 publish(Phase::Checking,4,"Firmware-Datei und SHA-256 werden geprüft ...");
 uint8_t expected[32]{};
 if(!fileDigest(file,expected)){fail("Firmware konnte nicht vollständig gelesen werden.");return;}
 publish(Phase::Checking,10,"Datei geprüft. Installation wird vorbereitet ...");
 if(c6){
  if(!validC6Hash(expected)){fail("C6 SHA-256 passt nicht zur freigegebenen ESP-Hosted-Firmware.");return;}
  if(!hostedIsInitialized()||!hostedHasUpdate()){fail("C6-SDIO-Update nicht verfügbar; ggf. UART-Recovery nötig.");return;}
  NetworkService::instance().setC6Maintenance(true);
  auto done=[&](const String& msg){NetworkService::instance().setC6Maintenance(false);fail(msg);};
  if(!hostedBeginUpdate()){done("C6 hat lokales Update abgelehnt.");return;}
  uint8_t data[2048];size_t remaining=bytes;bool ok=true;
  publish(Phase::Writing,10,"C6 wird lokal über SDIO aktualisiert ...");
  while(remaining){size_t want=std::min(remaining,sizeof(data));size_t n=file.read(data,want);
   if(n!=want||!hostedWriteUpdate(data,n)){ok=false;break;}remaining-=n;publish(Phase::Writing,uint8_t(10+75*(bytes-remaining)/bytes),"C6-Firmware wird geschrieben ...");vTaskDelay(1);}
  file.close();
  publish(Phase::Verifying,95,"C6-Firmware wird abgeschlossen ...");
  if(!ok||!hostedEndUpdate()){done("C6-Update fehlgeschlagen; UART-Recovery kann erforderlich sein.");return;}
  if(!hostedActivateUpdate()){done("C6-Firmware geschrieben, aber Aktivierung fehlgeschlagen.");return;}
  publish(Phase::Rebooting,100,"C6-Update aktiviert. Neustart läuft ...");delay(800);ESP.restart();return;
 }
 OtaWriteSession writer;
 if(!writer.acquired()){fail("Ein anderes OTA-Update läuft bereits.");return;}
 if(writer.begin(target,bytes)!=ESP_OK){fail("P4-OTA konnte nicht gestartet werden.");return;}
 uint8_t* buffer=writer.buffer();size_t remaining=bytes;bool ok=true;
 publish(Phase::Writing,10,"P4 wird lokal in den OTA-Slot geschrieben ...");
 while(remaining){const size_t count=std::min<size_t>(remaining,OtaWriteSession::kChunkSize);
  if(file.read(buffer,count)!=count||writer.write(buffer,count)!=ESP_OK){ok=false;break;}
  remaining-=count;
  publish(Phase::Writing,uint8_t(10+75*(bytes-remaining)/bytes),"P4-Firmware wird in den OTA-Slot geschrieben ...");
 }
 file.close();
 if(!ok){writer.abort();fail("P4-Firmware konnte nicht vollständig geschrieben werden.");return;}
 publish(Phase::Verifying,87,"P4-Firmware wird auf Bootfähigkeit geprüft ...");
 if(writer.finish()!=ESP_OK){fail("P4-Firmware ist nicht bootfähig.");return;}
 // Hash the real flash contents again; never select an unverified OTA slot.
 mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);ok=mbedtls_sha256_starts(&ctx,0)==0;
 size_t pos=0;
 while(ok&&pos<bytes){size_t count=std::min<size_t>(4096,bytes-pos);
  ok=esp_partition_read(target,pos,buffer,count)==ESP_OK&&mbedtls_sha256_update(&ctx,buffer,count)==0;
  pos+=count;publish(Phase::Verifying,uint8_t(87+12*pos/bytes),"P4-Flash wird per SHA-256 kontrolliert ...");vTaskDelay(1);}
 uint8_t real[32]{};if(ok)ok=mbedtls_sha256_finish(&ctx,real)==0;
 mbedtls_sha256_free(&ctx);
 if(!ok||memcmp(real,expected,32)){fail("P4-Flash-SHA-256 stimmt nicht; kein Boot-Slot-Wechsel.");return;}
 if(esp_ota_set_boot_partition(target)!=ESP_OK){fail("P4-Boot-Slot konnte nicht gesetzt werden.");return;}
 EventLog::instance().add("INFO","FW_MEDIA_READY",String("P4 local slot=")+target->label+" SHA="+digestHex(real));
 setState("P4-Firmware geprüft und im OTA-Slot bereit. Neustart bestätigen.",false);
 publish(Phase::Ready,100,"P4-Firmware geprüft. Jetzt neu starten.");
}
}

#include "UpdateService.h"
#include "StorageService.h"
#include "OtaWriteSession.h"
#include "PowerManager.h"
#include "../infrastructure/TransactionJournal.h"
#include "../core/EventLog.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <SD_MMC.h>
#include <mbedtls/sha256.h>
namespace hp {
namespace {
constexpr size_t kIoBufferSize=8192;
uint8_t* allocateIoBuffer(){uint8_t*b=(uint8_t*)ps_malloc(kIoBufferSize);return b?b:(uint8_t*)malloc(kIoBufferSize);}
bool fileSha256(const String&path,uint8_t out[32]){File f=SD_MMC.open(path,FILE_READ);if(!f)return false;mbedtls_sha256_context c;mbedtls_sha256_init(&c);bool ok=mbedtls_sha256_starts(&c,0)==0;uint8_t*b=allocateIoBuffer();if(!b){f.close();mbedtls_sha256_free(&c);return false;}while(ok&&f.available()){size_t n=f.read(b,kIoBufferSize);if(!n)break;ok=mbedtls_sha256_update(&c,b,n)==0;}if(ok)ok=mbedtls_sha256_finish(&c,out)==0;free(b);f.close();mbedtls_sha256_free(&c);return ok;}
}
UpdateService& UpdateService::instance(){static UpdateService u;return u;}bool UpdateService::begin(){return TransactionJournal::instance().begin();}
bool UpdateService::verifyPackageManifest(const String&m,const uint8_t*s,size_t n,UpdateManifest&o,String&e){if(!UpdateVerifier::instance().parseManifest(m,o,e))return false;if(!UpdateVerifier::instance().verifyManifestSignature(m,s,n,e))return false;staged_=o;verified_=true;TransactionJournal::instance().append(UpdateState::PackageVerified,o.versionCode,o.versionCode,o.versionCode);return true;}
bool UpdateService::preflight(const UpdateManifest&m,String&e){const esp_partition_t*next=esp_ota_get_next_update_partition(nullptr);if(!next||m.firmware.size>next->size){e="firmware_partition_size";return false;}auto check=[&](const char*l,const UpdateImageSpec&x){if(!x.size)return true;const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,l);return p&&x.size<=p->size;};if(!check("systemdata",m.systemData)||!check("appfs",m.appFs)){e="asset_partition_size";return false;}if((m.systemData.size||m.appFs.size)&&!StorageService::instance().sdAvailable()){e="sd_required_for_asset_backup";return false;}return true;}
bool UpdateService::writeFirmware(Stream& in,const UpdateManifest& m,String& e){
 if(!verified_||!preflight(m,e))return false;
 OtaWriteSession writer;
 if(!writer.acquired()){e="ota_busy";return false;}
 const auto* p=esp_ota_get_next_update_partition(nullptr);
 PerformanceGuard boost;
 auto result=writer.begin(p,m.firmware.size);
 if(result!=ESP_OK){e=String("ota_begin:")+esp_err_to_name(result);return false;}
 const uint32_t started=millis();
 uint32_t left=m.firmware.size;
 while(left){
  if(millis()-started>=180000U){e="ota_timeout";return false;}
  const size_t want=min<uint32_t>(OtaWriteSession::kChunkSize,left);
  const size_t got=in.readBytes(reinterpret_cast<char*>(writer.buffer()),want);
  if(got!=want){e="ota_truncated";return false;}
  result=writer.write(writer.buffer(),got);
  if(result!=ESP_OK){e=String("ota_write:")+esp_err_to_name(result);return false;}
  left-=got;
 }
 result=writer.finish();
 if(result!=ESP_OK){e=String("ota_validate:")+esp_err_to_name(result);return false;}
 String er;
 if(!UpdateVerifier::instance().verifyPartitionRaw(p->label,m.firmware.size,m.firmware.sha256,er)){e=er;return false;}
 TransactionJournal::instance().append(UpdateState::FirmwareWritten);return true;
}
bool UpdateService::copyPartitionToSd(const char*l,const char*f,uint32_t size,String&e){
 if(!StorageService::instance().sdAvailable()){e="sd_missing";return false;}
 const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,l);if(!p||size!=p->size){e="partition_missing";return false;}
 uint64_t freeBytes=SD_MMC.totalBytes()-SD_MMC.usedBytes();if(freeBytes<size+1024*1024){e="backup_space";return false;}
 String path=String("/homepanel/backups/update/")+f;SD_MMC.mkdir("/homepanel");SD_MMC.mkdir("/homepanel/backups");SD_MMC.mkdir("/homepanel/backups/update");SD_MMC.remove(path);File out=SD_MMC.open(path,FILE_WRITE);if(!out){e="backup_open";return false;}
 uint8_t*b=allocateIoBuffer();if(!b){out.close();e="memory";return false;}bool ok=true;for(uint32_t off=0;off<size&&ok;){size_t z=min<uint32_t>(kIoBufferSize,size-off);ok=esp_partition_read(p,off,b,z)==ESP_OK&&out.write(b,z)==z;off+=z;}out.flush();out.close();free(b);if(!ok){e="backup_write";return false;}
 uint8_t src[32],dst[32];if(!UpdateVerifier::instance().hashPartitionRaw(p->label,p->size,src)||!fileSha256(path,dst)||memcmp(src,dst,32)){e="backup_verify";return false;}File hf=SD_MMC.open(path+".sha256",FILE_WRITE);if(!hf||hf.write(src,32)!=32){if(hf)hf.close();e="backup_hash_write";return false;}hf.flush();hf.close();return true;
}
bool UpdateService::backupAssets(const UpdateManifest&m,String&e){
 if(!verified_||!preflight(m,e))return false;
 const esp_partition_t*sys=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"systemdata");
 const esp_partition_t*app=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"appfs");
 if(m.systemData.size&&(!sys||!copyPartitionToSd("systemdata","systemdata.bak",sys->size,e)))return false;
 if(m.appFs.size&&(!app||!copyPartitionToSd("appfs","appfs.bak",app->size,e)))return false;
 TransactionJournal::instance().append(UpdateState::BackupReady);return true;
}
bool UpdateService::streamPartition(Stream&in,const char*l,const UpdateImageSpec&s,String&e){const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,l);if(!p||s.size>p->size){e="partition_size";return false;}bool sys=!strcmp(l,"systemdata");if(sys)StorageService::instance().suspendInternalForUpdate();if(esp_partition_erase_range(p,0,(s.size+4095)&~4095u)!=ESP_OK){if(sys)StorageService::instance().resumeInternalAfterUpdate();e="asset_erase";return false;}uint8_t*b=allocateIoBuffer();if(!b){if(sys)StorageService::instance().resumeInternalAfterUpdate();e="memory";return false;}bool ok=true;uint32_t left=s.size,off=0;while(left&&ok){size_t z=min<uint32_t>(kIoBufferSize,left),got=in.readBytes((char*)b,z);ok=got==z&&esp_partition_write(p,off,b,z)==ESP_OK;off+=got;left-=got;}free(b);String er;if(ok)ok=UpdateVerifier::instance().verifyPartitionRaw(l,s.size,s.sha256,er);if(sys&&!StorageService::instance().resumeInternalAfterUpdate())ok=false;if(!ok)e=er.length()?er:"asset_write";return ok;}
bool UpdateService::writeAsset(Stream&in,const char*l,const UpdateImageSpec&s,String&e){if(!verified_||TransactionJournal::instance().state()<UpdateState::BackupReady){e="backup_not_ready";return false;}TransactionJournal::instance().append(UpdateState::AssetsWriting);if(!streamPartition(in,l,s,e)){TransactionJournal::instance().append(UpdateState::RecoveryRequired);return false;}TransactionJournal::instance().append(UpdateState::AssetsVerified);return true;}
bool UpdateService::activateFirmware(String& e){
 std::unique_lock<std::mutex> lock(OtaWriteSession::mutex(),std::try_to_lock);
 if(!lock.owns_lock()){e="ota_busy";return false;}
 const auto* p=esp_ota_get_next_update_partition(nullptr);
 // A different writer may have used the inactive slot since staging. Never
 // select it solely because a previous write once succeeded.
 if(!verified_||!p){e="firmware_not_verified";return false;}
 if(!UpdateVerifier::instance().verifyPartitionRaw(p->label,staged_.firmware.size,staged_.firmware.sha256,e))return false;
 OtaTrace::record(OtaTrace::Selecting,staged_.firmware.size);
 const auto result=esp_ota_set_boot_partition(p);
 if(result!=ESP_OK){OtaTrace::record(OtaTrace::Failed,staged_.firmware.size,result);e="boot_slot";return false;}
 OtaTrace::record(OtaTrace::Ready,staged_.firmware.size);
 TransactionJournal::instance().append(UpdateState::BootPending);return true;
}
bool UpdateService::restorePartitionFromSd(const char*l,const char*f,String&e){
 String path=String("/homepanel/backups/update/")+f;File in=SD_MMC.open(path,FILE_READ);if(!in){e="backup_missing";return false;}
 File hf=SD_MMC.open(path+".sha256",FILE_READ);uint8_t expected[32],actual[32];if(!hf||hf.read(expected,32)!=32){if(hf)hf.close();in.close();e="backup_hash_missing";return false;}hf.close();if(!fileSha256(path,actual)||memcmp(expected,actual,32)){in.close();e="backup_hash_invalid";return false;}
 const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,l);bool sys=!strcmp(l,"systemdata");if(!p||in.size()!=p->size){in.close();e="backup_size";return false;}
 if(sys)StorageService::instance().suspendInternalForUpdate();
 // No erase happens before the backup hash and exact target size have been verified.
 if(esp_partition_erase_range(p,0,p->size)!=ESP_OK){in.close();if(sys)StorageService::instance().resumeInternalAfterUpdate();e="restore_erase";return false;}
 uint8_t*b=allocateIoBuffer();bool ok=b;uint32_t off=0;while(ok&&in.available()){size_t n=in.read(b,kIoBufferSize);if(!n)break;ok=esp_partition_write(p,off,b,n)==ESP_OK;off+=n;}free(b);in.close();if(ok){uint8_t restored[32];ok=UpdateVerifier::instance().hashPartitionRaw(p->label,p->size,restored)&&!memcmp(restored,expected,32);}if(sys)ok&=StorageService::instance().resumeInternalAfterUpdate();if(!ok)e="restore_verify";return ok;
}
bool UpdateService::recoverAssetsFromBackup(String&e){if(!StorageService::instance().sdAvailable()){e="sd_missing";return false;}bool ok=true;if(SD_MMC.exists("/homepanel/backups/update/systemdata.bak"))ok&=restorePartitionFromSd("systemdata","systemdata.bak",e);if(SD_MMC.exists("/homepanel/backups/update/appfs.bak"))ok&=restorePartitionFromSd("appfs","appfs.bak",e);if(ok)TransactionJournal::instance().append(UpdateState::Committed);return ok;}
}

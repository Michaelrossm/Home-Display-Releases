#include "AssetManager.h"
#include <esp_partition.h>
#include "../services/StorageService.h"
namespace hp {
AssetManager& AssetManager::instance(){static AssetManager a;return a;}
AssetStatus AssetManager::checkPartition(const char*l){const esp_partition_t*p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,l);if(!p)return AssetStatus::Missing;uint8_t b[16];return esp_partition_read(p,0,b,sizeof(b))==ESP_OK?AssetStatus::Ready:AssetStatus::Invalid;}
bool AssetManager::begin(){
    systemData_=checkPartition("systemdata");
    appFs_=checkPartition("appfs");
    // StorageService owns the persistent appfs mount; a second concurrent
    // esp_vfs_littlefs_register() for the same partition would fail, so this
    // reuses its mount status instead of mounting the partition again here.
    if(appFs_==AssetStatus::Ready&&!StorageService::instance().appFsAvailable())appFs_=AssetStatus::Invalid;
    return systemData_==AssetStatus::Ready&&appFs_==AssetStatus::Ready;
}
bool AssetManager::verifyAll(){return begin();}
}


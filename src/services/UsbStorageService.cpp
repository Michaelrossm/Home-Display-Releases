#include "UsbStorageService.h"
#include <Arduino.h>
#include <usb/usb_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <bsp/esp32_p4_platform.h>
#include "SecurityService.h"
#include "StorageService.h"
#include "UsbAppFileSystem.h"
namespace hp {
static constexpr const char* kTag="USB-STORAGE";
UsbStorageService& UsbStorageService::instance(){static UsbStorageService s;return s;}
bool UsbStorageService::begin(){
 if(started_)return true;
 filesMutex_=xSemaphoreCreateRecursiveMutex();if(!filesMutex_)return false;
 queue_=xQueueCreate(8,sizeof(Message));if(!queue_){vSemaphoreDelete(filesMutex_);filesMutex_=nullptr;return false;}
 if(bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV,true)!=ESP_OK){vQueueDelete(queue_);queue_=nullptr;vSemaphoreDelete(filesMutex_);filesMutex_=nullptr;return false;}
 msc_host_driver_config_t msc{};
 msc.create_backround_task=true;msc.task_priority=5;msc.stack_size=4096;msc.callback=eventCb;
 if(msc_host_install(&msc)!=ESP_OK){bsp_usb_host_stop();vQueueDelete(queue_);queue_=nullptr;vSemaphoreDelete(filesMutex_);filesMutex_=nullptr;return false;}
 if(xTaskCreate(managerTask,"usb-volume",6144,this,4,nullptr)!=pdPASS){
     ESP_LOGE(kTag,"USB task allocation failed");msc_host_uninstall();bsp_usb_host_stop();
     vQueueDelete(queue_);queue_=nullptr;vSemaphoreDelete(filesMutex_);filesMutex_=nullptr;return false;
 }
 started_=true;return true;
}
void UsbStorageService::eventCb(const msc_host_event_t* event,void* arg){
 auto& s=instance();if(!s.queue_||!event)return;
 Message m{};
 if(event->event==msc_host_event_t::MSC_DEVICE_CONNECTED){m.connected=true;m.address=event->device.address;}
 else if(event->event==msc_host_event_t::MSC_DEVICE_DISCONNECTED){m.connected=false;m.handle=event->device.handle;
  // Readers must stop acquiring new handles before unmount begins.
  if(s.publishedDevice_.load()==event->device.handle){
   s.present_.store(false);s.mounted_.store(false,std::memory_order_release);++s.generation_;
  }
 }else return;
 xQueueSend(s.queue_,&m,0);
}
void UsbStorageService::managerTask(void* arg){
 auto& s=*static_cast<UsbStorageService*>(arg);
 Message m{};for(;;){if(xQueueReceive(s.queue_,&m,portMAX_DELAY)!=pdTRUE)continue;
   if(m.format)s.format(m.generation);else if(m.connected)s.attach(m.address);else s.detach(m.handle);
 }
}
void UsbStorageService::attach(uint8_t addr){
 if(device_){ESP_LOGW(kTag,"Only one USB drive supported");return;}
 if(msc_host_install_device(addr,&device_)!=ESP_OK){device_=nullptr;return;}
 publishedDevice_.store(device_);
 msc_host_device_info_t info{};
 if(msc_host_get_device_info(device_,&info)!=ESP_OK || info.sector_size!=512 || !info.sector_count){
  ESP_LOGW(kTag,"Unsupported USB geometry");publishedDevice_=nullptr;msc_host_uninstall_device(device_);device_=nullptr;return;}
 capacity_.store(uint64_t(info.sector_count)*info.sector_size);present_.store(true);++generation_;formatState_=0;
 const esp_vfs_fat_mount_config_t mount={.format_if_mount_failed=false,.max_files=5,.allocation_unit_size=32768};
 if(msc_host_vfs_register(device_,mountPath(),&mount,&vfs_)!=ESP_OK){
  ESP_LOGW(kTag,"Mount failed (FAT16/FAT32 required; no auto format)");
  vfs_=nullptr;formatState_=5;return;}
 capacity_.store(uint64_t(info.sector_count)*info.sector_size,std::memory_order_release);
 mounted_.store(true,std::memory_order_release);++generation_;
 prepareDirectories();
 ESP_LOGI(kTag,"Mounted USB drive: %llu bytes",(unsigned long long)capacity());
}
void UsbStorageService::detach(msc_host_device_handle_t handle){
 if(!device_||device_!=handle)return;
 present_.store(false);mounted_.store(false,std::memory_order_release);++generation_;
 while(filePins_.load(std::memory_order_acquire))vTaskDelay(pdMS_TO_TICKS(10));
 xSemaphoreTakeRecursive(filesMutex_,portMAX_DELAY);
 if(vfs_){const esp_err_t err=msc_host_vfs_unregister(vfs_);
  if(err!=ESP_OK)ESP_LOGW(kTag,"USB VFS unregister failed: %s",esp_err_to_name(err));vfs_=nullptr;}
 const esp_err_t err=msc_host_uninstall_device(device_);
 if(err!=ESP_OK)ESP_LOGW(kTag,"USB device uninstall failed: %s",esp_err_to_name(err));
 publishedDevice_=nullptr;device_=nullptr;capacity_.store(0,std::memory_order_release);
 xSemaphoreGiveRecursive(filesMutex_);
}
void UsbStorageService::prepareDirectories(){
 UsbFilesGuard guard;
 if(guard&&!StorageService::instance().prepareExternalDirectories(usbAppFileSystem()))
  ESP_LOGW(kTag,"USB standard folders incomplete; volume remains available");
}
bool UsbStorageService::acquireFiles(){
 if(!filesMutex_||!available()||formatBusy()||xSemaphoreTakeRecursive(filesMutex_,0)!=pdTRUE)return false;
 if(!available()||formatBusy()){xSemaphoreGiveRecursive(filesMutex_);return false;}return true;
}
bool UsbStorageService::pinFiles(uint32_t& gen){
 gen=generation();if(!available()||formatBusy())return false;
 filePins_.fetch_add(1,std::memory_order_acq_rel);
 if(!validFiles(gen)||formatBusy()){unpinFiles();return false;}return true;
}
bool UsbStorageService::usage(uint64_t& total,uint64_t& used){
 UsbFilesGuard guard;if(!guard)return false;uint64_t free=0;
 if(esp_vfs_fat_info(mountPath(),&total,&free)!=ESP_OK)return false;
 used=total>free?total-free:0;return true;
}
bool UsbStorageService::requestFormat(uint32_t expectedGeneration){
 if(!started_||!queue_||!present()||generation()!=expectedGeneration||
    !SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked())return false;
 bool expected=false;if(!formatPending_.compare_exchange_strong(expected,true))return false;
 Message m{};m.format=true;m.generation=expectedGeneration;
 formatState_=1;
 if(xQueueSend(queue_,&m,0)!=pdTRUE){formatPending_=false;formatState_=4;return false;}
 return true;
}
void UsbStorageService::format(uint32_t expectedGeneration){
 if(!present()||generation()!=expectedGeneration||!device_||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){
  formatState_=6;formatPending_=false;return;
 }
 mounted_=false; // Cancel existing readers, then wait until every handle is closed.
 const uint32_t waitStarted=millis();
 while(filePins_.load(std::memory_order_acquire)){
  if(millis()-waitStarted>=2000){mounted_=present()&&vfs_;formatState_=7;formatPending_=false;return;}
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 xSemaphoreTakeRecursive(filesMutex_,portMAX_DELAY);
 if(!present()||generation()!=expectedGeneration||!SecurityService::instance().settingsAllowed()||SecurityService::instance().tabletLocked()){
  mounted_=present()&&vfs_;formatState_=6;formatPending_=false;xSemaphoreGiveRecursive(filesMutex_);return;
 }
 ++generation_;const auto operationGeneration=generation();formatState_=2;
 const esp_vfs_fat_mount_config_t cfg={.format_if_mount_failed=false,.max_files=5,.allocation_unit_size=32768};
 esp_err_t err;
 if(vfs_){
  err=msc_host_vfs_format(device_,&cfg,vfs_);
  const auto unmount=msc_host_vfs_unregister(vfs_);if(unmount==ESP_OK)vfs_=nullptr;
  if(unmount!=ESP_OK)err=unmount;
  if(present()&&generation()==operationGeneration&&unmount==ESP_OK){
   const auto remount=msc_host_vfs_register(device_,mountPath(),&cfg,&vfs_);if(remount!=ESP_OK)err=remount;
  }
 }else{
  // This is a manually confirmed operation, never automatic mount recovery.
  auto explicitFormat=cfg;explicitFormat.format_if_mount_failed=true;
  err=msc_host_vfs_register(device_,mountPath(),&explicitFormat,&vfs_);
  // Force erasure even if the formerly unmountable volume is now mountable.
  if(err==ESP_OK){
   err=msc_host_vfs_format(device_,&cfg,vfs_);
   const auto unmount=msc_host_vfs_unregister(vfs_);if(unmount==ESP_OK)vfs_=nullptr;else err=unmount;
   if(present()&&generation()==operationGeneration&&unmount==ESP_OK){
    const auto remount=msc_host_vfs_register(device_,mountPath(),&cfg,&vfs_);if(remount!=ESP_OK)err=remount;
   }
  }
 }
 mounted_=present()&&generation()==operationGeneration&&vfs_&&err==ESP_OK;
 formatState_=!present()?6:err==ESP_OK?3:4;++generation_;formatPending_=false;
 xSemaphoreGiveRecursive(filesMutex_);
 if(available())prepareDirectories();
}
const char* UsbStorageService::formatStatus()const{
 switch(formatState_.load()){
 case 1:return "USB-Formatierung vorgemerkt";
 case 2:return "USB wird formatiert - nicht abziehen!";
 case 3:return "USB formatiert und wieder eingebunden";
 case 4:return "USB-Formatierung/Einbindung fehlgeschlagen; Stick am PC pruefen";
 case 5:return "USB erkannt, Dateisystem nicht eingebunden (FAT16/FAT32)";
 case 6:return "Abgebrochen: USB gewechselt/entfernt oder Einstellungen gesperrt";
 case 7:return "USB noch in Verwendung; Apps schliessen und erneut versuchen";
 default:return present()?"USB verbunden":"Kein USB-Speicher verbunden";
 }
}
}

#pragma once
#include <atomic>
#include <stdint.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <usb/msc_host.h>
#include <usb/msc_host_vfs.h>
namespace hp {
class UsbStorageService final {
public:
 static UsbStorageService& instance();
 bool begin();
 bool available() const {return mounted_.load(std::memory_order_acquire);}
 uint32_t generation() const {return generation_.load(std::memory_order_acquire);}
 uint64_t capacity() const {return capacity_.load(std::memory_order_acquire);}
 bool present()const{return present_.load(std::memory_order_acquire);}
 bool requestFormat(uint32_t expectedGeneration);
 bool formatBusy()const{return formatPending_.load();}
 const char* formatStatus()const;
 bool usage(uint64_t& total,uint64_t& used);
 // File handles can move between tasks; these leases have no task ownership.
 bool pinFiles(uint32_t& generation);
 void unpinFiles(){filePins_.fetch_sub(1,std::memory_order_release);}
 bool validFiles(uint32_t generation)const{return available()&&this->generation()==generation;}
 // All native USB filesystem users hold this lease until files are closed.
 bool acquireFiles();
 void releaseFiles(){xSemaphoreGiveRecursive(filesMutex_);}
 static constexpr const char* mountPath(){return "/homeusb";}
private:
 struct Message {bool connected;uint8_t address;msc_host_device_handle_t handle;bool format=false;uint32_t generation=0;};
 static void managerTask(void*);
 static void eventCb(const msc_host_event_t*,void*);
 void attach(uint8_t);
 void prepareDirectories();
 void detach(msc_host_device_handle_t);
 void format(uint32_t);
 QueueHandle_t queue_=nullptr;
 SemaphoreHandle_t filesMutex_=nullptr;
 msc_host_device_handle_t device_=nullptr;
 std::atomic<msc_host_device_handle_t> publishedDevice_{nullptr};
 msc_host_vfs_handle_t vfs_=nullptr;
 std::atomic<bool> mounted_{false};
 std::atomic<bool> present_{false},formatPending_{false};
 std::atomic<uint8_t> formatState_{0};
 std::atomic<uint32_t> generation_{0};
 std::atomic<uint32_t> filePins_{0};
 std::atomic<uint64_t> capacity_{0};
 bool started_=false;
};
class UsbFilesGuard {
 bool acquired_=UsbStorageService::instance().acquireFiles();
public:
 ~UsbFilesGuard(){if(acquired_)UsbStorageService::instance().releaseFiles();}
 explicit operator bool()const{return acquired_;}
 UsbFilesGuard()=default;
 UsbFilesGuard(const UsbFilesGuard&)=delete;
 UsbFilesGuard& operator=(const UsbFilesGuard&)=delete;
};
}

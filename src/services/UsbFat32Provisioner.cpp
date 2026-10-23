#include "UsbFat32Provisioner.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace hp { namespace {
constexpr uint32_t kFirst=2048; // 1 MiB alignment
constexpr uint32_t kFat32MaxClusters=0x0ffffff5;
void le16(uint8_t* p,uint16_t v){p[0]=v&255;p[1]=v>>8;}
void le32(uint8_t* p,uint32_t v){le16(p,uint16_t(v));le16(p+2,uint16_t(v>>16));}
struct Disk {
 msc_host_device_handle_t handle;
 const std::atomic<bool>& disconnected;
 bool write(uint32_t lba,const uint8_t* src)const{
  return !disconnected.load(std::memory_order_acquire)&&msc_host_write_sector(handle,lba,src,512)==ESP_OK;
 }
 bool read(uint32_t lba,uint8_t* dst)const{
  return !disconnected.load(std::memory_order_acquire)&&msc_host_read_sector(handle,lba,dst,512)==ESP_OK;
 }
};
}
bool UsbFat32Provisioner::run(msc_host_device_handle_t device,const msc_host_device_info_t& info,
                              const std::atomic<bool>& disconnected,std::atomic<uint8_t>& percent){
 percent.store(0);
 if(!device||disconnected.load()||info.sector_size!=512||info.sector_count<131072ULL||
    info.sector_count>UINT32_MAX)return false; // 64 MiB..2 TiB at 512 B
 const uint32_t total=uint32_t(info.sector_count);
 const uint32_t volume=total-kFirst;
 const uint32_t sectorsPerCluster=volume<4194304u?1u:volume<16777216u?8u:volume<67108864u?16u:64u; // 512 B..32 KiB clusters
 const uint32_t reserved=32,rootCluster=2;
 // Determine the FAT length including its own space, rounded up in 512-byte sectors.
 uint32_t fat=(volume/sectorsPerCluster+2+127)/128;
 for(unsigned n=0;n<16;++n){
  if(volume<=reserved+2*fat)return false;
  const uint32_t clusters=(volume-reserved-2*fat)/sectorsPerCluster;
  const uint32_t next=(clusters+2+127)/128;
  if(next<=fat)break;
  fat=next;
 }
 const uint32_t clusters=(volume-reserved-2*fat)/sectorsPerCluster;
 if(clusters<65525 || clusters>kFat32MaxClusters || fat<2)return false;
 const uint32_t firstFat=kFirst+reserved;
 const uint32_t firstData=firstFat+2*fat;
 if(firstData+sectorsPerCluster>total)return false;
 Disk d{device,disconnected};uint8_t block[512]{};
 // Check the physical device before the FIRST write; no FAT logical drive-number guessing.
 if(!d.read(0,block))return false;
 // GPT backup exists at the tail of some Unraid/Linux sticks; remove it first.
 memset(block,0,512);
 for(uint32_t i=0;i<34;++i){if(!d.write(total-34+i,block))return false;}
 // Clear boot + old partition metadata, including protective MBR / GPT primary headers.
 for(uint32_t i=0;i<kFirst;++i){if(!d.write(i,block))return false;}
 percent.store(1);
 // MBR: ONE active-or-not primary LBA FAT32 partition, occupies the remaining drive.
 // Write the final MBR only AFTER the FAT32 filesystem is fully prepared.
 uint8_t mbr[512]{};
 uint8_t* entry=mbr+446;entry[0]=0;entry[1]=0xfe;entry[2]=0xff;entry[3]=0xff;
 entry[4]=0x0c;entry[5]=0xfe;entry[6]=0xff;entry[7]=0xff;
 le32(entry+8,kFirst);le32(entry+12,volume);mbr[510]=0x55;mbr[511]=0xaa;
 // FAT32 BIOS parameter block / volume boot record (sector 0 and backup at sector 6).
 uint8_t vbr[512]{};
 vbr[0]=0xeb;vbr[1]=0x58;vbr[2]=0x90;memcpy(vbr+3,"HPESP32 ",8);
 le16(vbr+11,512);vbr[13]=uint8_t(sectorsPerCluster);le16(vbr+14,uint16_t(reserved));
 vbr[16]=2;le16(vbr+17,0);le16(vbr+19,0);vbr[21]=0xf8;
 le16(vbr+22,0);le16(vbr+24,63);le16(vbr+26,255);
 le32(vbr+28,kFirst);le32(vbr+32,volume);le32(vbr+36,fat);
 le16(vbr+40,0);le16(vbr+42,0);le32(vbr+44,rootCluster);
 le16(vbr+48,1);le16(vbr+50,6);vbr[64]=0x80;vbr[66]=0x29;
 le32(vbr+67,total^0x48505034);memcpy(vbr+71,"HOME-DISPLAY",11);
 memcpy(vbr+82,"FAT32   ",8);vbr[510]=0x55;vbr[511]=0xaa;
 if(!d.write(kFirst,vbr)||!d.write(kFirst+6,vbr))return false;
 uint8_t fsinfo[512]{};le32(fsinfo,0x41615252);le32(fsinfo+484,0x61417272);
 le32(fsinfo+488,clusters-1);le32(fsinfo+492,3);
 le32(fsinfo+508,0xaa550000);
 if(!d.write(kFirst+1,fsinfo)||!d.write(kFirst+7,fsinfo))return false;
 memset(block,0,512);
 for(uint32_t f=0;f<2;++f){
  for(uint32_t sector=0;sector<fat;++sector){
   if(sector==0){memset(block,0,512);le32(block,0x0ffffff8);le32(block+4,0x0fffffff);le32(block+8,0x0fffffff);}
   else memset(block,0,512);
   if(!d.write(firstFat+f*fat+sector,block))return false;
   if((sector&127u)==0){
    percent.store(uint8_t(2+uint64_t(f*fat+sector)*96/(2*fat)));
    vTaskDelay(1);
   }
  }
 }
 memset(block,0,512);
 for(uint32_t s=0;s<sectorsPerCluster;++s){if(!d.write(firstData+s,block))return false;}
 // Only publish a valid single partition table after all filesystem structures exist.
 if(!d.write(0,mbr))return false;
 uint8_t verify[512]{};
 if(!d.read(0,verify)||memcmp(verify,mbr,512))return false;
 if(!d.read(kFirst,verify)||memcmp(verify,vbr,512))return false;
 percent.store(100);return true;
}
}

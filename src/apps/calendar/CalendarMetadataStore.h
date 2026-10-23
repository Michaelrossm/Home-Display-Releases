#pragma once
#include "CalendarTypes.h"
#include "CalendarDate.h"
#include "EventDetails.h"
#include "../../services/StorageService.h"
#include <memory>
namespace hp::calendar {
inline bool validMetadataRow(const PersonRecord& r){return r.id&&r.email[0]&&r.displayName[0]&&memchr(r.email,0,sizeof(r.email))&&memchr(r.displayName,0,sizeof(r.displayName))&&r.colorIndex<=15&&(!r.birthdayMonth&&!r.birthdayDay||validDate(r.birthdayYear?r.birthdayYear:2000,r.birthdayMonth,r.birthdayDay));}
inline bool validMetadataRow(const CalendarRecord& r){return r.id&&r.name[0]&&memchr(r.name,0,sizeof(r.name))&&memchr(r.remoteAccountId,0,sizeof(r.remoteAccountId))&&r.kind<=5&&r.provider<=3&&r.colorIndex<=15&&r.enabled<=1;}
template<class T,size_t N> struct MetadataBlock {
 uint32_t magic=0x4850434d,version=1,count=0,checksum=0;
 T rows[N]{};
 bool valid()const{
  if(magic!=0x4850434d||version!=1||count>N||checksum!=(detailChecksum(rows,sizeof(rows))^detailChecksum(&count,sizeof(count))))return false;
  for(size_t i=0;i<count;++i){if(!validMetadataRow(rows[i]))return false;for(size_t j=0;j<i;++j)if(rows[j].id==rows[i].id)return false;}return true;
 }
};
// A separate versioned path preserves the original raw records for rollback.
// Once present, a damaged v2 file must not silently fall back to stale v1 data.
template<class T,size_t N> bool loadMetadata(const char* path,T(&out)[N],size_t& count,bool& present){
 auto& storage=StorageService::instance();present=false;
 for(const char* suffix:{"",".tmp",".bak"})if(storage.exists(StorageArea::UserData,String(path)+suffix))present=true;
 if(!present)return true;
 auto block=std::unique_ptr<MetadataBlock<T,N>>(new(std::nothrow) MetadataBlock<T,N>);if(!block)return false;
 auto validator=[&](fs::FS& fs,const String& target){auto f=fs.open(target,"r");return f&&f.size()==sizeof(*block)&&f.read(reinterpret_cast<uint8_t*>(block.get()),sizeof(*block))==sizeof(*block)&&block->valid();};
 if(!storage.recoverAtomicWrite(StorageArea::UserData,path,validator))return false;
 count=block->count;memcpy(out,block->rows,sizeof(out));return true;
}
template<class T,size_t N> bool saveMetadata(const char* path,const T(&rows)[N],size_t count){
 if(count>N)return false;auto block=std::unique_ptr<MetadataBlock<T,N>>(new(std::nothrow) MetadataBlock<T,N>);if(!block)return false;
 block->count=count;memcpy(block->rows,rows,count*sizeof(T));block->checksum=detailChecksum(block->rows,sizeof(block->rows))^detailChecksum(&block->count,sizeof(block->count));
 return StorageService::instance().writeAtomic(StorageArea::UserData,path,block.get(),sizeof(*block),[](fs::FS& fs,const String& target){
  auto stored=std::unique_ptr<MetadataBlock<T,N>>(new(std::nothrow) MetadataBlock<T,N>);if(!stored)return false;
  auto f=fs.open(target,"r");return f&&f.size()==sizeof(*stored)&&f.read(reinterpret_cast<uint8_t*>(stored.get()),sizeof(*stored))==sizeof(*stored)&&stored->valid();
 });
}
}

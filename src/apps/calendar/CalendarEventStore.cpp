#include "CalendarEventStore.h"
#include "../../services/StorageService.h"
#include "../../services/NetworkWorker.h"
#include <memory>
#include <algorithm>
namespace hp {namespace {
constexpr const char* root="/calendar/events";
String path(uint64_t id){char name[64];snprintf(name,sizeof(name),"%s/%08lx%08lx.bin",root,(unsigned long)(id>>32),(unsigned long)id);return name;}
bool eventValid(const EventRecord& e){return e.localId&&e.calendarId&&e.revision&&e.endUtc>e.startUtc&&
 memchr(e.uid,0,sizeof(e.uid))&&memchr(e.title,0,sizeof(e.title))&&memchr(e.location,0,sizeof(e.location))&&memchr(e.notes,0,sizeof(e.notes))&&memchr(e.externalHref,0,sizeof(e.externalHref));}
bool readBundle(uint64_t id,StoredCalendarEvent& out){
 auto& st=StorageService::instance();auto validate=[&](fs::FS& fs,const String& p){auto f=fs.open(p,FILE_READ);return f&&f.size()==sizeof(out)&&f.read(reinterpret_cast<uint8_t*>(&out),sizeof(out))==sizeof(out)&&out.valid()&&out.event.localId==id;};
 return st.recoverAtomicWrite(StorageArea::UserData,path(id),validate);
}
bool exists(uint64_t id){auto& st=StorageService::instance();for(const char* suffix:{"",".tmp",".bak"})if(st.exists(StorageArea::UserData,path(id)+suffix))return true;return false;}
bool identity(const String& name,uint64_t& id){
 const int slash=name.lastIndexOf('/');const String n=slash>=0?name.substring(slash+1):name;
 if(n.length()!=20||!n.endsWith(".bin"))return false;id=0;
 for(unsigned i=0;i<16;++i){char c=n[i];if(!isxdigit((unsigned char)c))return false;id=(id<<4)|(c<='9'?c-'0':tolower(c)-'a'+10);}return id!=0;
}
}
bool StoredCalendarEvent::valid()const{return magic==0x48504345&&version==1&&eventValid(event)&&details.valid()&&details.localId==event.localId&&rules.valid()&&
 checksum==(detailChecksum(&event,sizeof(event))^detailChecksum(&details,sizeof(details))^detailChecksum(&rules,sizeof(rules)));}
void StoredCalendarEvent::seal(){checksum=detailChecksum(&event,sizeof(event))^detailChecksum(&details,sizeof(details))^detailChecksum(&rules,sizeof(rules));}
bool CalendarEventStore::read(uint64_t id,StoredCalendarEvent& out,String& error,bool* legacy){
 if(legacy)*legacy=false;
 out={};if(!id){error="event_id";return false;}
 if(exists(id)){if(readBundle(id,out))return true;error="event_integrity";return false;}
 auto& st=StorageService::instance();auto* fs=st.fsFor(StorageClass::UserData);if(!fs){error="storage_unavailable";return false;}
 auto f=fs->open(st.pathFor(StorageClass::UserData,"/calendar/events.bin"),FILE_READ);if(!f){error="event_not_found";return false;}
 if(f.size()%sizeof(EventRecord)||f.size()>kSdLimit*sizeof(EventRecord)){error="legacy_event_integrity";return false;}
 EventRecord row{};bool found=false;
 while(f.read(reinterpret_cast<uint8_t*>(&row),sizeof(row))==sizeof(row))if(row.localId==id){if(!eventValid(row)){error="legacy_event_integrity";return false;}out.event=row;found=true;}
 if(!found){error="event_not_found";return false;}
 if(legacy)*legacy=true;
 out.details.localId=id;detailPreview(out.details.location,sizeof(out.details.location),out.event.location);detailPreview(out.details.notes,sizeof(out.details.notes),out.event.notes);
 // Legacy sidecars are resolved by CalendarService::eventDetails().
 out.seal();return true;
}
bool CalendarEventStore::write(const StoredCalendarEvent& record,String& error){
 if(!record.valid()){error="event_integrity";return false;}auto& st=StorageService::instance();
 if(!st.canStore(StorageClass::UserData)){error="storage_unavailable";return false;}
 if(!exists(record.event.localId)){
  auto* fs=st.fsFor(StorageClass::UserData);if(!fs){error="storage_unavailable";return false;}auto dir=fs->open(st.pathFor(StorageClass::UserData,root));std::vector<uint64_t> ids;
  const size_t limit=st.sdAvailable()?kSdLimit:kInternalLimit;
  for(auto f=dir.openNextFile();f;f=dir.openNextFile()){String name=f.path();f.close();if(name.endsWith(".tmp")||name.endsWith(".bak"))name.remove(name.length()-4);uint64_t id=0;if(identity(name,id)&&std::find(ids.begin(),ids.end(),id)==ids.end()){ids.push_back(id);if(ids.size()>=limit){error="event_storage_limit";return false;}}}
 }
 auto expected=std::unique_ptr<StoredCalendarEvent>(new(std::nothrow) StoredCalendarEvent);if(!expected){error="memory";return false;}
 auto validator=[&](fs::FS& fs,const String& p){auto f=fs.open(p,FILE_READ);return f&&f.size()==sizeof(*expected)&&f.read(reinterpret_cast<uint8_t*>(expected.get()),sizeof(*expected))==sizeof(*expected)&&expected->valid()&&expected->event.localId==record.event.localId&&expected->event.revision==record.event.revision;};
 if(!st.writeAtomic(StorageArea::UserData,path(record.event.localId),&record,sizeof(record),validator)){error="event_write_failed";return false;}return true;
}
bool CalendarEventStore::visit(const std::function<bool(const StoredCalendarEvent&)>& emit,String& error){
 auto& st=StorageService::instance();auto* fs=st.fsFor(StorageClass::UserData);if(!fs){error="storage_unavailable";return false;}
 auto record=std::unique_ptr<StoredCalendarEvent>(new(std::nothrow) StoredCalendarEvent);if(!record){error="memory";return false;}
 std::vector<uint64_t> ids;ids.reserve(32);auto dir=fs->open(st.pathFor(StorageClass::UserData,root));
 const auto started=millis();const size_t limit=st.sdAvailable()?kSdLimit:kInternalLimit;
 for(auto f=dir.openNextFile();f;f=dir.openNextFile()){
  String name=f.path();f.close();if(name.endsWith(".bak")||name.endsWith(".tmp"))name.remove(name.length()-4);
  uint64_t id=0;if(!identity(name,id)||std::find(ids.begin(),ids.end(),id)!=ids.end())continue;
  if(ids.size()>=limit){error="event_storage_limit";return false;}ids.push_back(id);
  if(!readBundle(id,*record)){error="event_integrity";return false;}if(!emit(*record))return true;
  NetworkWorker::instance().maintain();if(millis()-started>10000){error="query_deadline";return false;}
 }
 dir.close();auto legacy=fs->open(st.pathFor(StorageClass::UserData,"/calendar/events.bin"),FILE_READ);
 if(!legacy)return true;if(legacy.size()%sizeof(EventRecord)||legacy.size()>kSdLimit*sizeof(EventRecord)){error="legacy_event_integrity";return false;}
 // Old append-only stores can contain several revisions. Index offsets, not
 // whole records, and emit only the last revision for each identity.
 struct Index {uint64_t id;uint32_t offset;};std::vector<Index> index;EventRecord row{};
 while(legacy.available()){
  const uint32_t offset=legacy.position();if(legacy.read(reinterpret_cast<uint8_t*>(&row),sizeof(row))!=sizeof(row)){error="legacy_event_integrity";return false;}
  if(!eventValid(row)){error="legacy_event_integrity";return false;}
  auto found=std::find_if(index.begin(),index.end(),[&](const Index& v){return v.id==row.localId;});
  if(found==index.end())index.push_back({row.localId,offset});else found->offset=offset;
  if(millis()-started>10000){error="query_deadline";return false;}NetworkWorker::instance().maintain();
 }
 for(const auto& entry:index){
  if(std::find(ids.begin(),ids.end(),entry.id)!=ids.end())continue;
  if(!legacy.seek(entry.offset)||legacy.read(reinterpret_cast<uint8_t*>(&row),sizeof(row))!=sizeof(row)){error="legacy_event_integrity";return false;}
  if(!eventValid(row)){error="legacy_event_integrity";return false;}*record={};record->event=row;record->details.localId=row.localId;
  detailPreview(record->details.location,sizeof(record->details.location),row.location);detailPreview(record->details.notes,sizeof(record->details.notes),row.notes);record->seal();
  if(!emit(*record))return true;NetworkWorker::instance().maintain();
 }
 return true;
}
}

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
namespace disk=std::filesystem;
class String:public std::string{public:using std::string::string;using std::string::operator=;String(const std::string& s):std::string(s){}size_t length()const{return size();}int lastIndexOf(char c)const{auto p=rfind(c);return p==npos?-1:static_cast<int>(p);}String substring(size_t a)const{return substr(a);}};
uint32_t clockMs=1000;uint32_t millis(){return clockMs;}int64_t esp_timer_get_time(){return static_cast<int64_t>(clockMs)*1000;}
size_t strlcpy(char* out,const char* in,size_t n){const size_t len=strlen(in);if(n){const size_t c=std::min(n-1,len);memcpy(out,in,c);out[c]=0;}return len;}
const char* FILE_READ="r",*FILE_WRITE="w",*FILE_APPEND="a";
int writeBudget=-1;
struct FileState{String virtualPath;disk::path actual;std::fstream stream;bool directory=false;std::vector<disk::path> entries;size_t next=0;};
class File{std::shared_ptr<FileState> s;public:
 File()=default;File(const String& path,const char* mode){auto actual=disk::path("card")/std::string(path).substr(1);if(!disk::exists(actual)&&strcmp(mode,"r")==0)return;s=std::make_shared<FileState>();s->actual=actual;s->virtualPath=path;s->directory=disk::is_directory(actual);if(s->directory){for(auto& e:disk::directory_iterator(actual))s->entries.push_back(e.path());return;}
 auto flags=std::ios::binary;if(strcmp(mode,"r")==0)flags|=std::ios::in;else if(strcmp(mode,"r+")==0)flags|=std::ios::in|std::ios::out;else if(strcmp(mode,"a")==0)flags|=std::ios::out|std::ios::app;else flags|=std::ios::out|std::ios::trunc;s->stream.open(actual,flags);if(!s->stream)s.reset();}
 explicit operator bool()const{return bool(s);}bool isDirectory()const{return s&&s->directory;}String name()const{return s?s->actual.filename().string():String{};}String path()const{return s?s->virtualPath:String{};}
 size_t size(){return s&&!s->directory?disk::file_size(s->actual):0;}size_t position(){return static_cast<size_t>(s->stream.tellg());}
 bool seek(size_t n){s->stream.clear();s->stream.seekg(n);s->stream.seekp(n);return bool(s->stream);}size_t read(uint8_t* out,size_t n){s->stream.read(reinterpret_cast<char*>(out),n);return s->stream.gcount();}
 size_t write(const uint8_t* in,size_t n){size_t bytes=writeBudget<0?n:std::min(n,static_cast<size_t>(writeBudget));if(writeBudget>=0)writeBudget-=bytes;s->stream.write(reinterpret_cast<const char*>(in),bytes);return s->stream?bytes:0;}
 void flush(){if(s&&!s->directory)s->stream.flush();}void close(){s.reset();}
 File openNextFile(){if(!s||s->next>=s->entries.size())return {};auto p=s->entries[s->next++];return File(String("/"+disk::relative(p,"card").generic_string()),"r");}
};
namespace fs{class FS{public:File open(const String& p,const char* m){return File(p,m);}bool exists(const String& p){return disk::exists(disk::path("card")/std::string(p).substr(1));}bool remove(const String& p){return disk::remove(disk::path("card")/std::string(p).substr(1));}bool rename(const String& a,const String& b){std::error_code ec;disk::rename(disk::path("card")/std::string(a).substr(1),disk::path("card")/std::string(b).substr(1),ec);return !ec;}};}
#include "EnergyModel.h"
#include "EnergyDetailCore.h"
namespace hp{
class StorageService{public:bool sd=true,usb=false;fs::FS filesystem;
 struct ArchiveVolume{fs::FS* fs=nullptr;std::unique_ptr<fs::FS> owned;bool usb=false;uint32_t generation=0;};
 bool archiveVolume(ArchiveVolume& v){v.fs=sd||usb?&filesystem:nullptr;v.usb=!sd&&usb;return v.fs;}
 bool archiveParents(ArchiveVolume&,const String& p){disk::create_directories((disk::path("card")/"data"/std::string(p)).parent_path());return sd||usb;}
 bool archiveTruncate(ArchiveVolume&,const String& p,size_t n){return truncateSdUserData(p,n);}
 uint64_t archiveFreeBytes(const ArchiveVolume&){return sdFreeBytes();}
 uint64_t sdFreeBytes(){return 1024*1024*1024;}static StorageService& instance(){static StorageService s;return s;}bool sdAvailable(){return sd;}fs::FS* sdUserDataFs(){return sd?&filesystem:nullptr;}String sdUserDataPath(const String& p){return String("/data/"+std::string(p));}bool ensureSdUserDataParents(const String& p){disk::create_directories((disk::path("card")/"data"/std::string(p)).parent_path());return sd;}bool truncateSdUserData(const String& p,size_t n){std::error_code ec;disk::resize_file(disk::path("card")/"data"/std::string(p),n,ec);return !ec;}};
class TimeService{public:uint32_t now=1770000000;static TimeService& instance(){static TimeService t;return t;}bool valid(){return true;}int64_t nowUtc(){return now;}bool localFromEpoch(int64_t t,tm& out){time_t epoch=t;auto* p=localtime(&epoch);if(!p)return false;out=*p;return true;}};
}
#define private public
#include "EnergyDetailArchive.h"
#undef private
namespace hp{
#include "archive.inc"
}
struct Provider:hp::EnergyHistoryProvider{int calls=0;bool supportsHistoryBackfill()const override{return true;}const char* historySource()const override{return "fixture";}bool fetchHistory(int64_t from,int64_t to,uint16_t,const Visitor& visit)override{++calls;hp::EnergyHistoryPoint stale;stale.timestamp=from-86400;stale.resolutionSeconds=60;stale.values[0]=9999;stale.quality=2;visit(stale);for(int64_t t=from;t<to;t+=60){hp::EnergyHistoryPoint p;p.timestamp=t;p.resolutionSeconds=60;p.values[0]=400;p.quality=2;visit(p);}return true;}};
void timezone(const char* value){
#ifdef _WIN32
 _putenv_s("TZ",value);_tzset();
#else
 setenv("TZ",value,1);tzset();
#endif
}
int main(){using namespace hp;disk::create_directories("card");auto& storage=StorageService::instance();auto& time=TimeService::instance();
 assert(detailInterval(0)==5&&detailInterval(31)==5&&detailInterval(1)==1&&detailInterval(30)==30);assert(detailEstimate(5).pointsPerDay==288);
 assert(detailCrc("123456789",9)==0xcbf43926);assert(detailBucket(1761440400,300)!=detailBucket(1761444000,300)); // repeated local autumn hour keeps distinct UTC keys
 DetailBuffer b;float v[7]={100,NAN,NAN,NAN,10,12,NAN};uint16_t q=2|(2<<8)|(2<<10);const uint32_t base=detailBucket(time.now,300);
 b.add(base+1,v,q,60,5,false);v[0]=300;v[4]=20;v[5]=15;b.add(base+61,v,q,60,5,false);assert(b.count==1&&b.slots[0].record.values[0]==200&&b.slots[0].record.values[4]==20&&b.slots[0].record.values[5]==15&&std::isnan(b.slots[0].record.values[1]));
 b.add(base+61,v,q,60,5,true);const float before=b.slots[0].record.values[0];b.add(base+61,v,q,60,5,true);assert(b.slots[0].record.values[0]==before);v[0]=50;b.add(base+121,v,q,60,5,false);assert(b.slots[0].record.values[0]==before);
 DetailBuffer coarse;coarse.add(base,v,2,900,1,true);assert(coarse.count==1&&coarse.slots[0].record.intervalSeconds==900);for(unsigned i=0;i<120;++i)coarse.add(base+i*1800,v,2,900,1,true);assert(coarse.count==96&&coarse.dropped>0);
 DetailBlockHeader header;detailSeal(header,&b.slots[0].record,1);assert(detailHeaderValid(header));header.payloadCrc^=1;assert(!detailHeaderValid(header));
 EnergyHistoryProvider unsupported;assert(!unsupported.supportsHistoryBackfill());
 auto archive=std::make_unique<EnergyDetailArchive>();archive->maintain(true,5,0,false,nullptr);storage.sd=false;
 EnergySnapshot s;s.online=true;s.timestamp=time.now;s.updatedMs=clockMs;s.gridPower.value=123;s.gridPower.quality=EnergyQuality::Measured;archive->sample(s);clockMs+=1800000;time.now+=1800;archive->maintain(true,5,0,false,nullptr);assert(!disk::exists("card/data")&&archive->buffer.count==1);
 storage.sd=true;archive->maintain(true,5,0,false,nullptr);assert(archive->status.flushes==1&&archive->buffer.count==0);const String relative=archive->dayPath(s.timestamp);auto actual=disk::path("card/data")/std::string(relative);const auto initial=disk::file_size(actual);assert(initial==8+16+40);
 size_t exported=0;assert(archive->visit(s.timestamp-600,time.now,[&](const DetailRecord& r){++exported;assert(r.values[0]==123);return true;}));assert(exported==1);
 // Interrupt an existing-block quality patch after publishing its redo journal.
 assert(archive->scan(storage.filesystem,relative,true));archive->scratch_[0].values[0]=321;detailSeal(header,archive->scratch_,1);writeBudget=sizeof(DetailJournalHeader)+sizeof(DetailRecord)+sizeof(DetailBlockHeader)+2;
 assert(!archive->writeBlock(storage.filesystem,relative,8,header));writeBudget=-1;
 auto reboot=std::make_unique<EnergyDetailArchive>();assert(reboot->scan(storage.filesystem,relative,true));assert(reboot->scratch_[0].values[0]==321&&reboot->status.recoveredBlocks==1);
 {std::ofstream tail(actual,std::ios::binary|std::ios::app);tail<<"interrupted";}assert(archive->scan(storage.filesystem,relative,true));assert(disk::file_size(actual)==initial&&archive->status.recoveredBlocks==1);
 // Corrupt payload: recovery keeps the valid prefix, never formats the card.
 {std::fstream corrupt(actual,std::ios::binary|std::ios::in|std::ios::out);corrupt.seekp(28);corrupt.put('x');}assert(archive->scan(storage.filesystem,relative,true));assert(disk::file_size(actual)==8);
 Provider provider;clockMs+=1800000;time.now+=1800;archive->maintain(true,5,0,true,&provider);assert(provider.calls==1&&archive->status.lastBackfill==time.now);
 exported=0;assert(archive->visit(time.now-2400,time.now,[&](const DetailRecord& r){++exported;assert(r.sourceSeconds==60&&r.values[0]==400);return true;}));assert(exported>0&&exported<=9);
 // Repeated overlapping backfill upgrades/deduplicates existing records.
 clockMs+=1800000;archive->maintain(true,5,0,true,&provider);size_t again=0;assert(archive->visit(time.now-2400,time.now,[&](const DetailRecord&){++again;return true;}));assert(again==exported);
 auto failure=std::make_unique<EnergyDetailArchive>();failure->maintain(true,1,0,false,nullptr);failure->buffer.add(time.now-120,v,2,60,1,false);clockMs+=1800000;writeBudget=2;failure->maintain(true,1,0,false,nullptr);assert(failure->status.writeErrors&&failure->buffer.count);writeBudget=-1;clockMs+=1800000;failure->maintain(true,1,0,false,nullptr);assert(!failure->buffer.count);
 // Provider returning stale points cannot create records outside its actual time range.
 DetailBuffer upgrade;v[0]=100;upgrade.add(base,v,1,300,5,true);v[0]=200;upgrade.add(base,v,2,60,5,true);assert(upgrade.count==1&&upgrade.slots[0].record.values[0]==200&&upgrade.slots[0].record.sourceSeconds==60);
 const auto precisionBase=detailBucket(base,3600);
 DetailBuffer precision;v[0]=123;precision.add(precisionBase,v,1,60,1,false);v[0]=999;precision.add(precisionBase,v,2,3600,1,true);
 assert(precision.slots[0].record.values[0]==123&&precision.slots[0].record.sourceSeconds==60);
 DetailRecord localMinute;localMinute.timestamp=base;localMinute.values[0]=123;localMinute.sourceSeconds=localMinute.intervalSeconds=60;localMinute.quality=1;
 DetailRecord coarseImport=localMinute;coarseImport.values[0]=999;coarseImport.sourceSeconds=coarseImport.intervalSeconds=3600;coarseImport.quality=2;coarseImport.historyFields=1;
 assert(!detailMerge(localMinute,coarseImport)&&localMinute.values[0]==123&&localMinute.intervalSeconds==60);
 DetailRecord localQuarter=localMinute;localQuarter.intervalSeconds=900;localQuarter.sourceSeconds=3;
 DetailRecord finer=localMinute;finer.values[0]=456;assert(detailMerge(localQuarter,finer)&&localQuarter.values[0]==456&&localQuarter.intervalSeconds==60);
 // Retention is explicit, only touches owned date files; unrelated data survives.
 disk::create_directories("card/data/energy/detail");{std::ofstream keep("card/data/energy/detail/keep.txt");keep<<"user";}time.now+=3*86400;archive->maintain(true,5,0,false,nullptr);assert(disk::exists(actual));archive->maintain(true,5,1,false,nullptr);clockMs+=1800000;archive->maintain(true,5,1,false,nullptr);assert(!disk::exists(actual)&&disk::exists("card/data/energy/detail/keep.txt"));
 // Native calendar rollover uses local midnight, not an assumed day duration.
 const auto next=nextLocalDay(time.now);tm local{};time.localFromEpoch(next,local);assert(local.tm_hour==0&&local.tm_min==0&&next>time.now);
 timezone("CET-1CEST");bool spring=false,autumn=false;
 for(int day=1;day<=366;++day){tm midnight{};midnight.tm_year=126;midnight.tm_mon=0;midnight.tm_mday=day;midnight.tm_isdst=-1;const time_t start=mktime(&midnight);const auto end=nextLocalDay(static_cast<uint32_t>(start));const auto duration=end-start;if(duration==23*3600)spring=true;if(duration==25*3600)autumn=true;tm endLocal{};time.localFromEpoch(end,endLocal);assert(endLocal.tm_hour==0&&endLocal.tm_min==0);}
 assert(spring&&autumn);timezone("UTC");
 // Two separate day files and ordered export across midnight.
 auto rollover=std::make_unique<EnergyDetailArchive>();rollover->maintain(true,1,0,false,nullptr);tm midnight{};time.localFromEpoch(time.now,midnight);midnight.tm_hour=midnight.tm_min=midnight.tm_sec=0;midnight.tm_isdst=-1;uint32_t boundary=static_cast<uint32_t>(mktime(&midnight))+86400;
 rollover->buffer.add(boundary-60,v,2,60,1,false);rollover->buffer.add(boundary+60,v,2,60,1,false);time.now=boundary+600;clockMs+=1800000;rollover->maintain(true,1,0,false,nullptr);uint32_t previous=0;size_t crossed=0;assert(rollover->visit(boundary-120,boundary+120,[&](const DetailRecord& record){assert(record.timestamp>previous);previous=record.timestamp;++crossed;return true;}));assert(crossed==2);
 storage.sd=false;storage.usb=true;
 auto usbArchive=std::make_unique<EnergyDetailArchive>();usbArchive->maintain(true,1,0,false,nullptr);
 s.timestamp=time.now-60;s.updatedMs=clockMs;s.gridPower.value=333;usbArchive->sample(s);
 clockMs+=1800000;time.now+=1800;usbArchive->maintain(true,1,0,false,nullptr);
 assert(usbArchive->status.usbAvailable&&!usbArchive->status.sdAvailable&&!usbArchive->buffer.count);
 bool usbValue=false;assert(usbArchive->visit(time.now-2400,time.now,[&](const DetailRecord& r){usbValue|=r.values[0]==333;return true;}));assert(usbValue);
 storage.usb=false;usbArchive->maintain(true,1,0,false,nullptr);assert(!usbArchive->status.usbAvailable);
 printf("archive_psram_bytes=%zu\n",sizeof(EnergyDetailArchive));return 0;}

#include "EnergyHistoryImport.h"
#include "EnergyCsvCore.h"
#include "EnergyHistoryService.h"
#include "StorageService.h"
#include "TimeService.h"
#include "NetworkWorker.h"
#include "../apps/lua/LuaStorageBridge.h"
#include "../apps/lua/LuaJobService.h"
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <algorithm>
namespace hp {namespace {
struct Source {String url,secret,path,role;};
bool sourceFor(const String& owner,unsigned index,Source& out){
 if(!index||index>8)return false;
 JsonDocument profiles;if(deserializeJson(profiles,luaStringForNative(owner,"sources_json")))return false;
 auto p=profiles[index-1];if(!p["enabled"].as<bool>()||p["type"].as<String>()!="irtracker")return false;
 String host=p["host"].as<String>();host.trim();while(host.endsWith("/"))host.remove(host.length()-1);
 if(host.startsWith("http://"))host.remove(0,7);else if(host.startsWith("https://"))return false;
 if(!host.length()||host.length()>96)return false;
 for(size_t i=0;i<host.length();++i)if(static_cast<unsigned char>(host[i])<=32||strchr("/?#@",host[i]))return false;
 out.secret="meter"+String(index)+"_login";
 out.role=p["role"].as<String>();
 const String credentials=luaSecretForNative(owner,out.secret);const int colon=credentials.indexOf(':');
 const String identity=owner+"\n"+host+"\n"+out.secret+"\n"+(colon>=0?credentials.substring(0,colon):String());
 uint8_t digest[32];if(mbedtls_sha256(reinterpret_cast<const uint8_t*>(identity.c_str()),identity.length(),digest,0))return false;
 char hash[65];for(unsigned i=0;i<32;++i)snprintf(hash+i*2,3,"%02x",digest[i]);
 out.path=String("/homepanel/data/energy/imports/")+owner+"/"+hash+".csv";
 out.url="http://"+host+"/api/v1/history.csv?range=complete";
 if(!credentials.length())out.secret="";
 return true;
}
// Fixed-size CSV lines, including the last line without a newline. No whole-file buffer.
int line(File& file,char* text,size_t capacity){size_t n=0;while(file.available()){
 const int c=file.read();if(c<0)return -1;if(c=='\n'){text[n]=0;return 1;}if(c=='\r')continue;
 if(n+1>=capacity)return -1;text[n++]=static_cast<char>(c);
 }text[n]=0;return n?1:0;}
struct Merge {
 StorageService::ArchiveVolume& volume;const LuaJobService::Request& request;
 File previous,output;EnergyCsvRecord old;bool haveOld=false,header=false;
 char pending[241]{};size_t used=0;uint32_t lastOld=0,lastNew=0,start=millis();uint64_t bytes=0,rows=0;
 bool valid(String& error){if(!StorageService::instance().archiveVolumeValid(volume)){error="archive_storage_removed";return false;}
  if(request.cancelled&&request.cancelled->load()){error="cancelled";return false;}
  if(uint32_t(millis()-start)>120000){error="archive_timeout";return false;}return true;}
 bool next(String& error){char text[241];const int got=previous?line(previous,text,sizeof(text)):0;
  if(got<0||(got&&(!parseEnergyCsv(text,old)||old.timestamp<=lastOld))){error="archive_existing_invalid";return false;}
  haveOld=got==1;if(haveOld)lastOld=old.timestamp;return true;}
 bool emit(const EnergyCsvRecord& record,String& error){if(!valid(error))return false;
  char text[241];const int n=formatEnergyCsv(record,text,sizeof(text));
  if(n<=0||size_t(n)>=sizeof(text)||bytes+size_t(n)>64*1024*1024){error="archive_file_limit";return false;}
  if(output.write(reinterpret_cast<const uint8_t*>(text),n)!=size_t(n)){error="archive_write";return false;}
  bytes+=n;++rows;if((rows&255)==0){NetworkWorker::instance().maintain();vTaskDelay(1);}return true;}
 bool accept(String& error){pending[used]=0;
  if(!header){header=true;return !strcmp(pending,energyCsvHeader)||(error="archive_csv_header",false);}
  EnergyCsvRecord incoming;if(!parseEnergyCsv(pending,incoming)||incoming.timestamp<=lastNew){error="archive_csv_invalid_or_unsorted";return false;}
  lastNew=incoming.timestamp;
  while(haveOld&&old.timestamp<incoming.timestamp){if(!emit(old,error)||!next(error))return false;}
  if(haveOld&&old.timestamp==incoming.timestamp){incoming=mergeEnergyCsv(old,incoming);if(!next(error))return false;}
  return emit(incoming,error);
 }
 static bool write(void* context,const uint8_t* data,size_t size,String& error){auto& m=*static_cast<Merge*>(context);
  if(!m.valid(error))return false;
  for(size_t i=0;i<size;++i){const char c=data[i];if(c=='\r')continue;
   if(c=='\n'){if(!m.accept(error))return false;m.used=0;}
   else {if(m.used+1>=sizeof(m.pending)){error="archive_csv_line_limit";return false;}m.pending[m.used++]=c;}
  }return true;
 }
 bool finish(String& error){if(used&&!accept(error))return false;if(!header||!lastNew){error="archive_empty";return false;}
  while(haveOld){if(!emit(old,error)||!next(error))return false;}return valid(error);
 }
};
}
bool energyTrackerCredentials(const String& owner,unsigned index,String& host,String& password){
 Source source;if(!sourceFor(owner,index,source)||(source.role!="auto"&&source.role!="grid"&&source.role.length()))return false;
 const String credentials=luaSecretForNative(owner,"meter"+String(index)+"_login");
 const int colon=credentials.indexOf(':');if(credentials.length()&&(colon<0||credentials.substring(0,colon)!="admin"))return false;
 LuaJobService::Request target;target.url=source.url;target.permissions=perm(AppPermission::Network);
 String pinned,hostname;if(!LuaJobService::resolveHttpTarget(target,pinned,hostname))return false;
 host=pinned.substring(7,pinned.indexOf('/',7));password=colon>=0?credentials.substring(colon+1):String();return true;
}
void executeEnergyHistorySync(const LuaJobService::Request& request,LuaJobService::Result& result){
 Source source;if(!sourceFor(request.owner,request.resource,source)){result.error="archive_source_not_configured";return;}
 auto& storage=StorageService::instance();StorageService::ArchiveVolume volume;
 if(!storage.archiveVolume(volume)){result.error="external_storage_required";return;}
 auto& fs=*volume.fs;const String backup=source.path+".bak",part=source.path+".part";
 if(!fs.exists(source.path)&&fs.exists(backup)&&!fs.rename(backup,source.path)){result.error="archive_recovery";return;}
 Merge merge{volume,request};merge.previous=fs.open(source.path,FILE_READ);
 if(merge.previous){char header[241];if(line(merge.previous,header,sizeof(header))!=1||strcmp(header,energyCsvHeader)||!merge.next(result.error)){result.error="archive_existing_invalid";return;}
  const auto now=TimeService::instance().nowUtc(),modified=merge.previous.getLastWrite();
  if(modified>1577836800&&now>=modified&&now-modified<3600){result.ok=true;result.body="{\"cached\":true}";return;}
 }
 if(!storage.archiveParents(volume,source.path.substring(strlen("/homepanel/data/")))){result.error="archive_directory";return;}
 if(fs.exists(part)&&!fs.remove(part)){result.error="archive_partial_remove";return;}
 merge.output=fs.open(part,FILE_WRITE);if(!merge.output){result.error="archive_write";return;}
 const String header=String(energyCsvHeader)+"\n";
 if(merge.output.write(reinterpret_cast<const uint8_t*>(header.c_str()),header.length())!=header.length())result.error="archive_write";
 if(!result.error.length()){
  auto http=request;http.kind=0;http.method=0;http.url=source.url;http.basicSecret=source.secret;http.body="";http.streamMode=1;
  http.timeout=2500;http.deadlineMs=120000;http.nativeWrite=Merge::write;http.nativeWriteContext=&merge;
  LuaJobService::executeHttp(http,result);
 }
 if(result.ok&&!merge.finish(result.error))result.ok=false;
 merge.previous.close();merge.output.flush();merge.output.close();
 if(result.ok&&!merge.valid(result.error))result.ok=false;
 Source current;
 if(result.ok&&(!sourceFor(request.owner,request.resource,current)||current.path!=source.path||current.url!=source.url)){result.ok=false;result.error="archive_source_changed";}
 if(result.ok){std::lock_guard<std::mutex> guard(StorageService::mutationMutex());
  if(!storage.archiveVolumeValid(volume))result.error="archive_storage_removed";
  else if(fs.exists(backup)&&!fs.remove(backup))result.error="archive_backup_remove";
  else if(fs.exists(source.path)&&!fs.rename(source.path,backup))result.error="archive_backup";
  else if(!fs.rename(part,source.path)){if(fs.exists(backup))fs.rename(backup,source.path);result.error="archive_commit";}
  else {fs.remove(backup);storage.notifyMutation();}
 }
 result.ok=result.ok&&!result.error.length();
 if(!result.ok){if(storage.archiveVolumeValid(volume))fs.remove(part);return;}
 JsonDocument doc;doc["rows"]=merge.rows;doc["bytes"]=merge.bytes;doc["storage"]=volume.usb?"USB":"SD";doc["cached"]=false;
 serializeJson(doc,result.body);result.size=merge.bytes;
}
void executeEnergyHistoryQuery(const LuaJobService::Request& request,LuaJobService::Result& result){
 // Existing native cache remains the fallback when no external import exists.
 result.body=EnergyHistoryService::instance().query(request.url,64);result.ok=result.body.length()>0;
 const int64_t now=TimeService::instance().nowUtc();int64_t from=0;
 if(request.url=="hour"||request.url=="live")from=now-3600;
 else if(request.url=="today"){tm t{};if(!TimeService::instance().localFromEpoch(now,t))return;t.tm_hour=t.tm_min=t.tm_sec=0;t.tm_isdst=-1;from=mktime(&t);}
 else if(request.url=="week")from=now-7*86400;else if(request.url=="month")from=now-31*86400;else if(request.url=="year")from=now-366*86400;else return;
 StorageService::ArchiveVolume volume;if(now<=from||!StorageService::instance().archiveVolume(volume))return;
 for(unsigned sourceIndex=1;sourceIndex<=8;++sourceIndex){Source source;if(!sourceFor(request.owner,sourceIndex,source)||(source.role!="grid"&&source.role!="auto"&&source.role.length()))continue;
  File file=volume.fs->open(source.path,FILE_READ);if(!file)continue;
  char text[241];if(line(file,text,sizeof(text))!=1||strcmp(text,energyCsvHeader))continue;
  double sum[64]{};uint32_t weights[64]{},resolution[64]{};const uint32_t start=millis();EnergyCsvRecord record;
  uint32_t last=0;bool valid=true;size_t matched=0,scanned=0;
  while(file.available()){
   if(uint32_t(millis()-start)>8000||(request.cancelled&&request.cancelled->load())||!StorageService::instance().archiveVolumeValid(volume)){valid=false;break;}
   if(line(file,text,sizeof(text))!=1||!parseEnergyCsv(text,record)||record.timestamp<=last){valid=false;break;}last=record.timestamp;
   if((++scanned&255)==0){NetworkWorker::instance().maintain();vTaskDelay(1);}
   if(record.timestamp<from||record.timestamp>now||!std::isfinite(record.values[0]))continue;
   const size_t slot=std::min<int64_t>(63,(record.timestamp-from)*64/(now-from));
   if(resolution[slot]&&record.resolution>resolution[slot])continue;
   if(!resolution[slot]||record.resolution<resolution[slot]){resolution[slot]=record.resolution;sum[slot]=0;weights[slot]=0;}
   sum[slot]+=record.values[0];++weights[slot];++matched;
  }
  file.close();
  if(!valid||!matched)continue;
  bool local[64]{};
  EnergyHistoryService::instance().visitDetail(static_cast<uint32_t>(from),static_cast<uint32_t>(now),[&](const DetailRecord& record){
   if(uint32_t(millis()-start)>8000||(request.cancelled&&request.cancelled->load()))return false;
   if(record.timestamp<from||record.timestamp>now||!std::isfinite(record.values[0]))return true;
   const size_t slot=std::min<int64_t>(63,(record.timestamp-from)*64/(now-from));
   const uint32_t period=std::max(record.sourceSeconds,record.intervalSeconds);
   if(resolution[slot]&&period>resolution[slot])return true;
   if(!local[slot]||period<resolution[slot]){local[slot]=true;sum[slot]=0;weights[slot]=0;resolution[slot]=period;}
   sum[slot]+=record.values[0];++weights[slot];return true;
  });
  JsonDocument doc;auto array=doc.to<JsonArray>();
  for(unsigned i=0;i<64;++i)if(weights[i]){auto row=array.add<JsonObject>();row["timestamp"]=from+(now-from)*i/64;row["gridPower"]=sum[i]/weights[i];}
  Source current;if(!sourceFor(request.owner,sourceIndex,current)||current.path!=source.path){result.ok=false;result.error="archive_source_changed";return;}
  result.body="";serializeJson(doc,result.body);result.ok=true;return;
 }
}
}

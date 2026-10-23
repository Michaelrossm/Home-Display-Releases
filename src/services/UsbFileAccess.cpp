#include "UsbFileAccess.h"
#include "UsbStorageService.h"
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
namespace hp::usbfiles {
static String full(const String& relative){
 if(!UsbStorageService::instance().available()||relative.length()>240||!relative.startsWith("/")||relative.indexOf("..")>=0||relative.indexOf(':')>=0||relative.indexOf("//")>=0||relative.indexOf('\\')>=0)return {};
 return String(UsbStorageService::mountPath())+relative;
}
bool stat(const String& path,uint64_t& bytes,bool& dir){
 UsbFilesGuard guard;if(!guard)return false;
 const String name=full(path);if(!name.length())return false;
 struct ::stat st{};if(::stat(name.c_str(),&st))return false;
 bytes=st.st_size;dir=S_ISDIR(st.st_mode);return true;
}
bool list(const String& path,std::vector<Entry>& out,const std::atomic<bool>* cancel){
 UsbFilesGuard guard;if(!guard)return false;
 const String name=full(path);if(!name.length())return false;
 DIR* d=::opendir(name.c_str());if(!d)return false;
 const uint32_t gen=UsbStorageService::instance().generation();
 bool ok=true;dirent* e;
 while((e=::readdir(d))){
  if((cancel&&cancel->load())||!UsbStorageService::instance().available()||UsbStorageService::instance().generation()!=gen){ok=false;break;}
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  const String child=path+(path.endsWith("/")?"":"/")+e->d_name;
  uint64_t bytes=0;bool isdir=false;
  if(stat(child,bytes,isdir))out.push_back({String(e->d_name),bytes,isdir});
  if(out.size()>=1024)break;
 }
 ::closedir(d);return ok;
}
bool read(const String& path,uint64_t offset,uint8_t* dst,size_t cap,size_t& got,uint64_t& total,const std::atomic<bool>* cancel){
 UsbFilesGuard guard;if(!guard){got=total=0;return false;}
 got=0;total=0;const String name=full(path);if(!name.length()||!dst||cap>8*1024*1024)return false;
 const uint32_t gen=UsbStorageService::instance().generation();
 FILE* f=::fopen(name.c_str(),"rb");if(!f)return false;
 bool ok=false;
 do{
  if(::fseeko(f,0,SEEK_END)!=0)break;
  const off_t size=::ftello(f);if(size<0||offset>uint64_t(size))break;
  total=uint64_t(size);if(::fseeko(f,static_cast<off_t>(offset),SEEK_SET)!=0)break;
  const size_t want=static_cast<size_t>(std::min<uint64_t>(cap,total-offset));
  while(got<want){if((cancel&&cancel->load())||!UsbStorageService::instance().available()||UsbStorageService::instance().generation()!=gen)break;
   const size_t n=::fread(dst+got,1,std::min<size_t>(8192,want-got),f);if(!n)break;got+=n;}
  ok=got==want;
 }while(false);
 ::fclose(f);return ok;
}
}

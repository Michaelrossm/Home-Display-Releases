#include <string>
#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cassert>
struct String:std::string {
 using std::string::string;using std::string::operator=;
 String(const std::string& s):std::string(s){}
 String(const char* s,size_t n):std::string(s,n){}
 bool concat(const char* s,size_t n){append(s,n);return true;}
 int indexOf(char p)const{size_t n=find(p);return n==npos?-1:int(n);}
 size_t length()const{return size();}
 bool startsWith(const char* p)const{return rfind(p,0)==0;}
 bool endsWith(const char* p)const{size_t n=strlen(p);return size()>=n&&compare(size()-n,n,p)==0;}
 int indexOf(const char* p)const{size_t n=find(p);return n==npos?-1:int(n);}
};
size_t strlcpy(char* to,const char* from,size_t size){size_t n=strlen(from);if(size){memcpy(to,from,std::min(n,size-1));to[std::min(n,size-1)]=0;}return n;}
using portMUX_TYPE=int;
using SemaphoreHandle_t=void*;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
const char* FILE_READ="r";const char* FILE_WRITE="w";
uint32_t now=1;uint32_t millis(){return now;}
std::map<String,std::shared_ptr<std::vector<uint8_t>>> disk;
bool failWrite=false,failCommit=false;
class File {
 String path_;std::shared_ptr<std::vector<uint8_t>> data_;std::vector<String> children_;size_t cursor_=0;bool open_=false,directory_=false;
public:
 File()=default;
 explicit File(const String& path,const char* mode=FILE_READ):path_(path){
  if(mode==FILE_WRITE){disk[path]=std::make_shared<std::vector<uint8_t>>();data_=disk[path];open_=true;return;}
  auto it=disk.find(path);if(it!=disk.end()){data_=it->second;open_=true;return;}
  for(const auto& item:disk){const std::string prefix=path+"/";if(item.first.rfind(prefix,0)==0){
    const auto slash=item.first.find('/',prefix.size());String child=slash==std::string::npos?item.first:String(item.first.substr(0,slash));
    if(std::find(children_.begin(),children_.end(),child)==children_.end())children_.push_back(child);}}
  open_=directory_=!children_.empty();
 }
 explicit operator bool()const{return open_;}
 bool isDirectory()const{return directory_;}
 size_t size()const{return data_?data_->size():0;}
 const char* path()const{return path_.c_str();}
 File openNextFile(){return cursor_<children_.size()?File(children_[cursor_++]):File();}
 void close(){open_=false;}
 void flush(){}
 bool seek(size_t n){if(n>size())return false;cursor_=n;return true;}
 size_t write(const uint8_t* p,size_t n){if(!open_||!data_||failWrite){failWrite=false;return 0;}data_->insert(data_->end(),p,p+n);return n;}
 size_t read(uint8_t* p,size_t n){if(!open_||!data_)return 0;n=std::min(n,data_->size()-cursor_);memcpy(p,data_->data()+cursor_,n);cursor_+=n;return n;}
};
namespace fs {struct FS {File open(const String& p,const char* mode=FILE_READ){return File(p,mode);}};}
namespace hp {
enum class StorageClass{UserData};enum class StorageArea{UserData};
struct SecurityService {bool locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}};
struct StorageService {
 bool sd=true;fs::FS fs;
 static StorageService& instance(){static StorageService s;return s;}
 bool usesSd(StorageClass){return sd;}fs::FS* fsFor(StorageClass){return &fs;}
 template<class T>String pathFor(T,const String& p){return "/"+p;}
 bool remove(StorageArea,const String& p){disk.erase("/"+p);return true;}
 bool exists(StorageArea,const String& p){return disk.count("/"+p)>0;}
 bool rename(StorageArea,const String& a,const String& b){if(failCommit&&a.endsWith(".stream.part")){failCommit=false;return false;}
  auto it=disk.find("/"+a);if(it==disk.end())return false;disk["/"+b]=it->second;disk.erase(it);return true;}
 void recoverAtomicWrite(StorageArea,const String& p){if(!exists(StorageArea::UserData,p)&&exists(StorageArea::UserData,p+".bak"))rename(StorageArea::UserData,p+".bak",p);}
 bool ensureParentDirs(StorageArea,const String&){return true;}
};
struct NetworkWorker {using Fn=void(*)(void*);Fn hook=nullptr;void* context=nullptr;
 unsigned networkActivity=0;
 void beginNetworkActivity(){++networkActivity;}
 void endNetworkActivity(){assert(networkActivity);--networkActivity;}
 bool isWorkerContext()const{return true;}
 static NetworkWorker& instance(){static NetworkWorker w;return w;}bool addMaintenance(Fn fn,void* ctx){hook=fn;context=ctx;return true;}
 void maintain(){if(hook)hook(context);}
};
}

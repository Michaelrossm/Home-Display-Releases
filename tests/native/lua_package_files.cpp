#include <cassert>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "../../src/apps/lua/LuaImagePolicy.h"
#include "../../src/services/ImageCodec.h"
#include <memory>
#include <cstdlib>
using namespace hp;
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t size,int){return malloc(size);}
bool ImageCodec::dimensions(const uint8_t* bytes,size_t size,uint32_t& w,uint32_t& h){return LuaImagePolicy::pngSize(bytes,size,w,h);}
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& x):std::string(x){}
    int lastIndexOf(char c)const{const auto n=rfind(c);return n==npos?-1:static_cast<int>(n);}
    String substring(size_t n)const{return substr(n);}
    bool endsWith(const char* suffix)const{const size_t n=strlen(suffix);return size()>=n&&compare(size()-n,n,suffix)==0;}
    void remove(size_t n){erase(n);}
};
enum {FILE_READ,FILE_WRITE};
void delay(int){}
namespace fs {class FS;}
int openFiles=0,maxOpenFiles=100;
class File {
    fs::FS* fs_=nullptr;String path_;size_t pos_=0;bool dir_=false;
    std::shared_ptr<int> lease_;
public:
    File()=default;
    File(fs::FS* fs,const String& p,bool dir):fs_(fs),path_(p),dir_(dir){
        if(openFiles>=maxOpenFiles){fs_=nullptr;return;}++openFiles;
        lease_=std::shared_ptr<int>(new int(0),[](int* value){delete value;--openFiles;});}
    operator bool()const{return fs_!=nullptr;}
    bool isDirectory()const{return dir_;}
    const char* name()const{return path_.c_str();}
    size_t size()const;File openNextFile();
    void close(){fs_=nullptr;lease_.reset();}
    void flush(){}
    bool available();size_t read(uint8_t*,size_t);size_t write(const uint8_t*,size_t);
};
namespace fs {
class FS {
public:
    std::map<String,std::string> files;
    std::set<String> dirs;
    String failWrite,failRead,corruptWrite;
    bool exists(const String& p){return files.count(p)||dirs.count(p);}
    bool mkdir(const String& p){dirs.insert(p);return true;}
    bool remove(const String& p){return files.erase(p)>0;}
    bool rmdir(const String& p){for(const auto& file:files)if(file.first.compare(0,p.size()+1,p+"/")==0)return false;return dirs.erase(p)>0;}
    File open(const String& p,int mode){
        if(dirs.count(p))return File(this,p,true);
        if(mode==FILE_WRITE){files[p]="";return File(this,p,false);}
        return files.count(p)?File(this,p,false):File();
    }
};
}
size_t File::size()const{return fs_&&!dir_?fs_->files.at(path_).size():0;}
File File::openNextFile(){if(!fs_||!dir_)return {};std::vector<String> entries;const String prefix=path_+"/";
    for(const auto& file:fs_->files)if(file.first.compare(0,prefix.size(),prefix)==0&&file.first.find('/',prefix.size())==String::npos)entries.push_back(file.first);
    if(pos_>=entries.size())return {};return File(fs_,entries[pos_++],false);}
bool File::available(){return fs_&&path_!=fs_->failRead&&pos_<fs_->files[path_].size();}
size_t File::read(uint8_t* out,size_t n){
    if(path_==fs_->failRead)return 0;
    auto& data=fs_->files[path_];n=std::min(n,data.size()-pos_);memcpy(out,data.data()+pos_,n);pos_+=n;return n;
}
size_t File::write(const uint8_t* data,size_t n){
    if(path_==fs_->failWrite)return 0;fs_->files[path_].append((const char*)data,n);
    if(n&&path_==fs_->corruptWrite)fs_->files[path_].back()^=1;
    return n;
}
// The test runner extracts these exact helpers from the production installer.
#include "package_helpers.inc"
#include "../../src/infrastructure/AppEnabledState.h"
#include <mutex>
namespace hp {
class ConfigService {
public:
    enum class AppStateWriteResult { Ok, NotWritable, InvalidIdentity, InvalidRecord, WriteFailed };
    std::map<std::string,String> strings;std::map<std::string,bool> legacy;
    bool ready=true,failWrite=false;
    bool writable()const{return ready;}
    static bool validNvsKey(const char*);
    String getString(const char* key)const{auto it=strings.find(key);return it==strings.end()?String():it->second;}
    bool has(const char* key)const{assert(strlen(key)<=15);return legacy.count(key)||strings.count(key);}
    bool getBool(const char* key,bool fallback)const{auto it=legacy.find(key);return it==legacy.end()?fallback:it->second;}
    bool putString(const char* key,const String& value){if(failWrite||strlen(key)>15)return false;strings[key]=value;return true;}
    bool remove(const char* key){legacy.erase(key);return true;}
    bool appEnabled(const char*,bool)const;
    AppStateWriteResult saveAppEnabled(const char*,bool);
    bool removeAppEnabled(const char*);
};
#include "config_helpers.inc"
}
int main(){
    hp::ConfigService config;using Result=hp::ConfigService::AppStateWriteResult;
    assert(config.validNvsKey("123456789012345"));assert(!config.validNvsKey("1234567890123456"));
    assert(!config.validNvsKey(nullptr)&&!config.validNvsKey(""));
    config.legacy["napp_radio"]=false;assert(!config.appEnabled("radio",true));
    assert(config.saveAppEnabled("printer_control",true)==Result::Ok);
    assert(!config.appEnabled("radio",true)&&config.appEnabled("printer_control",false));
    assert(config.saveAppEnabled("radio",false)==Result::Ok); // migrate short legacy ID
    assert(config.saveAppEnabled("printer_control_extended_name",false)==Result::Ok);
    config.failWrite=true;assert(config.saveAppEnabled("printer_control",false)==Result::WriteFailed);
    assert(config.appEnabled("printer_control",false));config.failWrite=false;
    config.ready=false;assert(config.saveAppEnabled("new_app",true)==Result::NotWritable);config.ready=true;
    assert(config.saveAppEnabled("bad/id",true)==Result::InvalidIdentity);
    assert(config.removeAppEnabled("printer_control"));assert(!config.appEnabled("printer_control",false));
    assert(!config.appEnabled("radio",true)); // no other owner overwritten
    assert(config.saveAppEnabled("printer_control",true)==Result::Ok); // reinstall
    config.strings[hp::AppEnabledState::key]="v1\nradio=0\nradio=1\n";
    assert(config.saveAppEnabled("radio",true)==Result::InvalidRecord);
    hp::AppEnabledState enabled;
    static_assert(sizeof("app_enabled_v1")-1<=15);
    assert(enabled.decode(""));enabled.values["radio"]=false;
    enabled.values["printer_control"]=true;
    enabled.values["printer_control_extended_name"]=false;
    enabled.values[std::string(31,'a')]=true;
    enabled.values["printer_control_x"]=false; // same prefix, distinct identity
    hp::AppEnabledState restored;assert(restored.decode(enabled.encode()));
    assert(!restored.values.at("radio")&&restored.values.at("printer_control"));
    restored.values["printer_control"]=false;restored.values["new_app"]=true;
    assert(enabled.decode(restored.encode()));assert(!enabled.values.at("radio"));
    assert(!enabled.values.at("printer_control")&&enabled.values.at("new_app"));
    assert(!enabled.decode("v1\nradio=0\nradio=1\n")); // duplicate identity fails closed
    assert(!enabled.decode("v1\nradio=2\n"));
    assert(!enabled.decode("v1\nradio=0")); // truncated/corrupted record
    fs::FS fs;
    fs.dirs.insert("/live");fs.files["/live/app.json"]="old manifest";fs.files["/live/main.lua"]="old UI";
    assert(copyPackageFiles(fs,"/live","/backup")); // calculator without settings
    assert(!fs.exists("/backup/settings.lua"));
    fs.files["/live/settings.lua"]="old settings";
    fs.dirs.insert("/live/assets");fs.files["/live/assets/logo.png"]="old pixels";
    assert(copyPackageFiles(fs,"/live","/backup"));
    assert(fs.files["/backup/assets/logo.png"]=="old pixels");
    assert(fs.files["/backup/settings.lua"]=="old settings");
    // Realistic constrained file pool: directory + source + destination would
    // exceed two handles. Snapshotting/closing the iterator must avoid that.
    maxOpenFiles=2;assert(copyPackageFiles(fs,"/live","/backup"));assert(openFiles==0);maxOpenFiles=100;
    fs.failRead="/live/main.lua";assert(!copyFsFile(fs,"/live/main.lua","/short_copy"));
    assert(!fs.exists("/short_copy")&&fs.files["/live/main.lua"]=="old UI");fs.failRead="";
    assert(!copyFsFile(fs,"/live/main.lua","/live/main.lua"));assert(fs.files["/live/main.lua"]=="old UI");
    String copyError;fs.corruptWrite="/corrupt_copy";
    assert(!copyFsFile(fs,"/live/main.lua","/corrupt_copy",&copyError));
    assert(copyError=="copy_verify_failed:/corrupt_copy");
    assert(!fs.exists("/corrupt_copy")&&fs.files["/live/main.lua"]=="old UI");fs.corruptWrite="";
    fs.dirs.insert("/stage");fs.files["/stage/app.json"]="new manifest";fs.files["/stage/main.lua"]="new UI";
    fs.files["/stage/settings.lua"]="new settings";
    fs.dirs.insert("/stage/assets");fs.files["/stage/assets/logo.png"]="new pixels";
    assert(copyPackageFiles(fs,"/stage","/live"));assert(fs.files["/live/settings.lua"]=="new settings");
    assert(fs.files["/live/assets/logo.png"]=="new pixels");
    // An interrupted update must restore ALL previous package members.
    fs.failWrite="/live/settings.lua";assert(!copyPackageFiles(fs,"/stage","/live"));fs.failWrite="";
    clearPackageFiles(fs,"/live");assert(copyPackageFiles(fs,"/backup","/live"));
    assert(fs.files["/live/main.lua"]=="old UI"&&fs.files["/live/settings.lua"]=="old settings");
    assert(fs.files["/live/assets/logo.png"]=="old pixels");
    fs.failWrite="/live/assets/logo.png";assert(!copyPackageFiles(fs,"/stage","/live"));fs.failWrite="";
    assert(copyPackageFiles(fs,"/backup","/live"));assert(fs.files["/live/assets/logo.png"]=="old pixels");
    assert(!validatePackageAssets(fs,"/stage")); // malformed PNG rejected before activation
    const uint8_t png[33]={137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0,8,0,0,0,8};
    fs.files["/stage/assets/logo.png"]=std::string(reinterpret_cast<const char*>(png),sizeof(png));
    assert(validatePackageAssets(fs,"/stage"));
    // Reinstall/downgrade to a package without settings removes stale settings.
    fs.files.erase("/stage/settings.lua");assert(copyPackageFiles(fs,"/stage","/live"));
    assert(!fs.exists("/live/settings.lua"));
    fs.files["/live/settings.lua.part"]="partial";clearPackageFiles(fs,"/live");
    assert(!fs.exists("/live/main.lua")&&!fs.exists("/live/settings.lua.part"));
    assert(!fs.exists("/live/assets/logo.png"));
}

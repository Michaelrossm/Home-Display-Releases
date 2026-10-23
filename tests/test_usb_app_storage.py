from pathlib import Path
import unittest
import test_idle_usb_regressions as native_tests

ROOT = Path(__file__).resolve().parents[1]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


class UsbAppStorageTests(unittest.TestCase):
    def test_preference_migrates_legacy_sd_and_never_falls_back_from_usb(self):
        code=source("src/services/StorageService.cpp")
        methods=code[code.index("StorageClass StorageService::newAppStorage()"):code.index("namespace {",code.index("StorageClass StorageService::newAppStorage()"))]
        fake=r'''
#include <string>
#include <cassert>
using String=std::string;
namespace hp {
enum class StorageClass {AppPackage,Media,UsbAppPackage,UserData};
struct ConfigService {String value;bool legacy=false,writable=true;static ConfigService& instance(){static ConfigService c;return c;}
String getString(const char*,const char*)const{return value;}bool getBool(const char*,bool)const{return legacy;}
bool putString(const char* key,const String& v){assert(String(key)=="app_store");if(!writable)return false;value=v;return true;}};
struct StorageService {StorageClass newAppStorage()const;bool setNewAppStorage(StorageClass);};
'''
        body=r'''
}
int main(){using namespace hp;StorageService st;auto& config=ConfigService::instance();
assert(st.newAppStorage()==StorageClass::AppPackage);config.legacy=true;assert(st.newAppStorage()==StorageClass::Media);
assert(st.setNewAppStorage(StorageClass::UsbAppPackage));assert(st.newAppStorage()==StorageClass::UsbAppPackage);
config.legacy=false;assert(st.newAppStorage()==StorageClass::UsbAppPackage);
assert(!st.setNewAppStorage(StorageClass::UserData)&&config.value=="usb");
config.writable=false;assert(!st.setNewAppStorage(StorageClass::Media)&&config.value=="usb");
}
'''
        native_tests.IdleUsbRegressions().native(fake+methods+body)

    def test_real_usb_fs_handle_lifetime_and_volume_binding(self):
        fake = r'''
#include <memory>
#include <string>
#include <cstdint>
#include <cassert>
#include <ctime>
using boolean=bool;enum SeekMode {SeekSet};
struct String:std::string {using std::string::string;};
namespace fs {
struct FileImpl;struct FSImpl;using FileImplPtr=std::shared_ptr<FileImpl>;using FSImplPtr=std::shared_ptr<FSImpl>;
struct FileImpl {virtual ~FileImpl()=default;virtual size_t write(const uint8_t*,size_t)=0;virtual size_t read(uint8_t*,size_t)=0;
virtual void flush()=0;virtual bool seek(uint32_t,SeekMode)=0;virtual size_t position()const=0;virtual size_t size()const=0;
virtual bool setBufferSize(size_t)=0;virtual void close()=0;virtual time_t getLastWrite()=0;virtual const char* path()const=0;
virtual const char* name()const=0;virtual boolean isDirectory()=0;virtual FileImplPtr openNextFile(const char*)=0;
virtual boolean seekDir(long)=0;virtual String getNextFileName()=0;virtual String getNextFileName(bool*)=0;
virtual void rewindDirectory()=0;virtual operator bool()=0;};
struct FSImpl {virtual ~FSImpl()=default;void mountpoint(const char*){}
virtual FileImplPtr open(const char*,const char*,bool)=0;virtual bool exists(const char*)=0;
virtual bool rename(const char*,const char*)=0;virtual bool remove(const char*)=0;virtual bool mkdir(const char*)=0;virtual bool rmdir(const char*)=0;};
struct FS {FSImplPtr impl;explicit FS(FSImplPtr p):impl(p){}FileImplPtr open(const char*p,const char*m="r",bool c=false){return impl->open(p,m,c);}
bool remove(const char*p){return impl->remove(p);}bool rename(const char*a,const char*b){return impl->rename(a,b);}};
}
using namespace fs;unsigned mutations=0,reads=0,closed=0;
struct VFSImpl:FSImpl {FileImplPtr open(const char*,const char*,bool)override;
bool exists(const char*)override{return true;}bool rename(const char*,const char*)override{++mutations;return true;}
bool remove(const char*)override{++mutations;return true;}bool mkdir(const char*)override{++mutations;return true;}bool rmdir(const char*)override{++mutations;return true;}};
struct VFSFileImpl:FileImpl {VFSImpl* _fs;bool alive=true;bool child=false;String pathname;
VFSFileImpl(VFSImpl*f,const char*p,const char*):_fs(f),pathname(p){}~VFSFileImpl(){close();}
size_t write(const uint8_t*,size_t n)override{++mutations;return n;}size_t read(uint8_t*,size_t n)override{++reads;return n;}
void flush()override{}bool seek(uint32_t,SeekMode)override{return true;}size_t position()const override{return 0;}size_t size()const override{return 1;}
bool setBufferSize(size_t)override{return true;}void close()override{if(alive){alive=false;++closed;}}time_t getLastWrite()override{return 0;}
const char* path()const override{return pathname.c_str();}const char* name()const override{return path();}
boolean isDirectory()override{return pathname=="/dir";}FileImplPtr openNextFile(const char*)override{return {};}
boolean seekDir(long)override{return true;}String getNextFileName()override{return getNextFileName(nullptr);}
String getNextFileName(bool*)override{if(child)return {};child=true;return "/dir/main.lua";}void rewindDirectory()override{child=false;}
operator bool()override{return alive&&pathname!="/missing";}};
FileImplPtr VFSImpl::open(const char*p,const char*m,bool){return std::make_shared<VFSFileImpl>(this,p,m);}
namespace hp {struct StorageService {uint32_t revision=0;static StorageService& instance(){static StorageService s;return s;}void notifyMutation(){++revision;}};
struct UsbStorageService {bool available=true,busy=false;uint32_t generation=10,pins=0;
static UsbStorageService& instance(){static UsbStorageService s;return s;}static const char* mountPath(){return "/homeusb";}
bool pinFiles(uint32_t&g){g=generation;if(!available||busy)return false;++pins;return true;}
void unpinFiles(){assert(pins);--pins;}bool validFiles(uint32_t g)const{return available&&g==generation;}};}
'''
        body = r'''
int main(){using namespace hp;auto& usb=UsbStorageService::instance();
auto fs=usbAppFileSystemFor(10);auto dir=fs->open("/dir");assert(dir&&usb.pins==1);
auto child=dir->openNextFile("r");assert(child&&usb.pins==2);dir->close();dir->close();assert(usb.pins==1);
fs.reset();dir.reset();assert(usb.pins==1);uint8_t data=0;assert(child->read(&data,1)==1);
usb.available=false;assert(child->read(&data,1)==0&&!static_cast<bool>(*child));child.reset();assert(usb.pins==0);
usb.available=true;++usb.generation;auto old=usbAppFileSystemFor(10);assert(!old->open("/file")&&!old->remove("/file")&&!old->rename("/a","/b"));assert(mutations==0&&usb.pins==0);
auto current=usbAppFileSystemFor(11);assert(current->remove("/file")&&mutations==1&&usb.pins==0);
assert(!current->open("/missing")&&usb.pins==0);usb.busy=true;assert(!current->open("/file")&&usb.pins==0);
usb.busy=false;auto file=current->open("/file");auto shared=file;assert(usb.pins==1);file.reset();assert(usb.pins==1);shared.reset();assert(usb.pins==0);
}
'''
        native_tests.IdleUsbRegressions().native(fake + native_tests.stripped("src/services/UsbAppFileSystem.cpp") + body)

    def test_installer_registry_runtime_share_usb_identity(self):
        installer = source("src/apps/lua/LuaPackageInstaller.cpp")
        self.assertIn("storage.newAppStorage()", installer)
        self.assertIn("usbAppFileSystemFor(usbGeneration)", installer)
        self.assertIn("validFiles(targetUsbGeneration_)", installer)
        self.assertIn("duplicate_package_locations", installer)
        registry = source("src/apps/lua/LuaAppRegistry.cpp")
        self.assertIn("item.usbGeneration", registry)
        self.assertIn("unregisterApp(id.c_str())", registry.split("refreshUsbPackages()", 1)[1])
        self.assertIn("refreshUsbPackages()", source("src/ui/UiShell.cpp"))
        for path in ("src/apps/lua/LuaScriptPreparation.cpp", "src/apps/lua/LuaWidgetsBridge.cpp", "src/services/AppWebSettingsService.cpp"):
            self.assertIn("usbAppFileSystemFor(", source(path))
        self.assertIn("UsbApp", source("src/services/LuaPackageService.cpp"))
        self.assertIn('app.usb?"USB"', source("src/ui/StorageUsageUi.cpp"))
        for path in ("src/services/UsbAppFileSystem.cpp", "src/apps/lua/LuaPackageInstaller.cpp"):
            self.assertNotIn("xTaskCreate", source(path))

#include "UsbAppFileSystem.h"
#include "UsbStorageService.h"
#include "StorageService.h"
#include <vfs_api.h>
namespace hp {
namespace {
struct Pin {
 uint32_t generation=0;bool held=UsbStorageService::instance().pinFiles(generation);
 ~Pin(){if(held)UsbStorageService::instance().unpinFiles();}
};
class UsbFile final:public VFSFileImpl {
 FSImplPtr owner_;
 uint32_t generation_;bool held_=true;
 bool valid()const{return held_&&UsbStorageService::instance().validFiles(generation_);}
public:
 UsbFile(VFSImpl* fs,const char* path,const char* mode,uint32_t gen,FSImplPtr owner):VFSFileImpl(fs,path,mode),owner_(std::move(owner)),generation_(gen){}
 ~UsbFile()override{close();}
 void close()override{VFSFileImpl::close();if(held_){held_=false;UsbStorageService::instance().unpinFiles();}}
 size_t read(uint8_t* b,size_t n)override{return valid()?VFSFileImpl::read(b,n):0;}
 size_t write(const uint8_t* b,size_t n)override{return valid()?VFSFileImpl::write(b,n):0;}
 void flush()override{if(valid())VFSFileImpl::flush();}
 bool seek(uint32_t p,SeekMode m)override{return valid()&&VFSFileImpl::seek(p,m);}
 size_t size()const override{return valid()?VFSFileImpl::size():0;}
 size_t position()const override{return valid()?VFSFileImpl::position():0;}
 bool setBufferSize(size_t n)override{return valid()&&VFSFileImpl::setBufferSize(n);}
 time_t getLastWrite()override{return valid()?VFSFileImpl::getLastWrite():0;}
 boolean isDirectory()override{return valid()&&VFSFileImpl::isDirectory();}
 boolean seekDir(long p)override{return valid()&&VFSFileImpl::seekDir(p);}
 String getNextFileName()override{return getNextFileName(nullptr);}
 String getNextFileName(bool* dir)override{return valid()?VFSFileImpl::getNextFileName(dir):String();}
 void rewindDirectory()override{if(valid())VFSFileImpl::rewindDirectory();}
 FileImplPtr openNextFile(const char* mode)override{
  String path=getNextFileName();return path.length()?_fs->open(path.c_str(),mode,false):FileImplPtr{};
 }
 operator bool()override{return valid()&&VFSFileImpl::operator bool();}
};
class UsbFs final:public VFSImpl,public std::enable_shared_from_this<UsbFs> {
 uint32_t generation_;bool bound_;
 bool matches(const Pin& pin)const{return pin.held&&(!bound_||pin.generation==generation_);}
public:
 UsbFs(uint32_t generation=0,bool bound=false):generation_(generation),bound_(bound){mountpoint(UsbStorageService::mountPath());}
 FileImplPtr open(const char* path,const char* mode,bool create)override{
  if(mode&&strcmp(mode,"r"))StorageService::instance().notifyMutation();
  Pin pin;if(!matches(pin)||!path||path[0]!='/')return {};
  // Delegate create-parent semantics to the official Arduino VFS implementation.
  if(create){auto probe=VFSImpl::open(path,mode,true);if(!probe)return {};probe->close();}
  auto file=std::make_shared<UsbFile>(this,path,mode,pin.generation,shared_from_this());pin.held=false;
  if(!*file){file->close();return {};}return file;
 }
 bool exists(const char* p)override{Pin pin;return matches(pin)&&VFSImpl::exists(p);}
 bool rename(const char* a,const char* b)override{StorageService::instance().notifyMutation();Pin pin;return matches(pin)&&VFSImpl::rename(a,b);}
 bool remove(const char* p)override{StorageService::instance().notifyMutation();Pin pin;return matches(pin)&&VFSImpl::remove(p);}
 bool mkdir(const char* p)override{StorageService::instance().notifyMutation();Pin pin;return matches(pin)&&VFSImpl::mkdir(p);}
 bool rmdir(const char* p)override{StorageService::instance().notifyMutation();Pin pin;return matches(pin)&&VFSImpl::rmdir(p);}
};
}
fs::FS& usbAppFileSystem(){static fs::FS fs(std::make_shared<UsbFs>());return fs;}
std::unique_ptr<fs::FS> usbAppFileSystemFor(uint32_t generation){return std::unique_ptr<fs::FS>(new fs::FS(std::make_shared<UsbFs>(generation,true)));}
}

from pathlib import Path
import unittest
import test_idle_usb_regressions

ROOT=Path(__file__).resolve().parents[1]


class ExternalStorageLayoutTests(unittest.TestCase):
    def test_actual_preparation_is_idempotent_and_preserves_collisions(self):
        source=(ROOT/'src/services/StorageService.cpp').read_text(encoding='utf-8')
        method='bool StorageService::prepareExternalDirectories'+source.split('bool StorageService::prepareExternalDirectories',1)[1].split('static bool ensureDirs',1)[0]
        header=(ROOT/'src/services/ExternalStorageLayout.h').read_text().replace('#pragma once','')
        fake=r'''
#include <map>
#include <string>
#include <cassert>
#define ESP_LOGW(...) ((void)0)
constexpr const char* FILE_READ="r";
struct File{bool valid=false,directory=false;operator bool()const{return valid;}bool isDirectory(){return directory;}void close(){}};
namespace fs {struct FS{std::map<std::string,bool> entries;unsigned writes=0;bool writable=true;
 bool exists(const char* p){return entries.count(p);}
 File open(const char* p,const char*){return {exists(p),entries.at(p)};}
 bool mkdir(const char* p){if(!writable)return false;assert(!exists(p));std::string path=p;auto slash=path.rfind('/');if(slash>0)assert(entries.count(path.substr(0,slash))&&entries[path.substr(0,slash)]);entries[p]=true;++writes;return true;}
};}
'''
        body=r'''
namespace hp {struct StorageService{unsigned revisions=0;bool prepareExternalDirectories(fs::FS&);void notifyMutation(){++revisions;}};
'''+method+r'''
}
int main(){using namespace hp;StorageService storage;fs::FS usb;usb.entries["/existing.gcode"]=false;
 assert(storage.prepareExternalDirectories(usb));assert(usb.writes==12&&storage.revisions==1);
 for(const char* p:{"/Apps/32","/GCode","/Firmware","/Bilder","/Musik","/Sprachen","/homepanel/data","/homepanel/media","/homepanel/logs","/homepanel/voice"})assert(usb.entries.at(p));
 assert(!usb.entries.at("/existing.gcode"));assert(storage.prepareExternalDirectories(usb)&&usb.writes==12&&storage.revisions==1);
 fs::FS collision;collision.entries["/Apps"]=false;
 assert(!storage.prepareExternalDirectories(collision)&&collision.writes==0&&!collision.entries.at("/Apps"));
 fs::FS readonly;readonly.writable=false;assert(!storage.prepareExternalDirectories(readonly)&&readonly.writes==0);
 fs::FS picturesCollision;picturesCollision.entries["/Bilder"]=false;
 assert(!storage.prepareExternalDirectories(picturesCollision)&&!picturesCollision.entries.at("/Bilder"));
}
'''
        test_idle_usb_regressions.IdleUsbRegressions().native(fake+header+body)

    def test_mount_and_explicit_format_reuse_central_layout(self):
        config=(ROOT/'sdkconfig.defaults').read_text(encoding='utf-8')
        for setting in ('CONFIG_FATFS_LFN_HEAP=y','CONFIG_FATFS_MAX_LFN=255','CONFIG_FATFS_API_ENCODING_UTF_8=y'):
            self.assertIn(setting,config)
        usb=(ROOT/'src/services/UsbStorageService.cpp').read_text(encoding='utf-8')
        self.assertIn('prepareExternalDirectories(usbAppFileSystem())',usb)
        helper=usb.split('void UsbStorageService::prepareDirectories()',1)[1].split('bool UsbStorageService::acquireFiles()',1)[0]
        self.assertIn('UsbFilesGuard guard',helper)
        attach=usb.split('void UsbStorageService::attach(',1)[1].split('void UsbStorageService::detach(',1)[0]
        self.assertLess(attach.index('mounted_.store(true'),attach.index('prepareDirectories()'))
        self.assertIn('.format_if_mount_failed=false',attach)
        sd=(ROOT/'src/services/StorageService.cpp').read_text(encoding='utf-8')
        self.assertEqual(sd.count('prepareExternalDirectories(SD_MMC)'),2)
        self.assertIn('if(available())prepareDirectories()',usb)
        web=(ROOT/'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        self.assertIn('browserMount(areaName, fs, areaLabel)',web)
        self.assertIn('const String absolute = rel;',web)

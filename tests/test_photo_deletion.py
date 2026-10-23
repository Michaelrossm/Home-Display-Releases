from pathlib import Path
import unittest
import test_printer_service as printer

ROOT=Path(__file__).resolve().parents[1]

class PhotoDeletionTests(unittest.TestCase):
    def test_actual_worker_remove_permissions_revision_media_and_failures(self):
        source=(ROOT/'src/services/FileBrowserService.cpp').read_text(encoding='utf-8')
        method='bool FileBrowserService::removePhoto'+source.split('bool FileBrowserService::removePhoto',1)[1].split('File FileBrowserService::openRead',1)[0]
        fake=r'''
#include <mutex>
bool worker=true,locked=false,allowed=true,readOnly=false,missing=false,directory=false;unsigned deleted=0;String removed;
struct File{operator bool()const{return !missing;}bool isDirectory(){return directory;}void close(){}};
constexpr const char* FILE_READ="r";
namespace fs {struct FS{File open(const String&,const char*){return {};}bool remove(const String& path){if(readOnly)return false;++deleted;removed=path;return true;}};}
namespace hp {
DirectoryPageCache::Revision media{1,7,0};DirectoryPageCache::Revision pageRevision(){return media;}
struct SecurityWriteGuard{};struct SecurityService{static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}bool settingsAllowed(){return allowed;}};
struct NetworkWorker{static NetworkWorker& instance(){static NetworkWorker w;return w;}bool isWorkerContext(){return worker;}};
struct StorageService{static StorageService& instance(){static StorageService s;return s;}static std::mutex& mutationMutex(){static std::mutex m;return m;}fs::FS* sdUserDataFs(){static fs::FS fs;return &fs;}void notifyMutation(){++media.storage;}};
std::unique_ptr<fs::FS> usbAppFileSystemFor(uint32_t generation){return generation==media.usb?std::make_unique<fs::FS>():nullptr;}
struct ImageCodec{static bool supported(const char* path){return String(path).endsWith(".jpg");}};
struct Location{String path;bool sd=false,usb=false;};bool location(const String& uri,Location& out){if(uri.indexOf("..")>=0)return false;out.sd=uri.startsWith("sd:");out.usb=uri.startsWith("usb:");const int colon=uri.indexOf(':');if(colon<0)return false;out.path=uri.substring(colon+1);return true;}
struct FileBrowserService{static bool removePhoto(const String&,DirectoryPageCache::Revision,const std::atomic<bool>*,String&);};
'''
        body=r'''
}
int main(){using namespace hp;String error;std::atomic<bool> cancel{false};auto expected=media;
 worker=false;assert(!FileBrowserService::removePhoto("sd:/Bilder/a.jpg",expected,&cancel,error));worker=true;
 for(const char* uri:{"data:/Bilder/a.jpg","nas1:/Bilder/a.jpg","usb:/Apps/a.jpg","sd:/Bilder/../a.jpg","sd:/Bilder/a.lua"})assert(!FileBrowserService::removePhoto(uri,expected,&cancel,error));assert(!deleted);
 locked=true;assert(!FileBrowserService::removePhoto("sd:/Bilder/a.jpg",expected,&cancel,error));locked=false;allowed=false;assert(!FileBrowserService::removePhoto("sd:/Bilder/a.jpg",expected,&cancel,error));allowed=true;
 ++media.usb;assert(!FileBrowserService::removePhoto("usb:/Bilder/a.jpg",expected,&cancel,error));expected=media;
 readOnly=true;assert(!FileBrowserService::removePhoto("usb:/Bilder/a.jpg",expected,&cancel,error));readOnly=false;
 missing=true;assert(!FileBrowserService::removePhoto("sd:/Bilder/a.jpg",expected,&cancel,error));missing=false;directory=true;assert(!FileBrowserService::removePhoto("sd:/Bilder/a.jpg",expected,&cancel,error));directory=false;
 cancel=true;assert(!FileBrowserService::removePhoto("sd:/Bilder/a.jpg",expected,&cancel,error));cancel=false;
 assert(!deleted&&FileBrowserService::removePhoto("usb:/Bilder/a.jpg",expected,&cancel,error)&&deleted==1&&removed=="/Bilder/a.jpg");
 assert(!FileBrowserService::removePhoto("usb:/Bilder/a.jpg",expected,&cancel,error)&&deleted==1);
}
'''
        printer.PrinterServiceTests().native(printer.stripped('src/core/DirectoryPageCache.h')+fake+method+body)

    def test_confirmation_is_separate_from_queued_delete(self):
        app=(ROOT/'src/apps/photos/PhotoApp.cpp').read_text(encoding='utf-8')
        dialog=app.split('void showDelete(',1)[1].split('void scanLibrary',1)[0]
        self.assertIn('Bild wirklich löschen?',dialog)
        self.assertIn('"Abbrechen",29',dialog)
        self.assertIn('"Löschen",30',dialog)
        self.assertNotIn('removePhoto(',dialog)
        self.assertIn('p.displayedUri!=p.imageUri',dialog)
        self.assertIn('p.deletion=service.removePhoto(p.deleteUri,p.deleteRevision)',app)
        self.assertIn('if(p->deletion)p->deletion->canceled=true',app)

if __name__=='__main__':unittest.main()

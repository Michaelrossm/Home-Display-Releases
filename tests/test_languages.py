from pathlib import Path
import copy
import importlib.util
import io
import json
import unittest
import zipfile
import zlib
import test_printer_service as printer
import test_system_performance as native

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('language_packer', ROOT/'tools/package_languages.py')
packer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packer)

def array(name, blob):
    return 'inline constexpr uint8_t '+name+'[]={'+','.join(str(x) for x in blob)+'};\n'

class LanguageTests(unittest.TestCase):
    def test_only_german_deterministic_real_zip(self):
        data = packer.build(check=True)
        self.assertEqual([d['locale'] for d in data], ['de-DE'])
        raw = packer.validate(data[0])
        archive = (packer.SOURCES/'de-DE.zip').read_bytes()
        self.assertEqual(archive, packer.archive(raw))
        with zipfile.ZipFile(io.BytesIO(archive)) as package:
            self.assertEqual(package.namelist(), ['locale.json'])
            self.assertEqual(package.getinfo('locale.json').compress_type, zipfile.ZIP_DEFLATED)
            self.assertEqual(package.read('locale.json'), raw)
        self.assertLess(len(archive), len(raw))

    def test_package_rejects_invalid_source(self):
        data = json.loads((packer.SOURCES/'de-DE.json').read_text(encoding='utf-8'))
        for key, value in [('locale','../en'), ('direction','rtl'), ('schema',2), ('name','bad\nname')]:
            broken = copy.deepcopy(data);broken[key]=value
            with self.assertRaises(ValueError):packer.validate(broken)
        for value in ['', 'x'*2049, 'bad\x00value']:
            broken=copy.deepcopy(data);broken['messages']['common.save']=value
            with self.assertRaises(ValueError):packer.validate(broken)
        with self.assertRaises(ValueError):packer.pairs([('x',1),('x',2)])

    def test_actual_zip_metadata_guards(self):
        blob=(packer.SOURCES/'de-DE.zip').read_bytes()
        code=(ROOT/'src/services/languages/LanguageArchive.h').read_text(encoding='utf-8')+array('blob',blob)+r'''
#include <cassert>
#include <vector>
int main(){hp::LanguageArchive p;assert(p.parse(blob,sizeof(blob))&&p.size>p.compressedSize);
 for(size_t n=0;n<120;++n)assert(!p.parse(blob,n));assert(!p.parse(nullptr,120));
 for(size_t offset:{size_t(6),size_t(8),size_t(26),size_t(30),size_t(14),sizeof(blob)-22+8}){
  std::vector<uint8_t> bad(blob,blob+sizeof(blob));bad[offset]^=1;assert(!p.parse(bad.data(),bad.size()));}
 std::vector<uint8_t> bad(blob,blob+sizeof(blob));bad.push_back(0);assert(!p.parse(bad.data(),bad.size()));
}
'''
        native.SystemPerformanceTests().native(code)

    def test_actual_service_restart_fallback_and_no_per_lookup_unzip(self):
        de=json.loads((packer.SOURCES/'de-DE.json').read_text(encoding='utf-8'))
        en=copy.deepcopy(de);en['locale']='en-US';en['name']='English';en['messages']['common.save']='Save'
        de_raw, en_raw=packer.validate(de),packer.validate(en)
        de_zip, en_zip=packer.archive(de_raw),packer.archive(en_raw)
        assets='namespace hp::languageAssets {struct Asset{const char* id;const char* name;const uint8_t* zip;size_t size;};\n'+array('de',de_zip)+array('en',en_zip)+r'''
inline constexpr Asset catalog[]={{"de-DE","Deutsch",de,sizeof(de)},{"en-US","English",en,sizeof(en)}};}
'''
        # Zlib itself is the pinned library compiled in the P4 build. This host
        # fixture substitutes only inflate/CRC, to exercise service ownership,
        # parsing, fallback, worker publication and restart without a host DLL.
        fake=r'''
#include <cstddef>
constexpr const char* FILE_READ="r";
bool externalPresent=false,usbPresent=false,shortRead=false;unsigned externalReads=0,usbOpens=0;
struct File{bool valid=false;operator bool()const{return valid;}bool isDirectory(){return false;}size_t size(){return sizeof(hp::languageAssets::de);}void close(){}size_t read(uint8_t* dst,size_t n){++externalReads;memcpy(dst,hp::languageAssets::de,n);return shortRead?n-1:n;}};
namespace hp {
struct StorageService{static StorageService& instance(){static StorageService s;return s;}File openSdRead(const String& p){assert(p=="/Sprachen/de-DE.zip"||p=="/Sprachen/en-US.zip");return {externalPresent};}};
struct UsbFS{File open(const String& p,const char*){assert(p=="/Sprachen/de-DE.zip"||p=="/Sprachen/en-US.zip");++usbOpens;return {usbPresent};}};UsbFS& usbAppFileSystem(){static UsbFS fs;return fs;}
bool unlocked=true,locked=false;unsigned writes=0;String stored="invalid";
struct SecurityWriteGuard{};
struct SecurityService {static SecurityService& instance(){static SecurityService s;return s;}bool settingsAllowed(){return unlocked;}bool tabletLocked(){return locked;}};
struct ConfigService {static ConfigService& instance(){static ConfigService s;return s;}String getString(const char*,const char*){return stored;}bool putString(const char*,const String& value){stored=value;++writes;return true;}};
struct NetworkWorker {using Fn=void(*)(void*);Fn fn=nullptr;void* arg=nullptr;bool reject=false;
 static NetworkWorker& instance(){static NetworkWorker n;return n;}bool submitLocalTransfer(Fn f,void* p,const char*){if(reject)return false;assert(!fn);fn=f;arg=p;return true;}
 void drain(){assert(fn);auto f=fn;fn=nullptr;f(arg);}
};}
using Bytef=uint8_t;constexpr int MAX_WBITS=15,Z_OK=0,Z_FINISH=4,Z_STREAM_END=1;
struct z_stream{Bytef* next_in;unsigned avail_in;Bytef* next_out;unsigned avail_out;size_t total_out,total_in;};
unsigned inflates=0;bool useEnglish=false,badCrc=false;
int inflateInit2(z_stream*,int){return Z_OK;}void inflateEnd(z_stream*){}
'''
        fake+='const char* deRaw=R"JSON('+de_raw.decode('utf-8')+')JSON";\nconst char* enRaw=R"JSON('+en_raw.decode('utf-8')+')JSON";\n'
        fake+=r'''
int inflate(z_stream* s,int){++inflates;useEnglish=s->next_in==hp::languageAssets::en+41;const char* raw=useEnglish?enRaw:deRaw;assert(strlen(raw)==s->avail_out);memcpy(s->next_out,raw,s->avail_out);s->total_out=s->avail_out;s->total_in=s->avail_in;return Z_STREAM_END;}
'''
        fake+=f'uint32_t crc32(int,const Bytef*,size_t){{return badCrc?0:useEnglish?{zlib.crc32(en_raw)}u:{zlib.crc32(de_raw)}u;}}\n'
        body=r'''
int main(){using namespace hp;auto& s=LanguageService::instance();assert(s.begin()&&s.ready()&&!strcmp(s.active(),"de-DE")&&writes==0);
 assert(!strcmp(s.tr("common.save"),"Speichern"));assert(!strcmp(s.tr("missing","Deutsch"),"Deutsch"));
 const unsigned calls=inflates;for(unsigned i=0;i<1000;++i)assert(!strcmp(s.text("Speichern"),"Speichern"));assert(s.begin()&&inflates==calls);
 stored="de-DE";s.loadExternalAtBoot();assert(externalReads==0);externalPresent=true;s.loadExternalAtBoot();assert(externalReads==1&&!strcmp(s.active(),"de-DE"));externalPresent=false;
 usbPresent=true;s.loadExternalAtBoot();assert(externalReads==2&&!strcmp(s.active(),"de-DE"));usbPresent=false;
 shortRead=true;externalPresent=true;const auto unzipBefore=inflates;s.loadExternalAtBoot();assert(inflates==unzipBefore&&!strcmp(s.active(),"de-DE"));shortRead=false;externalPresent=false;
 const unsigned reads=externalReads;for(unsigned i=0;i<1000;++i)s.tr("common.save");assert(externalReads==reads);
 assert(!s.request("../en")&&!s.request("fr-FR"));assert(s.request("de-DE")&&!s.busy()&&writes==0);
 auto& worker=NetworkWorker::instance();assert(s.request("en-US")&&s.busy()&&writes==0);locked=true;worker.drain();s.pollUi();assert(writes==0&&!s.busy());locked=false;
 assert(s.request("en-US"));worker.drain();s.pollUi();assert(writes==1&&!strcmp(s.requested(),"en-US")&&!strcmp(s.active(),"de-DE"));
 LanguageService restarted;assert(restarted.begin()&&!strcmp(restarted.active(),"en-US"));assert(!strcmp(restarted.text("Speichern"),"Save"));
 assert(!strcmp(restarted.tr("unknown","German fallback"),"German fallback"));const auto before=inflates;restarted.catalogJson("common.save,common.cancel");assert(inflates==before);
 badCrc=true;LanguageService corrupt;assert(!corrupt.begin()&&!strcmp(corrupt.tr("unknown","Deutsch"),"Deutsch"));
}
'''
        code=assets+fake+printer.stripped('src/services/languages/LanguageArchive.h')+printer.stripped('src/services/LanguageService.h')+printer.stripped('src/services/LanguageService.cpp')+body
        printer.PrinterServiceTests().native(code)

    def test_all_clients_share_one_service(self):
        platform=(ROOT/'src/apps/lua/LuaPlatformBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('lua_register(L,"hp_language",language)',platform)
        self.assertIn('lua_register(L,"hp_tr",translate)',platform)
        service=(ROOT/'src/services/LanguageService.cpp').read_text(encoding='utf-8')
        self.assertNotIn('xTaskCreate',service)
        self.assertIn('submitLocalTransfer(saveWorker',service)
        self.assertIn('stream.total_out==archive.size',service)
        self.assertIn('stream.total_in==archive.compressedSize',service)
        self.assertIn('openSdRead(path)',service)
        self.assertIn('usbAppFileSystem().open(path,FILE_READ)',service)
        self.assertIn('candidate->count==german_->count',service)
        main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
        self.assertLess(main.index('UsbStorageService::instance().begin()'),main.index('LanguageService::instance().loadExternalAtBoot()'))
        self.assertLess(main.index('LanguageService::instance().loadExternalAtBoot()'),main.index('beginSharedAppServices()'))

if __name__=='__main__':unittest.main()

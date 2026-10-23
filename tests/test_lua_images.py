from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class LuaImageTests(unittest.TestCase):
    def test_production_path_and_png_limits(self):
        code=r'''
#include "LuaImagePolicy.h"
#include <cassert>
#include <cstring>
int main(){using namespace hp::LuaImagePolicy;
 for(const char* p:{"assets/logo.png","assets/radio-1.png","assets/A_1.png","assets/a.jpg","assets/a.jpeg","assets/a.bmp","assets/a.gif","assets/a.webp","assets/a.tga"})assert(validPath(p));
 for(const char* p:{"","logo.png","/assets/a.png","assets/../a.png","assets/.a.png","assets/a/evil.png","assets/a\\b.png","assets/a.svg","assets/a.PNG","https://x/a.png"})assert(!validPath(p));
 assert(!validPath(std::string("assets/a\0.png",13)));
 uint8_t bytes[33]={137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R'};
 bytes[18]=1;bytes[22]=1;uint32_t w=0,h=0;
 assert(pngSize(bytes,33,w,h)&&w==256&&h==256);
 assert(!pngSize(bytes,24,w,h));assert(!pngSize(bytes,maxFileBytes+1,w,h));
 bytes[19]=1;assert(!pngSize(bytes,33,w,h));bytes[19]=0;bytes[18]=0;assert(!pngSize(bytes,33,w,h));
 bytes[18]=1;bytes[0]=0;assert(!pngSize(bytes,33,w,h));bytes[0]=137;bytes[11]=12;assert(!pngSize(bytes,33,w,h));
 bytes[11]=13;size_t length=33;assert(!stripMetadata(bytes,length));
 uint8_t png[70]={};memcpy(png,bytes,33);png[36]=1;memcpy(png+37,"iCCP",4); // 1-byte compressed profile (stripped)
 memcpy(png+50,"IDAT",4);memcpy(png+62,"IEND",4);length=70;
 assert(stripMetadata(png,length)&&length==57);assert(!memcmp(png+37,"IDAT",4));
 uint8_t bad[45]={};memcpy(bad,bytes,33);bad[33]=255;length=45;assert(!stripMetadata(bad,length));
}'''
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder);(work/'check.cpp').write_text(code,encoding='utf-8')
            shutil.copy(ROOT/'src/apps/lua/LuaImagePolicy.h',work)
            if shutil.which('g++'):
                command=['g++','-std=c++17','check.cpp','-o','check.exe']
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists():self.skipTest('No native C++ compiler')
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 check.cpp /Fe:check.exe"'
            result=subprocess.run(command,cwd=work,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(work/'check.exe')],cwd=work,check=True,timeout=10)

    def test_worker_lifetime_and_compatibility(self):
        source=(ROOT/'src/apps/lua/LuaWidgetsBridge.cpp').read_text(encoding='utf-8')
        worker=source.split('void readImage(',1)[1].split('void imageTick',1)[0]
        self.assertNotIn('lv_',worker)
        self.assertNotIn('LuaApp*',worker)
        for text in ('std::shared_ptr<ImageData>','data->alive=false','memory_order_acquire','memory_order_release','NetworkWorker::instance().submit','ImageCodec::dimensions','ImageCodec::decode','lv_image_cache_drop','releaseNativeBytes','luaL_checktype(L,2,LUA_TTABLE)','hp_ui_image_status'):
            self.assertIn(text,source)
        self.assertIn('heap_caps_free(data->pixels);data->pixels=nullptr;',worker)

    def test_package_assets_survive_install_and_rollback(self):
        installer=(ROOT/'src/apps/lua/LuaPackageInstaller.cpp').read_text(encoding='utf-8')
        for text in ('copyPackageAssets(fs,fromDir,toDir,detail)','clearPackageAssets(fs,dir)','validatePackageAssets(*fs,stage)','LuaImagePolicy::maxAssets','LuaImagePolicy::maxPackageBytes','invalid_package_assets'):
            self.assertIn(text,installer)
        web=(ROOT/'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        self.assertIn('for(const asset of prepared.assets)',web)
        self.assertIn("'X-App-File':asset.name",web)
        self.assertIn('asset_parent_mkdir',web)

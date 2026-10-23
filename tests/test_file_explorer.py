from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class FileExplorerTests(unittest.TestCase):
    def test_real_common_image_decoders(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow needed to make independent test images')
        cmake=shutil.which('cmake') or str(Path.home()/'.platformio/packages/tool-cmake/bin/cmake.exe')
        ninja=shutil.which('ninja') or str(Path.home()/'.platformio/packages/tool-ninja/ninja.exe')
        if not Path(cmake).exists() or not Path(ninja).exists():self.skipTest('Native CMake/Ninja unavailable')
        build=ROOT/'.pio/native_image_codecs'
        source=ROOT/'tests/native/image_codecs'
        commands=f'"{cmake}" -S "{source}" -B "{build}" -G Ninja -DCMAKE_MAKE_PROGRAM="{ninja}" && "{cmake}" --build "{build}" --parallel 4'
        if os.name=='nt':
            setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
            if not setup.exists():self.skipTest('Host compiler unavailable')
            command=f'cmd /d /c "call "{setup}" >nul && {commands}"'
        else:
            command=['sh','-c',commands]
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
        self.assertEqual(result.returncode,0,(result.stdout+result.stderr)[-6000:])
        with tempfile.TemporaryDirectory(prefix='home-image-fixtures-') as folder:
            image=Image.new('RGB',(16,12));image.putdata([(x*16,y*20,100) for y in range(12) for x in range(16)])
            files=[]
            for extension,options in [('png',{}),('jpg',{}),('jpeg',{'progressive':True}),('bmp',{}),('gif',{}),('webp',{'lossless':True}),('tga',{})]:
                path=Path(folder)/f'check.{extension}';image.save(path,**options);files.append(str(path))
            rotated=Path(folder)/'rot90.jpg';exif=Image.Exif();exif[274]=6;image.save(rotated,exif=exif,quality=95);files.append(str(rotated))
            exe=build/('codec_check.exe' if os.name=='nt' else 'codec_check')
            result=subprocess.run([str(exe),*files],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_services_and_navigation_are_central_and_bounded(self):
        service=(ROOT/'src/services/FileBrowserService.cpp').read_text(encoding='utf-8')
        self.assertIn('NetworkWorker::instance().submit',service)
        self.assertNotIn('lv_',service)
        self.assertIn('pageSize=96',service)
        self.assertIn('selected_a.hpi',service)
        self.assertIn('selected_b.hpi',service)
        self.assertIn('writeAtomic(StorageArea::UserData',service)
        self.assertIn('security.settingsAllowed()',service)
        self.assertIn('::crc32',service)
        app=(ROOT/'src/apps/files/FileExplorerApp.cpp').read_text(encoding='utf-8')
        self.assertNotIn('fs->open',app)
        self.assertNotIn('xTaskCreate',app)
        self.assertIn('"files_nas",false',app)
        self.assertIn('FileBrowserService::list',app)
        self.assertIn('FileBrowserService::image',app)
        self.assertIn('startSmbDownload(index,profile->id,p.nasFolder)',app)
        self.assertIn('LV_FLEX_FLOW_ROW_WRAP',app)
        wallpaper=(ROOT/'src/ui/WallpaperService.cpp').read_text(encoding='utf-8')
        self.assertNotIn('lv_image_decoder_get_info',wallpaper)
        self.assertNotIn('sdAbsoluteExists',wallpaper)
        self.assertIn('FileBrowserService::image',wallpaper)
        self.assertIn('lv_image_cache_drop',wallpaper)
        self.assertIn('ready.load(std::memory_order_acquire)',wallpaper)

from pathlib import Path
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ImageCodecNativeTests(unittest.TestCase):
    def test_consumers_share_decoder_and_keep_own_limits(self):
        read=lambda path:(ROOT/path).read_text(encoding='utf-8')
        self.assertIn('width,height,false,cancelled,false,true',read('src/services/printer/PrinterFileJobs.cpp'))
        self.assertIn('580,470,false,true',read('src/apps/files/FileExplorerApp.cpp'))
        bridge=read('src/apps/lua/LuaMediaStorageBridge.cpp')
        self.assertIn('r.cancelled,center,packed)',bridge)
        self.assertIn('image_opaque_requires_contain',bridge)
        self.assertIn('"files","directories","opaque"',bridge)
        self.assertIn('wallpaper,cover,cancel,center,rgb565',read('src/services/FileBrowserService.cpp'))
        self.assertIn('shown_->rgb565?LV_COLOR_FORMAT_RGB565',read('src/ui/WallpaperService.cpp'))
        self.assertIn('decoded->rgb565=true',read('src/services/FileBrowserService.cpp'))
        self.assertIn('ImageCodec::decode(encoded.get(),size,256,256)',read('src/apps/lua/LuaWidgetsBridge.cpp'))

    def test_real_pinned_png_webp_stb_pixels_orientation_cancellation_and_oom(self):
        definitions=(ROOT/'components/home_image_codecs/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertNotIn('PRIVATE WEBP_REDUCE_SIZE',definitions)
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow is required only to generate host image fixtures')
        cmake=shutil.which('cmake') or 'C:/Users/micha/.platformio/packages/tool-cmake/bin/cmake.exe'
        ninja=shutil.which('ninja') or 'C:/Users/micha/.platformio/packages/tool-ninja/ninja.exe'
        setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
        if not Path(cmake).exists() or not Path(ninja).exists() or not setup.exists():self.skipTest('MSVC/CMake/Ninja host toolchain required')
        for folder in ('managed_components/espressif__libpng/libpng','managed_components/espressif__zlib/zlib'):
            if not (ROOT/folder).exists():self.skipTest('Pinned decoder sources required')
        with tempfile.TemporaryDirectory(prefix='home-display-codec-') as tmp:
            work=Path(tmp);include=work/'include';include.mkdir()
            shutil.copyfile(ROOT/'tests/native/image_codec_heap.h',include/'esp_heap_caps.h')
            shutil.copyfile(ROOT/'managed_components/espressif__libpng/libpng/scripts/pnglibconf.h.prebuilt',include/'pnglibconf.h')
            image=Image.new('RGBA',(1600,1000),(255,0,0,255));image.paste((0,255,0,128),(800,0,1600,1000))
            image.save(work/'photo.png');image.save(work/'photo.webp',quality=90)
            exif=Image.Exif();exif[274]=6;image.convert('RGB').save(work/'photo.jpg',exif=exif)
            stripes=Image.new('RGB',(128,128));stripes.putdata([(255,255,255) if x%2 else (0,0,0) for y in range(128) for x in range(128)])
            stripes.save(work/'stripes.png');stripes.save(work/'stripes.bmp')
            cmake_source=r'''
cmake_minimum_required(VERSION 3.20)
project(HomeDisplayCodecHost LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
file(GLOB Z "@ROOT@/managed_components/espressif__zlib/zlib/*.c")
list(FILTER Z EXCLUDE REGEX "/example\\.c$")
add_library(z STATIC ${Z})
target_include_directories(z PUBLIC "@ROOT@/managed_components/espressif__zlib/zlib")
file(GLOB PNG "@ROOT@/managed_components/espressif__libpng/libpng/png*.c")
list(FILTER PNG EXCLUDE REGEX "/pngtest\\.c$")
add_library(png STATIC ${PNG})
target_include_directories(png PUBLIC "@ROOT@/managed_components/espressif__libpng/libpng" "@WORK@/include")
target_link_libraries(png PUBLIC z)
file(GLOB WEBP "@ROOT@/components/home_image_codecs/libwebp/src/dec/*.c" "@ROOT@/components/home_image_codecs/libwebp/src/dsp/*.c" "@ROOT@/components/home_image_codecs/libwebp/src/utils/*.c")
add_library(webp STATIC ${WEBP})
target_include_directories(webp PUBLIC "@ROOT@/components/home_image_codecs/libwebp")
target_compile_definitions(webp PRIVATE malloc=hp_codec_malloc calloc=hp_codec_calloc realloc=hp_codec_realloc free=hp_codec_free)
add_executable(check "@ROOT@/src/services/ImageCodec.cpp" "@ROOT@/tests/native/image_codec_host.cpp")
target_include_directories(check PRIVATE "@WORK@/include" "@ROOT@/src/services" "@ROOT@/components/home_image_codecs" "@ROOT@/components/home_image_codecs/libwebp/src")
target_link_libraries(check PRIVATE png webp)
'''.replace('@ROOT@',ROOT.as_posix()).replace('@WORK@',work.as_posix())
            (work/'CMakeLists.txt').write_text(cmake_source,encoding='utf-8')
            command=f'cmd /d /c "call "{setup}" >nul && "{cmake}" -S "{work}" -B "{work / "build"}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="{ninja}" && "{cmake}" --build "{work / "build"}" -j 4"'
            built=subprocess.run(command,capture_output=True,text=True,timeout=240)
            self.assertEqual(built.returncode,0,(built.stdout+built.stderr)[-8000:])
            run=subprocess.run([str(work/'build/check.exe'),str(work/'photo.png'),str(work/'photo.webp'),str(work/'photo.jpg'),str(work/'stripes.png'),str(work/'stripes.bmp')],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            measurements=json.loads(run.stdout)
            print('HOST decoder measurement (not ESP32): '+json.dumps(measurements,sort_keys=True))

if __name__=='__main__':unittest.main()

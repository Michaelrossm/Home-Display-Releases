import configparser
import contextlib
import io
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import shutil
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

def load_tool(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'tools'/f'{name}.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

class ReleasePreparationTests(unittest.TestCase):
    def test_forced_include_normalizer_preserves_flags_and_is_idempotent(self):
        cmake=shutil.which('cmake') or str(ROOT/'.pio/core/packages/tool-cmake/bin/cmake.exe')
        ninja=shutil.which('ninja') or str(ROOT/'.pio/core/packages/tool-ninja/ninja.exe')
        if not Path(cmake).is_file() or not Path(ninja).is_file():self.skipTest('CMake/Ninja unavailable')
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.16)\nproject(flags NONE)\n'
                'add_library(test_flags INTERFACE)\n'
                f'include("{(ROOT/"cmake/compile_options.cmake").as_posix()}")\n'+r'''
set_property(TARGET test_flags PROPERTY INTERFACE_COMPILE_OPTIONS "-Wextra;-include;header with spaces.h;-DFOO=1")
set_property(TARGET test_flags PROPERTY COMPILE_OPTIONS "-O2;-include;other.h")
home_display_normalize_forced_includes(test_flags)
home_display_normalize_forced_includes(test_flags)
get_target_property(public test_flags INTERFACE_COMPILE_OPTIONS)
get_target_property(private test_flags COMPILE_OPTIONS)
if(NOT public STREQUAL "-Wextra;-includeheader with spaces.h;-DFOO=1")
 message(FATAL_ERROR "Public flags corrupted: ${public}")
endif()
if(NOT private STREQUAL "-O2;-includeother.h")
 message(FATAL_ERROR "Private flags corrupted: ${private}")
endif()
''',encoding='utf-8')
            r=subprocess.run([cmake,'-S',str(root),'-B',str(root/'build'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+ninja],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_isolation_preserves_all_hardware_profiles_and_schema(self):
        c=configparser.ConfigParser(interpolation=None);c.read(ROOT/'platformio.ini',encoding='utf-8')
        self.assertEqual(c['platformio']['core_dir'],'.pio/core')
        self.assertEqual(c['platformio']['default_envs'],'home_display_p4_legacy_dev')
        for hardware in ('legacy','r3'):
            for profile in ('dev','prod'):self.assertIn(f'env:home_display_p4_{hardware}_{profile}',c)
        self.assertEqual(c['env']['custom_intercom_enabled'],'0')
        version=re.search(r'kVersion = "([^"]+)"',(ROOT/'include/BuildInfo.h').read_text()).group(1)
        m=json.loads((ROOT/'PROJECT_MANIFEST.json').read_text())
        self.assertEqual(m['version'],version);self.assertEqual(m['config_schema'],2)
        self.assertNotIn('intercom',m['native_app_sources'])
        self.assertIn('intercom',m['disabled_features'])

    def test_actual_backend_registration_can_disable_and_enable_intercom(self):
        source='\n'.join(x for x in (ROOT/'src/apps/AppServices.cpp').read_text().splitlines() if not x.startswith('#include'))
        fixture=r'''
#include <cassert>
namespace hp {
int intercomStarts=0,normalStarts=0;
struct EventLog{static EventLog& instance(){static EventLog e;return e;}void add(const char*,const char*,const char*){}};
'''
        for name in ('RadioService','MusicPlayerService','WeatherService','PowerService','EnergyHistoryService','PrinterService','PhotoLibraryService','AppBackgroundScheduler','CalendarService','VideoService','GoliathService','IntercomService','IntercomAudioService'):
            counter='intercomStarts' if name in ('GoliathService','IntercomService','IntercomAudioService') else 'normalStarts'
            fixture+=f'struct {name}{{static {name}& instance(){{static {name} s;return s;}}bool begin(){{++{counter};return true;}}}};\n'
        fixture+='}\n'+source+r'''
int main(){assert(hp::beginSharedAppServices());assert(hp::normalStarts==10);assert(hp::intercomStarts==HOMEPANEL_FEATURE_INTERCOM*3);}
'''
        for enabled in (0,1):
            test_system_performance.SystemPerformanceTests().native('#define HOMEPANEL_FEATURE_INTERCOM '+str(enabled)+'\n'+fixture)

    def test_disabled_lua_intercom_keeps_general_camera_access(self):
        source=(ROOT/'src/apps/lua/LuaIntercomBridge.cpp').read_text()
        access='bool access('+source.split('bool access(',1)[1].split('bool foreground(',1)[0]
        fixture=r'''
#include <cassert>
enum class AppPermission{Intercom,Camera,Network};
struct LuaApp{bool allows(AppPermission){return true;}};
'''+access+r'''
int main(){LuaApp app;assert(access(&app,AppPermission::Camera));assert(access(&app,AppPermission::Intercom)==bool(HOMEPANEL_FEATURE_INTERCOM));assert(!access(nullptr,AppPermission::Camera));}
'''
        for enabled in (0,1):
            test_system_performance.SystemPerformanceTests().native('#define HOMEPANEL_FEATURE_INTERCOM '+str(enabled)+'\n'+fixture)

    def test_license_is_full_and_shared_by_native_and_web(self):
        license=(ROOT/'LICENSE.md').read_text(encoding='utf-8')
        self.assertIn('PolyForm Noncommercial License 1.0.0',license)
        self.assertIn('## No Liability',license)
        self.assertIn('Required Notice: Copyright (c) 2026 Michael Roßmann',license)
        cmake=(ROOT/'src/CMakeLists.txt').read_text()
        self.assertIn('EMBED_TXTFILES',cmake);self.assertIn('../LICENSE.md',cmake)
        ui=(ROOT/'src/ui/LicenseUi.cpp').read_text()
        self.assertIn('lv_label_set_text_static',ui);self.assertNotIn('StorageService',ui)
        self.assertIn('LicenseUi::build(content)',(ROOT/'src/apps/settings/SettingsApp.cpp').read_text())
        web=(ROOT/'src/services/WebServerService.cpp').read_text()
        self.assertIn('LicenseUi::projectText()',web);self.assertIn('LicenseUi::thirdPartyText()',web)

    def test_snapshot_exports_only_tracked_sources_without_history(self):
        tool=load_tool('create_release_snapshot')
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'source';root.mkdir();out=Path(d)/'snapshot';tool.ROOT=root
            subprocess.run(['git','init','-q',str(root)],check=True)
            (root/'main.cpp').write_text('int main(){}')
            (root/'personal-settings.json').write_text('not for publication')
            (root/'docs').mkdir();(root/'docs/NATIVE-INTERCOM.md').write_text('private field report')
            subprocess.run(['git','add','docs/NATIVE-INTERCOM.md'],cwd=root,check=True)
            subprocess.run(['git','add','main.cpp'],cwd=root,check=True)
            self.assertEqual(tool.export(out),1)
            self.assertFalse((out/'.git').exists());self.assertFalse((out/'personal-settings.json').exists())
            self.assertFalse((out/'docs/NATIVE-INTERCOM.md').exists())
            self.assertEqual((out/'main.cpp').read_text(),'int main(){}')
            self.assertFalse(json.loads((out/'release-source.json').read_text())['historyIncluded'])
            subprocess.run(['git','init','-q',str(out)],check=True)
            subprocess.run(['git','add','.'],cwd=out,check=True)
            self.assertEqual(tool.refresh_inventory(out),1)
            (root/'secret-private.pem').write_text('not public')
            subprocess.run(['git','add','secret-private.pem'],cwd=root,check=True)
            with self.assertRaises(ValueError):tool.export(Path(d)/'rejected')

    def test_public_key_embedding_rejects_private_material(self):
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives import serialization
        tool=load_tool('embed_public_key')
        with tempfile.TemporaryDirectory() as d:
            public=Path(d)/'public.pem';header=Path(d)/'key.h'
            key=ec.generate_private_key(ec.SECP256R1())
            public.write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
            tool.embed(public,header)
            self.assertIn('BEGIN PUBLIC KEY',header.read_text())
            public.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
            with self.assertRaises(ValueError):tool.embed(public,header)

    def test_dependency_notices_are_original_not_relicensed(self):
        tool=load_tool('export_dependency_licenses')
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'component';root.mkdir();out=Path(d)/'licenses'
            (root/'LICENSE').write_text('Original third-party license')
            (root/'WebP-PATENTS').write_text('Original patent grant')
            (root/'private-settings.json').write_text('not a notice')
            self.assertEqual(tool.collect([('component',root)],out),2)
            self.assertEqual((out/'component/LICENSE').read_text(),'Original third-party license')
            self.assertEqual((out/'component/WebP-PATENTS').read_text(),'Original patent grant')
            self.assertFalse((out/'component/private-settings.json').exists())

    def test_signed_release_contains_license_and_verifiable_manifest_not_private_key(self):
        from cryptography.hazmat.primitives import hashes,serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        tool=load_tool('build-release-package')
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)/'source';root.mkdir();tool.ROOT=root
            (root/'include').mkdir();(root/'signing').mkdir()
            (root/'include/BuildInfo.h').write_text('kVersion = "0.1.0";')
            (root/'LICENSE.md').write_text('Original project license')
            (root/'THIRD_PARTY_NOTICES.md').write_text('Original notices')
            key=ec.generate_private_key(ec.SECP256R1());private=Path(d)/'outside-private.pem'
            private.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
            (root/'signing/home-display-signing-public.pem').write_bytes(key.public_key().public_bytes(serialization.Encoding.PEM,serialization.PublicFormat.SubjectPublicKeyInfo))
            firmware=Path(d)/'firmware.bin';firmware.write_bytes(b'\xe9'+b'\0'*2047)
            out=Path(d)/'release'
            with patch('sys.argv',['package','--version','0.1.0','--firmware',str(firmware),'--private-key',str(private),'--out-dir',str(out)]),contextlib.redirect_stdout(io.StringIO()):tool.main()
            archive=next(out.glob('*-signed.zip'))
            with zipfile.ZipFile(archive) as z:
                self.assertIn('LICENSE.md',z.namelist());self.assertIn('THIRD_PARTY_NOTICES.md',z.namelist())
                manifest=next(n for n in z.namelist() if n.endswith('.json'))
                key.public_key().verify(z.read(manifest+'.sig'),z.read(manifest),ec.ECDSA(hashes.SHA256()))
                self.assertEqual(json.loads(z.read(manifest))['profile'],'home_display_p4_legacy_prod')
                self.assertTrue(all('private.pem' not in n for n in z.namelist()))

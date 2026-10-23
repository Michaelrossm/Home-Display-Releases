import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class PerformancePolicyTests(unittest.TestCase):
    def test_core_and_radio_policy_native(self):
        compiler=shutil.which('g++') or shutil.which('clang++')
        code=r'''
#include "src/core/PerformancePolicy.h"
#include <cassert>
int main(){using namespace hp::performance;
 assert(uiCoreFor(1)==0&&uiCoreFor(2)==1&&uiCoreFor(2,false)==0);
 assert(!radioBurst(false,0,false,0,0));
 assert(radioBurst(true,0,false,0,0));
 assert(radioBurst(false,1,false,0,0));
 assert(radioBurst(false,0,true,100,5099));
 assert(!radioBurst(false,0,true,100,5100));
 assert(radioBurst(false,0,true,0xfffffff0u,16));
 assert(!radioBurst(false,0,true,0xfffffff0u,6000));
}'''
        with tempfile.TemporaryDirectory(prefix='hp-performance-') as directory:
            source=Path(directory)/'policy.cpp';binary=Path(directory)/'policy.exe'
            source.write_text(code,encoding='utf-8')
            if compiler:
                command=[compiler,'-std=c++17','-I',str(ROOT),str(source),'-o',str(binary)]
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                if not setup.exists():self.skipTest('C++ compiler unavailable')
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /EHsc /std:c++17 /I"{ROOT}" policy.cpp /Fe:policy.exe"'
            result=subprocess.run(command,cwd=directory,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(binary)],check=True)

    def test_existing_tasks_only_and_c6_rpc_stays_central(self):
        def text(path):return (ROOT/path).read_text(encoding='utf-8')
        display=text('src/hardware/DisplayManager.cpp')
        self.assertIn('cfg.lv_adapter_cfg.task_core_id = performance::kUiCore',display)
        worker=text('src/services/NetworkWorker.cpp')
        self.assertEqual(worker.count('xTaskCreatePinnedToCore('),2)
        self.assertNotIn('WiFi.setSleep',worker)
        self.assertIn('std::atomic<bool> processing_',text('src/services/NetworkWorker.h'))
        self.assertIn('!NetworkWorker::instance().radioBurstActive()',text('src/services/NetworkService.cpp'))
        self.assertIn('~RadioActivity()',text('src/apps/lua/LuaJobService.cpp'))
        self.assertIn('endNetworkActivity()',text('src/apps/lua/LuaJobService.cpp'))
        for path in ('src/services/WebServerService.cpp','src/services/FileTransferService.cpp'):
            self.assertIn('cfg.core_id = performance::kIoCore<0 ? tskNO_AFFINITY',text(path))
        self.assertIn('CONFIG_FREERTOS_HZ=1000',text('sdkconfig.defaults'))

    def test_network_maintenance_is_fair_without_transfer_reentry(self):
        worker=(ROOT/'src/services/NetworkWorker.h').read_text(encoding='utf-8')
        window=worker.split('bool backgroundWindow()const',1)[1].split('}',1)[0]
        self.assertIn('xTaskGetCurrentTaskHandle()==task_',window)
        self.assertIn('!processing_',window)
        self.assertNotIn('uxQueueMessagesWaiting',window)
        service=(ROOT/'src/services/PrinterService.cpp').read_text(encoding='utf-8')
        self.assertIn('NetworkWorker::instance().queueEmpty()',service)
        self.assertIn('elapsed<1000?1000-elapsed:250',service)

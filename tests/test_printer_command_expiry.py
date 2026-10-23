from pathlib import Path
import unittest
import test_system_performance
ROOT=Path(__file__).resolve().parents[1]

class PrinterCommandExpiryTests(unittest.TestCase):
    def test_actual_expiry_policy_wraparound_and_transfer_exceptions(self):
        source=(ROOT/'src/services/printer/PrinterSafety.h').read_text(encoding='utf-8')
        test_system_performance.SystemPerformanceTests().native(source+r'''
#include <cassert>
int main(){using namespace hp;
assert(!printerCommandQueueExpired("start_file",100,10100));
assert(printerCommandQueueExpired("start_file",100,10101));
assert(printerCommandQueueExpired("gcode",100,20000));
assert(!printerCommandQueueExpired("upload_file",100,20000));
assert(!printerCommandQueueExpired("preview_file",100,20000));
assert(!printerCommandQueueExpired("pause",0xffffff00u,100));
assert(printerCommandQueueExpired("pause",0xffff0000u,100));
}
''')
        service=(ROOT/'src/services/PrinterService.cpp').read_text(encoding='utf-8').split('void PrinterService::runCommand',1)[1]
        self.assertLess(service.index('printerCommandQueueExpired'),service.index('loadProfiles(*o)'))
        self.assertLess(service.index('printerCommandQueueExpired'),service.index('WorkerPrinterTransport http'))
        branch=service.split('if(printerCommandQueueExpired',1)[1].split('o->busy=true',1)[0]
        self.assertIn('e->pendingUntil=0',branch)
        self.assertIn('return;',branch)
        bridge=(ROOT/'src/apps/lua/PrinterLuaBridge.cpp').read_text(encoding='utf-8')
        self.assertIn('"automatic_queue_start"',bridge)

if __name__=='__main__':unittest.main()

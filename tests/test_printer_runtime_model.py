import unittest
import test_printer_service as printer

class PrinterRuntimeModelTests(unittest.TestCase):
    def test_history_ring_identity_and_monotonic_wrap(self):
        printer.PrinterServiceTests().native(r'''
int main(){using namespace hp;PrinterTemperatureHistory history;PrinterStatus status;status.valid=true;status.temperatureCount=2;
 strcpy(status.temperatures[0].id,"bed");strcpy(status.temperatures[1].id,"tool");status.temperatures[0].current=50;
 for(unsigned i=0;i<80;++i){status.temperatures[1].current=i;assert(history.sample(status,100+i*5000));}
 assert(history.series[0].count==60&&history.series[1].count==60&&history.series[1].next==20);
 assert(history.series[1].data[history.series[1].next].current==20);
 assert(!history.sample(status,395101));status.valid=false;assert(!history.sample(status,500000));
 history.sampled=0xffffff00u;history.revision=10;status.valid=true;assert(!history.sample(status,100));assert(history.sample(status,10000));
}
''')

    def test_operation_confirmation_is_not_http_acceptance(self):
        printer.PrinterServiceTests().native(r'''
int main(){using namespace hp;PrinterOperation op;PrinterStatus status;status.valid=true;strcpy(status.state,"idle");status.httpChannelIdle=true;
 op.begin(1,"bed_mesh",100);assert(!op.observe(status,101)&&op.phase==PrinterOperationPhase::Queued);
 op.set(PrinterOperationPhase::Accepted,102);assert(!op.observe(status,103)&&op.phase==PrinterOperationPhase::Accepted);
 status.httpChannelIdle=false;assert(op.observe(status,104)&&op.phase==PrinterOperationPhase::Running);
 status.httpChannelIdle=true;assert(op.observe(status,105)&&op.phase==PrinterOperationPhase::Unknown);
 op.begin(2,"set_temperature",200);strcpy(op.component,"heater");op.target=210;op.set(PrinterOperationPhase::Accepted,201);
 status.heaterCount=1;strcpy(status.heaters[0].id,"heater");status.heaters[0].target=210;status.heaters[0].current=30;
 assert(!op.observe(status,202));status.heaters[0].current=209;assert(op.observe(status,203)&&op.phase==PrinterOperationPhase::Completed);
 op.begin(3,"extrude",300);op.set(PrinterOperationPhase::Accepted,301);assert(op.observe(status,600301)&&op.phase==PrinterOperationPhase::Unknown);
 op.begin(4,"set_flow_factor",700000);strcpy(op.component,"tool_1");op.target=120;status.flowFactor=120;op.set(PrinterOperationPhase::Accepted,700001);
 assert(!op.observe(status,700002)&&op.phase==PrinterOperationPhase::Accepted); // another tool/global scalar is not proof
}
''')

    def test_catalog_distinguishes_config_capability_and_readiness(self):
        printer.PrinterServiceTests().native(r'''
int main(){using namespace hp;PrinterStatus status;status.valid=true;strcpy(status.state,"idle");const auto* a=printerAction("z_tilt");assert(a&&a->confirm);
 assert(!strcmp(printerActionReason(*a,status,false),"stale"));assert(!strcmp(printerActionReason(*a,status,true),"unknown"));
 status.knownControls=511;assert(!strcmp(printerActionReason(*a,status,true),"not_supported"));
 status.configurableControls=PrinterZTilt;assert(!strcmp(printerActionReason(*a,status,true),"not_configured"));
 status.controls=PrinterZTilt;assert(!strcmp(printerActionReason(*a,status,true),"requires_homing"));
 for(int i=0;i<3;++i){status.axes[i].letter="XYZ"[i];status.axes[i].homed=true;status.axes[i].homedKnown=true;}status.axisCount=3;status.httpChannelIdle=true;
 assert(!*printerActionReason(*a,status,true));JsonDocument d;printerWriteActionCatalog(d.to<JsonArray>(),0,4);assert(d.size()==4&&d[0]["confirmation"]==true);
}
''')

if __name__=='__main__':unittest.main()

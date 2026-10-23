import unittest
from pathlib import Path
import test_printer_service as printer_tests

ROOT=Path(__file__).resolve().parents[1]

class PrinterFileTests(unittest.TestCase):
    def test_thumbnail_size_choice_and_local_multi_size_selection(self):
        printer_tests.PrinterServiceTests().native(r'''
namespace hp {void LuaJobService::executeHttp(const Request&,Result&) {}}
int main(){using namespace hp;
 PrinterThumbnailChoice small{64,64};assert(small.consider(300,300));assert(small.consider(64,64));assert(!small.consider(300,300));
 PrinterThumbnailChoice large{300,300};assert(large.consider(64,64));assert(large.consider(300,300));assert(!large.consider(64,64));
 PrinterThumbnailChoice fallback{400,400};assert(fallback.consider(64,64));assert(fallback.consider(300,300));assert(!fallback.consider(600,600));
 localGcodes["sd:/multi.gcode"]="; thumbnail begin 300x300 8\n; BAUGBw==\n; thumbnail end\n; thumbnail begin 64x64 8\n; AAECAw==\n; thumbnail end\n";
 String bytes,error;assert(printerLocalThumbnail("sd:/multi.gcode",bytes,nullptr,error,64,64)&&bytes[0]==0);
 assert(printerLocalThumbnail("sd:/multi.gcode",bytes,nullptr,error,300,300)&&bytes[0]==4);
}
''',file_jobs=True)

    def test_real_stream_helper_bounds_removal_and_local_thumbnail(self):
        printer_tests.PrinterServiceTests().native(r'''
String uploaded;unsigned requests=0;bool removeDuringUpload=false;
namespace hp {void LuaJobService::executeHttp(const Request& q,Result& result){
 assert(fileWorker&&q.streamMode==2&&q.timeout==5000&&q.deadlineMs==300000&&q.responseLimit==2048);
 ++requests;uploaded="";uint8_t chunk[512];
 for(size_t offset=0;offset<q.expected;){
  if(removeDuringUpload&&offset>512)filesOnline=false;
  const size_t count=q.nativeRead(q.nativeReadContext,offset,chunk,std::min<size_t>(sizeof(chunk),q.expected-offset));
  if(!count){result.error="file_read";return;}uploaded.append(reinterpret_cast<char*>(chunk),count);offset+=count;
 }
 result.ok=true;result.status=200;
}}
int main(){using namespace hp;LuaJobService::Request q;q.owner="owner";q.permissions=perm(AppPermission::Network)|perm(AppPermission::StorageRead);
 localGcodes["usb:/part.gcode"]=String(std::string(8193,'x'));LuaJobService::Result r;
 printerUploadHttp(q,"usb:/part.gcode","PREFIX","SUFFIX",r);assert(r.ok&&uploaded=="PREFIX"+localGcodes["usb:/part.gcode"]+"SUFFIX"&&requests==1);
 removeDuringUpload=true;r={};printerUploadHttp(q,"usb:/part.gcode","","",r);assert(!r.ok&&r.error=="file_read");removeDuringUpload=false;filesOnline=true;
 q.permissions=perm(AppPermission::Network);r={};const unsigned before=requests;printerUploadHttp(q,"usb:/part.gcode","","",r);assert(!r.ok&&requests==before);
 q.permissions|=perm(AppPermission::StorageRead);fileWorker=false;r={};printerUploadHttp(q,"usb:/part.gcode","","",r);assert(!r.ok&&r.error=="worker_required");fileWorker=true;
 localGcodes["sd:/part.gcode"]="; header\n; thumbnail begin 1x1 8\n; AAECAw==\n; thumbnail end\nG1 X10\n";
 String bytes,error;assert(printerLocalThumbnail("sd:/part.gcode",bytes,nullptr,error)&&bytes.length()==4&&bytes[0]==0&&bytes[3]==3);
 localGcodes["sd:/part.gcode"]="; thumbnail begin 1x1 4\n; ???!\n; thumbnail end\n";assert(!printerLocalThumbnail("sd:/part.gcode",bytes,nullptr,error));
 assert(printerMultipartBoundary().length()==56);
}''',file_jobs=True)

    def test_transfer_paths_decoding_tick_limits_and_reprap_upload(self):
        printer_tests.PrinterServiceTests().native(r'''
struct UploadPrinter:FakePrinter {
 String source,path;unsigned uploads=0;uint8_t method=0;
 hp::LuaJobService::Result upload(const String& p,const String& session,const String& file,const String& prefix,const String& suffix,uint8_t m)override{
  assert(session.length()&&!prefix.length()&&!suffix.length());++uploads;source=file;path=p;method=m;
  hp::LuaJobService::Result r;r.ok=true;r.status=200;r.body="{\"err\":0}";return r;
 }
};
int main(){using namespace hp;
 assert(luaTickIntervalValid(150)&&luaTickIntervalValid(2000)&&!luaTickIntervalValid(99)&&!luaTickIntervalValid(2001));
 assert(kLuaDefaultTickMs==750&&luaTickBackoff(150,1)==300&&luaTickBackoff(150,3)==600&&luaTickBackoff(1500,1)==1500);
 assert(luaTickBackoff(100,1)==250&&luaTickBackoff(750,3)==1000&&luaTickBackoff(2000,3)==2000);
 assert(luaTickSoftBudgetUs(100)==5000&&luaTickSoftBudgetUs(150)==7500&&luaTickSoftBudgetUs(750)==8000);
 assert(!luaTickShouldBackoff(1)&&!luaTickShouldBackoff(2)&&luaTickShouldBackoff(3));
 assert(luaTickPeriod(150,1200)==1200&&luaTickPeriod(150,0)==150);
 assert(printerLocalGcode("sd:/GCode/Teil.gcode")&&printerLocalGcode("usb:/Teil.GCO"));
 for(const char* p:{"usb:/../secret.gcode","sd:/homepanel/secrets.gcode","usb:/Apps/a/main.gcode","sd:/.cache/part.gcode","nas1:/part.gcode","usb:/part.png","usb:/x\"\r\n.gcode"})assert(!printerLocalGcode(p));
 String data;assert(printerThumbnailBase64("AAECAw==",data)&&data.length()==4&&data[0]==0&&data[3]==3);
 assert(!printerThumbnailBase64("A=EC",data)&&!printerThumbnailBase64("AAE?",data)&&!printerThumbnailBase64("AB==",data));
 const uint8_t qoi[]={113,111,105,102,0,0,0,7,0,0,0,1,4,0,254,10,20,30,193,122,161,136,255,1,2,3,4,9,0,0,0,0,0,0,0,1};
 uint8_t out[28]{};uint32_t w,h;assert(qoiDimensions(qoi,sizeof(qoi),w,h)&&w==7&&h==1);
 assert(qoiDecodeBgra(qoi,sizeof(qoi),out,sizeof(out)));assert(out[0]==30&&out[8]==30&&out[12+2]==11&&out[16+2]==12&&out[23]==4&&out[24]==30);
 for(size_t n=0;n<sizeof(qoi);++n)assert(!qoiDecodeBgra(qoi,n,out,sizeof(out)));
 assert(!qoiDecodeBgra(qoi,sizeof(qoi),out,sizeof(out)-1));std::atomic<bool> cancelled{true};assert(!qoiDecodeBgra(qoi,sizeof(qoi),out,sizeof(out),&cancelled));
 UploadPrinter t;PrinterProfile p;p.mode="standalone";RepRapPrinterAdapter adapter;String error;
 assert(adapter.upload(t,p,"usb:/part.gcode","0:/gcodes/part.gcode",error)&&t.uploads==1&&t.method==1&&t.path=="/rr_upload?name=0%3A%2Fgcodes%2Fpart.gcode");
 assert(!adapter.upload(t,p,"usb:/part.gcode","0:/sys/config.g",error)&&t.uploads==1);
 adapter.reset();p.mode="sbc";assert(adapter.upload(t,p,"sd:/part.gcode","0:/gcodes/part.gcode",error)&&t.method==2&&t.path=="/machine/file/0%3A%2Fgcodes%2Fpart.gcode");
}''')

    def test_worker_only_streaming_owner_cleanup_and_ui_guards(self):
        bridge=(ROOT/'src/apps/lua/PrinterLuaBridge.cpp').read_text(encoding='utf-8')
        jobs=(ROOT/'src/apps/lua/LuaJobService.cpp').read_text(encoding='utf-8')
        files=(ROOT/'src/services/printer/PrinterFileJobs.cpp').read_text(encoding='utf-8')
        ui=(ROOT/'src/apps/lua/LuaUiBridge.cpp').read_text(encoding='utf-8')
        for name in ('hp_printer_upload','hp_printer_preview'):
            self.assertIn(f'luaBridgeContract::wrap(L, "{name}", luaBridgeContract::Kind::Action)',bridge)
        action=bridge.split('int fileAction(',1)[1].split('int upload(',1)[0]
        self.assertIn('LuaRuntimeState::Foreground',action)
        self.assertNotIn('executeHttp',action)
        self.assertIn('isWorkerContext()',files)
        self.assertIn('request.nativeRead=Stream::read',files)
        self.assertIn('64U*1024U*1024U',files)
        self.assertIn('discardResult(slot.owner,result.body)',jobs)
        self.assertIn('guard(L,"hp_set_tick_interval",false)',ui)
        self.assertNotIn('xTaskCreate',files)

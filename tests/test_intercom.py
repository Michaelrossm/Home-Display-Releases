from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class IntercomTests(unittest.TestCase):
    def native(self,case):
        code=(ROOT/'src/services/intercom/MediaProtocol.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+'\n#include <cassert>\n#include <vector>\n'+case)

    def test_g711_roundtrip_and_known_silence(self):
        self.native(r'''
int main(){using namespace hp::intercom;assert(mulawDecode(0xff)==0&&mulawDecode(0x7f)==0);
assert(alawDecode(0xd5)==8&&alawDecode(0x55)==-8);
for(int x=-32768;x<32768;x+=37){int m=mulawDecode(mulawEncode(x)),a=alawDecode(alawEncode(x));
assert(m-x<1100&&x-m<1100&&a-x<1100&&x-a<1100);}
}
''')

    def test_rtp_bounds_padding_extension_and_sequence_wrap(self):
        self.native(r'''
int main(){using namespace hp::intercom;uint8_t p[24]={0xb0,0xe0,0xff,0xff,0,0,0,7,0,0,0,9,0,0,0,1,1,2,3,4,0x65,1,0,2};
RtpPacket r;assert(r.parse(p,24)&&r.sequence==65535&&r.timestamp==7&&r.ssrc==9&&r.marker&&r.size==2&&r.data[0]==0x65);
for(unsigned n=0;n<21;++n)assert(!r.parse(p,n));p[23]=25;assert(!r.parse(p,24));p[23]=2;p[15]=255;assert(!r.parse(p,24));
}
''')

    def test_h264_single_stap_fu_loss_and_capacity(self):
        self.native(r'''
int main(){using namespace hp::intercom;uint8_t buffer[64]{};H264Assembler a(buffer,sizeof(buffer));
uint8_t single[]={0x65,1,2};RtpPacket p;p.data=single;p.size=3;p.marker=true;p.sequence=65535;p.timestamp=1;
assert(a.packet(p)&&a.size()==7&&buffer[3]==1&&buffer[4]==0x65);a.consumed();
assert(!a.gap());
uint8_t fu1[]={0x7c,0x85,1,2},fu2[]={0x7c,0x45,3,4};p.data=fu1;p.size=4;p.marker=false;p.timestamp=2;p.sequence=0;
assert(!a.packet(p));p.data=fu2;p.sequence=1;p.marker=true;assert(a.packet(p)&&a.size()==9&&buffer[4]==0x65&&buffer[8]==4);a.consumed();
p.data=fu1;p.sequence=2;p.timestamp=3;p.marker=false;assert(!a.packet(p));p.data=fu2;p.sequence=4;p.marker=true;assert(!a.packet(p)&&a.size()==0);a.consumed();
uint8_t stap[]={0x78,0,2,0x67,9,0,2,0x68,8};p.data=stap;p.size=sizeof(stap);p.sequence=5;p.timestamp=4;assert(a.packet(p)&&a.size()==12);a.consumed();
stap[2]=55;p.sequence=6;p.timestamp=5;assert(!a.packet(p));
a.consumed();p.data=single;p.size=3;p.sequence=9;p.timestamp=8;assert(a.packet(p)&&a.gap());a.consumed();
H264Assembler small(buffer,5);p.data=single;p.size=3;p.sequence=7;p.timestamp=6;assert(!small.packet(p)&&small.size()==0);
}
''')

    def test_sps_baseline_resolution_and_reference_limits(self):
        self.native(r'''
std::vector<uint8_t> sps(unsigned width,unsigned height,unsigned refs,unsigned profile=66){
std::vector<unsigned> bits;auto put=[&](unsigned v,unsigned n){for(int i=int(n)-1;i>=0;--i)bits.push_back((v>>i)&1);};
auto ue=[&](unsigned v){unsigned n=0,k=v+1;while(k>>=1)++n;put(0,n);put(v+1,n+1);};
put(profile,8);put(0,8);put(30,8);ue(0);ue(0);ue(0);ue(0);ue(refs);put(0,1);ue(width/16-1);ue(height/16-1);put(1,1);put(1,1);put(0,1);put(0,1);put(1,1);
std::vector<uint8_t> out{0x67};for(unsigned i=0;i<bits.size();i+=8){unsigned v=0;for(unsigned j=0;j<8;++j)v=(v<<1)|(i+j<bits.size()?bits[i+j]:0);out.push_back(v);}return out;}
int main(){using namespace hp::intercom;SpsInfo info;auto good=sps(640,480,2);assert(baselineSps(good.data(),good.size(),info)&&info.width==640&&info.height==480&&info.refs==2);
for(auto bad:{sps(1280,720,2),sps(640,480,8),sps(640,480,2,100)})assert(!baselineSps(bad.data(),bad.size(),info));
for(unsigned n=0;n<5;++n)assert(!baselineSps(good.data(),n,info));}
''')

    def test_permissions_queue_pin_and_web_auth(self):
        bridge=(ROOT/'src/apps/lua/LuaIntercomBridge.cpp').read_text(encoding='utf-8')
        parser=(ROOT/'src/apps/lua/LuaAppManifest.cpp').read_text(encoding='utf-8')
        web=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        for name in ['camera','microphone','intercom','door.open']:
            self.assertIn('"'+name+'"',parser);self.assertIn("'"+name+"'",web)
        self.assertIn('BackgroundExecuting',bridge);self.assertIn('AppPermission::DoorOpen',bridge)
        self.assertIn('job.owner=app->id()', ''.join(bridge.split()));self.assertIn('job.kind=5', ''.join(bridge.split()))
        self.assertNotIn('esp_codec_dev_',bridge);self.assertNotIn('xTaskCreate',bridge)
        route=web.split('esp_err_t WebServerService::intercomEventHandler')[1].split('esp_err_t WebServerService::appsHandler')[0]
        self.assertIn('writeAuthorized(req)',route);self.assertIn('strcmp(device.provider,"lan_event")',route)
        recovery=(ROOT/'src/services/RecoveryWebService.cpp').read_text(encoding='utf-8')
        self.assertNotIn('/api/intercom',recovery)

    def test_no_tasks_bounded_frames_and_owner_cleanup(self):
        video=(ROOT/'src/services/VideoService.cpp').read_text(encoding='utf-8')
        service=(ROOT/'src/services/IntercomService.cpp').read_text(encoding='utf-8')
        self.assertIn('submitTransfer(decodeJob',video);self.assertIn('addMaintenance',video)
        compact_video=''.join(video.split());compact_service=''.join(service.split())
        self.assertIn('baselineSps',video);self.assertIn('state_.generation==job.generation',compact_video)
        self.assertIn('resync_.exchange(false)',video);self.assertIn('images_[i].expired()',video)
        self.assertNotIn('lv_',video);self.assertNotIn('xTaskCreate',video+service)
        self.assertIn('call==state_.call&&!pending_',compact_service);self.assertIn('confirmed&&',compact_service)
        self.assertIn('providerCall_==call',compact_service);self.assertIn('doorConfirmCall',service)
        self.assertIn('registerOwnerCleanup',service);self.assertIn('getSecretString',service)
        self.assertNotIn('EventLog::',service)

if __name__=='__main__':unittest.main()

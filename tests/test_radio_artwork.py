from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]


class RadioArtworkTests(unittest.TestCase):
    def test_worker_decode_resource_failure_and_discard(self):
        source=(ROOT/'src/apps/lua/RadioLuaBridge.cpp').read_text(encoding='utf-8')
        methods=source.split('void discardArtwork(',1)[1].split('int artwork(',1)[0]
        helper=test_system_performance.SystemPerformanceTests()
        helper.native(helper.string_fixture()+r'''
#include <ArduinoJson.h>
struct Image{unsigned width=192,height=120;};
bool httpOk=true,decodeOk=true,poolOk=true;unsigned calls=0,decodes=0,releases=0;
struct LuaJobService{
 struct Request{String owner;const std::atomic<bool>* cancelled=nullptr;};
 struct Result{bool ok=false;String body,error;size_t size=0;};
 static void executeHttp(const Request&,Result& r){++calls;r.ok=httpOk;r.body="image bytes";if(!httpOk)r.error="timeout";}
};
struct ImageCodec{static std::shared_ptr<Image> decode(uint8_t*,size_t,unsigned w,unsigned h,bool cover,const std::atomic<bool>*,bool center,bool packed){
 assert(w==192&&h==192&&!cover&&!center&&packed);++decodes;return decodeOk?std::make_shared<Image>():nullptr;}};
struct ImageResourceService{
 static ImageResourceService& instance(){static ImageResourceService s;return s;}
 unsigned store(const String& owner,std::shared_ptr<Image>){assert(owner=="radio");return poolOk?7:0;}
 void release(const String& owner,unsigned id){assert(owner=="radio"&&id==7);++releases;}
};
void discardArtwork('''+methods+r'''
int main(){LuaJobService::Request request;request.owner="radio";std::atomic<bool> cancel{false};request.cancelled=&cancel;
 LuaJobService::Result result;executeArtwork(request,result);assert(result.ok&&calls==1&&decodes==1);
 discardArtwork("radio",result.body);assert(releases==1);
 httpOk=false;result={};executeArtwork(request,result);assert(!result.ok&&result.error=="timeout"&&decodes==1);
 httpOk=true;decodeOk=false;result={};executeArtwork(request,result);assert(!result.ok&&result.error=="radio_artwork_decode_or_limit");
 decodeOk=true;cancel=true;result={};executeArtwork(request,result);assert(!result.ok&&result.error=="cancelled");
 cancel=false;poolOk=false;result={};executeArtwork(request,result);assert(!result.ok&&result.error=="image_budget");
 discardArtwork("radio","bad JSON");assert(releases==1);
}
''',includes=(ROOT/'.pio/libdeps/home_display_p4_legacy_dev/ArduinoJson/src',))

    def test_only_configured_radio_metadata_can_be_queued(self):
        source=(ROOT/'src/apps/lua/RadioLuaBridge.cpp').read_text(encoding='utf-8')
        request=source.split('int artwork(',1)[1].split('int command(',1)[0]
        self.assertIn('request.url=s.graphicUri',request)
        self.assertNotIn('luaL_checkstring',request)
        for part in ('request.responseLimit=65536','Lane::Transfer','request.timeout=2500',
                     'request.required=perm(AppPermission::Audio)','request.owner=app->id()',
                     'request.discardResult=discardArtwork'):
            self.assertIn(part,request)
        self.assertIn('field(L,"artwork_uri",s.graphicUri)',source)
        self.assertNotIn('xTaskCreate',source)
        self.assertNotIn('lv_image_create',source)


if __name__=='__main__':unittest.main()

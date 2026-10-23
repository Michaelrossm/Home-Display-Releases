from pathlib import Path
import unittest
import test_printer_service as printer
import test_system_performance as native

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')

class PhotoPerformanceTests(unittest.TestCase):
    def test_render_bounds_orientation_and_rgb565(self):
        native.SystemPerformanceTests().native(read('src/services/ImageRenderPolicy.h')+read('src/services/ImageOrientation.h')+r'''
#include <cassert>
int main(){using P=hp::ImageRenderPolicy;
 for(unsigned edge:{800,1280,1600,1920})for(unsigned w:{1,16,800,1280,4000,8192})for(unsigned h:{1,16,800,1280,4000,8192}){
  auto s=P::fit(w,h,edge,edge);assert(s.width&&s.height&&s.width<=w&&s.height<=h&&s.width<=edge&&s.height<=edge);
  assert(uint64_t(s.width)*s.height<=P::maxPixels);
 }
 auto screen=P::fit(3840,2400,1280,1280);assert(screen.width==1280&&screen.height==800);
 auto portrait=P::fit(2400,3840,1280,1280);assert(portrait.width==800&&portrait.height==1280);
 auto thumb=P::fit(300,300,48,48);assert(thumb.width==48&&thumb.height==48);
 assert(!P::fit(0,100,1280,1280).width);assert(!P::quality(900)&&P::quality(1920));
 assert(P::rgb565(0,0,255)==0xf800&&P::rgb565(255,0,0)==0x001f&&P::rgb565(0,255,0)==0x07e0&&P::rgb565(255,255,255,0)==0);
 for(unsigned orientation=1;orientation<=8;++orientation){const unsigned width=3,height=2,lw=orientation>=5?height:width,lh=orientation>=5?width:height;bool seen[6]{};
  for(unsigned y=0;y<lh;++y)for(unsigned x=0;x<lw;++x){uint32_t px,py;hp::imageSourcePoint(width,height,orientation,x,y,px,py);assert(px<width&&py<height&&!seen[py*width+px]);seen[py*width+px]=true;}}
}
''')

    def test_pinch_focus_pan_clamp_and_reset(self):
        native.SystemPerformanceTests().native(read('src/apps/photos/PhotoZoom.h')+r'''
#include <cassert>
int main(){hp::PhotoZoom z;
 assert(!z.transform(2,100,0,0,900,400,1280,800,1280,800));
 assert(z.transform(2,200,0,0,900,400,1280,800,1280,800));assert(z.scale==512&&z.panX==-260&&z.panY==0);
 z.transform(2,200,0,0,950,400,1280,800,1280,800);assert(z.panX==-210);
 z.transform(1,0,100,100,0,0,1280,800,1280,800);z.transform(1,0,200,100,0,0,1280,800,1280,800);assert(z.panX==-110);
 z.transform(1,0,10000,-10000,0,0,1280,800,1280,800);assert(z.panX==640&&z.panY==-400);
 z.transform(0,0,0,0,0,0,1280,800,1280,800);z.reset();assert(z.scale==256&&!z.panX&&!z.panY&&!z.fingers);
 z.transform(2,100,0,0,640,400,1280,800,500,800);z.transform(2,200,0,0,640,400,1280,800,500,800);assert(z.panX==0);
 assert(!z.transform(2,200,0,0,640,400,1280,800,500,800));
}
''')

    def test_real_cache_latest_selection_pins_prefetch_pressure_and_failures(self):
        fake=r'''
#include <array>
constexpr int MALLOC_CAP_INTERNAL=4,pdTRUE=1,portMAX_DELAY=-1,LV_IMAGE_HEADER_MAGIC=1,LV_COLOR_FORMAT_RGB565=2,LV_COLOR_FORMAT_ARGB8888=4;
using SemaphoreHandle_t=void*;SemaphoreHandle_t xSemaphoreCreateMutex(){return (void*)1;}int xSemaphoreTake(void*,int){return pdTRUE;}void xSemaphoreGive(void*){}
size_t psramFree=24*1024*1024;size_t heap_caps_get_free_size(int cap){return cap==MALLOC_CAP_INTERNAL?150000:psramFree;}size_t heap_caps_get_largest_free_block(int){return 100000;}size_t heap_caps_get_minimum_free_size(int cap){return heap_caps_get_free_size(cap);}
struct lv_image_dsc_t {struct {uint32_t magic=0,cf=0,w=0,h=0,stride=0;}header;uint32_t data_size=0;const uint8_t* data=nullptr;};
namespace hp {
struct DecodedImage{uint8_t* pixels=nullptr;uint32_t width=0,height=0;bool rgb565=true;~DecodedImage(){free(pixels);}};
struct ImageSource{};
struct ImageRequest{std::atomic<bool> canceled{false},ready{false};std::shared_ptr<DecodedImage> image;std::shared_ptr<ImageSource> source;String message;uint32_t sourceWidth=0,sourceHeight=0,queueMs=0,readMs=0,decodeMs=0;};
struct FileBrowserService {
 struct Job{String uri;uint16_t edge;std::shared_ptr<ImageRequest> request;bool reused;};inline static std::vector<Job> jobs;inline static DirectoryPageCache::Revision media{1,1,1};
 static DirectoryPageCache::Revision directoryRevision(){return media;}
 static std::shared_ptr<ImageRequest> image(const String& uri,unsigned w,unsigned h,bool wallpaper,bool rgb565,std::shared_ptr<ImageSource> source){assert(w==h&&!wallpaper&&rgb565);auto r=std::make_shared<ImageRequest>();jobs.push_back({uri,uint16_t(w),r,bool(source)});return r;}
 static void finish(size_t index,bool fail=false,unsigned sourceEdge=2400){auto& job=jobs.at(index);auto& r=*job.request;now+=25;r.queueMs=3;r.readMs=8;r.decodeMs=14;r.sourceWidth=sourceEdge;r.sourceHeight=sourceEdge*5/8;
  if(!fail){const auto out=ImageRenderPolicy::fit(r.sourceWidth,r.sourceHeight,job.edge,job.edge);r.image=std::make_shared<DecodedImage>();r.image->width=out.width;r.image->height=out.height;r.image->pixels=(uint8_t*)malloc(size_t(out.width)*out.height*2);assert(r.image->pixels);r.image->pixels[0]=77;r.source=std::make_shared<ImageSource>();}else r.message="read_failed";r.ready=true;}
};}
'''
        body=r'''
int main(){using namespace hp;auto& pool=ImageResourceService::instance();assert(pool.begin());PhotoImageCache cache;
 cache.select("sd:/Bilder/a.jpg",1280);cache.poll("",true,false);assert(FileBrowserService::jobs.size()==1&&FileBrowserService::jobs[0].edge==480);
 cache.poll("",true,false);assert(FileBrowserService::jobs.size()==1);FileBrowserService::finish(0);cache.poll("",false,false);auto preview=cache.visible();assert(preview&&!cache.final());
 cache.poll("",true,false);assert(FileBrowserService::jobs.size()==2&&FileBrowserService::jobs[1].reused);FileBrowserService::finish(1);cache.poll("",true,false);auto displayed=cache.visible();assert(displayed&&displayed!=preview&&cache.final());
 cache.select("sd:/Bilder/b.jpg",1280);cache.poll("",true,false);size_t b=FileBrowserService::jobs.size()-1;
 cache.select("sd:/Bilder/c.jpg",1280);cache.poll("",true,false);assert(FileBrowserService::jobs[b].request->canceled);size_t c=FileBrowserService::jobs.size()-1;
 FileBrowserService::finish(b);cache.poll("",false,false);assert(!cache.visible()); // stale result never becomes selected
 FileBrowserService::finish(c);cache.poll("",false,false);assert(cache.visible());assert(displayed->resource->pixels->pixels[0]==77);
 cache.select("sd:/Bilder/a.jpg",1280);size_t before=FileBrowserService::jobs.size();cache.poll("",true,false);assert(cache.final()&&FileBrowserService::jobs.size()==before);
 cache.poll("sd:/Bilder/d.jpg",true,true);size_t d=FileBrowserService::jobs.size()-1;assert(cache.busy());
 cache.select("sd:/Bilder/d.jpg",1280);FileBrowserService::finish(d,true);cache.poll("",true,false);before=FileBrowserService::jobs.size();cache.poll("",true,false);assert(cache.failed()&&FileBrowserService::jobs.size()==before);
 cache.select("sd:/Bilder/a.jpg",1280);cache.poll("sd:/Bilder/e.jpg",true,true);auto speculative=FileBrowserService::jobs.back().request;psramFree=4*1024*1024;cache.poll("sd:/Bilder/e.jpg",true,true);assert(speculative->canceled&&cache.visible());psramFree=24*1024*1024;
 cache.select("sd:/Bilder/f.jpg",1280);cache.poll("",true,false);auto loading=FileBrowserService::jobs.back().request;++FileBrowserService::media.usb;cache.poll("",true,false);assert(loading->canceled&&!cache.visible());assert(displayed->resource->pixels->pixels[0]==77);
 auto unavailable=FileBrowserService::jobs.size()-1;FileBrowserService::finish(unavailable,true);cache.poll("",true,false);assert(cache.failed());
 cache.clear();assert(displayed->resource->pixels->pixels[0]==77);preview.reset();
 auto large=std::make_shared<DecodedImage>();large->width=1536;large->height=2048;large->pixels=(uint8_t*)malloc(1536*2048*2);auto blocker=pool.lease("other",large);large.reset();assert(blocker);
 cache.select("usb:/Bilder/oom.jpg",1280);cache.poll("",true,false);FileBrowserService::finish(FileBrowserService::jobs.size()-1);cache.poll("",true,false);assert(cache.failed()&&!cache.visible()&&displayed->resource->pixels->pixels[0]==77);
 cache.clear();blocker.reset();displayed.reset();pool.releaseOwner("native.photos");
 for(unsigned i=0;i<100;++i){PhotoImageCache opened;opened.select("usb:/Bilder/small.jpg",800);opened.poll("",true,false);FileBrowserService::finish(FileBrowserService::jobs.size()-1,false,100);opened.poll("",true,false);assert(opened.final()&&opened.visible());opened.clear();pool.releaseOwner("native.photos");}
 JsonDocument report;assert(!deserializeJson(report,PhotoImageCache::metricsJson()));assert(report["cache_peak_bytes"].as<unsigned>()<=PhotoImageCache::budget);assert(report["first_ready"]["samples"].as<unsigned>()>0);
}
'''
        code=printer.stripped('src/core/DirectoryPageCache.h')+printer.stripped('src/services/ImageRenderPolicy.h')+fake+printer.stripped('src/core/LatencyHistogram.h')+printer.stripped('src/services/ImageResourceService.h')+printer.stripped('src/services/ImageResourceService.cpp')+printer.stripped('src/services/PhotoImageCache.h')+printer.stripped('src/services/PhotoImageCache.cpp')+body
        printer.PrinterServiceTests().native(code)

    def test_actual_fullscreen_power_lease_is_released_idempotently(self):
        app=read('src/apps/photos/PhotoApp.cpp');method=app.split('void fullscreenPower(',1)[1].split('void present(',1)[0]
        native.SystemPerformanceTests().native(r'''
#include <cassert>
#include <cstdint>
uint32_t now=1;uint32_t millis(){return now;}
enum class IdleLease{PhotoViewer};struct IdleService{unsigned lease=0;static IdleService& instance(){static IdleService s;return s;}void keepAwake(IdleLease,unsigned duration){lease=duration;}};
struct PowerManager{unsigned users=0;static PowerManager& instance(){static PowerManager p;return p;}bool acquirePerformance(){++users;return true;}void releasePerformance(){assert(users);--users;}};
struct PhotoPage{bool performance=false;uint32_t lastLease=0;};
void fullscreenPower('''+method+r'''
int main(){PhotoPage p;fullscreenPower(p,true);fullscreenPower(p,true);assert(PowerManager::instance().users==1&&IdleService::instance().lease==5000);now=2000;fullscreenPower(p,true);assert(PowerManager::instance().users==1);fullscreenPower(p,false);fullscreenPower(p,false);assert(!PowerManager::instance().users&&!IdleService::instance().lease);}
''')
        self.assertIn('fullscreenPower(*p,false)',app)
        self.assertIn('p.cache.clear();fullscreenPower(p,false);return;',app)

    def test_decoder_and_swap_do_not_change_bsp_buffers_or_create_tasks(self):
        codec=read('src/services/ImageCodec.cpp');app=read('src/apps/photos/PhotoApp.cpp')
        self.assertIn('JPEG_PIXEL_FORMAT_RGB565_LE',codec)
        self.assertIn('cfg.options.use_scaling=1',codec)
        self.assertIn('cfg.options.use_threads=0',codec)
        self.assertIn('png_read_row(png,s->row,nullptr)',codec)
        self.assertIn('lv_obj_delete_async(previous)',app)
        self.assertIn('p.cache.seed(',app)
        self.assertLess(app.index('p.picture=image;p.shownLease='),app.index('lv_obj_delete_async(previous)'))
        for forbidden in ('lv_timer_handler','xTaskCreate','esp_lcd_panel_draw_bitmap','bsp_display_start'):
            self.assertNotIn(forbidden,app+read('src/services/PhotoImageCache.cpp'))

if __name__=='__main__':unittest.main()

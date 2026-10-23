#include "ImageCodec.h"
#include "esp_heap_caps.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <chrono>
#define CHECK(x) do{if(!(x)){std::cerr<<"check failed: "<<__LINE__<<" "<<#x;return 1;}}while(false)
std::vector<uint8_t> bytes(const char* path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(int argc,char** argv){
    CHECK(argc==6);auto png=bytes(argv[1]),webp=bytes(argv[2]),jpeg=bytes(argv[3]);CHECK(!png.empty()&&!webp.empty()&&!jpeg.empty());
    using namespace codecHeap;using Clock=std::chrono::steady_clock;
    reset();auto start=Clock::now();auto baseline=hp::ImageCodec::decode(png.data(),png.size(),800,800);
    auto beforeUs=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();CHECK(baseline&&baseline->width==800&&baseline->height==500&&!baseline->rgb565);size_t beforePeak=peak;baseline.reset();CHECK(!live);
    reset();start=Clock::now();auto photo=hp::ImageCodec::decode(png.data(),png.size(),800,800,false,nullptr,false,true);
    auto afterUs=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();CHECK(photo&&photo->rgb565&&photo->width==800&&photo->height==500);
    const size_t afterPeak=peak;CHECK(afterPeak<2*1024*1024&&beforePeak>afterPeak*4);auto* pixels=(uint16_t*)photo->pixels;CHECK(pixels[0]==0xf800&&pixels[799]==0x0400);photo.reset();CHECK(!live);
    for(size_t failure=1;failure<=20;++failure){reset();failAt=failure;auto image=hp::ImageCodec::decode(png.data(),png.size(),800,800,false,nullptr,false,true);image.reset();CHECK(!live);}
    reset();std::atomic<bool> cancelled{false};stop=&cancelled;stopAt=4;auto interrupted=hp::ImageCodec::decode(png.data(),png.size(),800,800,false,&cancelled,false,true);CHECK(!interrupted&&!live);
    reset();auto broken=png;broken.resize(broken.size()-8);auto damaged=hp::ImageCodec::decode(broken.data(),broken.size(),800,800,false,nullptr,false,true);CHECK(!damaged&&!live);
    reset();auto web=hp::ImageCodec::decode(webp.data(),webp.size(),800,800,false,nullptr,false,true);CHECK(web&&web->rgb565&&web->width==800&&web->height==500);web.reset();CHECK(!live);
    reset();auto portrait=hp::ImageCodec::decode(jpeg.data(),jpeg.size(),800,800,false,nullptr,false,true);CHECK(portrait&&portrait->rgb565&&portrait->width==500&&portrait->height==800);portrait.reset();CHECK(!live);
    reset();auto exact=hp::ImageCodec::decode(jpeg.data(),jpeg.size(),1600,1600,false,nullptr,false,true);CHECK(exact&&exact->width==1000&&exact->height==1600);exact.reset();CHECK(!live);
    for(int arg=4;arg<6;++arg){reset();auto source=bytes(argv[arg]);auto filtered=hp::ImageCodec::decode(source.data(),source.size(),8,8,false,nullptr,false,true);
        CHECK(filtered&&filtered->width==8&&filtered->height==8);const auto* samples=reinterpret_cast<const uint16_t*>(filtered->pixels);
        for(unsigned i=0;i<64;++i)CHECK(samples[i]==0x8410); // Stripes average to grey, not nearest-neighbour black/white.
        filtered.reset();CHECK(!live);
    }
    std::cout<<"{\"host_png_before_us\":"<<beforeUs<<",\"host_png_after_us\":"<<afterUs<<",\"before_tracked_peak_bytes\":"<<beforePeak<<",\"after_tracked_peak_bytes\":"<<afterPeak<<"}";
}

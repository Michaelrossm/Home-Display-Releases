#include "../../../src/services/ImageCodec.h"
#include <fstream>
#include <vector>
#include <cassert>
#include <string>
int main(int argc,char** argv){assert(argc>=2);
    for(int i=1;i<argc;++i){std::ifstream file(argv[i],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        assert(file&&hp::ImageCodec::supported(argv[i]));uint32_t w=0,h=0;
        assert(hp::ImageCodec::dimensions(bytes.data(),bytes.size(),w,h));assert(w==16&&h==12);
        const bool rotated=std::string(argv[i]).find("rot90")!=std::string::npos;
        auto preview=hp::ImageCodec::decode(bytes.data(),bytes.size(),8,8);assert(preview&&preview->width==(rotated?6:8)&&preview->height==(rotated?8:6));
        auto packed=hp::ImageCodec::decode(bytes.data(),bytes.size(),8,8,false,nullptr,false,true);
        assert(packed&&packed->rgb565&&packed->width<=8&&packed->height<=8);
        assert(rotated?packed->height>packed->width:packed->width>packed->height);
        assert(packed->width*packed->height<=64);
        if(rotated)assert(preview->pixels[1]>180&&preview->pixels[2]<40);
        auto cover=hp::ImageCodec::decode(bytes.data(),bytes.size(),10,10,true);assert(cover&&cover->width==10&&cover->height==10);
        for(size_t n=3;n<cover->width*cover->height*4;n+=4)assert(cover->pixels[n]==255);
    }
    uint8_t junk[40]={};assert(!hp::ImageCodec::decode(junk,sizeof(junk),100,100));
    assert(!hp::ImageCodec::supported("photo.heic"));assert(!hp::ImageCodec::supported("document.svg"));
    assert(!hp::ImageCodec::decode(junk,sizeof(junk),0,100));
}

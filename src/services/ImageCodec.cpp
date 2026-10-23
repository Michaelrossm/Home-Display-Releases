#include "ImageCodec.h"
#include "ImageOrientation.h"
#include "ImageRenderPolicy.h"
#include "QoiDecoder.h"
#include "../apps/lua/LuaImagePolicy.h"
#include <esp_heap_caps.h>
#include <png.h>
#include <webp/decode.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#ifdef ESP_PLATFORM
#include <esp_jpeg_dec.h>
#endif
// Only the private WebP component remaps these symbols. The rest of the
// firmware's malloc/FreeRTOS/LVGL allocators remain unchanged.
extern "C" void* hp_codec_malloc(size_t n){return n<=16*1024*1024?heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):nullptr;}
extern "C" void* hp_codec_calloc(size_t count,size_t size){if(size&&count>16*1024*1024/size)return nullptr;return heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
extern "C" void* hp_codec_realloc(void* p,size_t n){return n<=16*1024*1024?heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):nullptr;}
extern "C" void hp_codec_free(void* p){heap_caps_free(p);}
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(n) heap_caps_malloc((n),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define STBI_REALLOC(p,n) heap_caps_realloc((p),(n),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define STBI_FREE(p) heap_caps_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
namespace hp {
#ifdef ESP_PLATFORM
namespace {
uint8_t* scaledJpeg(uint8_t* bytes,size_t size,int& width,int& height,uint32_t maxWidth,uint32_t maxHeight,bool cover,unsigned orientation,bool rgb565=false){
    const double requestedW=orientation>=5?maxHeight:maxWidth,requestedH=orientation>=5?maxWidth:maxHeight;
    double scale=cover?std::max(requestedW/width,requestedH/height):std::min(requestedW/width,requestedH/height);
    scale=std::max(.125,std::min(1.0,scale));uint32_t outWidth=width,outHeight=height;
    jpeg_dec_config_t config=DEFAULT_JPEG_DEC_CONFIG();config.output_type=rgb565?JPEG_PIXEL_FORMAT_RGB565_LE:JPEG_PIXEL_FORMAT_RGB888;
    if(scale<.99&&width>=8&&height>=8){outWidth=std::min<uint32_t>(uint32_t(width)/8*8,uint32_t(std::ceil(width*scale/8))*8);outHeight=std::min<uint32_t>(uint32_t(height)/8*8,uint32_t(std::ceil(height*scale/8))*8);
        config.scale.width=outWidth;config.scale.height=outHeight;}
    if(!outWidth||!outHeight||uint64_t(outWidth)*outHeight>ImageCodec::maxSourcePixels)return nullptr;
    struct Handle{jpeg_dec_handle_t value=nullptr;~Handle(){if(value)jpeg_dec_close(value);}} handle;
    if(jpeg_dec_open(&config,&handle.value)!=JPEG_ERR_OK)return nullptr;
    jpeg_dec_io_t io{};io.inbuf=bytes;io.inbuf_len=size;jpeg_dec_header_info_t header{};int length=0;
    if(jpeg_dec_parse_header(handle.value,&io,&header)!=JPEG_ERR_OK||jpeg_dec_get_outbuf_len(handle.value,&length)!=JPEG_ERR_OK||length!=int(outWidth*outHeight*(rgb565?2:3)))return nullptr;
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> rgb(static_cast<uint8_t*>(heap_caps_aligned_alloc(16,length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    if(!rgb)return nullptr;io.outbuf=rgb.get();if(jpeg_dec_process(handle.value,&io)!=JPEG_ERR_OK)return nullptr;
    if(rgb565){width=outWidth;height=outHeight;return rgb.release();}
    auto* bgra=static_cast<uint8_t*>(heap_caps_malloc(size_t(outWidth)*outHeight*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!bgra)return nullptr;
    for(size_t i=0;i<size_t(outWidth)*outHeight;++i){bgra[i*4]=rgb.get()[i*3+2];bgra[i*4+1]=rgb.get()[i*3+1];bgra[i*4+2]=rgb.get()[i*3];bgra[i*4+3]=255;}
    width=outWidth;height=outHeight;return bgra;
}
}
#endif
namespace {
bool photoHeadroom(size_t bytes){return heap_caps_get_free_size(MALLOC_CAP_SPIRAM)>bytes+4*1024*1024&&heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)>=bytes;}
std::shared_ptr<DecodedImage> render565(uint8_t* raw,uint32_t w,uint32_t h,bool packed,unsigned orientation,uint32_t maxWidth,uint32_t maxHeight,const std::atomic<bool>* cancel){
    const auto out=ImageRenderPolicy::fit(orientation>=5?h:w,orientation>=5?w:h,maxWidth,maxHeight);
    if(!out.width||!out.height)return {};
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)<size_t(out.width)*out.height*2+2*1024*1024)return {};
    auto result=std::make_shared<DecodedImage>();result->width=out.width;result->height=out.height;result->rgb565=true;
    result->pixels=static_cast<uint8_t*>(heap_caps_malloc(size_t(out.width)*out.height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!result->pixels)return {};
    const uint32_t lw=orientation>=5?h:w,lh=orientation>=5?w:h;
    auto* pixels=reinterpret_cast<uint16_t*>(result->pixels);
    if(!packed&&orientation==1&&w==out.width&&h==out.height){for(uint32_t y=0;y<h;++y){if(cancel&&cancel->load())return {};for(uint32_t x=0;x<w;++x){const auto* c=raw+(size_t(y)*w+x)*4;pixels[size_t(y)*w+x]=ImageRenderPolicy::rgb565(c[0],c[1],c[2],c[3]);}}return result;}
    // Rotation without resizing needs no interpolation or RGB565 unpacking.
    if(out.width==lw&&out.height==lh){for(uint32_t y=0;y<lh;++y){if(cancel&&cancel->load())return {};
        for(uint32_t x=0;x<lw;++x){uint32_t px,py;imageSourcePoint(w,h,orientation,x,y,px,py);const size_t at=size_t(py)*w+px;
            if(packed)pixels[size_t(y)*lw+x]=reinterpret_cast<const uint16_t*>(raw)[at];
            else{const auto* c=raw+at*4;pixels[size_t(y)*lw+x]=ImageRenderPolicy::rgb565(c[0],c[1],c[2],c[3]);}
        }}return result;}
    auto sample=[&](uint32_t x,uint32_t y){uint32_t px,py;imageSourcePoint(w,h,orientation,x,y,px,py);const size_t at=size_t(py)*w+px;
        if(packed){const uint16_t p=reinterpret_cast<uint16_t*>(raw)[at];return uint32_t(p&31)*255/31|((uint32_t(p>>5)&63)*255/63)<<8|uint32_t(p>>11)*255/31<<16;}
        const auto* c=raw+at*4;return uint32_t(c[0])*c[3]/255|(uint32_t(c[1])*c[3]/255)<<8|(uint32_t(c[2])*c[3]/255)<<16;
    };
    auto coordinate=[](uint32_t i,uint32_t size,uint32_t target){return uint32_t(std::max<int64_t>(0,std::min<int64_t>(int64_t(size-1)*256,int64_t(2*i+1)*size*256/(2*target)-128)));};
    for(uint32_t y=0;y<out.height;++y){if(cancel&&cancel->load())return {};
        const uint32_t sy=coordinate(y,lh,out.height),y0=sy>>8,y1=std::min(lh-1,y0+1),fy=sy&255;
        for(uint32_t x=0;x<out.width;++x){const uint32_t sx=coordinate(x,lw,out.width),x0=sx>>8,x1=std::min(lw-1,x0+1),fx=sx&255;
            const uint32_t a=sample(x0,y0),b=sample(x1,y0),c=sample(x0,y1),d=sample(x1,y1);uint8_t rgb[3];
            for(unsigned channel=0;channel<3;++channel){const unsigned shift=channel*8;
                const uint32_t top=((a>>shift)&255)*(256-fx)+((b>>shift)&255)*fx,bottom=((c>>shift)&255)*(256-fx)+((d>>shift)&255)*fx;
                rgb[channel]=(top*(256-fy)+bottom*fy+32768)>>16;}
            pixels[size_t(y)*out.width+x]=ImageRenderPolicy::rgb565(rgb[0],rgb[1],rgb[2]);}
    }return result;
}
// PNG has no reduced-IDCT mode. Non-interlaced images are consumed a row at a
// time, retaining one source row + the final RGB565 surface, not a full BGRA original.
uint8_t* pngRows565(const uint8_t* bytes,size_t size,uint32_t width,uint32_t height,ImageRenderPolicy::Size out,const std::atomic<bool>* cancel){
    if(size<29||bytes[28]!=PNG_INTERLACE_NONE)return nullptr;
    struct State{const uint8_t* data;size_t size,offset;uint8_t* row;uint8_t* pixels;uint32_t* sums;};
    auto* s=static_cast<State*>(hp_codec_calloc(1,sizeof(State)));if(!s)return nullptr;s->data=bytes;s->size=size;
    auto* png=png_create_read_struct_2(PNG_LIBPNG_VER_STRING,nullptr,[](png_structp p,png_const_charp){png_longjmp(p,1);},[](png_structp,png_const_charp){},nullptr,
        [](png_structp,png_alloc_size_t n)->png_voidp{return hp_codec_malloc(n);},[](png_structp,png_voidp p){hp_codec_free(p);});
    auto* info=png?png_create_info_struct(png):nullptr;
    if(!png||!info){if(png)png_destroy_read_struct(&png,nullptr,nullptr);hp_codec_free(s);return nullptr;}
    if(setjmp(png_jmpbuf(png))){hp_codec_free(s->row);hp_codec_free(s->pixels);hp_codec_free(s->sums);png_destroy_read_struct(&png,&info,nullptr);hp_codec_free(s);return nullptr;}
    png_set_read_fn(png,s,[](png_structp p,png_bytep dst,png_size_t n){auto* s=static_cast<State*>(png_get_io_ptr(p));if(n>s->size-s->offset)png_error(p,"PNG truncated");memcpy(dst,s->data+s->offset,n);s->offset+=n;});
    png_set_user_limits(png,8192,8192);png_set_chunk_malloc_max(png,65536);png_set_chunk_cache_max(png,8);png_read_info(png,info);
    if(png_get_image_width(png,info)!=width||png_get_image_height(png,info)!=height)png_error(png,"PNG dimensions");
    const int color=png_get_color_type(png,info),depth=png_get_bit_depth(png,info);const bool alpha=png_get_valid(png,info,PNG_INFO_tRNS);
    if(depth==16)png_set_strip_16(png);if(color==PNG_COLOR_TYPE_PALETTE)png_set_palette_to_rgb(png);
    if(color==PNG_COLOR_TYPE_GRAY&&depth<8)png_set_expand_gray_1_2_4_to_8(png);
    if(alpha)png_set_tRNS_to_alpha(png);if(color==PNG_COLOR_TYPE_GRAY||color==PNG_COLOR_TYPE_GRAY_ALPHA)png_set_gray_to_rgb(png);
    if(!(color&PNG_COLOR_MASK_ALPHA)&&!alpha)png_set_filler(png,255,PNG_FILLER_AFTER);png_set_bgr(png);png_read_update_info(png,info);
    if(png_get_rowbytes(png,info)!=width*4)png_error(png,"PNG row format");
    s->row=static_cast<uint8_t*>(hp_codec_malloc(width*4+out.width*3));s->pixels=static_cast<uint8_t*>(hp_codec_malloc(size_t(out.width)*out.height*2));s->sums=static_cast<uint32_t*>(hp_codec_calloc(out.width*3,sizeof(uint32_t)));if(!s->row||!s->pixels||!s->sums)png_error(png,"PNG memory");
    uint32_t target=0;
    for(uint32_t y=0;y<height;++y){if(cancel&&cancel->load())png_error(png,"PNG cancelled");png_read_row(png,s->row,nullptr);
        auto* filtered=s->row+width*4;
        for(uint32_t x=0;x<out.width;++x){const uint32_t left=x*width,right=(x+1)*width;uint32_t rgb[3]{};
            for(uint32_t source=left/out.width;source<(right+out.width-1)/out.width;++source){const uint32_t weight=std::min(right,(source+1)*out.width)-std::max(left,source*out.width);const auto* c=s->row+source*4;
                for(unsigned channel=0;channel<3;++channel)rgb[channel]+=uint32_t(c[channel])*c[3]/255*weight;}
            for(unsigned channel=0;channel<3;++channel)filtered[x*3+channel]=(rgb[channel]+width/2)/width;
        }
        uint32_t start=y*out.height;const uint32_t end=(y+1)*out.height;
        while(start<end&&target<out.height){const uint32_t until=std::min(end,(target+1)*height),weight=until-start;
            for(uint32_t x=0;x<out.width*3;++x)s->sums[x]+=uint32_t(filtered[x])*weight;
            if(until==(target+1)*height){auto* dst=reinterpret_cast<uint16_t*>(s->pixels)+size_t(target)*out.width;
                for(uint32_t x=0;x<out.width;++x)dst[x]=ImageRenderPolicy::rgb565((s->sums[x*3]+height/2)/height,(s->sums[x*3+1]+height/2)/height,(s->sums[x*3+2]+height/2)/height);
                memset(s->sums,0,out.width*3*sizeof(uint32_t));++target;}
            start=until;
        }}
    png_read_end(png,info);uint8_t* result=s->pixels;hp_codec_free(s->row);hp_codec_free(s->sums);png_destroy_read_struct(&png,&info,nullptr);hp_codec_free(s);return result;
}
}
DecodedImage::~DecodedImage(){heap_caps_free(pixels);}
bool ImageCodec::supported(const char* path){if(!path)return false;std::string name=path;for(auto& c:name)if(c>='A'&&c<='Z')c+=32;
    const auto dot=name.rfind('.');if(dot==name.npos)return false;const auto ext=name.substr(dot);
    return ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp"||ext==".gif"||ext==".webp"||ext==".tga"||ext==".qoi";}
bool ImageCodec::dimensions(const uint8_t* bytes,size_t size,uint32_t& width,uint32_t& height){
    if(!bytes||size<12||size>maxFileBytes)return false;int w=0,h=0,channels=0;
    if(!memcmp(bytes,"qoif",4))return qoiDimensions(bytes,size,width,height);
    if(!memcmp(bytes,"\x89PNG\r\n\x1a\n",8)){if(!LuaImagePolicy::pngSize(bytes,size,width,height,8192,maxFileBytes))return false;return uint64_t(width)*height<=maxSourcePixels;}
    if(!memcmp(bytes,"RIFF",4)&&!memcmp(bytes+8,"WEBP",4)){if(!WebPGetInfo(bytes,size,&w,&h))return false;}
    else if(!stbi_info_from_memory(bytes,static_cast<int>(size),&w,&h,&channels))return false;
    const bool jpeg=bytes[0]==255&&bytes[1]==216;
    if(w<1||h<1||w>8192||h>8192||uint64_t(w)*h>(jpeg?64*1024*1024:maxSourcePixels))return false;width=w;height=h;return true;
}
std::shared_ptr<DecodedImage> ImageCodec::decode(uint8_t* bytes,size_t size,uint32_t maxWidth,uint32_t maxHeight,bool cover,const std::atomic<bool>* cancelled,bool center,bool rgb565){
    if(cancelled&&cancelled->load())return {};
    if(!bytes||size<12||size>maxFileBytes||!maxWidth||!maxHeight||maxWidth>(rgb565?ImageRenderPolicy::maxEdge:1280)||maxHeight>(rgb565?ImageRenderPolicy::maxEdge:1280)|| (rgb565&&(cover||center)))return {};
    const bool png=!memcmp(bytes,"\x89PNG\r\n\x1a\n",8),webp=!memcmp(bytes,"RIFF",4)&&!memcmp(bytes+8,"WEBP",4);
    const unsigned orientation=jpegOrientation(bytes,size);
    int w=0,h=0,components=0;uint8_t* raw=nullptr;bool packed=false;
    auto valid=[&](){return w>0&&h>0&&w<=8192&&h<=8192&&uint64_t(w)*h<=maxSourcePixels;};
    if(!memcmp(bytes,"qoif",4)){
        uint32_t qw,qh;if(!qoiDimensions(bytes,size,qw,qh))return {};w=qw;h=qh;
        if(rgb565&&!photoHeadroom(size_t(w)*h*4))return {};
        raw=static_cast<uint8_t*>(heap_caps_malloc(size_t(w)*h*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!raw||!qoiDecodeBgra(bytes,size,raw,size_t(w)*h*4,cancelled)){heap_caps_free(raw);return {};}
    }else if(png){
        // The shared metadata policy is also used for native, larger pictures.
        // Validate the native header separately; policy dimensions are Lua-specific.
        if(size<33)return {};auto be=[](const uint8_t* p){return(uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];};
        w=be(bytes+16);h=be(bytes+20);if(!valid())return {};
        if(rgb565&&bytes[28]==PNG_INTERLACE_NONE){const auto out=ImageRenderPolicy::fit(w,h,maxWidth,maxHeight);auto result=std::make_shared<DecodedImage>();result->width=out.width;result->height=out.height;result->rgb565=true;result->pixels=pngRows565(bytes,size,w,h,out,cancelled);return result->pixels?result:std::shared_ptr<DecodedImage>{};}
        if(rgb565&&!photoHeadroom(size_t(w)*h*4))return {};
        if(!LuaImagePolicy::stripMetadata(bytes,size,8192,maxFileBytes))return {};
        png_image image{};image.version=PNG_IMAGE_VERSION;
        if(!png_image_begin_read_from_memory(&image,bytes,size)){png_image_free(&image);return {};}
        if(image.width!=uint32_t(w)||image.height!=uint32_t(h)){png_image_free(&image);return {};}
        image.format=PNG_FORMAT_BGRA;raw=static_cast<uint8_t*>(heap_caps_malloc(size_t(w)*h*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        const bool ok=raw&&png_image_finish_read(&image,nullptr,raw,0,nullptr);png_image_free(&image);if(!ok){heap_caps_free(raw);return {};}
    }else if(webp){if(!WebPGetInfo(bytes,size,&w,&h)||!valid())return {};
        if(rgb565){const auto out=ImageRenderPolicy::fit(w,h,maxWidth,maxHeight);w=out.width;h=out.height;if(!photoHeadroom(size_t(w)*h*6))return {};WebPDecoderConfig cfg;if(!WebPInitDecoderConfig(&cfg))return {};
            cfg.options.use_scaling=1;cfg.options.scaled_width=w;cfg.options.scaled_height=h;cfg.options.use_threads=0;cfg.output.colorspace=MODE_BGRA;cfg.output.is_external_memory=1;
            raw=static_cast<uint8_t*>(heap_caps_malloc(size_t(w)*h*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!raw)return {};
            cfg.output.u.RGBA.rgba=raw;cfg.output.u.RGBA.stride=w*4;cfg.output.u.RGBA.size=size_t(w)*h*4;
            if(WebPDecode(bytes,size,&cfg)!=VP8_STATUS_OK){WebPFreeDecBuffer(&cfg.output);heap_caps_free(raw);return {};}WebPFreeDecBuffer(&cfg.output);
        }else{
        raw=static_cast<uint8_t*>(heap_caps_malloc(size_t(w)*h*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!raw||!WebPDecodeBGRAInto(bytes,size,raw,size_t(w)*h*4,w*4)){heap_caps_free(raw);return {};}}
    }else{if(!stbi_info_from_memory(bytes,static_cast<int>(size),&w,&h,&components)||w<1||h<1||w>8192||h>8192)return {};
#ifdef ESP_PLATFORM
        if(!center&&bytes[0]==255&&bytes[1]==216&&uint64_t(w)*h<=64*1024*1024&&(rgb565||uint64_t(w)*h>512*512)){const auto out=ImageRenderPolicy::fit(orientation>=5?h:w,orientation>=5?w:h,maxWidth,maxHeight);raw=scaledJpeg(bytes,size,w,h,rgb565?out.width:maxWidth,rgb565?out.height:maxHeight,cover,orientation,rgb565);packed=rgb565&&raw;}
#endif
        if(!raw){if(!valid()||(rgb565&&!photoHeadroom(size_t(w)*h*4)))return {};raw=stbi_load_from_memory(bytes,static_cast<int>(size),&w,&h,&components,4);if(!raw)return {};
            for(size_t i=0;i<size_t(w)*h*4;i+=4)std::swap(raw[i],raw[i+2]);}
    }
    std::unique_ptr<uint8_t,decltype(&heap_caps_free)> original(raw,heap_caps_free);
    if(cancelled&&cancelled->load())return {};
    if(rgb565){const auto out=ImageRenderPolicy::fit(w,h,maxWidth,maxHeight);if(packed&&orientation==1&&out.width==uint32_t(w)&&out.height==uint32_t(h)){auto result=std::make_shared<DecodedImage>();result->width=w;result->height=h;result->rgb565=true;result->pixels=original.release();return result;}return render565(raw,w,h,packed,orientation,maxWidth,maxHeight,cancelled);}
    auto result=std::make_shared<DecodedImage>();
    const uint32_t logicalWidth=orientation>=5?h:w,logicalHeight=orientation>=5?w:h;
    const double scale=center?1.0:cover?std::max(double(maxWidth)/logicalWidth,double(maxHeight)/logicalHeight):std::min(1.0,std::min(double(maxWidth)/logicalWidth,double(maxHeight)/logicalHeight));
    result->width=center?std::min(logicalWidth,maxWidth):cover?maxWidth:std::max<uint32_t>(1,uint32_t(logicalWidth*scale));result->height=center?std::min(logicalHeight,maxHeight):cover?maxHeight:std::max<uint32_t>(1,uint32_t(logicalHeight*scale));
    if(!cover&&orientation==1&&result->width==uint32_t(w)&&result->height==uint32_t(h)){result->pixels=original.release();return result;}
    result->pixels=static_cast<uint8_t*>(heap_caps_malloc(size_t(result->width)*result->height*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!result->pixels)return {};
    const double left=(cover||center)?(logicalWidth-result->width/scale)/2:0,top=(cover||center)?(logicalHeight-result->height/scale)/2:0;
    auto sample=[&](uint32_t x,uint32_t y,unsigned c){uint32_t px,py;imageSourcePoint(w,h,orientation,x,y,px,py);
        return raw[(size_t(py)*w+px)*4+c];};
    // Bilinear resampling is one-time worker work, never part of a frame refresh.
    for(uint32_t y=0;y<result->height;++y){
        if(cancelled&&cancelled->load())return {};
        for(uint32_t x=0;x<result->width;++x){
        const double sx=std::max(0.0,std::min(double(logicalWidth-1),left+(x+.5)/scale-.5)),sy=std::max(0.0,std::min(double(logicalHeight-1),top+(y+.5)/scale-.5));
        const uint32_t x0=sx,y0=sy,x1=std::min(x0+1,logicalWidth-1),y1=std::min(y0+1,logicalHeight-1);const double dx=sx-x0,dy=sy-y0;
        for(unsigned c=0;c<4;++c)result->pixels[(size_t(y)*result->width+x)*4+c]=uint8_t((1-dy)*((1-dx)*sample(x0,y0,c)+dx*sample(x1,y0,c))+dy*((1-dx)*sample(x0,y1,c)+dx*sample(x1,y1,c)));
    }}return result;
}
}

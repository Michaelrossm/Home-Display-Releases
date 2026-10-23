#include "QrGenerator.h"
#include <src/libs/qrcode/qrcodegen.h>
#include <memory>
#include <algorithm>
#include <new>
#include <mbedtls/platform_util.h>
namespace hp {
bool QrGenerator::matrix(const String& payload,int& size,String& bits){
    size=0;bits="";if(!payload.length()||payload.length()>maxPayload)return false;
    constexpr size_t bytes=qrcodegen_BUFFER_LEN_FOR_VERSION(33);
    std::unique_ptr<uint8_t[]> buffers(new(std::nothrow) uint8_t[bytes*2]{});if(!buffers)return false;
    auto* scratch=buffers.get();auto* qr=scratch+bytes;
    const bool ok=qrcodegen_encodeText(payload.c_str(),scratch,qr,qrcodegen_Ecc_LOW,1,33,qrcodegen_Mask_AUTO,true);
    mbedtls_platform_zeroize(scratch,bytes);if(!ok){mbedtls_platform_zeroize(qr,bytes);return false;}
    size=qrcodegen_getSize(qr);if(!bits.reserve((size*size+3)/4)){mbedtls_platform_zeroize(qr,bytes);size=0;return false;}
    const char* hex="0123456789abcdef";unsigned nibble=0,count=0;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){nibble=(nibble<<1)|unsigned(qrcodegen_getModule(qr,x,y));if(++count==4){bits+=hex[nibble];count=0;nibble=0;}}
    if(count)bits+=hex[nibble<<(4-count)];mbedtls_platform_zeroize(qr,bytes);return true;
}
bool QrGenerator::update(lv_obj_t* qr,const String& payload){
    int size=0;String bits;if(!matrix(payload,size,bits))return false;
    auto* draw=lv_canvas_get_draw_buf(qr);if(!draw||draw->header.cf!=LV_COLOR_FORMAT_I1){mbedtls_platform_zeroize(const_cast<char*>(bits.c_str()),bits.length());return false;}
    const int width=draw->header.w,height=draw->header.h,scale=std::min(width,height)/(size+8);
    if(scale<2){mbedtls_platform_zeroize(const_cast<char*>(bits.c_str()),bits.length());return false;}
    lv_draw_buf_clear(draw,nullptr);lv_canvas_set_palette(qr,0,lv_color_to_32(lv_color_white(),LV_OPA_COVER));lv_canvas_set_palette(qr,1,lv_color_to_32(lv_color_black(),LV_OPA_COVER));
    auto* pixels=static_cast<uint8_t*>(draw->data)+8;const int marginX=(width-size*scale)/2,marginY=(height-size*scale)/2;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){const int i=y*size+x;const char ch=bits[i/4];const unsigned value=ch<='9'?ch-'0':ch-'a'+10;if(!(value&(8>>(i%4))))continue;
        for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx){const int px=marginX+x*scale+dx,py=marginY+y*scale+dy;pixels[py*draw->header.stride+px/8]|=uint8_t(0x80>>(px%8));}}
    mbedtls_platform_zeroize(const_cast<char*>(bits.c_str()),bits.length());lv_image_cache_drop(draw);lv_obj_invalidate(qr);return true;
}
namespace {String escapeWifi(const String& input){String out;for(size_t i=0;i<input.length();++i){const char c=input[i];if(c=='\\'||c==';'||c==','||c==':'||c=='"')out+='\\';out+=c;}return out;}}
String QrGenerator::wifi(const String& ssid,const String& password){
    if(!ssid.length()||ssid.length()>32||password.length()>64)return "";
    return String("WIFI:T:")+(password.length()?"WPA":"nopass")+";S:"+escapeWifi(ssid)+";P:"+escapeWifi(password)+";;";
}
}

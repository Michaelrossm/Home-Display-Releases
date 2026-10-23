#include "PrinterFileJobs.h"
#include "../FileBrowserService.h"
#include "../NetworkWorker.h"
#include "../ImageResourceService.h"
#include "PrinterThumbnailPolicy.h"
#include <esp_random.h>
#include <algorithm>
namespace hp {
namespace {
struct Stream {
    File file;String prefix,suffix;size_t bytes=0;
    static size_t read(void* context,uint64_t offset,uint8_t* buffer,size_t capacity){
        auto& s=*static_cast<Stream*>(context);
        if(!s.file||s.file.size()!=s.bytes)return 0;
        if(offset<s.prefix.length()){const size_t n=std::min<size_t>(capacity,s.prefix.length()-offset);memcpy(buffer,s.prefix.c_str()+offset,n);return n;}
        offset-=s.prefix.length();
        if(offset<s.bytes){const size_t n=std::min<size_t>(capacity,s.bytes-offset);if(s.file.position()!=offset&&!s.file.seek(offset))return 0;return s.file.read(buffer,n);}
        offset-=s.bytes;if(offset>=s.suffix.length())return 0;
        const size_t n=std::min<size_t>(capacity,s.suffix.length()-offset);memcpy(buffer,s.suffix.c_str()+offset,n);return n;
    }
};
}
void printerUploadHttp(LuaJobService::Request request,const String& source,const String& prefix,const String& suffix,LuaJobService::Result& result){
    if(!NetworkWorker::instance().isWorkerContext()){result.error="worker_required";return;}
    if(!(request.permissions&perm(AppPermission::StorageRead))||!printerLocalGcode(source)){result.error="printer_upload_source_denied";return;}
    Stream stream{FileBrowserService::openRead(source),prefix,suffix,0};
    if(!stream.file||stream.file.isDirectory()){result.error="printer_upload_source_unavailable";return;}
    stream.bytes=stream.file.size();
    if(!stream.bytes||stream.bytes>64U*1024U*1024U){result.error="printer_upload_size_limit";return;}
    request.nativeRead=Stream::read;request.nativeReadContext=&stream;
    request.streamMode=2;request.expected=stream.bytes+prefix.length()+suffix.length();
    request.timeout=5000;request.deadlineMs=300000;request.responseLimit=2048;
    LuaJobService::executeHttp(request,result); // No submit/wait recursion; consumes current owner job.
}
bool printerLocalThumbnail(const String& source,String& bytes,const std::atomic<bool>* cancelled,String& error,unsigned width,unsigned height){
    if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
    if(!printerLocalGcode(source)){error="printer_upload_source_denied";return false;}
    File file=FileBrowserService::openRead(source);if(!file||file.isDirectory()){error="printer_upload_source_unavailable";return false;}
    PrinterThumbnailChoice choice{width,height};String encoded,fallback;char line[257];size_t used=0,read=0;bool collecting=false;unsigned wantedW=0,wantedH=0;
    auto acceptLine=[&]()->int{
        line[used]=0;String text(line);used=0;text.trim();
        if(!collecting){unsigned w=0,h=0;
            if(sscanf(text.c_str(),"; thumbnail begin %ux%u",&w,&h)!=2&&sscanf(text.c_str(),"; thumbnail_QOI begin %ux%u",&w,&h)!=2&&sscanf(text.c_str(),"; thumbnail_JPG begin %ux%u",&w,&h)!=2)return 0;
            if(w&&h&&w<=512&&h<=512){wantedW=w;wantedH=h;collecting=true;encoded="";}return 0;
        }
        if(text.startsWith("; thumbnail end")||text.startsWith("; thumbnail_QOI end")||text.startsWith("; thumbnail_JPG end")){
            auto candidate=choice;
            if(candidate.consider(wantedW,wantedH)){
                if(!printerThumbnailBase64(encoded,bytes)){encoded="";collecting=false;return 0;}
                choice=candidate;
                if(wantedW==width&&wantedH==height)return 1;
                fallback=std::move(bytes);
            }encoded="";collecting=false;return 0;
        }
        if(!text.startsWith(";")){error="printer_thumbnail_invalid";return -1;}
        text=text.substring(1);text.trim();if(encoded.length()+text.length()>90000){encoded="";collecting=false;return 0;}encoded+=text;return 0;
    };
    // Slicer comment thumbnails only, no G-code parser/execution. Bounded header scan.
    uint8_t chunk[512];
    while(file.available()&&read<192*1024){
        if(cancelled&&cancelled->load()){error="cancelled";return false;}
        const size_t count=file.read(chunk,std::min<size_t>(sizeof(chunk),192*1024-read));
        if(!count){error="printer_upload_source_unavailable";return false;}read+=count;
        for(size_t i=0;i<count;++i){const char c=chunk[i];
            if(c!='\n'&&used<sizeof(line)-1){if(c!='\r')line[used++]=c;continue;}
            if(c!='\n'){error="printer_thumbnail_line_limit";return false;}
            const int result=acceptLine();if(result)return result>0;
        }
    }
    if(fallback.length()){bytes=std::move(fallback);return true;}
    error="printer_thumbnail_missing_or_limit";return false;
}
bool printerPreviewImage(const String& owner,String& bytes,const std::atomic<bool>* cancelled,String& result,String& error,unsigned width,unsigned height){
    if(!NetworkWorker::instance().isWorkerContext()){error="worker_required";return false;}
    uint32_t w,h;if(bytes.length()>65536||!ImageCodec::dimensions(reinterpret_cast<const uint8_t*>(bytes.c_str()),bytes.length(),w,h)||w>512||h>512){error="printer_thumbnail_invalid_or_limit";return false;}
    if(!width||!height||width>512||height>512){error="printer_preview_dimensions";return false;}
    auto image=ImageCodec::decode(reinterpret_cast<uint8_t*>(const_cast<char*>(bytes.c_str())),bytes.length(),width,height,false,cancelled,false,true);
    if(!image){error="printer_thumbnail_decode_failed";return false;}
    if(cancelled&&cancelled->load()){error="cancelled";return false;}
    const auto imageWidth=image->width,imageHeight=image->height;const uint32_t handle=ImageResourceService::instance().store(owner,std::move(image));
    if(!handle){error="printer_thumbnail_memory";return false;}
    if(cancelled&&cancelled->load()){ImageResourceService::instance().release(owner,handle);error="cancelled";return false;}
    result="{\"handle\":"+String(handle)+",\"width\":"+String(imageWidth)+",\"height\":"+String(imageHeight)+"}";return true;
}
void printerDiscardPreview(const char* owner,const String& body){
    if(!owner||body.length()>256)return;JsonDocument doc;
    if(!deserializeJson(doc,body)&&doc["handle"].is<uint32_t>())ImageResourceService::instance().release(owner,doc["handle"].as<uint32_t>());
}
String printerMultipartBoundary(){
    uint8_t nonce[16];esp_fill_random(nonce,sizeof(nonce));char text[57]="HomeDisplayPrinterUpload";
    for(unsigned i=0;i<16;++i)snprintf(text+24+i*2,3,"%02x",nonce[i]);return text;
}
}

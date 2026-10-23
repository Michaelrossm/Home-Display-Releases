#pragma once
#include "PrinterAdapter.h"
namespace hp {
// Shared, bounded file transfer on the existing NetworkWorker.
inline bool printerLocalGcode(const String& source){
    if(!source.startsWith("sd:/")&&!source.startsWith("usb:/"))return false;
    const String path=source.substring(source.indexOf(':')+1);
    if(!printerPath(path.c_str(),"/"))return false;
    if(path=="/homepanel"||path.startsWith("/homepanel/")||path=="/Apps"||path.startsWith("/Apps/"))return false;
    for(size_t i=0;i<path.length();++i)if(path[i]=='/'&&i+1<path.length()&&path[i+1]=='.')return false;
    String ext=source.substring(source.lastIndexOf('.')+1);ext.toLowerCase();
    return ext=="gcode"||ext=="gco"||ext=="gc";
}
void printerUploadHttp(LuaJobService::Request request,const String& source,const String& prefix,const String& suffix,LuaJobService::Result& result);
bool printerPreviewImage(const String& owner,String& bytes,const std::atomic<bool>* cancelled,String& result,String& error,unsigned width=320,unsigned height=220);
bool printerLocalThumbnail(const String& source,String& bytes,const std::atomic<bool>* cancelled,String& error,unsigned width=320,unsigned height=220);
void printerDiscardPreview(const char* owner,const String& body);
String printerMultipartBoundary();
}

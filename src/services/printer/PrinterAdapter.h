#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../apps/lua/LuaJobService.h"
#include "PrinterSafety.h"
namespace hp {
struct PrinterProfile {
    String owner,id,name,host,protocol,mode,passwordSecret;
    String calibration="none",alignment="none";
    uint16_t port=80;
    bool background=true,enabled=true;
};
class PrinterTransport {
public:
    uint16_t previewWidth=320,previewHeight=220;
    virtual ~PrinterTransport()=default;
    virtual LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t limit=16384)=0;
    virtual String password()=0;
    virtual bool stopped()const=0;
    virtual LuaJobService::Result upload(const String&,const String&,const String&,const String&,const String&,uint8_t=1){LuaJobService::Result r;r.error="printer_upload_unsupported";return r;}
};
class PrinterAdapter {
public:
    virtual ~PrinterAdapter()=default;
    virtual bool poll(PrinterTransport&,const PrinterProfile&,PrinterStatus&,String& error)=0;
    virtual bool list(PrinterTransport&,const PrinterProfile&,const String& directory,String& json,String& error)=0;
    virtual bool command(PrinterTransport&,const PrinterProfile&,const String& kind,JsonVariantConst parameters,PrinterStatus&,String& reply,String& error)=0;
    virtual bool thumbnail(PrinterTransport&,const PrinterProfile&,const String&,String&,String& error){error="printer_thumbnail_unsupported";return false;}
};
}

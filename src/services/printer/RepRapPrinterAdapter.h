#pragma once
#include "PrinterAdapter.h"
namespace hp {
class RepRapPrinterAdapter final:public PrinterAdapter {
public:
    bool pollConsole(PrinterTransport&,const PrinterProfile&,String&);
    bool poll(PrinterTransport&,const PrinterProfile&,PrinterStatus&,String&)override;
    bool list(PrinterTransport&,const PrinterProfile&,const String&,String&,String&)override;
    void setListOffset(uint16_t offset){listOffset_=offset;}
    bool command(PrinterTransport&,const PrinterProfile&,const String&,JsonVariantConst,PrinterStatus&,String&,String&)override;
    bool upload(PrinterTransport&,const PrinterProfile&,const String& source,const String& target,String& error);
    bool thumbnail(PrinterTransport&,const PrinterProfile&,const String&,String&,String&)override;
    void reset(){session_="";reply_="";expires_=0;sbc_=false;replyKnown_=false;metadataKnown_=false;}
    String takeReply(){String out=std::move(reply_);reply_="";return out;}
    const char* api()const{return sbc_?"dsf":"rrf_standalone";}
    static String encode(const String&);
    static bool normalize(JsonVariantConst,PrinterStatus&,String&);
private:
    String session_,reply_;uint32_t expires_=0,sessionTimeout_=8000,replySequence_=0;bool sbc_=false,replyKnown_=false;
    uint32_t metadataAt_=0;bool metadataKnown_=false;uint16_t listOffset_=0;
    bool connect(PrinterTransport&,const PrinterProfile&,String&);
    LuaJobService::Result fetch(PrinterTransport&,const String&,uint8_t=0,const String& = "",size_t=16384);
};
}

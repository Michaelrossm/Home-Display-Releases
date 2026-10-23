#pragma once
#include "PrinterAdapter.h"
namespace hp {
class MoonrakerPrinterAdapter final : public PrinterAdapter {
public:
    bool poll(PrinterTransport&,const PrinterProfile&,PrinterStatus&,String&) override;
    bool list(PrinterTransport&,const PrinterProfile&,const String&,String&,String&) override;
    bool command(PrinterTransport&,const PrinterProfile&,const String&,JsonVariantConst,PrinterStatus&,String&,String&) override;
    bool thumbnail(PrinterTransport&,const PrinterProfile&,const String&,String&,String&)override;
    const char* api() const { return "moonraker"; }
    String takeReply(){String s=reply_;reply_="";return s;}
    static String encode(const String&);
private:
    String reply_;
    String query_;uint32_t discoveredAt_=0,inventoryHash_=0;double eventTime_=-1;bool discoveryTruncated_=false;
    bool discover(PrinterTransport&,String&);
};
}

#pragma once
#include "PrinterAdapter.h"
namespace hp {
// OctoPrint REST adapter, suitable for older Prusa MK2/MK3/MK3S using Marlin.
// Shares the existing worker, transport, owner-scoped jobs and safety records.
class OctoPrintPrinterAdapter final : public PrinterAdapter {
public:
 bool poll(PrinterTransport&,const PrinterProfile&,PrinterStatus&,String&) override;
 bool list(PrinterTransport&,const PrinterProfile&,const String&,String&,String&) override;
 bool command(PrinterTransport&,const PrinterProfile&,const String&,JsonVariantConst,PrinterStatus&,String&,String&) override;
 const char* api()const{return "octoprint";}
 String takeReply(){return "";}
 static String encode(const String&);
private:
 bool fetch(PrinterTransport&,const String&,JsonDocument&,String&,size_t limit=16384);
 bool send(PrinterTransport&,const String&,const String&,String&);
};
}

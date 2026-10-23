#include "PostalLookupJob.h"
#include "PostalLookupService.h"
namespace hp {
void executePostalLookupJob(const LuaJobService::Request& request,LuaJobService::Result& result){
    JsonDocument args,out;
    if(request.body.length()>512||deserializeJson(args,request.body,DeserializationOption::NestingLimit(2))){result.error="postal_arguments";return;}
    if(!lookupPostalCode(args.as<JsonVariantConst>(),out,result.error,request.cancelled))return;
    if(out.overflowed()||measureJson(out)>8192){result.error="postal_response_limit";return;}
    serializeJson(out,result.body);result.ok=true;result.status=200;
}
}

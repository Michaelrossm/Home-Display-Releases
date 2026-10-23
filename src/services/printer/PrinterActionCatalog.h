#pragma once
#include "PrinterComponents.h"
#include "PrinterRuntimeModel.h"
namespace hp {
struct PrinterActionDescriptor {
    const char *id,*label,*parameter,*unit;
    uint32_t capability;double minimum,maximum;bool confirm,idle,homed;
};
inline constexpr PrinterActionDescriptor printerActions[]={
 {"home","Referenzieren","axes","",0,0,0,true,true,false},
 {"move","Achse bewegen","distance","mm",0,-10,10,true,true,true},
 {"set_temperature","Temperatur setzen","target","C",0,0,350,false,false,false},
 {"set_fan","Luefter setzen","percent","%",0,0,100,false,false,false},
 {"set_speed_factor","Geschwindigkeit","percent","%",0,10,300,false,false,false},
 {"pause","Druck pausieren","","",0,0,0,false,false,false},
 {"resume","Druck fortsetzen","","",0,0,0,true,false,false},
 {"cancel","Druck abbrechen","","",0,0,0,true,false,false},
 {"start_file","Datei drucken","path","",0,0,0,true,true,false},
 {"run_macro","Makro starten","name","",0,0,0,true,true,false},
 {"bed_mesh","Bett vermessen","","",PrinterMesh,0,0,true,true,true},
 {"z_tilt","Z ausrichten","","",PrinterZTilt,0,0,true,true,true},
 {"gantry_level","Gantry ausrichten","","",PrinterGantry,0,0,true,true,true},
 {"screws_tilt","Bettschrauben messen","","",PrinterScrews,0,0,true,true,true},
 {"mesh_clear","Mesh deaktivieren","","",PrinterMeshClear,0,0,true,true,true},
 {"select_tool","Werkzeug aktivieren","tool","",PrinterToolSelect,0,0,true,true,true},
 {"extrude","Filament bewegen","distance","mm",PrinterExtrude,-10,10,true,true,false},
 {"set_flow_factor","Materialfluss","percent","%",PrinterFlow,10,200,true,false,false},
 {"motors_off","Motoren loesen","","",PrinterMotorsOff,0,0,true,true,false}
};
inline const PrinterActionDescriptor* printerAction(const char* id){for(const auto& a:printerActions)if(!strcmp(a.id,id))return &a;return nullptr;}
inline const char* printerActionReasonText(const char* reason){
    if(!*reason)return "";if(!strcmp(reason,"not_configured"))return "Am Drucker nicht konfiguriert oder im Profil nicht freigegeben";
    if(!strcmp(reason,"not_supported"))return "Dieser Adapter unterstützt die Funktion nicht";
    if(!strcmp(reason,"requires_homing"))return "Achsen zuerst referenzieren";
    if(!strcmp(reason,"cold_or_inactive_tool"))return "Aktive heiße Düse erforderlich";
    if(!strcmp(reason,"busy"))return "Drucker oder Steuerkanal beschäftigt";
    if(!strcmp(reason,"stale"))return "Keine frischen Druckerdaten";
    if(!strcmp(reason,"unknown_limits"))return "Sichere Grenzen oder Zuordnung unbekannt";
    if(!strcmp(reason,"not_printing"))return "Kein laufender Druck";
    if(!strcmp(reason,"not_paused"))return "Druck ist nicht pausiert";
    return "Fähigkeit noch nicht sicher erkannt";
}
inline void printerWriteOperation(JsonObject out,const PrinterOperation& operation){out["id"]=operation.id;out["action"]=operation.action;out["state"]=printerOperationName(operation.phase);
    out["changed_ms"]=operation.changed;out["started_ms"]=operation.started;}
inline const char* printerActionReason(const PrinterActionDescriptor& a,const PrinterStatus& s,bool online){
    if(!online||!s.valid)return "stale";
    if(a.capability&&!(s.controls&a.capability)){
        if(!(s.knownControls&a.capability))return "unknown";
        return s.configurableControls&a.capability?"not_configured":"not_supported";
    }
    if(!strcmp(a.id,"move")&&!s.httpMillimeters)return "unknown_limits";
    if(!strcmp(a.id,"set_temperature")){bool safe=false;for(size_t i=0;i<s.heaterCount;++i)safe|=s.heaters[i].limit&&s.heaters[i].mappingSafe&&!s.heaters[i].fault;
        if(!safe)return "unknown_limits";}
    if(!strcmp(a.id,"set_fan")){bool manual=false;for(size_t i=0;i<s.fanCount;++i)manual|=!s.fans[i].automatic;if(!manual)return "not_supported";}
    if(!strcmp(a.id,"run_macro")&&s.calibrationHomes)return "not_supported";
    if(!strcmp(a.id,"pause")&&strcmp(s.state,"printing")&&strcmp(s.state,"processing"))return "not_printing";
    if(!strcmp(a.id,"resume")&&strcmp(s.state,"paused"))return "not_paused";
    if(!strcmp(a.id,"cancel")&&!printerActive(s))return "not_printing";
    if(a.idle&&!printerIdle(s))return "busy";
    if(a.homed&&!s.calibrationHomes&&!printerAllHomed(s))return "requires_homing";
    if(a.idle&&!s.calibrationHomes&&!s.httpChannelIdle)return "busy";
    if(!strcmp(a.id,"extrude")&&!s.canExtrude)return "cold_or_inactive_tool";
    return "";
}
inline void printerWriteActionStates(JsonArray out,const PrinterStatus& s,bool online){
    for(const auto& a:printerActions){auto o=out.add<JsonObject>();o["id"]=a.id;const char* reason=printerActionReason(a,s,online);
        o["available"]=!*reason;o["reason"]=reason;o["reason_detail"]=printerActionReasonText(reason);}
}
inline void printerWriteActionCatalog(JsonArray out,size_t offset,size_t count){
    for(size_t i=offset;i<sizeof(printerActions)/sizeof(*printerActions)&&i<offset+count;++i){const auto& a=printerActions[i];auto o=out.add<JsonObject>();
        o["id"]=a.id;o["label"]=a.label;o["parameter"]=a.parameter;o["unit"]=a.unit;o["min"]=a.minimum;o["max"]=a.maximum;
        o["confirmation"]=a.confirm;o["idle"]=a.idle;o["homed"]=a.homed;o["component_limits_required"]=!strcmp(a.id,"set_temperature");}
}
}

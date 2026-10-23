struct ModernPrinter:hp::PrinterTransport {
    bool octo=false,malformed=false,stopping=false;String state="standby",motion="Ready",extruder="extruder";
    unsigned posts=0;String lastPath,lastBody;
    String password()override{return "";}bool stopped()const override{return stopping;}
    hp::LuaJobService::Result request(const String& path,const String&,uint8_t method=0,const String& body="",size_t=16384)override{
        hp::LuaJobService::Result r;r.ok=true;r.status=200;lastPath=path;
        if(method){++posts;lastBody=body;r.body="{\"result\":\"ok\"}";return r;}
        if(malformed){r.body="{}";return r;}
        JsonDocument d;
        if(path=="/printer/objects/list"){
            auto a=d["result"]["objects"].to<JsonArray>();for(const char* name:{"extruder","heater_bed","fan"})a.add(name);
        }else if(path=="/api/job"){
            d["state"]=state;d["progress"]["completion"]=25;d["job"]["file"]["name"]="test.gcode";
        }else if(path=="/api/printer"){
            auto flags=d["state"]["flags"].to<JsonObject>();flags["operational"]=true;flags["ready"]=true;
            flags["printing"]=(state=="Printing");flags["paused"]=(state=="Paused");
            flags["pausing"]=(state=="Pausing");flags["cancelling"]=(state=="Cancelling");
            d["temperature"]["tool0"]["actual"]=205;
        }else if(path.startsWith("/api/files/local")){
            auto entries=d[path.indexOf("local?")>=0?"files":"children"].to<JsonArray>();
            auto folder=entries.add<JsonObject>();folder["name"]="folder";folder["type"]="folder";
            auto file=entries.add<JsonObject>();file["name"]="test.gcode";file["type"]="machinecode";
            auto bad=entries.add<JsonObject>();bad["name"]="..";bad["type"]="machinecode";
        }else if(path.startsWith("/server/files/directory")){
            d["result"]["dirs"].to<JsonArray>().add<JsonObject>()["dirname"]="folder";
            d["result"]["files"].to<JsonArray>().add<JsonObject>()["filename"]="test.gcode";
        }else if(path.startsWith("/printer/objects/query")){
            auto model=d["result"]["status"].to<JsonObject>();
            model["print_stats"]["state"]=state;model["print_stats"]["info"]["current_layer"]=2;
            model["print_stats"]["info"]["total_layer"]=10;model["idle_timeout"]["state"]=motion;
            model["toolhead"]["extruder"]=extruder;model["toolhead"]["homed_axes"]="xyz";
            for(const char* field:{"position","axis_minimum","axis_maximum"}){
                auto a=model["toolhead"][field].to<JsonArray>();
                for(int i=0;i<3;++i)a.add(!strcmp(field,"position")?100:!strcmp(field,"axis_minimum")?0:250);
            }
            model["extruder"]["temperature"]=205;model["extruder"]["target"]=210;
            model["heater_bed"]["temperature"]=55;
            auto cfg=model["configfile"]["settings"].to<JsonObject>();
            cfg["printer"]["max_velocity"]=300;cfg["printer"]["max_z_velocity"]=5;
            cfg["extruder"]["max_temp"]=285;cfg["heater_bed"]["max_temp"]=120;
            cfg["gcode_macro TEST"].to<JsonObject>();cfg["gcode_macro _PRIVATE"].to<JsonObject>();
        }else{r.ok=false;r.status=404;}
        serializeJson(d,r.body);return r;
    }
};

struct FakePrinter:hp::PrinterTransport {
    std::vector<String> paths,sessions;String state="idle",lastCode,lastPath,lastBody;uint8_t lastMethod=0;
    int auth=0;bool sbc=false,homed=true,invalidModel=false,failCommand=false;unsigned commands=0;
    String password()override{return "reprap";}bool stopped()const override{return false;}
    hp::LuaJobService::Result request(const String& path,const String& session,uint8_t method=0,const String& body="",size_t=16384)override{
        paths.push_back(path);sessions.push_back(session);lastPath=path;lastMethod=method;lastBody=body;
        hp::LuaJobService::Result r;r.ok=true;r.status=200;
        if(path.startsWith("/rr_connect")){if(sbc){r.ok=false;r.status=404;}else r.body="{\"err\":"+String(auth)+",\"sessionTimeout\":8000,\"sessionKey\":123456}";}
        else if(path.startsWith("/machine/connect"))r.body="{\"sessionKey\":\"DSF-token\"}";
        else if(path.startsWith("/rr_model")||path=="/machine/model"){
            String model="{\"state\":{\"status\":\""+state+"\",\"currentTool\":0},\"inputs\":[{\"name\":\"HTTP\",\"distanceUnit\":\"mm\",\"state\":\"idle\",\"inMacro\":false,\"inverseTimeMode\":false}],";
            model+="\"move\":{\"axes\":[{\"letter\":\"X\",\"homed\":"+String(homed?"true":"false")+",\"userPosition\":100,\"machinePosition\":100,\"min\":0,\"max\":250,\"speed\":12000},{\"letter\":\"Y\",\"homed\":true,\"userPosition\":100,\"machinePosition\":100,\"min\":0,\"max\":210,\"speed\":600},{\"letter\":\"Z\",\"homed\":true,\"userPosition\":20,\"machinePosition\":20,\"min\":0,\"max\":210,\"speed\":600}],\"speedFactor\":1},";
            model+="\"tools\":[{\"number\":0,\"heaters\":[1]}],\"heat\":{\"bedHeaters\":[0],\"heaters\":[{\"current\":55,\"active\":60,\"max\":120,\"state\":\"active\"},{\"current\":205,\"active\":210,\"max\":300,\"state\":\"active\",\"monitors\":[{\"condition\":\"tooHigh\",\"limit\":285}]}]},";
            model+="\"sensors\":{\"analog\":[]},\"fans\":[{\"actualValue\":0.5,\"thermostatic\":{\"sensors\":[]}},{\"actualValue\":1,\"thermostatic\":{\"sensors\":[0]}}],\"job\":{\"file\":{\"fileName\":\"test.gcode\",\"size\":10000,\"numLayers\":120},\"filePosition\":2500,\"duration\":600,\"layer\":3,\"timesLeft\":{\"file\":1800}}}";
            if(invalidModel)model="{}";
            r.body=sbc?model:String("{\"result\":")+model+"}";
            if(!sbc){
                const int start=path.indexOf("key=")+4;
                const auto found=path.find('&',start);const int end=found==String::npos?-1:static_cast<int>(found);
                const String key=path.substring(start,end<0?path.length():end);
                if(key.length()){JsonDocument full,response;deserializeJson(full,model);
                    response["key"]=key;response["result"].set(full[key]);
                    r.body="";serializeJson(response,r.body);}
            }
        }else if(path.startsWith("/rr_filelist")||path.startsWith("/machine/directory")){
            String entries="[{\"name\":\"test.gcode\",\"type\":\"f\",\"size\":100},{\"name\":\"folder\",\"type\":\"d\"},{\"name\":\"../config.g\",\"type\":\"f\"}]";
            r.body=sbc?entries:String("{\"err\":0,\"next\":0,\"files\":")+entries+"}";
        }else if(path.startsWith("/rr_gcode")||path=="/machine/code?async=true"){
            ++commands;lastCode=sbc?body:path.substring(strlen("/rr_gcode?gcode="),path.length());
            if(failCommand){r.ok=false;r.status=0;r.error="timeout";}else r.body=sbc?"": "{\"buff\":512}";
        }else if(path=="/rr_reply")r.body="ok";
        else{r.ok=false;r.status=404;r.error="http_status";}return r;
    }
};

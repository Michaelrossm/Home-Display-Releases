#include "ConfigMigration.h"
#include "../services/ConfigService.h"
namespace hp {
bool ConfigMigration::v1ToV2(ConfigService& c){
    // v0.2.7 had no explicit schema. Keep every existing key and only normalize
    // the historic default hostname when it was never customized.
    if(!c.has("host") || c.getString("host","homepanel") == "homepanel") c.putString("host","home-display");
    if(c.has("radio_pin")){ c.putSecretString("radio_pin", c.getString("radio_pin","1234")); c.remove("radio_pin"); }
    if(c.has("power_pwd")){ c.putSecretString("power_pwd", c.getString("power_pwd","")); c.remove("power_pwd"); }
    return c.putU32("cfg_schema",2);
}
bool ConfigMigration::run(ConfigService& c,uint32_t from,uint32_t to){
    if(from==0||from>to) return false;
    while(from<to){
        bool ok=false;
        switch(from){ case 1: ok=v1ToV2(c); break; default: return false; }
        if(!ok) return false;
        ++from;
    }
    return true;
}
}

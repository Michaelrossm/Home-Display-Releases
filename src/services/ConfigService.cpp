#include "ConfigService.h"
#include "../infrastructure/ConfigMigration.h"
#include "../infrastructure/AppEnabledState.h"
#include <mutex>

namespace hp {
namespace { std::mutex appStateMutex; }
namespace {
AppEnabledState cachedAppState;
bool appStateLoaded=false;
std::map<std::pair<std::string,bool>,bool> appStateDefaults;
void invalidateAppState(){appStateLoaded=false;appStateDefaults.clear();}
}
bool ConfigService::validNvsKey(const char* key){return key&&key[0]&&strlen(key)<=15;}
bool ConfigService::appEnabled(const char* id,bool fallback) const {
    if(!id||!AppEnabledState::validId(id))return fallback;
    std::lock_guard<std::mutex> lock(appStateMutex);
    if(!appStateLoaded){cachedAppState=AppEnabledState{};if(!cachedAppState.decode(getString(AppEnabledState::key).c_str()))cachedAppState.values.clear();appStateLoaded=true;}
    {
        const auto it=cachedAppState.values.find(id);if(it!=cachedAppState.values.end())return it->second;
    }
    const auto key=std::make_pair(std::string(id),fallback);
    const auto cached=appStateDefaults.find(key);if(cached!=appStateDefaults.end())return cached->second;
    const String legacy=String("napp_")+id;
    const bool value=legacy.length()<=15&&has(legacy.c_str())?getBool(legacy.c_str(),fallback):fallback;
    if(appStateDefaults.size()>=128)appStateDefaults.clear();appStateDefaults[key]=value;return value;
}
ConfigService::AppStateWriteResult ConfigService::saveAppEnabled(const char* id,bool enabled){
    if(!writable())return AppStateWriteResult::NotWritable;
    if(!id||!AppEnabledState::validId(id))return AppStateWriteResult::InvalidIdentity;
    std::lock_guard<std::mutex> lock(appStateMutex);AppEnabledState state;
    const String record=getString(AppEnabledState::key);
    if((has(AppEnabledState::key)&&!record.length())||!state.decode(record.c_str()))return AppStateWriteResult::InvalidRecord;
    if(!state.values.count(id)&&state.values.size()>=128)return AppStateWriteResult::InvalidRecord;
    state.values[id]=enabled;const auto data=state.encode();
    if(data.size()>AppEnabledState::maxBytes)return AppStateWriteResult::InvalidRecord;
    if(!putString(AppEnabledState::key,String(data.c_str())))return AppStateWriteResult::WriteFailed;
    invalidateAppState();return AppStateWriteResult::Ok;
}
bool ConfigService::removeAppEnabled(const char* id){
    if(!writable()||!id||!AppEnabledState::validId(id))return false;
    std::lock_guard<std::mutex> lock(appStateMutex);AppEnabledState state;
    const String record=getString(AppEnabledState::key);
    if((has(AppEnabledState::key)&&!record.length())||!state.decode(record.c_str()))return false;
    const String legacy=String("napp_")+id;
    if(legacy.length()<=15&&!remove(legacy.c_str()))return false;
    if(!state.values.erase(id)){invalidateAppState();return true;}
    const bool ok=putString(AppEnabledState::key,String(state.encode().c_str()));if(ok)invalidateAppState();return ok;
}
ConfigService& ConfigService::instance(){ static ConfigService c; return c; }

bool ConfigService::begin(){
    if(prefsOpen_) return writable() || newerSchema();
    prefsOpen_ = prefs_.begin("homepanel", false);
    if(!prefsOpen_){ status_=ConfigStatus::Error; return false; }
    schema_ = prefs_.isKey("cfg_schema") ? prefs_.getUInt("cfg_schema", 1) : 1;
    if(schema_ > build::kConfigSchema){ status_=ConfigStatus::NewerSchema; return true; }
    if(schema_ < build::kConfigSchema){
        status_=ConfigStatus::Ready; // migration writes through the same validated store
        if(!migrate(schema_, build::kConfigSchema)){ status_=ConfigStatus::Error; return false; }
        status_=ConfigStatus::Migrated;
    } else status_=ConfigStatus::Ready;
    return true;
}

bool ConfigService::migrate(uint32_t from,uint32_t to){
    return ConfigMigration::run(*this, from, to);
}
bool ConfigService::migrateV1ToV2(){ return true; }

String ConfigService::getString(const char*k,const char*f)const{return prefsOpen_&&prefs_.isKey(k)?prefs_.getString(k,f):String(f);}
uint8_t ConfigService::getU8(const char*k,uint8_t f)const{return prefsOpen_?prefs_.getUChar(k,f):f;}
uint16_t ConfigService::getU16(const char*k,uint16_t f)const{return prefsOpen_?prefs_.getUShort(k,f):f;}
uint32_t ConfigService::getU32(const char*k,uint32_t f)const{return prefsOpen_?prefs_.getUInt(k,f):f;}
bool ConfigService::getBool(const char*k,bool f)const{return prefsOpen_?prefs_.getBool(k,f):f;}
bool ConfigService::has(const char*k)const{return prefsOpen_&&prefs_.isKey(k);}
bool ConfigService::putString(const char*k,const String&v){if(!validNvsKey(k)||!writable())return false;if(prefs_.getType(k)==PT_STR&&prefs_.getString(k)==v)return true;return changed(prefs_.putString(k,v)>0);}
bool ConfigService::putU8(const char*k,uint8_t v){if(!validNvsKey(k)||!writable())return false;if(prefs_.getType(k)==PT_U8&&prefs_.getUChar(k)==v)return true;return changed(prefs_.putUChar(k,v)==sizeof(v));}
bool ConfigService::putU16(const char*k,uint16_t v){if(!validNvsKey(k)||!writable())return false;if(prefs_.getType(k)==PT_U16&&prefs_.getUShort(k)==v)return true;return changed(prefs_.putUShort(k,v)==sizeof(v));}
bool ConfigService::putU32(const char*k,uint32_t v){if(!validNvsKey(k)||!writable())return false;if(prefs_.getType(k)==PT_U32&&prefs_.getUInt(k)==v)return true;return changed(prefs_.putUInt(k,v)==sizeof(v));}
bool ConfigService::putBool(const char*k,bool v){if(!validNvsKey(k)||!writable())return false;if(prefs_.getType(k)==PT_U8&&prefs_.getUChar(k)==uint8_t(v))return true;return changed(prefs_.putBool(k,v)==sizeof(v));}
bool ConfigService::remove(const char*k){return validNvsKey(k)&&writable()&&(!prefs_.isKey(k)||changed(prefs_.remove(k)));}

bool ConfigService::clearKeys(const char*const*keys,size_t n){ if(!writable()) return false; bool ok=true; for(size_t i=0;i<n;++i) ok &= remove(keys[i]); return ok; }
bool ConfigService::reset(ConfigScope s){
 if(!writable()) return false;
 static const char* network[]={"host","wifi_enabled","wifi_ssid","wifi_active","wifi_preferred","wifi_prof_v2",
 "wifi_ssid_0","wifi_ssid_1","wifi_ssid_2","wifi_ip_0","wifi_ip_1","wifi_ip_2",
 "wifi_gw_0","wifi_gw_1","wifi_gw_2","wifi_mask_0","wifi_mask_1","wifi_mask_2",
 "wifi_dns1_0","wifi_dns1_1","wifi_dns1_2","wifi_dns2_0","wifi_dns2_1","wifi_dns2_2",
 "net_wlanoff","net_wps","net_wpsmax","net_txauto","net_lanwait","net_wsched","net_wsstart","net_wsend"};
 static const char* ui[]={"ui_lang","brightness","rotation","ui_wallon","ui_walldim","ui_wall","ui_theme","ui_theme_file","ui_accent","als_auto","als_min","als_max","als_dark","als_bright","pm_energy","pm_lsleep","n_db","n_da","n_se","n_sb","n_sa","n_of","m_db","m_da","m_se","m_sb","m_sa","m_of","p_db","p_da","p_se","p_sb","p_sa","p_of"};
 static const char* media[]={"music_vol","music_resume","music_shuffle","radio_host","radio_pin","ar_parent","ar_vlim","ar_vmax","ar_lsrc","ar_lpwr","ar_ldsp"};
 static const char* user[]={"region_country","weather","power_host","power_pwd","br_http","br_https","br_wan","br_maxkb","ed_enabled","ed_interval","ed_retention","ed_backfill","app_store_sd","app_store","files_nas","photos_cfg"};
 switch(s){
  case ConfigScope::Network:{
   bool ok=clearKeys(network,sizeof(network)/sizeof(*network));
   for(const char* key : {"wifi_pwd","wifi_pwd_0","wifi_pwd_1","wifi_pwd_2"}) ok &= removeSecret(key);
   return ok;
  }
  case ConfigScope::Ui:return clearKeys(ui,sizeof(ui)/sizeof(*ui));
  case ConfigScope::Media:return clearKeys(media,sizeof(media)/sizeof(*media));
  case ConfigScope::UserSettings:return clearKeys(user,sizeof(user)/sizeof(*user));
  case ConfigScope::WebApps:return true;
  case ConfigScope::CalendarAccounts:{ auto&p=calendarSecrets(); return calendarSecretsOpen_&&p.clear(); }
  case ConfigScope::AllUser:{ bool ok=clearKeys(network,sizeof(network)/sizeof(*network))&&clearKeys(ui,sizeof(ui)/sizeof(*ui))&&clearKeys(media,sizeof(media)/sizeof(*media))&&clearKeys(user,sizeof(user)/sizeof(*user)); auto&s=secrets(); if(secretsOpen_) ok&=changed(s.clear()); auto&cs=calendarSecrets(); if(calendarSecretsOpen_) ok&=cs.clear(); return ok; }
 }
 return false;
}
Preferences& ConfigService::secrets(){ if(!secretsOpen_) secretsOpen_=secrets_.begin("home_secrets",false); return secrets_; }
Preferences& ConfigService::calendarSecrets(){ if(!calendarSecretsOpen_) calendarSecretsOpen_=calendarSecrets_.begin("cal_auth",false); return calendarSecrets_; }
bool ConfigService::hasSecret(const char*k){auto&p=secrets();return secretsOpen_&&p.isKey(k);}
bool ConfigService::credentialsReady(){secrets();return secretsOpen_;}
String ConfigService::getSecretString(const char*k,const char*f){auto&p=secrets();return secretsOpen_&&p.isKey(k)?p.getString(k,f):String(f);}
bool ConfigService::putSecretString(const char*k,const String&v){if(!validNvsKey(k)||!writable())return false;auto&p=secrets();if(!secretsOpen_)return false;if(p.getType(k)==PT_STR&&p.getString(k)==v)return true;
 // Advance before changing the admin secret: even changing back to an older
 // password must not resurrect previously issued remembered cookies.
 if(!strcmp(k,"web_admin_pwd")){const auto generation=getU32("web_auth_rev",0);if(generation==UINT32_MAX||!putU32("web_auth_rev",generation+1))return false;}
 return changed(p.putString(k,v)>0);}
bool ConfigService::removeSecret(const char*k){if(!validNvsKey(k)||!writable())return false;auto&p=secrets();if(!secretsOpen_)return false;if(!p.isKey(k))return true;
 if(!strcmp(k,"web_admin_pwd")){const auto generation=getU32("web_auth_rev",0);if(generation==UINT32_MAX||!putU32("web_auth_rev",generation+1))return false;}
 return changed(p.remove(k));}
}

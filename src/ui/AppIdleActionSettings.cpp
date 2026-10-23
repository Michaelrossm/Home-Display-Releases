#include "AppIdleActionSettings.h"
#include "SettingsWidgets.h"
#include "UiTheme.h"
#include "../services/AppIdleActionService.h"
#include "../services/ConfigService.h"
#include "../core/AppManager.h"
#include <ArduinoJson.h>
#include <array>
#include <algorithm>
#include <new>
namespace hp {namespace {
struct Controls {lv_obj_t *app=nullptr,*after=nullptr,*enabled=nullptr,*status=nullptr;
    std::array<String,32> ids{};String savedOwner,arguments="{}";size_t count=1;};
void changed(lv_event_t* event){auto& c=*static_cast<Controls*>(lv_event_get_user_data(event));
    const size_t selected=lv_dropdown_get_selected(c.app);if(selected>=c.count)return;
    const bool enabled=selected&&lv_obj_has_state(c.enabled,LV_STATE_CHECKED);
    const String owner=selected?c.ids[selected]:c.savedOwner;
    const String arguments=owner==c.savedOwner?c.arguments:String("{}");
    const bool saved=AppIdleActionService::instance().configure(owner,lv_slider_get_value(c.after)*60,arguments,enabled);
    lv_label_set_text(c.status,saved?"Gespeichert. Start nur vom Homescreen; Beruehrung beendet die Aktion.":"Nicht gespeichert. Einstellungen entsperren und erneut versuchen.");
    if(saved){c.savedOwner=owner;c.arguments=arguments;}
}
}
void addAppIdleActionSettings(lv_obj_t* parent){auto* c=new(std::nothrow) Controls;if(!c)return;
    auto* root=SettingsWidgets::content(parent,false);UiTheme::instance().styleCard(root);lv_obj_set_height(root,LV_SIZE_CONTENT);lv_obj_set_flex_grow(root,0);
    lv_obj_add_event_cb(root,[](lv_event_t* e){delete static_cast<Controls*>(lv_event_get_user_data(e));},LV_EVENT_DELETE,c);
    SettingsWidgets::label(root,"App als Inaktivitaetsaktion",0);
    JsonDocument record;deserializeJson(record,ConfigService::instance().getString("idle_action",""));
    c->savedOwner=record["owner"].as<String>();if(record["arguments"].is<JsonObject>()){c->arguments="";serializeJson(record["arguments"],c->arguments);}
    c->app=lv_dropdown_create(root);String options="Keine App";size_t selected=0;auto& apps=AppManager::instance();
    for(size_t i=0;i<apps.count()&&c->count<c->ids.size();++i){auto* app=apps.at(i);
        if(!app||!app->userInstallable()||!apps.isEnabled(app)||!(app->permissions()&perm(AppPermission::IdleIntegration)))continue;
        c->ids[c->count]=app->id();if(c->savedOwner==app->id())selected=c->count;++c->count;
        String name=app->name();name.replace("\n"," ");options+="\n"+name;
    }
    lv_dropdown_set_options(c->app,options.c_str());lv_dropdown_set_selected(c->app,selected);lv_obj_set_width(c->app,400);
    SettingsWidgets::label(root,"Start nach 1 bis 60 Minuten (Schieberegler)",0);c->after=lv_slider_create(root);lv_obj_set_width(c->after,400);lv_slider_set_range(c->after,1,60);
    lv_slider_set_value(c->after,std::max<uint32_t>(1,std::min<uint32_t>(60,(record["after"]|300u)/60)),LV_ANIM_OFF);
    c->enabled=lv_switch_create(root);if(selected&&(record["enabled"]|false))lv_obj_add_state(c->enabled,LV_STATE_CHECKED);
    c->status=SettingsWidgets::label(root,c->count>1?"Nur Apps mit power.idleIntegration stehen zur Auswahl.":"Noch keine aktivierte App mit Inaktivitaetsintegration installiert.",0);
    lv_obj_add_event_cb(c->app,changed,LV_EVENT_VALUE_CHANGED,c);lv_obj_add_event_cb(c->enabled,changed,LV_EVENT_VALUE_CHANGED,c);lv_obj_add_event_cb(c->after,changed,LV_EVENT_RELEASED,c);
}
}

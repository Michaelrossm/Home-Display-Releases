#include "CountryPicker.h"
#include "../services/RegionService.h"
#include "SettingsWidgets.h"
namespace hp {namespace {
void changed(lv_event_t* e){if(lv_event_get_target(e)!=lv_event_get_current_target(e))return;auto* model=static_cast<lv_obj_t*>(lv_event_get_user_data(e));const unsigned index=lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    if(lv_obj_has_state(model,LV_STATE_DISABLED)){lv_obj_add_state(static_cast<lv_obj_t*>(lv_event_get_target(e)),LV_STATE_DISABLED);return;}
    if(index<=RegionService::count())lv_textarea_set_text(model,index?RegionService::code(index-1):"");
}
void sync(lv_event_t* e){if(lv_event_get_target(e)!=lv_event_get_current_target(e))return;auto* dropdown=static_cast<lv_obj_t*>(lv_event_get_user_data(e));const char* text=lv_textarea_get_text(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    if(lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(e)),LV_STATE_DISABLED))lv_obj_add_state(dropdown,LV_STATE_DISABLED);else lv_obj_remove_state(dropdown,LV_STATE_DISABLED);
    const int i=RegionService::find(text);unsigned selected=0;
    if(i>=0)selected=i+1;else if(*text){selected=RegionService::count()+1;if(lv_dropdown_get_option_count(dropdown)<=selected)lv_dropdown_add_option(dropdown,"Gespeichertes Land (unverändert)",LV_DROPDOWN_POS_LAST);}
    lv_dropdown_set_selected(dropdown,selected);
}
}
lv_obj_t* countryPicker(lv_obj_t* parent,lv_obj_t* model){
    auto* dropdown=lv_dropdown_create(parent);String options=String("Systemstandard · ")+RegionService::name(RegionService::instance().selected());
    for(size_t i=0;i<RegionService::count();++i){options+='\n';options+=RegionService::name(i);}
    lv_dropdown_set_options(dropdown,options.c_str());lv_obj_set_width(dropdown,LV_PCT(100));SettingsWidgets::darkInput(dropdown);lv_obj_add_flag(model,LV_OBJ_FLAG_HIDDEN);
    const char* text=lv_textarea_get_text(model);const int index=RegionService::find(text);
    if(index>=0)lv_dropdown_set_selected(dropdown,index+1);else if(*text){lv_dropdown_add_option(dropdown,"Gespeichertes Land (unverändert)",LV_DROPDOWN_POS_LAST);lv_dropdown_set_selected(dropdown,RegionService::count()+1);}
    lv_obj_add_event_cb(dropdown,changed,LV_EVENT_VALUE_CHANGED,model);lv_obj_add_event_cb(model,sync,LV_EVENT_VALUE_CHANGED,dropdown);lv_obj_add_event_cb(model,sync,LV_EVENT_STYLE_CHANGED,dropdown);
    lv_obj_add_event_cb(dropdown,[](lv_event_t* e){if(lv_event_get_target(e)!=lv_event_get_current_target(e))return;auto* field=static_cast<lv_obj_t*>(lv_event_get_user_data(e));if(lv_obj_is_valid(field))while(lv_obj_remove_event_cb_with_user_data(field,sync,lv_event_get_target(e))){}},LV_EVENT_DELETE,model);
    return dropdown;
}
}

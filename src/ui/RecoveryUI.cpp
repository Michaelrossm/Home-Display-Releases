#include "RecoveryUI.h"
#include "../core/BootManager.h"
#include "../core/RecoveryManager.h"
#include "../services/StorageService.h"
#include "../services/ConfigService.h"
#include "../infrastructure/TransactionJournal.h"
#include "../infrastructure/AssetManager.h"
#include "BuildInfo.h"
#include "../hardware/DisplayManager.h"
namespace hp {
RecoveryUI& RecoveryUI::instance(){static RecoveryUI r;return r;}
static bool factoryArmed=false;
static void factoryAction(lv_event_t*e){auto*b=lv_event_get_target_obj(e);if(!factoryArmed){factoryArmed=true;auto*l=lv_obj_get_child(b,0);if(l)lv_label_set_text(l,"CONFIRM Full Factory Reset");return;}RecoveryManager::instance().execute(RecoveryAction::FullFactoryReset,true);}
static void recoveryAction(lv_event_t*e){auto a=(RecoveryAction)(uintptr_t)lv_event_get_user_data(e);RecoveryManager::instance().execute(a,false);}
static lv_obj_t* button(lv_obj_t*p,const char*txt,RecoveryAction a,int x,int y){auto*b=lv_button_create(p);lv_obj_set_size(b,230,58);lv_obj_set_pos(b,x,y);auto*l=lv_label_create(b);lv_label_set_text(l,txt);lv_obj_center(l);lv_obj_add_event_cb(b,recoveryAction,LV_EVENT_CLICKED,(void*)(uintptr_t)a);return b;}
bool RecoveryUI::show(lv_obj_t*p){if(!p)return false;DisplayUiGuard uiGuard(5000);if(!uiGuard)return false;lv_obj_clean(p);lv_obj_set_style_bg_color(p,lv_color_hex(0x0b1220),0);auto*t=lv_label_create(p);String s="Home-Display Recovery\n\nFirmware: "+String(build::kVersion)+"\nReset: "+String(BootManager::instance().state().resetReason)+"\nUpdate state: "+String((int)TransactionJournal::instance().state())+"\nSystemData: "+String((int)AssetManager::instance().systemDataStatus())+"\nAppFS: "+String((int)AssetManager::instance().appFsStatus())+"\nConfig schema: "+String(ConfigService::instance().schema())+"\nSD: "+String(StorageService::instance().sdAvailable()?"OK":"nicht vorhanden")+"\nGrund: "+RecoveryManager::instance().reason();lv_label_set_text(t,s.c_str());lv_obj_set_pos(t,45,45);button(p,"Restart",RecoveryAction::Restart,45,410);button(p,"Previous firmware",RecoveryAction::PreviousFirmware,295,410);button(p,"Repair assets",RecoveryAction::RepairAssets,545,410);button(p,"Reset network",RecoveryAction::ResetNetwork,795,410);button(p,"Reset UI",RecoveryAction::ResetUi,45,485);button(p,"Reset WebApps",RecoveryAction::ResetWebApps,295,485);button(p,"Reset calendar",RecoveryAction::ResetCalendarAccounts,545,485);button(p,"Reset media",RecoveryAction::ResetMedia,795,485);auto*fb=lv_button_create(p);lv_obj_set_size(fb,310,58);lv_obj_set_pos(fb,795,485);auto*fl=lv_label_create(fb);lv_label_set_text(fl,"Arm Full Factory Reset");lv_obj_center(fl);lv_obj_add_event_cb(fb,factoryAction,LV_EVENT_CLICKED,nullptr);auto*warn=lv_label_create(p);lv_label_set_text(warn,"Full Factory Reset erfordert zwei Klicks und loescht keine Firmware-/Recovery-Partitionen.");lv_obj_set_pos(warn,45,590);return true;}
}

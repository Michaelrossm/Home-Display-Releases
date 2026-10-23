#include "CalculatorApp.h"
#include "../../ui/UiTheme.h"
namespace hp {
namespace {
struct Key {const char* label;const char* code;};
const Key keys[]={
    {"Alles löschen","AC"},{"±","+/-"},{"%","%"},{"÷","/"},
    {"√ Wurzel","sqrt"},{"x² Quadrat","x^2"},{"1/x Kehrwert","1/x"},{"×","x"},
    {"7","7"},{"8","8"},{"9","9"},{"−","-"},
    {"4","4"},{"5","5"},{"6","6"},{"+","+"},
    {"1","1"},{"2","2"},{"3","3"},{"=","="},
    {"Ziffer löschen","DEL"},{"0","0"},{",",","},{"00","00"}
};
const int32_t columns[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
const int32_t rows[]={LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_FR(1),LV_GRID_TEMPLATE_LAST};
std::string mathematicalText(const std::string& text){std::string result;for(char c:text){if(c=='*')result+="×";else if(c=='/')result+="÷";else if(c=='-')result+="−";else result+=c;}return result;}
}
void CalculatorApp::open(lv_obj_t* parent){
    close();model_=CalculatorModel{};
    root_=lv_obj_create(parent);lv_obj_remove_style_all(root_);lv_obj_set_size(root_,LV_PCT(100),LV_PCT(100));
    lv_obj_add_event_cb(root_,[](lv_event_t* e){auto* app=static_cast<CalculatorApp*>(lv_event_get_user_data(e));app->root_=app->display_=app->history_=nullptr;},LV_EVENT_DELETE,this);
    auto& theme=UiTheme::instance();
    lv_obj_remove_flag(root_,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(root_,theme.background(),0);lv_obj_set_style_bg_opa(root_,LV_OPA_COVER,0);
    auto* body=lv_obj_create(root_);lv_obj_remove_style_all(body);lv_obj_set_size(body,LV_PCT(52),LV_PCT(96));lv_obj_center(body);
    lv_obj_set_flex_flow(body,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(body,8,0);lv_obj_remove_flag(body,LV_OBJ_FLAG_SCROLLABLE);
    history_=lv_label_create(body);lv_obj_set_width(history_,LV_PCT(100));lv_obj_set_height(history_,28);lv_label_set_long_mode(history_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(history_,LV_TEXT_ALIGN_RIGHT,0);lv_obj_set_style_text_color(history_,theme.textSecondary(),0);
    lv_obj_set_style_text_font(history_,fontWithGermanFallback(&lv_font_montserrat_18),0);
    display_=lv_label_create(body);lv_obj_set_size(display_,LV_PCT(100),64);lv_label_set_long_mode(display_,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(display_,LV_TEXT_ALIGN_RIGHT,0);lv_obj_set_style_text_color(display_,theme.textPrimary(),0);lv_obj_set_style_text_font(display_,fontWithGermanFallback(&lv_font_montserrat_48),0);
    auto* grid=lv_obj_create(body);lv_obj_remove_style_all(grid);lv_obj_set_width(grid,LV_PCT(100));lv_obj_set_flex_grow(grid,1);
    lv_obj_remove_flag(grid,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_grid_dsc_array(grid,columns,rows);
    lv_obj_set_style_pad_row(grid,10,0);lv_obj_set_style_pad_column(grid,20,0);
    for(unsigned i=0;i<24;++i){
        auto* button=lv_button_create(grid);lv_obj_remove_style_all(button);
        lv_obj_set_grid_cell(button,LV_GRID_ALIGN_STRETCH,i%4,1,LV_GRID_ALIGN_STRETCH,i/4,1);
        const bool operation=i%4==3&&i<20;
        lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);lv_obj_set_style_radius(button,29,0);
        lv_obj_set_style_bg_color(button,operation?theme.accent():i<8?theme.surface():theme.card(),0);
        lv_obj_set_style_bg_color(button,operation?theme.accent():theme.backgroundAlt(),LV_STATE_PRESSED);
        lv_obj_set_style_border_width(button,2,LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button,theme.border(),0);lv_obj_set_style_border_width(button,1,0);
        auto* label=lv_label_create(button);lv_label_set_text(label,keys[i].label);lv_obj_center(label);
        lv_obj_set_style_text_color(label,operation?theme.onAccent():theme.textPrimary(),0);
        lv_obj_set_style_text_font(label,fontWithGermanFallback(std::strlen(keys[i].label)>6?&lv_font_montserrat_18:&lv_font_montserrat_28),0);
        lv_obj_set_user_data(button,const_cast<Key*>(&keys[i]));
        lv_obj_add_event_cb(button,[](lv_event_t* e){auto* app=static_cast<CalculatorApp*>(lv_event_get_user_data(e));
            const auto* key=static_cast<const Key*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
            app->model_.press(key->code);app->update();},LV_EVENT_CLICKED,this);
    }
    update();
}
void CalculatorApp::update(){const auto text=mathematicalText(model_.display());const auto history=mathematicalText(model_.history());if(text!=lv_label_get_text(display_))lv_label_set_text(display_,text.c_str());if(history!=lv_label_get_text(history_))lv_label_set_text(history_,history.c_str());}
void CalculatorApp::close(){if(root_)lv_obj_delete(root_);root_=display_=history_=nullptr;model_=CalculatorModel{};}
}

#include "LuaWidgetsBridge.h"
#include "LuaCollectionWidgets.h"
#include "LuaApp.h"
#include "../../ui/UiTheme.h"
#include "../../ui/IconCatalog.h"
#include "../../ui/NativeAppIcon.h"
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include "LuaImagePolicy.h"
#include "../../services/StorageService.h"
#include "../../services/UsbAppFileSystem.h"
#include "../../services/NetworkWorker.h"
#include "../../services/ImageCodec.h"
#include <memory>
#include <atomic>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
lv_obj_t* object(lua_State* L,int index=1) {
    auto* o=static_cast<lv_obj_t*>(lua_touserdata(L,index));auto* app=LuaApp::self(L);
    if(!o||!app||!lv_obj_is_valid(o))return nullptr;
    for(auto* parent=o;parent;parent=lv_obj_get_parent(parent))if(parent==app->screenRoot())return o;
    return nullptr;
}
int push(lua_State* L,lv_obj_t* o){if(o)lua_pushlightuserdata(L,o);else lua_pushnil(L);return 1;}
int spinner(lua_State* L){auto* p=object(L);return push(L,p?lv_spinner_create(p):nullptr);}
int dropdown(lua_State* L){auto* p=object(L);if(!p)return push(L,nullptr);const char* options=luaL_checkstring(L,2);if(strlen(options)>4096)return push(L,nullptr);auto* o=lv_dropdown_create(p);lv_dropdown_set_options(o,options);UiTheme::instance().styleCard(o);return push(L,o);}
int dropdownValue(lua_State* L){auto* o=object(L);if(!o||!lv_obj_check_type(o,&lv_dropdown_class)){lua_pushnil(L);return 1;}lua_pushinteger(L,lv_dropdown_get_selected(o));return 1;}
int dropdownSet(lua_State* L){auto* o=object(L);const auto n=luaL_checkinteger(L,2);if(o&&lv_obj_check_type(o,&lv_dropdown_class)&&n>=0&&n<lv_dropdown_get_option_count(o))if(lv_dropdown_get_selected(o)!=n)lv_dropdown_set_selected(o,n);return 0;}
int dropdownOptions(lua_State* L){auto* o=object(L);size_t size=0;const char* text=luaL_checklstring(L,2,&size);
    if(o&&lv_obj_check_type(o,&lv_dropdown_class)&&size<=4096&&strlen(text)==size)lv_dropdown_set_options(o,text);return 0;}
int checkbox(lua_State* L){auto* p=object(L);if(!p)return push(L,nullptr);auto* o=lv_checkbox_create(p);lv_checkbox_set_text(o,luaL_optstring(L,2,""));if(lua_toboolean(L,3))lv_obj_add_state(o,LV_STATE_CHECKED);return push(L,o);}
int checked(lua_State* L){auto* o=object(L);lua_pushboolean(L,o&&lv_obj_has_state(o,LV_STATE_CHECKED));return 1;}
int arc(lua_State* L){auto* p=object(L);return push(L,p?lv_arc_create(p):nullptr);}
int arcSet(lua_State* L){auto* o=object(L);if(o&&lv_obj_check_type(o,&lv_arc_class)){
        const int value=LV_CLAMP(0,luaL_checkinteger(L,2),100);
        if(lv_arc_get_value(o)!=value)lv_arc_set_value(o,value);
    }return 0;}
int progress(lua_State* L){auto* p=object(L);return push(L,p?lv_bar_create(p):nullptr);}
int progressSet(lua_State* L){auto* o=object(L);if(o&&lv_obj_check_type(o,&lv_bar_class)){
        const int value=LV_CLAMP(0,luaL_checkinteger(L,2),100);
        if(lv_bar_get_value(o)!=value)lv_bar_set_value(o,value,LV_ANIM_OFF);
    }return 0;}
int list(lua_State* L){auto* p=object(L);return push(L,p?lv_list_create(p):nullptr);}
int listItem(lua_State* L){auto* p=object(L);if(!p||!lv_obj_check_type(p,&lv_list_class))return push(L,nullptr);return push(L,lv_list_add_button(p,nullptr,luaL_checkstring(L,2)));}
int tabview(lua_State* L){auto* p=object(L);return push(L,p?lv_tabview_create(p):nullptr);}
int tab(lua_State* L){auto* p=object(L);if(!p||!lv_obj_check_type(p,&lv_tabview_class))return push(L,nullptr);return push(L,lv_tabview_add_tab(p,luaL_checkstring(L,2)));}
int table(lua_State* L){auto* p=object(L);if(!p)return push(L,nullptr);const auto rows=luaL_optinteger(L,2,1),cols=luaL_optinteger(L,3,1);if(rows<1||rows>64||cols<1||cols>8)return push(L,nullptr);auto* o=lv_table_create(p);lv_table_set_row_count(o,rows);lv_table_set_column_count(o,cols);return push(L,o);}
int tableSet(lua_State* L){auto* o=object(L);const auto row=luaL_checkinteger(L,2),col=luaL_checkinteger(L,3);const char* value=luaL_checkstring(L,4);if(o&&lv_obj_check_type(o,&lv_table_class)&&row>=0&&row<lv_table_get_row_count(o)&&col>=0&&col<lv_table_get_column_count(o)&&strlen(value)<=512){
        const char* previous=lv_table_get_cell_value(o,row,col);
        if(!previous||strcmp(previous,value))lv_table_set_cell_value(o,row,col,value);
    }return 0;}
int flex(lua_State* L){auto* o=object(L);const char* direction=luaL_optstring(L,2,"column");if(o)lv_obj_set_flex_flow(o,!strcmp(direction,"row")?LV_FLEX_FLOW_ROW:!strcmp(direction,"row_wrap")?LV_FLEX_FLOW_ROW_WRAP:LV_FLEX_FLOW_COLUMN);return 0;}
int gap(lua_State* L){auto* o=object(L);const int n=LV_CLAMP(0,luaL_checkinteger(L,2),128);if(o){lv_obj_set_style_pad_row(o,n,0);lv_obj_set_style_pad_column(o,n,0);}return 0;}
int grow(lua_State* L){auto* o=object(L);if(o)lv_obj_set_flex_grow(o,LV_CLAMP(0,luaL_checkinteger(L,2),16));return 0;}
int padding(lua_State* L){auto* o=object(L);if(o)lv_obj_set_style_pad_all(o,LV_CLAMP(0,luaL_checkinteger(L,2),128),0);return 0;}
int margin(lua_State* L){auto* o=object(L);if(o)lv_obj_set_style_margin_all(o,LV_CLAMP(0,luaL_checkinteger(L,2),128),0);return 0;}
int symbol(lua_State* L){lua_pushstring(L,IconCatalog::forApp(luaL_checkstring(L,1)));return 1;}
String iconMarker(const char* name){
    if(strlen(name)>48)return "";
    if(!strcmp(name,"weather"))return "@weather";
    if(!strcmp(name,"printer")||!strcmp(name,"3d_printer"))return "@printer";
    if(!strcmp(name,"drop"))return "@drop";
    if(!strcmp(name,"rain_drops"))return "@rain_drops";
    if(!strcmp(name,"radio"))return "@radio";
    for(const char* semantic:{"speaker","speaker_muted","heart","heart_outline","energy","solar","battery","house"})
        if(!strcmp(name,semantic))return String("@")+semantic;
    if(!strcmp(name,"sun"))return "@weather_clear";
    String marker=String("@weather_")+name;
    if(appIconKind(marker.c_str()))return marker;
    return "";
}
int icon(lua_State* L){auto* p=object(L);if(!p)return push(L,nullptr);const auto marker=iconMarker(luaL_checkstring(L,2));
    return push(L,createAppIcon(p,marker.length()?marker.c_str():IconCatalog::forApp(lua_tostring(L,2)),48,UiTheme::instance().accent()));}
int iconSet(lua_State* L){auto* o=object(L);const char* name=luaL_checkstring(L,2);if(!o)return 0;
    const auto marker=iconMarker(name);
    if(marker.length())setAppIconSymbol(o,marker.c_str());
    else if(lv_obj_check_type(o,&lv_label_class))lv_label_set_text(o,IconCatalog::forApp(name));return 0;}
int iconButton(lua_State* L){auto* p=object(L);if(!p)return push(L,nullptr);const auto marker=iconMarker(luaL_checkstring(L,2));
    if(!marker.length())return push(L,nullptr);
    auto* b=lv_button_create(p);UiTheme::instance().styleCard(b);
    auto* i=createAppIcon(b,marker.c_str(),24,UiTheme::instance().accent());lv_obj_center(i);return push(L,b);}
int iconButtonSet(lua_State* L){auto* b=object(L);const auto marker=iconMarker(luaL_checkstring(L,2));
    if(b&&lv_obj_check_type(b,&lv_button_class)&&marker.length()&&lv_obj_get_child_count(b))setAppIconSymbol(lv_obj_get_child(b,0),marker.c_str());return 0;}
struct ImageData {
    lv_image_dsc_t descriptor{};uint8_t* pixels=nullptr;
    std::atomic<bool> alive{true};std::atomic<unsigned> status{0};
    ~ImageData(){heap_caps_free(pixels);}
};
struct ImageJob;
struct ImageHolder {std::shared_ptr<ImageData> data;LuaApp* owner;size_t reserved;lv_obj_t* object;lv_timer_t* timer=nullptr;ImageJob* pending=nullptr;};
struct ImageJob {std::shared_ptr<ImageData> data;String path;bool appFs;bool usb;uint32_t usbGeneration;};
void readImage(void* pointer) {
    std::unique_ptr<ImageJob> job(static_cast<ImageJob*>(pointer));auto data=job->data;
    if(!data->alive)return;
    auto fail=[&](){heap_caps_free(data->pixels);data->pixels=nullptr;data->status.store(2,std::memory_order_release);};
    auto& storage=StorageService::instance();File file;
    if(job->usb){if(auto fs=usbAppFileSystemFor(job->usbGeneration))file=fs->open(job->path,FILE_READ);}
    else if(job->appFs){if(auto* fs=storage.fsFor(StorageClass::AppPackage))file=fs->open(storage.pathFor(StorageClass::AppPackage,job->path),FILE_READ);}
    else file=storage.openSdRead(job->path);
    if(!file||file.isDirectory()||file.size()<33||file.size()>LuaImagePolicy::maxFileBytes){fail();return;}
    size_t size=file.size();std::unique_ptr<uint8_t,decltype(&heap_caps_free)> encoded(static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),heap_caps_free);
    if(!encoded||file.read(encoded.get(),size)!=size){fail();return;}file.close();
    uint32_t width=0,height=0;if(!ImageCodec::dimensions(encoded.get(),size,width,height)||width>256||height>256||!data->alive){fail();return;}
    auto decoded=ImageCodec::decode(encoded.get(),size,256,256);if(!decoded||!data->alive){fail();return;}
    const size_t bytes=size_t(width)*height*4;data->pixels=decoded->pixels;decoded->pixels=nullptr;
    data->descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;data->descriptor.header.cf=LV_COLOR_FORMAT_ARGB8888;
    data->descriptor.header.w=width;data->descriptor.header.h=height;data->descriptor.header.stride=width*4;
    data->descriptor.data_size=bytes;data->descriptor.data=data->pixels;data->status.store(1,std::memory_order_release);
}
void imageTick(lv_timer_t* timer) {
    auto* holder=static_cast<ImageHolder*>(lv_timer_get_user_data(timer));
    if(holder->pending){
        lv_area_t area{};lv_obj_get_coords(holder->object,&area);area.x2=std::max(area.x1,area.x2);area.y2=std::max(area.y1,area.y2);
        const bool visible=!lv_obj_has_flag(holder->object,LV_OBJ_FLAG_HIDDEN)&&lv_obj_area_is_visible(lv_obj_get_parent(holder->object),&area);
        if(!visible||(holder->owner->lifecycle()!=LuaRuntimeState::Foreground&&holder->owner->lifecycle()!=LuaRuntimeState::Opening)){lv_timer_set_period(timer,250);return;}
        lv_timer_set_period(timer,50);
        if(!holder->owner->reserveNativeBytes(LuaImagePolicy::maxPixelBytes)){holder->data->status=2;delete holder->pending;holder->pending=nullptr;}
        else {holder->reserved=LuaImagePolicy::maxPixelBytes;
            if(!NetworkWorker::instance().submitTransfer(readImage,holder->pending,"lua-image")){holder->owner->releaseNativeBytes(holder->reserved);holder->reserved=0;return;}
            holder->pending=nullptr;
        }
    }
    const auto status=holder->data->status.load(std::memory_order_acquire);
    if(!status)return;
    if(status==1){lv_image_set_src(holder->object,&holder->data->descriptor);const size_t used=holder->data->descriptor.data_size;
        holder->owner->releaseNativeBytes(holder->reserved-used);holder->reserved=used;}
    else{holder->owner->releaseNativeBytes(holder->reserved);holder->reserved=0;}
    holder->timer=nullptr;lv_timer_delete(timer);
}
lv_obj_t* ownImage(lv_obj_t* parent,LuaApp* owner,std::shared_ptr<ImageData> data,size_t reserved,bool pending) {
    auto* holder=new(std::nothrow) ImageHolder{std::move(data),owner,reserved,nullptr};
    if(!holder){data->alive=false;owner->releaseNativeBytes(reserved);return nullptr;}
    holder->object=lv_image_create(parent);lv_obj_set_user_data(holder->object,holder);
    lv_obj_add_event_cb(holder->object,[](lv_event_t* e){auto* h=static_cast<ImageHolder*>(lv_event_get_user_data(e));h->data->alive=false;
        if(h->timer)lv_timer_delete(h->timer);lv_image_cache_drop(&h->data->descriptor);lv_image_header_cache_drop(&h->data->descriptor);
        delete h->pending;h->owner->releaseNativeBytes(h->reserved);delete h;},LV_EVENT_DELETE,holder);
    if(pending)holder->timer=lv_timer_create(imageTick,50,holder);else lv_image_set_src(holder->object,&holder->data->descriptor);
    return holder->object;
}
int imageStatus(lua_State* L){auto* o=object(L);if(!o||!lv_obj_check_type(o,&lv_image_class)){lua_pushnil(L);return 1;}
    auto* h=static_cast<ImageHolder*>(lv_obj_get_user_data(o));const unsigned s=h?h->data->status.load(std::memory_order_acquire):2;
    lua_pushstring(L,s==0?"loading":s==1?"ready":"error");return 1;}
int image(lua_State* L) {
    auto* parent=object(L);auto* app=LuaApp::self(L);if(!parent||!app)return push(L,nullptr);
    if(lua_type(L,2)==LUA_TSTRING){size_t length=0;const char* path=lua_tolstring(L,2,&length);
        if(!LuaImagePolicy::validPath(std::string(path,length))||!app->assetPackageFolder().length())return push(L,nullptr);
        auto data=std::make_shared<ImageData>();auto* job=new(std::nothrow) ImageJob{data,app->assetPackageFolder()+"/"+String(path),app->assetPackageOnAppFs(),app->assetPackageOnUsb(),app->assetUsbGeneration()};
        if(!job)return push(L,nullptr);
        auto* object=ownImage(parent,app,std::move(data),0,true);if(!object){delete job;return push(L,nullptr);}
        static_cast<ImageHolder*>(lv_obj_get_user_data(object))->pending=job;
        return push(L,object);
    }
    luaL_checktype(L,2,LUA_TTABLE);
    lua_getfield(L,2,"width");const int w=lua_tointeger(L,-1);lua_pop(L,1);
    lua_getfield(L,2,"height");const int h=lua_tointeger(L,-1);lua_pop(L,1);
    lua_getfield(L,2,"pixels");size_t size;const char* pixels=lua_tolstring(L,-1,&size);
    if(!pixels||w<1||h<1||w>128||h>128||size!=static_cast<size_t>(w*h*2)){lua_pop(L,1);return push(L,nullptr);}
    if(!app->reserveNativeBytes(size)){lua_pop(L,1);return push(L,nullptr);}
    auto data=std::make_shared<ImageData>();
    data->pixels=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!data->pixels){app->releaseNativeBytes(size);lua_pop(L,1);return push(L,nullptr);}
    memcpy(data->pixels,pixels,size);lua_pop(L,1);
    data->descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;data->descriptor.header.cf=LV_COLOR_FORMAT_RGB565;
    data->descriptor.header.w=w;data->descriptor.header.h=h;data->descriptor.header.stride=w*2;
    data->descriptor.data_size=size;data->descriptor.data=data->pixels;
    data->status=1;return push(L,ownImage(parent,app,std::move(data),size,false));
}
int line(lua_State* L) {
    auto* parent=object(L);luaL_checktype(L,2,LUA_TTABLE);const size_t count=lua_rawlen(L,2);
    if(!parent||count<2||count>64)return push(L,nullptr);
    auto* points=static_cast<lv_point_precise_t*>(heap_caps_malloc(count*sizeof(lv_point_precise_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!points)return push(L,nullptr);
    for(size_t i=0;i<count;++i){lua_rawgeti(L,2,i+1);if(!lua_istable(L,-1)){lua_pop(L,1);heap_caps_free(points);return push(L,nullptr);}lua_rawgeti(L,-1,1);const auto x=lua_tointeger(L,-1);lua_pop(L,1);lua_rawgeti(L,-1,2);const auto y=lua_tointeger(L,-1);lua_pop(L,2);if(x<-4096||x>4096||y<-4096||y>4096){heap_caps_free(points);return push(L,nullptr);}points[i]={static_cast<lv_value_precise_t>(x),static_cast<lv_value_precise_t>(y)};}
    auto* result=lv_line_create(parent);lv_line_set_points(result,points,count);
    lv_obj_add_event_cb(result,[](lv_event_t* e){heap_caps_free(lv_event_get_user_data(e));},LV_EVENT_DELETE,points);
    return push(L,result);
}
void color(lua_State* L,const char* name,lv_color_t value){lua_pushinteger(L,lv_color_to_u32(value)&0xffffff);lua_setfield(L,-2,name);}
int theme(lua_State* L){auto& t=UiTheme::instance();lua_newtable(L);color(L,"background",t.background());color(L,"background_alt",t.backgroundAlt());color(L,"surface",t.surface());color(L,"card",t.card());color(L,"border",t.border());color(L,"text",t.textPrimary());color(L,"text_secondary",t.textSecondary());color(L,"accent",t.accent());color(L,"on_accent",t.onAccent());color(L,"success",t.success());color(L,"warning",t.warning());color(L,"error",t.error());lua_pushinteger(L,t.mode());lua_setfield(L,-2,"mode");lua_pushinteger(L,t.accentIndex());lua_setfield(L,-2,"accent_index");lua_pushinteger(L,t.generation());lua_setfield(L,-2,"generation");lua_pushboolean(L,t.dark());lua_setfield(L,-2,"dark");return 1;}
int card(lua_State* L){auto* o=object(L);if(o)UiTheme::instance().styleCard(o);return 0;}
int primary(lua_State* L){auto* o=object(L);if(o)UiTheme::instance().stylePrimaryButton(o);return 0;}
int input(lua_State* L){auto* o=object(L);if(o)UiTheme::instance().styleInput(o);return 0;}
void deleteToast(lv_timer_t* timer){auto* o=static_cast<lv_obj_t*>(lv_timer_get_user_data(timer));if(o&&lv_obj_is_valid(o))lv_obj_delete(o);else lv_timer_delete(timer);}
void closeDialog(lv_event_t* event){auto* o=static_cast<lv_obj_t*>(lv_event_get_user_data(event));if(o&&lv_obj_is_valid(o))lv_obj_delete(o);}
int toast(lua_State* L){auto* app=LuaApp::self(L);const char* text=luaL_checkstring(L,1);if(!app||strlen(text)>512)return 0;auto* o=lv_label_create(app->screenRoot());lv_label_set_text(o,text);UiTheme::instance().styleCard(o);lv_obj_set_width(o,LV_PCT(80));lv_obj_align(o,LV_ALIGN_BOTTOM_MID,0,-20);auto* timer=lv_timer_create(deleteToast,2500,o);lv_obj_add_event_cb(o,[](lv_event_t* e){lv_timer_delete(static_cast<lv_timer_t*>(lv_event_get_user_data(e)));},LV_EVENT_DELETE,timer);return 0;}
int alert(lua_State* L){auto* app=LuaApp::self(L);const char* title=luaL_checkstring(L,1);const char* text=luaL_checkstring(L,2);if(!app||strlen(title)>128||strlen(text)>2048)return 0;auto* box=lv_msgbox_create(app->screenRoot());lv_msgbox_add_title(box,title);lv_msgbox_add_text(box,text);auto* close=lv_msgbox_add_footer_button(box,"OK");lv_obj_add_event_cb(close,closeDialog,LV_EVENT_CLICKED,box);lv_obj_center(box);return 0;}
}
void registerLuaWidgetsBridge(lua_State* L){
    lua_register(L,"hp_ui_symbol",symbol);
    lua_register(L,"hp_ui_icon_set",iconSet);
    lua_register(L,"hp_ui_icon_button",iconButton);lua_register(L,"hp_ui_icon_button_set",iconButtonSet);
    registerLuaCollectionWidgets(L);
    lua_register(L,"hp_ui_spinner",spinner);lua_register(L,"hp_ui_dropdown",dropdown);
    lua_register(L,"hp_ui_dropdown_value",dropdownValue);lua_register(L,"hp_ui_dropdown_set_value",dropdownSet);
    lua_register(L,"hp_ui_dropdown_set_options",dropdownOptions);
    lua_register(L,"hp_ui_checkbox",checkbox);lua_register(L,"hp_ui_checkbox_checked",checked);
    lua_register(L,"hp_ui_arc",arc);lua_register(L,"hp_ui_arc_set",arcSet);
    lua_register(L,"hp_ui_progress",progress);lua_register(L,"hp_ui_progress_set",progressSet);
    lua_register(L,"hp_ui_list",list);lua_register(L,"hp_ui_list_item",listItem);
    lua_register(L,"hp_ui_tabview",tabview);lua_register(L,"hp_ui_tab",tab);
    lua_register(L,"hp_ui_table",table);lua_register(L,"hp_ui_table_set",tableSet);
    lua_register(L,"hp_ui_image",image);lua_register(L,"hp_ui_line",line);
    lua_register(L,"hp_ui_image_status",imageStatus);
    lua_register(L,"hp_ui_flex",flex);lua_register(L,"hp_ui_gap",gap);lua_register(L,"hp_ui_grow",grow);
    lua_register(L,"hp_ui_padding",padding);lua_register(L,"hp_ui_margin",margin);lua_register(L,"hp_ui_icon",icon);
    lua_register(L,"hp_theme",theme);lua_register(L,"hp_ui_style_card",card);lua_register(L,"hp_ui_style_primary",primary);lua_register(L,"hp_ui_style_input",input);
    lua_register(L,"hp_toast",toast);lua_register(L,"hp_alert",alert);
}
}

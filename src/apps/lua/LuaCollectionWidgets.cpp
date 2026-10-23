#include "LuaCollectionWidgets.h"
#include "LuaApp.h"
#include "../../core/VirtualListWindow.h"
#include "../calendar/MonthGridModel.h"
#include "../../ui/UiTheme.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <new>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {namespace {
constexpr uint32_t kListMagic=0x48505631;
constexpr uint32_t kMonthMagic=0x48504D31;
bool boundedString(lua_State* L,int table,const char* key,char* out,size_t capacity,bool required=true);
struct MonthState {
    uint32_t magic=kMonthMagic;LuaApp* owner=nullptr;lv_obj_t* root=nullptr;
    struct Cell {MonthState* month=nullptr;lv_obj_t* button=nullptr,*label=nullptr;uint16_t count=0;uint8_t colorCount=0;uint32_t colors[4]{};unsigned index=0;};
    Cell cells[42]{};calendar::MonthGridModel model;
    char select[97]{},today[11]{},selected[11]{};
    unsigned created=0,render=0;bool dirty=true;
};
struct ListItem {char id[97]{},label[257]{};bool enabled=true;};
struct ListState {
    uint32_t magic=kListMagic;
    LuaApp* owner=nullptr;lv_obj_t* root=nullptr,*content=nullptr,*status=nullptr;
    struct Row{ListState* list=nullptr;lv_obj_t* button=nullptr,*label=nullptr;uint32_t index=0;char id[97]{};bool enabled=false;};
    Row rows[VirtualListWindow::kRows]{};
    ListItem items[VirtualListWindow::kPage]{};
    uint32_t offset=0,total=0,pageCount=0,created=0,render=0,requested=UINT32_MAX;
    char scroll[97]{},select[97]{},selected[97]{},message[257]{};
    bool dirty=true;
};
lv_obj_t* object(lua_State* L){
    auto* o=static_cast<lv_obj_t*>(lua_touserdata(L,1));auto* app=LuaApp::self(L);
    if(!app||!o||!lv_obj_is_valid(o))return nullptr;
    for(auto* p=o;p;p=lv_obj_get_parent(p))if(p==app->screenRoot())return o;
    return nullptr;
}
ListState* listState(lua_State* L){auto* o=object(L);if(!o)return nullptr;auto* s=static_cast<ListState*>(lv_obj_get_user_data(o));return s&&s->magic==kListMagic?s:nullptr;}
MonthState* monthState(lua_State* L){auto* o=object(L);if(!o)return nullptr;auto* s=static_cast<MonthState*>(lv_obj_get_user_data(o));return s&&s->magic==kMonthMagic?s:nullptr;}
void monthSelect(lv_event_t* e){
    auto* cell=static_cast<MonthState::Cell*>(lv_event_get_user_data(e));auto* s=cell->month;
    strlcpy(s->selected,s->model.cells[cell->index].iso,sizeof(s->selected));s->dirty=true;s->render=0;s->owner->requestUiWork();
    const String name(s->select),date(s->selected);s->owner->callUiEvent(name.c_str(),date.c_str(),cell->index,42);
}
void monthMarkers(lv_event_t* e){
    auto* cell=static_cast<MonthState::Cell*>(lv_event_get_user_data(e));lv_area_t coords{};lv_obj_get_coords(cell->button,&coords);
    auto* layer=lv_event_get_layer(e);const int width=6,gap=4;
    int x=(coords.x1+coords.x2-int(cell->colorCount)*(width+gap))/2;
    for(unsigned i=0;i<cell->colorCount;++i){lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(cell->colors[i]);d.bg_opa=LV_OPA_COVER;d.radius=2;
        lv_area_t mark{x,coords.y2-11,x+width,coords.y2-6};lv_draw_rect(layer,&d,&mark);x+=width+gap;
    }
}
int createMonth(lua_State* L){
    auto* parent=object(L);auto* app=LuaApp::self(L);
    if(!parent||!app->reserveNativeBytes(sizeof(MonthState))){lua_pushnil(L);return 1;}
    void* memory=heap_caps_malloc(sizeof(MonthState),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory){app->releaseNativeBytes(sizeof(MonthState));lua_pushnil(L);return 1;}
    auto* s=new(memory) MonthState;s->owner=app;s->model.set(1970,1,false);
    s->root=lv_obj_create(parent);lv_obj_set_user_data(s->root,s);lv_obj_set_size(s->root,LV_PCT(100),420);
    lv_obj_set_style_pad_all(s->root,0,0);lv_obj_set_style_bg_opa(s->root,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(s->root,0,0);lv_obj_clear_flag(s->root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s->root,[](lv_event_t* e){auto* s=static_cast<MonthState*>(lv_event_get_user_data(e));s->owner->releaseNativeBytes(sizeof(MonthState));s->~MonthState();heap_caps_free(s);},LV_EVENT_DELETE,s);
    lv_obj_add_event_cb(s->root,[](lv_event_t* e){auto* s=static_cast<MonthState*>(lv_event_get_user_data(e));s->dirty=true;s->render=0;s->owner->requestUiWork();},LV_EVENT_SIZE_CHANGED,s);
    lua_pushlightuserdata(L,s->root);return 1;
}
int setMonth(lua_State* L){
    auto* s=monthState(L);luaL_checktype(L,2,LUA_TTABLE);if(!s)return 0;
    auto integer=[&](const char* name){lua_getfield(L,2,name);const int v=luaL_checkinteger(L,-1);lua_pop(L,1);return v;};
    const int year=integer("year"),month=integer("month");
    lua_getfield(L,2,"week_start");const char* start=luaL_optstring(L,-1,"monday");const bool sunday=!strcmp(start,"sunday");
    if(!sunday&&strcmp(start,"monday")){lua_pop(L,1);return luaL_error(L,"month week start");}lua_pop(L,1);
    calendar::MonthGridModel model;if(!model.set(year,month,sunday))return luaL_error(L,"month range");
    char today[11]{},selected[11]{};
    if(!boundedString(L,2,"today",today,sizeof(today),false)||!boundedString(L,2,"selected",selected,sizeof(selected),false)||
       (today[0]&&!calendar::MonthGridModel::validIso(today))||(selected[0]&&!calendar::MonthGridModel::validIso(selected)))return luaL_error(L,"month date");
    lua_getfield(L,2,"days");if(!lua_isnil(L,-1)&&!lua_istable(L,-1)){lua_pop(L,1);return luaL_error(L,"month days");}
    const int days=lua_gettop(L);const size_t count=lua_istable(L,days)?lua_rawlen(L,days):0;
    if(count>42){lua_pop(L,1);return luaL_error(L,"month day budget");}
    struct Mark{uint16_t count=0;uint8_t colors=0;uint32_t color[4]{};};Mark marks[42]{};bool seen[42]{};
    for(size_t i=0;i<count;++i){lua_rawgeti(L,days,i+1);const int row=lua_gettop(L);char date[11]{};
        if(!lua_istable(L,row)||!boundedString(L,row,"date",date,sizeof(date))){lua_pop(L,2);return luaL_error(L,"month day date");}
        int index=-1;for(unsigned j=0;j<42;++j)if(!strcmp(model.cells[j].iso,date))index=j;
        if(index<0||seen[index]){lua_pop(L,2);return luaL_error(L,"month day outside grid or duplicate");}seen[index]=true;
        lua_getfield(L,row,"count");const auto events=luaL_optinteger(L,-1,0);lua_pop(L,1);if(events<0||events>65535){lua_pop(L,2);return luaL_error(L,"month count");}marks[index].count=events;
        lua_getfield(L,row,"colors");if(!lua_isnil(L,-1)&&!lua_istable(L,-1)){lua_pop(L,3);return luaL_error(L,"month colors");}
        const size_t colors=lua_istable(L,-1)?lua_rawlen(L,-1):0;if(colors>4){lua_pop(L,3);return luaL_error(L,"month color budget");}
        marks[index].colors=colors;for(size_t j=0;j<colors;++j){lua_rawgeti(L,-1,j+1);const auto color=luaL_checkinteger(L,-1);lua_pop(L,1);if(color<0||color>0xffffff){lua_pop(L,3);return luaL_error(L,"month color");}marks[index].color[j]=color;}
        lua_pop(L,2);
    }
    lua_pop(L,1);bool changed=strcmp(s->today,today)||(selected[0]&&strcmp(s->selected,selected));
    for(unsigned i=0;i<42;++i)changed|=strcmp(s->model.cells[i].iso,model.cells[i].iso)!=0||s->model.cells[i].inMonth!=model.cells[i].inMonth||s->model.cells[i].weekend!=model.cells[i].weekend;
    for(unsigned i=0;i<42;++i){auto& cell=s->cells[i];auto& mark=marks[i];changed|=cell.count!=mark.count||cell.colorCount!=mark.colors||memcmp(cell.colors,mark.color,sizeof(mark.color));cell.count=mark.count;cell.colorCount=mark.colors;memcpy(cell.colors,mark.color,sizeof(mark.color));}
    s->model=model;strlcpy(s->today,today,sizeof(s->today));if(selected[0])strlcpy(s->selected,selected,sizeof(s->selected));
    if(changed){s->dirty=true;s->render=0;s->owner->requestUiWork();}return 0;
}
void scrollEvent(lv_event_t* e){
    auto* s=static_cast<ListState*>(lv_event_get_user_data(e));s->dirty=true;s->render=0;s->owner->requestUiWork();
    // Page requests are dispatched by the existing UI pump, after rendering.
}
void selectedEvent(lv_event_t* e){
    auto* row=static_cast<ListState::Row*>(lv_event_get_user_data(e));auto* s=row->list;
    if(!row->enabled||!row->id[0])return;
    strlcpy(s->selected,row->id,sizeof(s->selected));s->dirty=true;s->render=0;s->owner->requestUiWork();
    const String name(s->select),id(row->id);const auto index=row->index;
    s->owner->callUiEvent(name.c_str(),id.c_str(),index,s->total);
}
int createList(lua_State* L){
    auto* parent=object(L);auto* app=LuaApp::self(L);
    if(!parent||!app->reserveNativeBytes(sizeof(ListState))){lua_pushnil(L);return 1;}
    void* memory=heap_caps_malloc(sizeof(ListState),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory){app->releaseNativeBytes(sizeof(ListState));lua_pushnil(L);return 1;}
    auto* s=new(memory) ListState;s->owner=app;
    s->root=lv_obj_create(parent);lv_obj_set_user_data(s->root,s);
    lv_obj_set_size(s->root,LV_PCT(100),400);lv_obj_set_scroll_dir(s->root,LV_DIR_VER);
    lv_obj_set_style_pad_all(s->root,0,0);lv_obj_set_style_bg_opa(s->root,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(s->root,0,0);
    s->content=lv_obj_create(s->root);lv_obj_remove_style_all(s->content);lv_obj_set_size(s->content,LV_PCT(100),1);lv_obj_clear_flag(s->content,LV_OBJ_FLAG_SCROLLABLE);
    s->status=lv_label_create(s->root);lv_label_set_text(s->status,"Wird geladen ...");lv_obj_align(s->status,LV_ALIGN_TOP_MID,0,8);
    lv_obj_add_event_cb(s->root,scrollEvent,LV_EVENT_SCROLL,s);
    lv_obj_add_event_cb(s->root,scrollEvent,LV_EVENT_SIZE_CHANGED,s);
    lv_obj_add_event_cb(s->root,[](lv_event_t* e){auto* s=static_cast<ListState*>(lv_event_get_user_data(e));s->owner->releaseNativeBytes(sizeof(ListState));s->~ListState();heap_caps_free(s);},LV_EVENT_DELETE,s);
    lua_pushlightuserdata(L,s->root);return 1;
}
bool boundedString(lua_State* L,int table,const char* key,char* out,size_t capacity,bool required){
    lua_getfield(L,table,key);size_t length=0;const char* value=lua_tolstring(L,-1,&length);
    const bool ok=value&&length<capacity&&(!required||length)&&strlen(value)==length;
    if(ok)strlcpy(out,value,capacity);else if(!required&&lua_isnil(L,-1)){out[0]=0;lua_pop(L,1);return true;}
    lua_pop(L,1);return ok;
}
int setItems(lua_State* L){
    auto* s=listState(L);luaL_checktype(L,3,LUA_TTABLE);
    const auto offset=luaL_checkinteger(L,2),total=luaL_checkinteger(L,4);const size_t count=lua_rawlen(L,3);
    if(!s||offset<0||total<0||total>1000000||count>VirtualListWindow::kPage||uint64_t(offset)+count>uint64_t(total))return luaL_error(L,"virtual list page bounds");
    // Two passes: validation is transactional and never puts a 35 KiB array on
    // the UI stack. Data is held in one bounded PSRAM page, not in Lua history.
    ListItem item;
    uint32_t hashes[VirtualListWindow::kPage]{};
    for(size_t i=0;i<count;++i){lua_rawgeti(L,3,i+1);const int table=lua_gettop(L);
        if(!lua_istable(L,table)||!boundedString(L,table,"id",item.id,sizeof(item.id))||!boundedString(L,table,"text",item.label,sizeof(item.label),false)){lua_pop(L,1);return luaL_error(L,"virtual list item");}
        uint32_t hash=2166136261u;for(const char* p=item.id;*p;++p)hash=(hash^uint8_t(*p))*16777619u;hashes[i]=hash;
        for(size_t j=0;j<i;++j)if(hashes[j]==hash){lua_rawgeti(L,3,j+1);lua_getfield(L,-1,"id");const bool duplicate=!strcmp(item.id,lua_tostring(L,-1));lua_pop(L,2);if(duplicate){lua_pop(L,1);return luaL_error(L,"virtual list duplicate id");}}
        lua_getfield(L,table,"enabled");if(!lua_isnil(L,-1)&&!lua_isboolean(L,-1)){lua_pop(L,2);return luaL_error(L,"virtual list enabled");}lua_pop(L,2);
    }
    bool changed=s->offset!=offset||s->pageCount!=count||s->total!=total;
    for(size_t i=0;i<count;++i){lua_rawgeti(L,3,i+1);const int table=lua_gettop(L);auto& target=s->items[i];
        boundedString(L,table,"id",item.id,sizeof(item.id));boundedString(L,table,"text",item.label,sizeof(item.label),false);
        lua_getfield(L,table,"enabled");item.enabled=lua_isnil(L,-1)||lua_toboolean(L,-1);lua_pop(L,2);
        changed|=strcmp(target.id,item.id)||strcmp(target.label,item.label)||target.enabled!=item.enabled;target=item;
    }
    if(!changed)return 0;
    s->offset=offset;s->pageCount=count;s->total=total;s->dirty=true;s->render=0;
    s->owner->requestUiWork();return 0;
}
int callbacks(lua_State* L){
    auto* s=listState(L);if(!s)return 0;const char* name=luaL_checkstring(L,2);const bool scroll=lua_toboolean(L,3);
    if(strlen(name)>96)return luaL_error(L,"virtual list callback limit");strlcpy(scroll?s->scroll:s->select,name,97);return 0;
}
int status(lua_State* L){auto* s=listState(L);if(!s)return 0;const char* text=luaL_checkstring(L,2);if(strlen(text)>256)return luaL_error(L,"list status limit");strlcpy(s->message,text,sizeof(s->message));s->dirty=true;s->render=0;s->owner->requestUiWork();return 0;}
int monthCallback(lua_State* L){auto* s=monthState(L);const char* name=luaL_checkstring(L,2);if(strlen(name)>96)return luaL_error(L,"month callback limit");if(s)strlcpy(s->select,name,sizeof(s->select));return 0;}
int pending(lua_State* L){auto* m=monthState(L);auto* s=listState(L);lua_pushboolean(L,m?(m->dirty||m->created<42):(s&&(s->dirty||s->created<VirtualListWindow::kRows)));return 1;}
int step(lua_State* L){
    auto* s=listState(L);const unsigned budget=LV_CLAMP(0,luaL_checkinteger(L,2),8);unsigned used=0;const uint32_t start=micros();
    if(auto* m=monthState(L)){
        while(m->created<42&&used+2<=budget&&uint32_t(micros()-start)<2000){
            auto& c=m->cells[m->created];c.month=m;c.index=m->created++;
            c.button=lv_button_create(m->root);c.label=lv_label_create(c.button);
            lv_obj_set_style_shadow_width(c.button,0,0);lv_obj_set_style_radius(c.button,4,0);
            lv_obj_set_style_text_font(c.label,fontWithGermanFallback(&lv_font_montserrat_18),0);lv_obj_center(c.label);
            lv_obj_add_event_cb(c.button,monthSelect,LV_EVENT_CLICKED,&c);
            lv_obj_add_event_cb(c.button,monthMarkers,LV_EVENT_DRAW_MAIN_END,&c);used+=2;
        }
        while(m->dirty&&m->created==42&&m->render<42&&used<budget&&uint32_t(micros()-start)<2000){
            const unsigned i=m->render++;auto& c=m->cells[i];const auto& day=m->model.cells[i];++used;
            const int w=std::max<int>(7,lv_obj_get_content_width(m->root)),h=std::max<int>(6,lv_obj_get_content_height(m->root));
            lv_obj_set_pos(c.button,(i%7)*w/7,(i/7)*h/6);lv_obj_set_size(c.button,w/7-2,h/6-2);
            char text[24];if(c.count)snprintf(text,sizeof(text),"%u\n%u",unsigned(day.date.day),unsigned(c.count));else snprintf(text,sizeof(text),"%u",unsigned(day.date.day));
            if(strcmp(lv_label_get_text(c.label),text))lv_label_set_text(c.label,text);
            const bool selected=!strcmp(day.iso,m->selected),today=!strcmp(day.iso,m->today);
            lv_obj_set_style_bg_color(c.button,selected?UiTheme::instance().accent():lv_color_hex(day.weekend?0x263545:0x182532),0);
            lv_obj_set_style_bg_opa(c.button,day.inMonth?LV_OPA_COVER:LV_OPA_50,0);
            lv_obj_set_style_outline_width(c.button,today?2:0,0);lv_obj_set_style_outline_color(c.button,UiTheme::instance().accent(),0);
            lv_obj_invalidate(c.button);
        }
        if(m->render==42)m->dirty=false;
        lua_pushinteger(L,used);lua_pushboolean(L,m->created==42&&!m->dirty);return 2;
    }
    if(s){
        while(s->created<VirtualListWindow::kRows&&used+2<=budget&&uint32_t(micros()-start)<2000){
            auto& row=s->rows[s->created++];row.list=s;row.button=lv_button_create(s->content);row.label=lv_label_create(row.button);
            lv_obj_set_size(row.button,LV_PCT(100),VirtualListWindow::kRowHeight);lv_obj_set_style_radius(row.button,0,0);
            lv_obj_set_style_shadow_width(row.button,0,0);lv_obj_set_style_text_font(row.label,fontWithGermanFallback(&lv_font_montserrat_18),0);
            lv_obj_set_width(row.label,LV_PCT(95));lv_label_set_long_mode(row.label,LV_LABEL_LONG_DOT);lv_obj_center(row.label);
            lv_obj_add_event_cb(row.button,selectedEvent,LV_EVENT_CLICKED,&row);used+=2;
        }
        if(s->dirty&&s->created==VirtualListWindow::kRows){
            lv_obj_set_height(s->content,std::max<uint32_t>(1,s->total*VirtualListWindow::kRowHeight));
            const auto window=VirtualListWindow::at(std::max<int32_t>(0,lv_obj_get_scroll_y(s->root)),lv_obj_get_height(s->root),s->total);
            while(s->render<VirtualListWindow::kRows&&used<budget&&uint32_t(micros()-start)<2000){
                auto& row=s->rows[s->render];const unsigned slot=s->render++;++used;
                if(slot>=window.count){lv_obj_add_flag(row.button,LV_OBJ_FLAG_HIDDEN);continue;}
                lv_obj_remove_flag(row.button,LV_OBJ_FLAG_HIDDEN);row.index=window.first+slot;
                lv_obj_set_pos(row.button,0,row.index*VirtualListWindow::kRowHeight);
                const bool cached=row.index>=s->offset&&row.index-s->offset<s->pageCount;
                const auto* item=cached?&s->items[row.index-s->offset]:nullptr;
                const char* text=item?item->label:(s->message[0]?s->message:"Wird geladen ...");
                if(strcmp(lv_label_get_text(row.label),text))lv_label_set_text(row.label,text);
                strlcpy(row.id,item?item->id:"",sizeof(row.id));row.enabled=item&&item->enabled;
                if(row.enabled)lv_obj_remove_state(row.button,LV_STATE_DISABLED);else lv_obj_add_state(row.button,LV_STATE_DISABLED);
                if(row.id[0]&&!strcmp(row.id,s->selected))lv_obj_add_state(row.button,LV_STATE_CHECKED);else lv_obj_remove_state(row.button,LV_STATE_CHECKED);
            }
            if(s->render==VirtualListWindow::kRows){s->dirty=false;
                if(s->total)lv_obj_add_flag(s->status,LV_OBJ_FLAG_HIDDEN);else{lv_label_set_text(s->status,s->message[0]?s->message:"Keine Eintraege");lv_obj_remove_flag(s->status,LV_OBJ_FLAG_HIDDEN);}
                if(!window.cached(s->offset,s->pageCount)&&s->requested!=window.first&&s->scroll[0]){
                    s->requested=window.first;lua_pushinteger(L,used);lua_pushboolean(L,true);lua_pushinteger(L,window.first);return 3;
                }
            }
        }
    }
    lua_pushinteger(L,used);lua_pushboolean(L,s&&!s->dirty&&s->created==VirtualListWindow::kRows);return 2;
}
}
void registerLuaCollectionWidgets(lua_State* L){
    lua_register(L,"hp_ui_month_grid",createMonth);lua_register(L,"hp_ui_month_set",setMonth);lua_register(L,"hp_ui_month_callback",monthCallback);
    lua_register(L,"hp_ui_virtual_list",createList);lua_register(L,"hp_ui_virtual_items",setItems);
    lua_register(L,"hp_ui_virtual_callbacks",callbacks);lua_register(L,"hp_ui_collection_status",status);
    lua_register(L,"hp_ui_collection_pending",pending);lua_register(L,"hp_ui_collection_step",step);
}
}

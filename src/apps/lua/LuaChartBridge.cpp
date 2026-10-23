#include "LuaChartBridge.h"
#include "LuaApp.h"
#include "../../ui/UiTheme.h"
#include "../../core/ChartHistoryWindow.h"
#include <esp_heap_caps.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <new>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace hp {
namespace {
struct Chart {
    LuaApp* owner=nullptr;
    ChartHistoryWindow* history[4]{};
    lv_chart_series_t* handles[4]{};
    lv_obj_t* legend=nullptr;
    lv_obj_t* scale=nullptr;
    unsigned series=0;
    bool automatic=true;
    lv_chart_cursor_t* cursor=nullptr;
    lv_obj_t* cursorValue=nullptr;
    std::array<String,32> cursorLabels;
    String cursorSuffix;
    unsigned cursorCount=0,cursorIndex=0;
    std::array<String,32> labels;
    std::array<const char*,33> text{};
};
lv_obj_t* object(lua_State* L) {
    auto* o=static_cast<lv_obj_t*>(lua_touserdata(L,1));auto* app=LuaApp::self(L);
    if(!o||!app||!lv_obj_is_valid(o))return nullptr;
    for(auto* p=o;p;p=lv_obj_get_parent(p))if(p==app->screenRoot())return o;
    return nullptr;
}
Chart* chartState(lv_obj_t* o){return o&&lv_obj_check_type(o,&lv_chart_class)?static_cast<Chart*>(lv_obj_get_user_data(o)):nullptr;}
void updateCursor(lv_obj_t* chart,Chart* state,unsigned index){
    auto* series=state->handles[0];const unsigned count=lv_chart_get_point_count(chart);
    if(!state->cursorValue||!series||!count||!state->cursorCount)return;
    index=std::min(index,std::min(count,state->cursorCount)-1);state->cursorIndex=index;
    const auto value=lv_chart_get_y_array(chart,series)[index];
    if(value==LV_CHART_POINT_NONE){
        lv_point_t point{count>1?int32_t(uint64_t(lv_obj_get_content_width(chart))*index/(count-1)):0,0};
        lv_chart_set_cursor_pos(chart,state->cursor,&point);
        lv_label_set_text_fmt(state->cursorValue,"%s · Keine Daten",state->cursorLabels[index].c_str());return;
    }
    lv_chart_set_cursor_point(chart,state->cursor,series,index);
    lv_label_set_text_fmt(state->cursorValue,"%s · %ld%s",state->cursorLabels[index].c_str(),long(value),state->cursorSuffix.c_str());
}
void dragCursor(lv_event_t* event){
    auto* chart=lv_event_get_target_obj(event);auto* state=chartState(chart);
    auto* input=lv_event_get_indev(event);
    if(!state||!state->cursor||!input||lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER)return;
    lv_point_t point;lv_indev_get_point(input,&point);lv_area_t area;lv_obj_get_content_coords(chart,&area);
    const auto count=lv_chart_get_point_count(chart);const int width=area.x2-area.x1;
    if(!count||width<=0)return;
    const int x=std::max(0,std::min(width,int(point.x-area.x1)));
    const unsigned selected=(uint32_t(x)*(count-1)+width/2)/width;
    if(selected!=state->cursorIndex)updateCursor(chart,state,selected);
}
int create(lua_State* L) {
    auto* parent=object(L);if(!parent){lua_pushnil(L);return 1;}
    luaL_checktype(L,2,LUA_TTABLE);
    lua_getfield(L,2,"points");const int points=luaL_optinteger(L,-1,60);lua_pop(L,1);
    if(points<1||points>256){lua_pushnil(L);return 1;}
    lua_getfield(L,2,"type");const bool bar=!strcmp(luaL_optstring(L,-1,"line"),"bar");lua_pop(L,1);
    lua_getfield(L,2,"min");const bool manual=!lua_isnil(L,-1);const int32_t min=lua_tointeger(L,-1);lua_pop(L,1);
    lua_getfield(L,2,"max");const bool hasMax=!lua_isnil(L,-1);const int32_t max=lua_tointeger(L,-1);lua_pop(L,1);
    if(manual!=hasMax||(manual&&(min>=max||min<-1000000000||max>1000000000))){lua_pushnil(L);return 1;}
    auto* owner=LuaApp::self(L);if(!owner->reserveNativeBytes(sizeof(Chart))){lua_pushnil(L);return 1;}
    auto* memory=heap_caps_malloc(sizeof(Chart),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory){owner->releaseNativeBytes(sizeof(Chart));lua_pushnil(L);return 1;}
    auto* state=new(memory) Chart;state->owner=owner;state->automatic=!manual;
    auto* chart=lv_chart_create(parent);lv_chart_set_type(chart,bar?LV_CHART_TYPE_BAR:LV_CHART_TYPE_LINE);lv_chart_set_point_count(chart,points);
    lv_obj_remove_flag(chart,LV_OBJ_FLAG_SCROLLABLE);
    auto& theme=UiTheme::instance();
    lv_obj_set_style_bg_color(chart,theme.card(),0);
    lv_obj_set_style_bg_opa(chart,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(chart,theme.border(),0);
    lv_obj_set_style_text_color(chart,theme.textSecondary(),0);
    lv_obj_set_style_line_color(chart,theme.border(),LV_PART_MAIN);
    lv_obj_set_user_data(chart,state);lv_obj_add_event_cb(chart,[](lv_event_t* e){auto* s=static_cast<Chart*>(lv_event_get_user_data(e));for(auto* h:s->history)if(h){h->~ChartHistoryWindow();heap_caps_free(h);s->owner->releaseNativeBytes(sizeof(ChartHistoryWindow));}s->owner->releaseUiObjects(s->series+(s->cursorValue?1:0));s->owner->releaseNativeBytes(sizeof(Chart));s->~Chart();heap_caps_free(s);},LV_EVENT_DELETE,state);
    if(manual)lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,min,max);
    state->legend=lv_obj_create(chart);lv_obj_remove_style_all(state->legend);lv_obj_set_size(state->legend,LV_PCT(100),LV_SIZE_CONTENT);lv_obj_set_flex_flow(state->legend,LV_FLEX_FLOW_ROW);lv_obj_set_style_pad_column(state->legend,12,0);lv_obj_align(state->legend,LV_ALIGN_TOP_LEFT,0,0);lv_obj_set_style_pad_top(chart,24,0);
    lua_pushlightuserdata(L,chart);return 1;
}
int add(lua_State* L) {
    auto* chart=object(L);auto* state=chartState(chart);const char* name=luaL_checkstring(L,2);
    if(!state||state->series>=4||strlen(name)>96){lua_pushnil(L);return 1;}
    if(!state->owner->reserveUiObjects(1)){lua_pushnil(L);return 1;}
    const uint32_t defaults[]={0x7CC4FF,0x6EE7B7,0xF4B860,0xF87171};
    const uint32_t color=static_cast<uint32_t>(luaL_optinteger(L,3,defaults[state->series]))&0xffffff;
    auto* series=lv_chart_add_series(chart,lv_color_hex(color),LV_CHART_AXIS_PRIMARY_Y);
    if(!series){state->owner->releaseUiObjects(1);lua_pushnil(L);return 1;}
    state->handles[state->series++]=series;
    auto* label=lv_label_create(state->legend);lv_label_set_text(label,name);lv_obj_set_style_text_color(label,lv_color_hex(color),0);lv_obj_set_style_text_font(label,fontWithGermanFallback(&lv_font_montserrat_14),0);
    lua_pushlightuserdata(L,series);return 1;
}
int set(lua_State* L) {
    auto* chart=object(L);auto* state=chartState(chart);auto* target=static_cast<lv_chart_series_t*>(lua_touserdata(L,2));
    luaL_checktype(L,3,LUA_TTABLE);if(!state||!target)return 0;
    bool found=false;for(auto* s=lv_chart_get_series_next(chart,nullptr);s;s=lv_chart_get_series_next(chart,s))if(s==target)found=true;
    const uint32_t count=lv_chart_get_point_count(chart);if(!found||lua_rawlen(L,3)!=count)return 0;
    std::array<int32_t,256> values;
    for(uint32_t i=0;i<count;++i){lua_rawgeti(L,3,i+1);
        if(lua_isboolean(L,-1)&&!lua_toboolean(L,-1)){values[i]=LV_CHART_POINT_NONE;lua_pop(L,1);continue;}
        const bool numeric=lua_isnumber(L,-1);const double value=lua_tonumber(L,-1);lua_pop(L,1);
        if(!numeric||!std::isfinite(value)||value<-1000000000||value>1000000000)return 0;values[i]=static_cast<int32_t>(std::llround(value));}
    auto* current=lv_chart_get_y_array(chart,target);bool changed=false;
    for(uint32_t i=0;i<count;++i)if(current[i]!=values[i]){current[i]=values[i];changed=true;}
    if(!changed)return 0;
    if(state->automatic) {
        int32_t min=0,max=1;
        for(auto* s=lv_chart_get_series_next(chart,nullptr);s;s=lv_chart_get_series_next(chart,s)) {
            const auto* data=lv_chart_get_y_array(chart,s);
            for(uint32_t i=0;i<count;++i)if(data[i]!=LV_CHART_POINT_NONE){min=std::min(min,data[i]);max=std::max(max,data[i]);}
        }
        const int32_t pad=std::max<int32_t>(1,(max-min)/10);
        lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,min-pad,max+pad);
    }
    lv_chart_refresh(chart);return 0;
}
int cursorLabels(lua_State* L){
    auto* chart=object(L);auto* state=chartState(chart);luaL_checktype(L,2,LUA_TTABLE);
    const size_t count=lua_rawlen(L,2);size_t suffixLength=0;const char* suffix=luaL_optlstring(L,3,"",&suffixLength);
    if(!state||count<1||count>32||count!=lv_chart_get_point_count(chart)||suffixLength>16)return 0;
    std::array<String,32> labels;
    for(size_t i=0;i<count;++i){lua_rawgeti(L,2,i+1);size_t size=0;const char* label=lua_tolstring(L,-1,&size);if(!label||size>32){lua_pop(L,1);return 0;}labels[i]=String(label,size);lua_pop(L,1);}
    if(!state->cursor){
        if(!state->owner->reserveUiObjects(1))return 0;
        state->cursor=lv_chart_add_cursor(chart,UiTheme::instance().accent(),LV_DIR_VER);
        state->cursorValue=lv_label_create(chart);lv_obj_align(state->cursorValue,LV_ALIGN_TOP_RIGHT,0,0);
        lv_obj_set_style_text_color(state->cursorValue,UiTheme::instance().textPrimary(),0);
        lv_obj_set_style_text_font(state->cursorValue,fontWithGermanFallback(&lv_font_montserrat_18),0);
        lv_obj_remove_flag(state->cursorValue,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(chart,dragCursor,LV_EVENT_PRESSED,nullptr);lv_obj_add_event_cb(chart,dragCursor,LV_EVENT_PRESSING,nullptr);
    }
    state->cursorLabels=std::move(labels);state->cursorCount=count;state->cursorSuffix=String(suffix,suffixLength);
    updateCursor(chart,state,state->cursorIndex);return 0;
}
int range(lua_State* L){auto* chart=object(L);auto* state=chartState(chart);const auto lo=luaL_checkinteger(L,2),hi=luaL_checkinteger(L,3);
    if(state&&lo<hi&&lo>=-1000000000&&hi<=1000000000){state->automatic=false;lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,lo,hi);lv_chart_refresh(chart);}return 0;}
int resize(lua_State* L){auto* chart=object(L);const auto count=luaL_checkinteger(L,2);if(chartState(chart)&&count>=1&&count<=256&&lv_chart_get_point_count(chart)!=count)lv_chart_set_point_count(chart,count);return 0;}
int append(lua_State* L){auto* chart=object(L);auto* state=chartState(chart);auto* target=static_cast<lv_chart_series_t*>(lua_touserdata(L,2));
    const auto time=luaL_checknumber(L,3),value=luaL_checknumber(L,4);if(!state||!std::isfinite(time)||time<0||time>9007199254740991.0||!std::isfinite(value)||value<-1000000000||value>1000000000){lua_pushboolean(L,false);return 1;}
    unsigned index=0;while(index<state->series&&state->handles[index]!=target)++index;
    if(index>=state->series){lua_pushboolean(L,false);return 1;}
    auto*& h=state->history[index];if(!h&&state->owner->reserveNativeBytes(sizeof(ChartHistoryWindow))){
        auto* memory=heap_caps_malloc(sizeof(ChartHistoryWindow),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(memory)h=new(memory) ChartHistoryWindow;else state->owner->releaseNativeBytes(sizeof(ChartHistoryWindow));}
    lua_pushboolean(L,h&&h->append(int64_t(time),int32_t(std::llround(value))));return 1;
}
int historyRange(lua_State* L){auto* chart=object(L);auto* state=chartState(chart);auto* target=static_cast<lv_chart_series_t*>(lua_touserdata(L,2));
    const auto from=luaL_checknumber(L,3),to=luaL_checknumber(L,4);const unsigned limit=luaL_optinteger(L,5,256);
    if(!state||limit<2||limit>256||!std::isfinite(from)||!std::isfinite(to)||from<0||to<from||to>9007199254740991.0)return 0;
    unsigned index=0;while(index<state->series&&state->handles[index]!=target)++index;
    if(index>=state->series||!state->history[index])return 0;
    // PSRAM scratch avoids placing a 4-KiB sample array on the UI stack.
    const size_t bytes=limit*sizeof(ChartHistoryWindow::Sample);if(!state->owner->reserveNativeBytes(bytes))return 0;
    auto* samples=static_cast<ChartHistoryWindow::Sample*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!samples){state->owner->releaseNativeBytes(bytes);return 0;}
    const auto count=state->history[index]->range(int64_t(from),int64_t(to),samples,limit);
    if(count){lua_pushvalue(L,1);lua_pushvalue(L,2);lua_createtable(L,lv_chart_get_point_count(chart),0);
        const auto points=lv_chart_get_point_count(chart);for(unsigned i=0;i<points;++i){if(i<count)lua_pushinteger(L,samples[i].value);else lua_pushboolean(L,false);lua_rawseti(L,-2,i+1);}
        // Reuse incremental series validation/autoscale, not a second renderer.
        lua_replace(L,3);lua_settop(L,3);set(L);
    }heap_caps_free(samples);state->owner->releaseNativeBytes(bytes);lua_pushinteger(L,count);return 1;
}
int labels(lua_State* L) {
    auto* chart=object(L);auto* state=chartState(chart);luaL_checktype(L,2,LUA_TTABLE);
    const size_t count=lua_rawlen(L,2);if(!state||count<2||count>32)return 0;
    std::array<String,32> values;
    for(size_t i=0;i<count;++i){lua_rawgeti(L,2,i+1);size_t size;const char* text=lua_tolstring(L,-1,&size);if(!text||size>64){lua_pop(L,1);return 0;}values[i]=String(text,size);lua_pop(L,1);}
    state->labels=std::move(values);for(size_t i=0;i<count;++i)state->text[i]=state->labels[i].c_str();state->text[count]=nullptr;
    if(!state->scale){state->scale=lv_scale_create(chart);lv_obj_set_size(state->scale,LV_PCT(100),28);lv_obj_align(state->scale,LV_ALIGN_BOTTOM_MID,0,0);lv_scale_set_mode(state->scale,LV_SCALE_MODE_HORIZONTAL_BOTTOM);lv_obj_set_style_pad_bottom(chart,32,0);}
    lv_scale_set_total_tick_count(state->scale,count);lv_scale_set_major_tick_every(state->scale,1);lv_scale_set_range(state->scale,0,count-1);lv_scale_set_text_src(state->scale,state->text.data());return 0;
}
}
void registerLuaChartBridge(lua_State* L){lua_register(L,"hp_chart_create",create);lua_register(L,"hp_chart_add_series",add);lua_register(L,"hp_chart_set_series",set);lua_register(L,"hp_chart_set_labels",labels);lua_register(L,"hp_chart_set_cursor_labels",cursorLabels);lua_register(L,"hp_chart_resize",resize);lua_register(L,"hp_chart_set_range",range);lua_register(L,"hp_chart_append",append);lua_register(L,"hp_chart_history_range",historyRange);}
}

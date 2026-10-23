#pragma once
#include "UiTheme.h"
#include <cstring>
#include <algorithm>

namespace hp {
inline uintptr_t appIconKind(const char* symbol){
    if(!symbol)return 0;
    const char* names[]={"@intercom","@contacts","@calculator","@weather","@weather_clear","@weather_clear_night","@weather_cloudy","@weather_rain","@weather_storm","@weather_snow","@weather_fog","@weather_wind","@printer","@weather_partly_cloudy","@weather_drizzle","@weather_heavy_rain","@photos","@settings","@drop","@calendar","@rain_drops","@radio","@files","@speaker","@speaker_muted","@heart","@heart_outline","@energy","@solar","@battery","@house","@music"};
    for(uintptr_t i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!strcmp(symbol,names[i]))return i;
    return 0;
}
inline bool setAppIconSymbol(lv_obj_t* object,const char* symbol){
    const auto tag=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(object));
    const auto kind=appIconKind(symbol);
    if((tag&~uintptr_t(255))!=0x48490000u||(!kind&&(!symbol||strcmp(symbol,"@intercom"))))return false;
    if((tag&255)!=kind){lv_obj_set_user_data(object,reinterpret_cast<void*>(0x48490000u|kind));lv_obj_invalidate(object);}return true;
}
// Static vectors: one LVGL object, no font/emoji dependency, bitmap or timer.
inline lv_obj_t* createAppIcon(lv_obj_t* parent,const char* symbol,int size,lv_color_t color){
    const bool contacts=symbol&&!strcmp(symbol,"@contacts");
    const bool intercom=symbol&&!strcmp(symbol,"@intercom");
    const bool calculator=symbol&&!strcmp(symbol,"@calculator");
    const auto kind=appIconKind(symbol);
    if(!contacts&&!intercom&&!calculator&&!kind){auto* label=lv_label_create(parent);lv_label_set_text(label,symbol?symbol:LV_SYMBOL_LIST);
        lv_obj_set_style_text_font(label,fontWithGermanFallback(size>=40?&lv_font_montserrat_48:&lv_font_montserrat_24),0);
        lv_obj_set_style_text_color(label,color,0);return label;}
    auto* icon=lv_obj_create(parent);lv_obj_remove_style_all(icon);lv_obj_set_size(icon,size,size);
    lv_obj_remove_flag(icon,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(icon,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(icon,color,0);
    lv_obj_set_user_data(icon,reinterpret_cast<void*>(0x48490000u|kind));
    lv_obj_add_event_cb(icon,[](lv_event_t* e){
        auto* obj=lv_event_get_target_obj(e);auto* layer=lv_event_get_layer(e);lv_area_t a;lv_obj_get_coords(obj,&a);
        const int size=std::min(lv_obj_get_width(obj),lv_obj_get_height(obj));auto color=lv_obj_get_style_text_color(obj,0);
        a.x1+=(lv_obj_get_width(obj)-size)/2;a.y1+=(lv_obj_get_height(obj)-size)/2;
        const auto kind=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj))&255;
        auto rect=[&](int x,int y,int w,int h,int radius,bool filled){lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);
            d.bg_color=d.border_color=color;d.bg_opa=filled?LV_OPA_COVER:LV_OPA_TRANSP;
            d.border_width=filled?0:std::max(1,size/22);d.border_opa=LV_OPA_COVER;d.radius=radius*size/48;
            lv_area_t r{a.x1+x*size/48,a.y1+y*size/48,a.x1+(x+w)*size/48,a.y1+(y+h)*size/48};lv_draw_rect(layer,&d,&r);};
        if(kind==31){
            color=lv_color_hex(0xCF72E8);rect(13,7,5,29,2,true);rect(34,4,5,28,2,true);
            for(int x=0;x<25;++x)rect(14+x,7-x/8,1,6,0,true);
            color=lv_color_hex(0x6EBBF4);rect(4,29,15,12,7,true);rect(25,25,15,12,7,true);
        }else if(kind==23||kind==24){
            rect(5,19,11,12,2,true);for(int x=0;x<13;++x)rect(15+x,18-x/2,1,14+x,1,true);
            if(kind==23){rect(32,17,3,16,2,true);rect(39,11,3,28,2,true);}
            else for(int i=0;i<14;++i){rect(31+i,17+i,2,2,1,true);rect(31+i,30-i,2,2,1,true);}
        }else if(kind==25||kind==26){
            const bool fill=kind==25;
            rect(5,8,20,20,11,fill);rect(23,8,20,20,11,fill);
            for(int y=0;y<19;++y){const int inset=y*16/18;if(fill)rect(7+inset,24+y,34-inset*2,1,0,true);else {rect(7+inset,24+y,2,2,0,true);rect(39-inset,24+y,2,2,0,true);}}
        }else if(kind==27){
            color=lv_color_hex(0xF4BD48);rect(20,3,9,22,2,true);rect(12,21,22,7,2,true);rect(17,26,8,18,2,true);
        }else if(kind==28){
            color=lv_color_hex(0xFFD568);rect(29,2,14,14,8,true);
            color=lv_color_hex(0x58A5E8);rect(3,20,40,22,3,true);
            color=lv_color_hex(0xBDDFF8);for(int x=0;x<3;++x)rect(12+x*11,22,2,17,0,true);rect(5,29,36,2,0,true);
        }else if(kind==29){
            color=lv_color_hex(0x59C99B);rect(4,12,37,25,4,false);rect(41,20,4,10,2,true);rect(8,16,26,17,2,true);
        }else if(kind==30){
            color=lv_color_hex(0x6FB1EE);for(int i=0;i<17;++i)rect(6+i,23-i,36-i*2,2,0,true);rect(12,23,25,21,2,true);
            color=lv_color_hex(0xE6F0FA);rect(21,30,8,14,1,true);
        }else if(kind==16){
            color=lv_color_hex(0x81B3EF);rect(3,6,42,36,5,true);
            color=lv_color_hex(0xA86BE0);rect(6,9,36,29,3,true);
            color=lv_color_hex(0xFFD267);rect(27,12,10,10,6,true);
            color=lv_color_hex(0x56C5A5);rect(8,27,32,11,4,true);rect(13,22,14,14,6,true);
            color=lv_color_hex(0xE5F4FF);rect(3,6,42,36,5,false);
        }else if(kind==17){
            color=lv_color_hex(0x9FAAB5);rect(8,8,32,32,17,true);
            rect(20,2,8,11,2,true);rect(20,35,8,11,2,true);rect(2,20,11,8,2,true);rect(35,20,11,8,2,true);
            rect(6,6,9,9,2,true);rect(33,6,9,9,2,true);rect(6,33,9,9,2,true);rect(33,33,9,9,2,true);
            color=lv_color_hex(0xDCE1E6);rect(14,14,20,20,12,true);
            color=lv_color_hex(0x64717E);rect(18,18,12,12,8,true);
        }else if(kind==22){
            color=lv_color_hex(0x448BCD);rect(3,10,19,9,3,true);rect(3,15,42,29,4,true);
            color=lv_color_hex(0xE8F1FC);rect(15,5,23,29,2,true);
            color=lv_color_hex(0x96B4D3);for(int y=0;y<3;++y)rect(20,12+y*5,13,2,1,true);
            color=lv_color_hex(0x6AB2EF);rect(3,28,42,16,4,true);
        }else if(kind==21){
            color=lv_color_hex(0xA8BDCE);rect(10,4,29,3,1,true);
            color=lv_color_hex(0xE9AF52);rect(3,13,42,30,5,true);
            color=lv_color_hex(0x426F99);rect(8,18,20,19,10,true);
            color=lv_color_hex(0xC3E0F4);rect(14,24,8,8,5,true);
            color=lv_color_hex(0xF9EDD5);rect(31,19,9,7,2,true);rect(33,31,5,5,3,true);
        }else if(kind==20){
            color=lv_color_hex(0x45B9F4);
            for(int i=0;i<3;++i){const int x=3+i*15,y=i==1?1:12;rect(x+2,y+13,10,15,6,true);rect(x+4,y+6,6,14,4,true);rect(x+6,y+1,2,12,1,true);}
        }else if(kind==19){
            color=lv_color_hex(0xE9F1FA);rect(5,7,38,37,5,true);
            color=lv_color_hex(0xEF6671);rect(5,7,38,12,4,true);
            color=lv_color_hex(0x8EA5BC);rect(13,3,4,10,2,true);rect(31,3,4,10,2,true);
            color=lv_color_hex(0x477BD1);
            for(int y=0;y<2;++y)for(int x=0;x<3;++x)rect(12+x*10,24+y*9,5,5,1,true);
        }else if(kind==18){
            color=lv_color_hex(0x45B9F4);rect(12,20,24,24,14,true);rect(17,12,14,21,8,true);rect(21,5,6,20,4,true);
            color=lv_color_hex(0xBAE8FF);rect(17,27,4,10,3,true);
        }else if(kind==12){
            color=lv_color_hex(0x8CAFC8);rect(5,4,38,40,3,false);rect(8,8,3,31,1,true);rect(37,8,3,31,1,true);
            color=lv_color_hex(0x56B9EF);rect(10,14,28,4,1,true);rect(18,30,13,8,1,true);
            color=lv_color_hex(0xF7B44A);rect(21,17,7,8,1,true);rect(23,25,3,4,1,true);
            color=lv_color_hex(0xDCE8F0);rect(10,38,28,3,1,true);rect(8,44,7,2,1,true);rect(33,44,7,2,1,true);
        }else if(kind>=3){
            const bool sun=kind==3||kind==4||kind==13||kind==14;
            if(sun){
                const int c=kind==4?24:17,r=kind==4?10:8,p=kind==4?15:12,len=kind==4?6:4;
                color=lv_color_hex(0xFFB740);rect(c-1,c-p-len,3,len,1,true);rect(c-1,c+p,3,len,1,true);rect(c-p-len,c-1,len,3,1,true);rect(c+p,c-1,len,3,1,true);
                for(int x:{-1,1})for(int y:{-1,1})rect(c+x*p-2,c+y*p-2,5,5,2,true);
                color=lv_color_hex(0xFFD568);rect(c-r,c-r,2*r,2*r,r,true);
                color=lv_color_hex(0xFFE9A7);rect(c-r+4,c-r+3,6,5,3,true);
            }
            if(kind==5){color=lv_color_hex(0xADCDF5);rect(12,6,25,27,16,true);color=lv_color_hex(0xFFC857);rect(5,7,3,3,2,true);rect(38,29,3,3,2,true);}
            if(kind!=4&&kind!=5&&kind!=11){
                color=lv_color_hex((kind==8||kind==15)?0x8199AD:0xDFEBF4);
                rect(6,20,15,16,9,true);rect(15,13,22,24,13,true);rect(30,22,13,14,8,true);rect(11,27,27,10,5,true);
                color=lv_color_hex((kind==8||kind==15)?0x637E95:0xA7C1D3);rect(12,32,26,5,3,true);
            }
            if(kind==3||kind==7||kind==14||kind==15){color=lv_color_hex(0x4DBCF5);for(int i=0;i<3;++i)rect(13+i*10,39+(i&1)*2,3,6,2,true);}
            if(kind==8){color=lv_color_hex(0xFFD25A);rect(24,29,5,8,1,true);rect(20,35,8,4,1,true);rect(20,38,4,7,1,true);}
            if(kind==9){color=lv_color_hex(0x8ED7FF);for(int i=0;i<3;++i){rect(12+i*11,39,3,7,1,true);rect(10+i*11,41,7,3,1,true);}}
            if(kind==10||kind==11){color=lv_color_hex(0xA7C7D9);for(int i=0;i<3;++i)rect(6+i*3,kind==10?32+i*5:13+i*9,35-i*6,2,1,true);}
        }else if(kind==2){
            color=lv_color_hex(0x6476C9);rect(7,2,34,44,5,true);
            color=lv_color_hex(0xBBDEF2);rect(11,6,26,15,2,true);
            color=lv_color_hex(0x263C66);
            // Vector "1+1" remains legible in the 32 px dock without font clipping.
            rect(15,10,2,8,0,true);rect(13,17,6,1,0,true);
            rect(22,13,6,2,0,true);rect(24,11,2,6,0,true);
            rect(32,10,2,8,0,true);rect(30,17,6,1,0,true);
            for(int row=0;row<3;++row)for(int col=0;col<3;++col){color=lv_color_hex(col==2?0xFFC45E:0xE2E8FF);rect(12+col*9,25+row*6,5,3,1,true);}
        }else if(kind==1){
            color=lv_color_hex(0x479FCE);rect(6,3,34,41,5,true);rect(36,11,7,5,1,true);rect(36,24,7,5,1,true);
            color=lv_color_hex(0xFFD2A5);rect(18,11,10,10,6,true);
            color=lv_color_hex(0xE9F6FF);rect(12,25,22,11,6,true);
        }else{
            rect(7,2,34,44,5,false);rect(12,7,24,17,3,false); // indoor display
            rect(20,30,8,9,4,true);rect(17,37,14,3,2,true);rect(23,42,2,2,1,true); // bell + clapper
        }
    },LV_EVENT_DRAW_MAIN,nullptr);
    return icon;
}
}

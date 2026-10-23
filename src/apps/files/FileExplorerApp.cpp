#include "FileExplorerApp.h"
#include "../../ui/InputKeyboard.h"
#include "../../services/FileBrowserService.h"
#include "../../services/FileTextPreviewService.h"
#include "../../services/InternalFilesService.h"
#include "../../services/NasService.h"
#include "../../services/StorageService.h"
#include "../../services/ConfigService.h"
#include "../../services/SecurityService.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/UiTheme.h"
#include "../../ui/UiShell.h"
#include "../../ui/SecurityUi.h"
#include <algorithm>
#include <new>

namespace hp {
struct FileExplorerPage {
    lv_obj_t *root=nullptr,*sidebar=nullptr,*nasPlaces=nullptr,*heading=nullptr,*status=nullptr;
    lv_obj_t *search=nullptr,*grid=nullptr,*detail=nullptr,*keyboard=nullptr,*previewLabel=nullptr;
    lv_timer_t* timer=nullptr;
    String uri="data:/",selected,nasFolder;
    size_t offset=0;
    int nas=-1;
    uint32_t nasGeneration=0;
    bool picker=false,more=false,gridView=true,imageShown=false,loading=false;
    std::vector<FileBrowserEntry> entries;
    std::shared_ptr<DirectoryRequest> directory;
    std::shared_ptr<ImageRequest> image;
    std::shared_ptr<FileTextPreviewRequest> text;
};
namespace {
lv_obj_t* label(lv_obj_t* parent,const char* text,int width=0,const lv_font_t* font=&lv_font_montserrat_18) {
    return SettingsWidgets::label(parent,text,width,font);
}
lv_obj_t* button(lv_obj_t* parent,const char* text,lv_event_cb_t callback,FileExplorerPage* page) {
    auto* b=lv_button_create(parent);lv_obj_set_height(b,48);
    lv_obj_add_event_cb(b,callback,LV_EVENT_CLICKED,page);
    auto* title=lv_label_create(b);lv_label_set_text(title,text);lv_obj_center(title);return b;
}
String sizeText(uint64_t bytes) {
    char buf[48];
    if(bytes>=1024ULL*1024*1024)snprintf(buf,sizeof(buf),"%.2f GiB",double(bytes)/(1024ULL*1024*1024));
    else if(bytes>=1024*1024)snprintf(buf,sizeof(buf),"%.1f MiB",double(bytes)/(1024*1024));
    else if(bytes>=1024)snprintf(buf,sizeof(buf),"%.1f KiB",double(bytes)/1024);
    else snprintf(buf,sizeof(buf),"%llu Byte",static_cast<unsigned long long>(bytes));
    return buf;
}
void render(FileExplorerPage&);void load(FileExplorerPage&);void renderNasSources(FileExplorerPage&);void selectEntry(lv_event_t*);
void cancelPreviews(FileExplorerPage& p) {
    if(p.image)p.image->canceled=true;
    if(p.text)p.text->canceled=true;
    p.image.reset();p.text.reset();p.previewLabel=nullptr;p.imageShown=false;
}
void destroy(FileExplorerPage* p) {
    if(!p)return;
    if(p->directory)p->directory->canceled=true;
    cancelPreviews(*p);
    if(p->timer)lv_timer_delete(p->timer);
    delete p;
}
void navigate(FileExplorerPage& p,const String& target) {
    p.offset=0;p.nas=-1;p.nasFolder="";p.uri=target;
    if(p.search)lv_textarea_set_text(p.search,"");
    load(p);
}
void selectSource(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    const uintptr_t source=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    switch(source) {
        case 0:navigate(p,"data:/");break;
        case 1:navigate(p,"apps:/");break;
        case 2:navigate(p,"sd:/");break;
        case 3:navigate(p,"usb:/");break;
        case 4:navigate(p,"system:/");break;
        case 5:navigate(p,"config:/");break;
        case 6:navigate(p,"cache:/");break;
        case 7:navigate(p,"recovery:/");break;
        default:break;
    }
}
void selectNas(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    const uintptr_t index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    if(index>=NasService::instance().profileCount())return;
    const auto* profile=NasService::instance().profile(index);
    if(!profile||!profile->enabled||profile->protocol!=NasProtocol::Smb2)return;
    p.nas=static_cast<int>(index);p.nasFolder="";p.offset=0;
    if(p.search)lv_textarea_set_text(p.search,"");
    load(p);
}
void nasSettings(lv_event_t*){UiShell::instance().requestAppOpen("settings");}
void renderNasSources(FileExplorerPage& p) {
    if(!p.nasPlaces)return;
    lv_obj_clean(p.nasPlaces);
    if(!ConfigService::instance().getBool("files_nas",false)) {
        label(p.nasPlaces,"NAS-Zugriff ausgeschaltet.");return;
    }
    auto& nas=NasService::instance();size_t shown=0;
    for(size_t i=0;i<nas.profileCount();++i) {
        const auto* profile=nas.profile(i);
        if(!profile||!profile->enabled||profile->protocol!=NasProtocol::Smb2)continue;
        auto* b=button(p.nasPlaces,profile->name,selectNas,&p);
        lv_obj_set_width(b,LV_PCT(100));lv_obj_set_user_data(b,reinterpret_cast<void*>(i));
        ++shown;
    }
    if(!shown)label(p.nasPlaces,"Kein aktives SMB2/3-Profil.");
}
void nasAccess(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    auto* sw=static_cast<lv_obj_t*>(lv_event_get_target(event));
    auto& security=SecurityService::instance();auto& config=ConfigService::instance();
    if(!security.settingsAllowed()||security.tabletLocked()) {
        if(config.getBool("files_nas",false))lv_obj_add_state(sw,LV_STATE_CHECKED);
        else lv_obj_remove_state(sw,LV_STATE_CHECKED);
        SecurityUi::requestSettingsUnlock();
        lv_label_set_text(p.status,"Einstellungen entsperren, um den NAS-Zugriff zu ändern.");return;
    }
    SecurityWriteGuard guard;
    const bool enabled=lv_obj_has_state(sw,LV_STATE_CHECKED);
    if(!config.putBool("files_nas",enabled)) {
        if(config.getBool("files_nas",false))lv_obj_add_state(sw,LV_STATE_CHECKED);
        else lv_obj_remove_state(sw,LV_STATE_CHECKED);
        lv_label_set_text(p.status,"NAS-Einstellung konnte nicht gespeichert werden.");return;
    }
    if(!enabled&&p.nas>=0)navigate(p,"data:/");
    renderNasSources(p);
}
void refresh(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    renderNasSources(p);load(p);
}
void up(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    if(p.nas>=0) {
        const int slash=p.nasFolder.lastIndexOf('/');
        p.nasFolder=slash<0?String():p.nasFolder.substring(0,slash);
    } else if(InternalFilesService::isUri(p.uri)) {
        const int slash=p.uri.lastIndexOf('/');const int colon=p.uri.indexOf(':');
        p.uri=slash<=colon+1?p.uri.substring(0,colon+1)+"/":p.uri.substring(0,slash);
    } else p.uri=FileBrowserService::parent(p.uri);
    p.offset=0;load(p);
}
void mode(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    p.gridView=!p.gridView;render(p);
}
void previous(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    if(p.nas>=0)return;
    if(p.offset>=96){p.offset-=96;load(p);}
}
void next(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    if(p.nas>=0)return;
    if(p.more){p.offset+=96;load(p);}
}
void search(lv_event_t* event){render(*static_cast<FileExplorerPage*>(lv_event_get_user_data(event)));}
void focus(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    InputKeyboard::bind(p.keyboard,p.search);lv_obj_remove_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(p.keyboard);
}
void keyboard(lv_event_t* event) {
    if(lv_event_get_code(event)!=LV_EVENT_READY&&lv_event_get_code(event)!=LV_EVENT_CANCEL)return;
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    lv_keyboard_set_textarea(p.keyboard,nullptr);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
}
void applyWallpaper(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    if(!SecurityService::instance().settingsAllowed()){
        SecurityUi::requestSettingsUnlock();
        lv_label_set_text(p.status,"Einstellungen entsperren und erneut auswählen.");return;
    }
    // Cancel only the old image, preserving the selected file URI.
    if(p.image)p.image->canceled=true;
    p.imageShown=false;
    p.image=FileBrowserService::image(p.selected,lv_display_get_horizontal_resolution(nullptr),lv_display_get_vertical_resolution(nullptr),true);
    if(p.timer){lv_timer_set_period(p.timer,200);lv_timer_reset(p.timer);}
    lv_label_set_text(p.status,"Hintergrund wird als lokale Kopie gespeichert ...");
}
void download(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    const auto index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    if(p.nas<0||!ConfigService::instance().getBool("files_nas",false))return;
    const auto* profile=NasService::instance().profile(p.nas);if(!profile)return;
    if(!NasService::instance().startSmbDownload(index,profile->id,p.nasFolder))
        lv_label_set_text(p.status,"NAS-Kopie nicht gestartet. Dienst beschäftigt oder SD fehlt.");
    else lv_label_set_text(p.status,"Datei wird auf SD kopiert, ohne vorhandene Dateien zu überschreiben.");
}
void openCopy(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    navigate(p,String("sd:")+StorageService::instance().pathFor(StorageClass::Media,"/nas"));
}
void render(FileExplorerPage& p) {
    if(!p.grid)return;
    lv_obj_clean(p.grid);
    lv_obj_set_flex_flow(p.grid,p.gridView?LV_FLEX_FLOW_ROW_WRAP:LV_FLEX_FLOW_COLUMN);
    String needle=p.search?String(lv_textarea_get_text(p.search)):String();needle.toLowerCase();size_t visible=0;
    for(size_t i=0;i<p.entries.size();++i) {
        const auto& entry=p.entries[i];String lower=entry.name;lower.toLowerCase();
        if(needle.length()&&lower.indexOf(needle)<0)continue;
        auto* card=lv_button_create(p.grid);
        lv_obj_set_size(card,p.gridView?LV_PCT(31):LV_PCT(100),p.gridView?121:62);
        UiTheme::instance().styleCard(card);lv_obj_set_style_radius(card,14,0);
        lv_obj_set_user_data(card,reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(card,selectEntry,LV_EVENT_CLICKED,&p);
        const bool picture=ImageCodec::supported(entry.name.c_str());
        auto* glyph=lv_label_create(card);
        lv_label_set_text(glyph,entry.directory?LV_SYMBOL_DIRECTORY:picture?LV_SYMBOL_IMAGE:LV_SYMBOL_FILE);
        lv_obj_set_style_text_font(glyph,fontWithGermanFallback(&lv_font_montserrat_28),0);
        lv_obj_set_style_text_color(glyph,entry.directory?lv_color_hex(0xF4BF59):UiTheme::instance().accent(),0);
        lv_obj_align(glyph,LV_ALIGN_TOP_LEFT,0,0);
        auto* name=lv_label_create(card);lv_label_set_text(name,entry.name.c_str());
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);lv_obj_set_width(name,p.gridView?LV_PCT(100):LV_PCT(72));
        lv_obj_align(name,p.gridView?LV_ALIGN_BOTTOM_LEFT:LV_ALIGN_LEFT_MID,p.gridView?0:44,p.gridView?-15:0);
        auto* detail=lv_label_create(card);lv_label_set_text(detail,entry.directory?"Ordner":sizeText(entry.size).c_str());
        lv_obj_set_style_text_font(detail,fontWithGermanFallback(&lv_font_montserrat_14),0);
        lv_obj_align(detail,p.gridView?LV_ALIGN_BOTTOM_LEFT:LV_ALIGN_RIGHT_MID,0,0);++visible;
    }
    if(!visible)label(p.grid,p.loading?"Ordner wird geladen ...":"Keine Dateien auf dieser Seite.");
}
void load(FileExplorerPage& p) {
    if(p.directory)p.directory->canceled=true;
    p.directory.reset();cancelPreviews(p);p.loading=true;p.entries.clear();p.more=false;
    if(p.detail){lv_obj_clean(p.detail);lv_obj_add_flag(p.detail,LV_OBJ_FLAG_HIDDEN);}
    render(p);
    if(p.timer){lv_timer_set_period(p.timer,200);lv_timer_reset(p.timer);}
    if(p.nas>=0) {
        auto& nas=NasService::instance();const auto* profile=nas.profile(p.nas);
        if(!ConfigService::instance().getBool("files_nas",false)||!profile||!profile->enabled||profile->protocol!=NasProtocol::Smb2) {
            p.loading=false;render(p);lv_label_set_text(p.status,"NAS deaktiviert oder nicht konfiguriert.");return;
        }
        lv_label_set_text(p.heading,(String(profile->name)+" / "+p.nasFolder).c_str());
        p.nasGeneration=nas.browseState().generation;
        if(!nas.startSmbBrowse(p.nas,p.nasFolder)) {
            p.loading=false;render(p);lv_label_set_text(p.status,"NAS nicht gestartet. Netzwerk oder laufenden Auftrag prüfen.");
        } else lv_label_set_text(p.status,"NAS-Ordner wird geladen ...");
    } else {
        lv_label_set_text(p.heading,p.uri.c_str());
        p.directory=InternalFilesService::isUri(p.uri)?InternalFilesService::list(p.uri,p.offset):FileBrowserService::list(p.uri,p.offset);
        lv_label_set_text(p.status,"Dateien werden im Hintergrund gelesen ...");
    }
}
struct Preview {std::shared_ptr<DecodedImage> image;lv_image_dsc_t descriptor{};};
void showImage(FileExplorerPage& p,std::shared_ptr<DecodedImage> image) {
    auto* preview=new(std::nothrow) Preview{std::move(image)};if(!preview)return;
    const auto& pixels=*preview->image;
    preview->descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;
    preview->descriptor.header.cf=pixels.rgb565?LV_COLOR_FORMAT_RGB565:LV_COLOR_FORMAT_ARGB8888;
    preview->descriptor.header.w=pixels.width;preview->descriptor.header.h=pixels.height;
    preview->descriptor.header.stride=pixels.width*(pixels.rgb565?2:4);
    preview->descriptor.data=pixels.pixels;
    preview->descriptor.data_size=preview->descriptor.header.stride*pixels.height;
    auto* object=lv_image_create(p.detail);lv_image_set_src(object,&preview->descriptor);
    lv_obj_set_size(object,LV_PCT(100),360);lv_image_set_inner_align(object,LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_add_event_cb(object,[](lv_event_t* event) {
        auto* data=static_cast<Preview*>(lv_event_get_user_data(event));
        lv_image_cache_drop(&data->descriptor);lv_image_header_cache_drop(&data->descriptor);delete data;
    },LV_EVENT_DELETE,preview);
}
void selectEntry(lv_event_t* event) {
    auto& p=*static_cast<FileExplorerPage*>(lv_event_get_user_data(event));
    const size_t index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(event))));
    if(index>=p.entries.size())return;
    const auto entry=p.entries[index];
    if(entry.directory){p.offset=0;if(p.nas>=0)p.nasFolder=entry.uri;else p.uri=entry.uri;load(p);return;}
    cancelPreviews(p);p.selected=entry.uri;
    lv_obj_clean(p.detail);lv_obj_set_width(p.detail,LV_PCT(52));
    lv_obj_remove_flag(p.detail,LV_OBJ_FLAG_HIDDEN);
    label(p.detail,entry.name.c_str(),0,fontWithGermanFallback(&lv_font_montserrat_24));
    label(p.detail,sizeText(entry.size).c_str());
    if(p.nas>=0) {
        const auto* profile=NasService::instance().profile(p.nas);if(!profile)return;
        label(p.detail,"NAS-Datei (SMB). Dateien werden nicht verändert.");
        auto* b=button(p.detail,"Auf SD kopieren",download,&p);
        lv_obj_set_user_data(b,reinterpret_cast<void*>(index));
        button(p.detail,"SD-Downloads anzeigen",openCopy,&p);
        p.selected=String("nas")+String(profile->id)+":/"+entry.uri;
    }
    if(!FileBrowserService::validUri(p.selected)&&!InternalFilesService::isUri(p.selected)) {
        label(p.detail,"Ungültiger Dateipfad. Vorschau gesperrt.");return;
    }
    if(!InternalFilesService::isUri(p.selected) && (ImageCodec::supported(p.selected.c_str())||p.selected.endsWith(".hpi"))) {
        if(!p.selected.endsWith(".hpi"))button(p.detail,"Als Hintergrund wählen",applyWallpaper,&p);
        p.image=FileBrowserService::image(p.selected,580,470,false,true);
        lv_label_set_text(p.status,"Bildvorschau wird geladen ...");
    } else if(FileTextPreviewService::supports(p.selected)) {
        p.previewLabel=label(p.detail,"Textvorschau wird geladen ...");
        lv_obj_set_width(p.previewLabel,LV_PCT(100));
        lv_label_set_long_mode(p.previewLabel,LV_LABEL_LONG_WRAP);
        p.text=FileTextPreviewService::request(p.selected);
        lv_label_set_text(p.status,"Textdatei wird schreibgeschützt geöffnet ...");
    } else {
        label(p.detail,"Dieser Dateityp kann angezeigt, aber derzeit nicht direkt wiedergegeben oder ausgeführt werden.");
        label(p.detail,"Unterstützt: Bilder, Text, Lua, JSON, CSV, Log und G-Code als Vorschau.");
    }
    if(p.timer){lv_timer_set_period(p.timer,200);lv_timer_reset(p.timer);}
}
void poll(lv_timer_t* timer) {
    auto& p=*static_cast<FileExplorerPage*>(lv_timer_get_user_data(timer));
    lv_timer_set_period(timer,(p.loading||p.directory||p.text||(p.image&&!p.imageShown))?200:2000);
    if(SecurityService::instance().tabletLocked()) {
        if(p.text)p.text->canceled=true;
        if(p.image)p.image->canceled=true;
        if(p.detail){lv_obj_clean(p.detail);lv_obj_add_flag(p.detail,LV_OBJ_FLAG_HIDDEN);}
        cancelPreviews(p);return;
    }
    if(p.directory&&p.directory->ready.load(std::memory_order_acquire)) {
        p.loading=false;p.entries=std::move(p.directory->entries);p.more=p.directory->more;
        lv_label_set_text(p.status,p.directory->message.c_str());p.directory.reset();render(p);
    }
    if(p.image&&!p.imageShown&&p.image->ready.load(std::memory_order_acquire)) {
        p.imageShown=true;lv_label_set_text(p.status,p.image->message.c_str());
        if(p.image->image)showImage(p,p.image->image);
    }
    if(p.text&&p.text->ready.load(std::memory_order_acquire)) {
        if(p.previewLabel&&lv_obj_is_valid(p.previewLabel)) {
            lv_label_set_text(p.previewLabel,p.text->text.length()?p.text->text.c_str():p.text->message.c_str());
        }
        lv_label_set_text(p.status,p.text->message.c_str());p.text.reset();p.previewLabel=nullptr;
    }
    if(p.nas>=0) {
        auto& nas=NasService::instance();const auto state=nas.browseState();
        const auto* profile=nas.profile(p.nas);
        if(!ConfigService::instance().getBool("files_nas",false)||!profile||!profile->enabled) {
            p.entries.clear();render(p);lv_obj_clean(p.detail);p.nas=-1;
            lv_label_set_text(p.status,"NAS-Zugriff deaktiviert.");return;
        }
        if(!state.busy&&state.generation!=p.nasGeneration) {
            p.loading=false;p.nasGeneration=state.generation;
            lv_label_set_text(p.status,state.profileId==profile->id?state.message:"Anderer NAS-Auftrag beendet. Bitte neu laden.");
            if(state.success&&state.profileId==profile->id&&String(state.folder)==p.nasFolder) {
                p.entries.clear();
                for(uint8_t i=0;i<state.count;++i) {
                    NasBrowseEntry entry;
                    if(nas.browseEntry(i,entry))p.entries.push_back({entry.name,entry.path,entry.size,entry.directory});
                }
                render(p);
            }
        }
    }
}
}
std::atomic<bool> FileExplorerApp::picker_{false};
void FileExplorerApp::open(lv_obj_t* parent) {
    close();page_=new(std::nothrow) FileExplorerPage{};if(!page_)return;
    auto& p=*page_;p.picker=picker_.exchange(false);
    p.root=lv_obj_create(parent);lv_obj_set_size(p.root,LV_PCT(100),LV_PCT(100));
    UiTheme::instance().styleScreen(p.root);
    lv_obj_set_flex_flow(p.root,LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(p.root,14,0);lv_obj_set_style_pad_column(p.root,18,0);
    lv_obj_add_event_cb(p.root,[](lv_event_t* event) {
        auto* app=static_cast<FileExplorerApp*>(lv_event_get_user_data(event));
        if(!app->page_||lv_event_get_target(event)!=app->page_->root)return;
        destroy(app->page_);app->page_=nullptr;
    },LV_EVENT_DELETE,this);
    p.sidebar=SettingsWidgets::content(p.root,true);
    lv_obj_set_size(p.sidebar,245,LV_PCT(100));lv_obj_set_flex_grow(p.sidebar,0);
    UiTheme::instance().styleCard(p.sidebar);
    label(p.sidebar,"Dateien",0,fontWithGermanFallback(&lv_font_montserrat_28));
    label(p.sidebar,"Speicherorte",0,fontWithGermanFallback(&lv_font_montserrat_18));
    const char* sources[]={"Display-Daten","App-Pakete","SD-Karte","USB-Stick","Systemdateien","Konfiguration","Cache","Recovery"};
    for(uintptr_t i=0;i<8;++i) {
        auto* b=button(p.sidebar,sources[i],selectSource,&p);
        lv_obj_set_width(b,LV_PCT(100));lv_obj_set_user_data(b,reinterpret_cast<void*>(i));
    }
    label(p.sidebar,"NAS-Freigaben");
    auto* access=lv_switch_create(p.sidebar);
    if(ConfigService::instance().getBool("files_nas",false))lv_obj_add_state(access,LV_STATE_CHECKED);
    lv_obj_add_event_cb(access,nasAccess,LV_EVENT_VALUE_CHANGED,&p);
    p.nasPlaces=SettingsWidgets::content(p.sidebar,false);
    lv_obj_set_height(p.nasPlaces,LV_SIZE_CONTENT);lv_obj_set_flex_grow(p.nasPlaces,0);
    renderNasSources(p);
    button(p.sidebar,"NAS einrichten",nasSettings,&p);
    auto* body=SettingsWidgets::content(p.root,false);
    lv_obj_set_size(body,0,LV_PCT(100));lv_obj_set_flex_grow(body,1);
    if(p.picker)label(body,"Hintergrundbild auswählen: Bild öffnen und Als Hintergrund wählen.");
    p.heading=label(body,"Durchsuchen",0,fontWithGermanFallback(&lv_font_montserrat_24));
    auto* tools=SettingsWidgets::row(body,54);
    button(tools,"Zurück",up,&p);button(tools,"Aktualisieren",refresh,&p);
    button(tools,"Raster / Liste",mode,&p);button(tools,"Vorige",previous,&p);button(tools,"Nächste",next,&p);
    p.search=lv_textarea_create(body);lv_textarea_set_one_line(p.search,true);
    UiTheme::instance().styleInput(p.search);
    lv_textarea_set_max_length(p.search,80);
    lv_textarea_set_placeholder_text(p.search,"Dateiname auf dieser Seite suchen");
    lv_obj_set_width(p.search,LV_PCT(100));
    lv_obj_add_event_cb(p.search,search,LV_EVENT_VALUE_CHANGED,&p);
    lv_obj_add_event_cb(p.search,focus,LV_EVENT_FOCUSED,&p);
    p.status=label(body,"Bilder und Textdateien können direkt angezeigt werden.",0,fontWithGermanFallback(&lv_font_montserrat_14));
    auto* area=SettingsWidgets::row(body,0);lv_obj_set_flex_grow(area,1);
    p.grid=SettingsWidgets::content(area,true);lv_obj_set_height(p.grid,LV_PCT(100));
    lv_obj_set_flex_grow(p.grid,1);lv_obj_set_style_pad_row(p.grid,12,0);lv_obj_set_style_pad_column(p.grid,12,0);
    p.detail=SettingsWidgets::content(area,true);UiTheme::instance().styleCard(p.detail);
    lv_obj_set_height(p.detail,LV_PCT(100));lv_obj_set_flex_grow(p.detail,0);
    lv_obj_add_flag(p.detail,LV_OBJ_FLAG_HIDDEN);
    p.keyboard=lv_keyboard_create(p.root);lv_obj_set_width(p.keyboard,LV_PCT(100));
    lv_obj_set_height(p.keyboard,280);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_FLOATING);
    lv_obj_align(p.keyboard,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p.keyboard,keyboard,LV_EVENT_ALL,&p);
    p.timer=lv_timer_create(poll,200,&p);load(p);
}
void FileExplorerApp::close(){if(page_&&page_->root&&lv_obj_is_valid(page_->root))lv_obj_delete(page_->root);}
}

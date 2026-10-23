#include "PhotoApp.h"
#include "../../ui/InputKeyboard.h"
#include "PhotoZoom.h"
#include "../../hardware/DisplayManager.h"
#include "../../services/PhotoLibraryService.h"
#include "../../services/ImageResourceService.h"
#include "../../services/SecurityService.h"
#include "../../services/ConfigService.h"
#include "../../services/PhotoImageCache.h"
#include "../../services/IdleService.h"
#include "../../services/PowerManager.h"
#include "../../services/UiPerformanceMonitor.h"
#include "../../ui/UiShell.h"
#include "../../ui/SettingsWidgets.h"
#include "../../ui/UiTheme.h"
#include <esp_random.h>
#include <ArduinoJson.h>
#include <algorithm>
namespace hp {
enum class PhotoView:uint8_t{Library,Favorites,Folders,Settings,Viewer,Frame,Albums};
struct PhotoPage {
    lv_obj_t *root=nullptr,*toolbar=nullptr,*body=nullptr,*status=nullptr,*title=nullptr,*viewer=nullptr,*picture=nullptr,*keyboard=nullptr;
    lv_obj_t *host=nullptr,*overlay=nullptr,*heart=nullptr,*overlayMessage=nullptr;bool fullscreen=false,controls=false,manualStep=false;uint32_t suppressTapUntil=0,fitScale=256;PhotoZoom zoom;
    lv_obj_t *quality=nullptr,*confirm=nullptr;PhotoImageCache cache;std::shared_ptr<ImageResourceService::Lease> shownLease;
    std::shared_ptr<PhotoDeleteRequest> deletion;String deleteUri,prefetchUri;DirectoryPageCache::Revision displayedRevision,deleteRevision;
    bool performance=false;uint32_t lastLease=0,lastGesture=0,touchSequence=0,lastTransform=0,qualityClock=0;size_t frameNext=SIZE_MAX;
    lv_obj_t *start=nullptr,*end=nullptr,*interval=nullptr,*days[7]{},*schedule=nullptr,*recursive=nullptr,*automatic=nullptr,*takeover=nullptr,*returnAfter=nullptr;
    lv_obj_t *thumbs[12]{};lv_timer_t* timer=nullptr;
    std::shared_ptr<ImageResourceService::Lease> thumbPins[12];bool thumbComplete[12]{};
    PhotoView view=PhotoView::Library,wanted=PhotoView::Library;bool change=true,autoFrame=false,manualFrame=false,initialized=false;
    int action=0;size_t offset=0,selected=0,thumbNext=0,imageIndex=0;uint32_t revision=0,nextAt=0,mediaCheck=0,pictureSince=0;unsigned failures=0;
    DirectoryPageCache::Revision mediaRevision;
    String uri="sd:/",imageUri,displayedUri,album,filter;
    std::vector<size_t> shown,filtered;std::vector<String> sources,albums;
    std::shared_ptr<PhotoScanRequest> scan;std::shared_ptr<PhotoLibrary> library;
    std::shared_ptr<DirectoryRequest> directory;std::shared_ptr<ImageRequest> image;PhotoShuffleBag bag;
};
namespace {
void bubble(lv_obj_t*,lv_obj_t*);
PhotoPage& page(lv_event_t* e){return *static_cast<PhotoPage*>(lv_event_get_user_data(e));}
lv_obj_t* label(lv_obj_t* parent,const char* text){auto* l=SettingsWidgets::label(parent,text,0,fontWithGermanFallback(&lv_font_montserrat_18));if(strlen(text)>65)lv_obj_set_width(l,LV_PCT(100));return l;}
void status(PhotoPage& p,const String& text){if(text!=lv_label_get_text(p.status))lv_label_set_text(p.status,text.c_str());if(p.overlayMessage)lv_label_set_text(p.overlayMessage,text.c_str());}
void click(lv_event_t* e){auto& p=page(e);if(p.fullscreen&&(DisplayManager::instance().pinchSpan()||int32_t(millis()-p.suppressTapUntil)<0))return;p.action=int(reinterpret_cast<intptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e)))));}
lv_obj_t* button(lv_obj_t* parent,const char* text,int action,PhotoPage& p){auto* b=lv_button_create(parent);lv_obj_set_height(b,46);
    UiTheme::instance().styleCard(b);lv_obj_set_user_data(b,reinterpret_cast<void*>(intptr_t(action)));lv_obj_add_event_cb(b,click,LV_EVENT_CLICKED,&p);
    auto* l=lv_label_create(b);lv_label_set_text(l,text);if(strlen(text)>45){lv_obj_set_width(b,LV_PCT(95));lv_obj_set_width(l,LV_PCT(100));lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);}lv_obj_center(l);return b;}
void controls(PhotoPage& p,bool visible){p.controls=visible;if(!p.overlay)return;if(visible)lv_obj_remove_flag(p.overlay,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(p.overlay,LV_OBJ_FLAG_HIDDEN);}
void viewerTap(lv_event_t* e){auto& p=page(e);if(DisplayManager::instance().pinchSpan()||int32_t(millis()-p.suppressTapUntil)<0)return;
    for(auto* o=static_cast<lv_obj_t*>(lv_event_get_target(e));o&&o!=p.viewer;o=lv_obj_get_parent(o))if(o==p.overlay)return;
    controls(p,!p.controls);
}
void drawHeart(lv_event_t* e){auto& p=page(e);auto* object=static_cast<lv_obj_t*>(lv_event_get_target(e));lv_area_t a;lv_obj_get_coords(object,&a);auto* layer=lv_event_get_layer(e);
    const bool favorite=p.displayedUri.length()&&PhotoLibraryService::instance().favorite(p.displayedUri);
    // Vector outline/fill: independent of installed fonts or emoji glyph coverage.
    static constexpr int16_t points[][2]={{0,-6},{-4,-10},{-10,-10},{-14,-6},{-14,0},{-10,6},{0,15},{10,6},{14,0},{14,-6},{10,-10},{4,-10}};
    const int cx=(a.x1+a.x2)/2,cy=(a.y1+a.y2)/2;
    for(unsigned i=0;i<12;++i){const auto& x=points[i];const auto& y=points[(i+1)%12];
        if(favorite){lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);d.color=lv_color_hex(0xFF4775);d.p[0]={lv_value_precise_t(cx),lv_value_precise_t(cy)};d.p[1]={lv_value_precise_t(cx+x[0]),lv_value_precise_t(cy+x[1])};d.p[2]={lv_value_precise_t(cx+y[0]),lv_value_precise_t(cy+y[1])};lv_draw_triangle(layer,&d);}
        else {lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=lv_color_white();d.width=2;d.round_start=d.round_end=1;d.p1={lv_value_precise_t(cx+x[0]),lv_value_precise_t(cy+x[1])};d.p2={lv_value_precise_t(cx+y[0]),lv_value_precise_t(cy+y[1])};lv_draw_line(layer,&d);}
    }
}
lv_obj_t* overlayButton(PhotoPage& p,const char* symbol,int action,lv_align_t align,int x,int y){auto* b=button(p.overlay,symbol,action,p);lv_obj_set_size(b,48,48);lv_obj_set_style_radius(b,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(b,lv_color_black(),0);lv_obj_set_style_bg_opa(b,LV_OPA_40,0);lv_obj_set_style_border_width(b,0,0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_set_style_text_color(b,lv_color_white(),0);lv_obj_align(b,align,x,y);return b;}
void makeViewer(PhotoPage& p){
    p.viewer=lv_obj_create(p.body);lv_obj_remove_style_all(p.viewer);lv_obj_set_size(p.viewer,LV_PCT(100),LV_PCT(100));lv_obj_set_style_bg_color(p.viewer,lv_color_black(),0);lv_obj_set_style_bg_opa(p.viewer,LV_OPA_COVER,0);lv_obj_clear_flag(p.viewer,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(p.viewer,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(p.viewer,viewerTap,LV_EVENT_SHORT_CLICKED,&p);
    p.overlay=lv_obj_create(p.viewer);lv_obj_remove_style_all(p.overlay);lv_obj_set_size(p.overlay,LV_PCT(100),LV_PCT(100));lv_obj_clear_flag(p.overlay,LV_OBJ_FLAG_CLICKABLE);lv_obj_clear_flag(p.overlay,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(p.overlay,LV_OBJ_FLAG_FLOATING);
    overlayButton(p,LV_SYMBOL_CLOSE,22,LV_ALIGN_TOP_RIGHT,-18,18);p.heart=overlayButton(p,"",18,LV_ALIGN_TOP_LEFT,18,18);lv_obj_add_event_cb(p.heart,drawHeart,LV_EVENT_DRAW_MAIN,&p);
    overlayButton(p,LV_SYMBOL_LEFT,16,LV_ALIGN_LEFT_MID,16,0);overlayButton(p,LV_SYMBOL_RIGHT,17,LV_ALIGN_RIGHT_MID,-16,0);overlayButton(p,LV_SYMBOL_TRASH,28,LV_ALIGN_BOTTOM_RIGHT,-18,-18);
    p.overlayMessage=label(p.overlay,"");lv_obj_set_width(p.overlayMessage,LV_PCT(80));lv_label_set_long_mode(p.overlayMessage,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(p.overlayMessage,lv_color_white(),0);lv_obj_set_style_text_align(p.overlayMessage,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(p.overlayMessage,LV_ALIGN_BOTTOM_MID,0,-18);controls(p,false);
}
void cancelImage(PhotoPage& p){if(p.image)p.image->canceled=true;p.image.reset();}
bool showPixels(lv_obj_t* image,std::shared_ptr<DecodedImage> pixels,std::shared_ptr<ImageResourceService::Lease>* keep=nullptr){
    auto resource=ImageResourceService::instance().lease("native.photos",std::move(pixels));if(!resource)return false;
    auto* pin=new(std::nothrow) std::shared_ptr<ImageResourceService::Lease>(resource);if(!pin)return false;
    lv_image_set_src(image,&resource->resource->descriptor);lv_obj_add_event_cb(image,[](lv_event_t* e){auto* p=static_cast<std::shared_ptr<ImageResourceService::Lease>*>(lv_event_get_user_data(e));
        lv_image_cache_drop(&(*p)->resource->descriptor);lv_image_header_cache_drop(&(*p)->resource->descriptor);delete p;},LV_EVENT_DELETE,pin);if(keep)*keep=std::move(resource);return true;
}
void fullscreenPower(PhotoPage& p,bool enabled){
    if(enabled){if(!p.performance)p.performance=PowerManager::instance().acquirePerformance();if(!p.lastLease||uint32_t(millis()-p.lastLease)>=1000){IdleService::instance().keepAwake(IdleLease::PhotoViewer,5000);p.lastLease=millis();}}
    else{IdleService::instance().keepAwake(IdleLease::PhotoViewer,0);p.lastLease=0;if(p.performance){PowerManager::instance().releasePerformance();p.performance=false;}}
}
void present(PhotoPage& p,std::shared_ptr<ImageResourceService::Lease> pin){
    if(!pin||pin==p.shownLease||!p.viewer)return;
    auto* owner=new(std::nothrow) std::shared_ptr<ImageResourceService::Lease>(pin);if(!owner){status(p,"Bildanzeige: Speicher belegt.");return;}
    auto* image=lv_image_create(p.viewer);const auto& pixels=*pin->resource->pixels;
    lv_image_set_src(image,&pin->resource->descriptor);lv_obj_add_event_cb(image,[](lv_event_t* e){auto* owner=static_cast<std::shared_ptr<ImageResourceService::Lease>*>(lv_event_get_user_data(e));auto* descriptor=&(*owner)->resource->descriptor;lv_image_cache_drop(descriptor);lv_image_header_cache_drop(descriptor);delete owner;},LV_EVENT_DELETE,owner);
    lv_obj_update_layout(p.viewer);lv_obj_set_size(image,pixels.width,pixels.height);lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CENTER);lv_image_set_pivot(image,pixels.width/2,pixels.height/2);lv_image_set_antialias(image,true);
    const bool changed=p.displayedUri!=p.imageUri;if(changed){p.zoom.reset();p.pictureSince=millis();p.nextAt=p.pictureSince+uint32_t(PhotoLibraryService::instance().settings().interval)*1000;}
    p.fitScale=std::min<uint32_t>(uint32_t(lv_obj_get_width(p.viewer))*256/pixels.width,uint32_t(lv_obj_get_height(p.viewer))*256/pixels.height);
    lv_image_set_scale(image,p.fitScale*p.zoom.scale/256);lv_obj_align(image,LV_ALIGN_CENTER,p.zoom.panX,p.zoom.panY);lv_obj_move_background(image);
    // Publish the complete replacement before retiring the old LVGL object.
    // Async deletion is cancelled by LVGL if its parent closes first.
    auto* previous=p.picture;p.picture=image;p.shownLease=std::move(pin);p.displayedUri=p.imageUri;p.displayedRevision=p.cache.revision();
    if(previous){lv_obj_add_flag(previous,LV_OBJ_FLAG_HIDDEN);lv_obj_delete_async(previous);}
    if(p.heart)lv_obj_invalidate(p.heart);p.failures=0;status(p,p.displayedUri.substring(p.displayedUri.lastIndexOf('/')+1));
}
size_t neighbor(PhotoPage& p,int direction){if(!p.library||!p.library->count)return 0;size_t next=p.selected;
    for(size_t i=0;i<p.library->count;++i){next=(next+p.library->count+direction)%p.library->count;if(p.view==PhotoView::Frame||photoInAlbum(p.library->path(next),p.filter))return next;}return p.selected;
}
void showDelete(PhotoPage& p){
    if(p.confirm||p.deletion||!p.displayedUri.length()||p.displayedUri!=p.imageUri){status(p,"Bildwechsel läuft; danach erneut versuchen.");return;}
    if(!(p.displayedUri.startsWith("sd:/Bilder/")||p.displayedUri.startsWith("usb:/Bilder/"))){status(p,"Löschen nur für Bilder unter SD/USB:/Bilder. Andere Quellen sind schreibgeschützt.");return;}
    p.deleteUri=p.displayedUri;p.deleteRevision=p.displayedRevision;p.confirm=lv_obj_create(p.root);lv_obj_add_flag(p.confirm,LV_OBJ_FLAG_FLOATING);lv_obj_set_size(p.confirm,LV_PCT(100),LV_PCT(100));lv_obj_set_style_bg_color(p.confirm,lv_color_black(),0);lv_obj_set_style_bg_opa(p.confirm,LV_OPA_70,0);lv_obj_set_style_border_width(p.confirm,0,0);
    auto* card=lv_obj_create(p.confirm);UiTheme::instance().styleCard(card);lv_obj_set_size(card,460,210);lv_obj_center(card);lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);label(card,"Bild wirklich löschen?");auto* row=SettingsWidgets::row(card,58);button(row,"Abbrechen",29,p);button(row,"Löschen",30,p);controls(p,false);bubble(p.confirm,p.keyboard);
}
void scanLibrary(PhotoPage& p,bool favorites){if(p.scan)p.scan->canceled=true;
    p.mediaRevision=FileBrowserService::directoryRevision();p.mediaCheck=millis();
    p.scan=PhotoLibraryService::scan(PhotoLibraryService::instance().settings(),favorites);status(p,"Fotoliste wird im Hintergrund geladen …");}
void directory(PhotoPage& p){if(p.directory)p.directory->canceled=true;p.nextAt=0;p.directory=FileBrowserService::list(p.uri,p.offset);status(p,"Ordner wird geladen …");}
void hideKeyboard(PhotoPage& p){if(p.keyboard){lv_keyboard_set_textarea(p.keyboard,nullptr);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);}}
void focus(lv_event_t* e){auto& p=page(e);InputKeyboard::bind(p.keyboard,static_cast<lv_obj_t*>(lv_event_get_target(e)));lv_obj_remove_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(p.keyboard);}
void bubble(lv_obj_t* object,lv_obj_t* keyboard){if(object==keyboard)return;lv_obj_add_flag(object,LV_OBJ_FLAG_EVENT_BUBBLE);for(uint32_t i=0;i<lv_obj_get_child_count(object);++i)bubble(lv_obj_get_child(object,i),keyboard);}
lv_obj_t* input(lv_obj_t* parent,const char* text,PhotoPage& p,bool clock=true){auto* t=lv_textarea_create(parent);lv_textarea_set_one_line(t,true);lv_textarea_set_max_length(t,5);lv_textarea_set_text(t,text);lv_obj_set_width(t,160);SettingsWidgets::darkInput(t);lv_obj_add_event_cb(t,focus,LV_EVENT_FOCUSED,&p);
    InputKeyboard::setMode(t,clock?keyboardInput::Mode::DateTime:keyboardInput::Mode::Integer);
    if(clock)lv_obj_add_event_cb(t,[](lv_event_t* e){auto* t=static_cast<lv_obj_t*>(lv_event_get_target(e));const char* s=lv_textarea_get_text(t);if(strlen(s)==4&&s[0]>='0'&&s[0]<='9'&&s[1]>='0'&&s[1]<='9'&&s[2]>='0'&&s[2]<='9'&&s[3]>='0'&&s[3]<='9'){char out[6]={s[0],s[1],':',s[2],s[3],0};lv_textarea_set_text(t,out);}},LV_EVENT_VALUE_CHANGED,nullptr);return t;}
void renderGrid(PhotoPage& p){cancelImage(p);lv_obj_clean(p.body);p.shown.clear();p.thumbNext=0;std::fill(std::begin(p.thumbs),std::end(p.thumbs),nullptr);
    for(auto& pin:p.thumbPins)pin.reset();
    lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_ROW_WRAP);lv_obj_set_style_pad_row(p.body,12,0);lv_obj_set_style_pad_column(p.body,12,0);
    auto* sorting=SettingsWidgets::row(p.body,50);const auto& settings=PhotoLibraryService::instance().settings();
    auto* nameSort=button(sorting,"Name",23,p);auto* dateSort=button(sorting,"Datum",24,p);button(sorting,settings.descending?LV_SYMBOL_DOWN:LV_SYMBOL_UP,25,p);
    UiTheme::instance().stylePrimaryButton(settings.sortDate?dateSort:nameSort);
    p.filtered.clear();if(p.library)for(size_t i=0;i<p.library->count;++i)if(photoInAlbum(p.library->path(i),p.filter))p.filtered.push_back(i);
    if(p.offset>=p.filtered.size())p.offset=0;
    if(p.filtered.empty()){label(p.body,"Noch keine Fotos. Bilder auf SD/USB in Bilder ablegen oder weitere Quellen auswählen.");return;}
    for(size_t n=p.offset;n<p.filtered.size()&&n<p.offset+12;++n){const size_t i=p.filtered[n];const String uri=p.library->path(i);auto* b=button(p.body,"",1000+int(i),p);lv_obj_set_size(b,LV_PCT(23),145);
        auto* icon=lv_label_create(b);lv_label_set_text(icon,LV_SYMBOL_IMAGE);lv_obj_align(icon,LV_ALIGN_TOP_MID,0,10);
        auto* name=lv_label_create(b);const String text=(PhotoLibraryService::instance().favorite(uri)?String(LV_SYMBOL_OK " "):String())+uri.substring(uri.lastIndexOf('/')+1);
        lv_label_set_text(name,text.c_str());lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);lv_obj_set_width(name,LV_PCT(100));lv_obj_align(name,LV_ALIGN_BOTTOM_LEFT,0,0);
        p.thumbs[p.shown.size()]=b;p.shown.push_back(i);
    }
    status(p,(p.filter.length()?p.filter:String("Alle Quellen"))+" · "+String(p.filtered.size())+" Fotos · Seite "+String(p.offset/12+1));
}
void renderAlbums(PhotoPage& p){cancelImage(p);lv_obj_clean(p.body);p.albums.clear();lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_COLUMN);
    auto* row=SettingsWidgets::row(p.body,52);button(row,LV_SYMBOL_LEFT " Zurück",20,p);button(row,"Alle Fotos dieses Albums",21,p);
    label(p.body,p.album.length()?p.album.c_str():"Alben · SD, USB und NAS gemeinsam");
    if(p.library)for(size_t i=0;i<p.library->count;++i){const String path=photoAlbum(p.library->path(i));
        if(p.album.length()&&!path.startsWith(p.album+"/"))continue;
        const unsigned start=p.album.length()?p.album.length()+1:0;const String rest=path.substring(start);const int slash=rest.indexOf('/');
        const String child=(p.album.length()?p.album+"/":String())+(slash<0?rest:rest.substring(0,slash));
        if(std::find(p.albums.begin(),p.albums.end(),child)==p.albums.end())p.albums.push_back(child);
    }
    std::sort(p.albums.begin(),p.albums.end(),[](const String& a,const String& b){return strcmp(a.c_str(),b.c_str())<0;});
    if(p.offset>=p.albums.size())p.offset=0;
    for(size_t i=p.offset;i<p.albums.size()&&i<p.offset+12;++i){const String text=String(LV_SYMBOL_DIRECTORY " ")+p.albums[i].substring(p.albums[i].lastIndexOf('/')+1);
        button(p.body,text.c_str(),6000+int(i),p);}
    if(p.albums.empty())label(p.body,"Keine weiteren Unteralben. Fotos über die Schaltfläche oben öffnen.");
    status(p,String(p.albums.size())+" Unteralben · Seite "+String(p.offset/12+1)+" · keine Trennung nach Speichermedium");
}
void renderFolders(PhotoPage& p){lv_obj_clean(p.body);lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_COLUMN);
    auto* row=SettingsWidgets::row(p.body,52);p.sources={"sd:/","usb:/","data:/"};
    button(row,"SD",300,p);button(row,"USB",301,p);button(row,"Display",302,p);
    auto& nas=NasService::instance();for(size_t i=0;i<nas.profileCount();++i){NasProfile profile;if(!ConfigService::instance().getBool("files_nas",false)||!nas.copyProfile(i,profile)||!profile.enabled||profile.protocol!=NasProtocol::Smb2)continue;
        p.sources.push_back(String("nas")+String(profile.id)+":/");button(row,profile.name,300+int(p.sources.size()-1),p);}
    button(row,"NAS einrichten",19,p);label(p.body,"Standardquelle: Bilder auf SD und USB inklusive Unterordnern (unter Zeitplan abschaltbar).");label(p.body,p.uri.c_str());row=SettingsWidgets::row(p.body,52);button(row,"Übergeordneter Ordner",12,p);button(row,"Diesen Ordner auswählen / abwählen",13,p);
    auto* selected=SettingsWidgets::row(p.body,0);lv_obj_set_height(selected,LV_SIZE_CONTENT);lv_obj_set_flex_flow(selected,LV_FLEX_FLOW_ROW_WRAP);
    const auto& settings=PhotoLibraryService::instance().settings();size_t count=0;for(size_t i=0;i<settings.folders.size();++i)if(settings.folders[i].length()){
        button(selected,(String(LV_SYMBOL_CLOSE " ")+settings.folders[i]).c_str(),400+int(i),p);++count;}
    if(!count)label(selected,"Keine Quellordner ausgewählt (bis zu 8 möglich).");
    if(p.directory&&p.directory->ready){for(size_t i=0;i<p.directory->entries.size();++i){const auto& entry=p.directory->entries[i];if(entry.directory)button(p.body,(String(LV_SYMBOL_DIRECTORY " ")+entry.name).c_str(),5000+int(i),p);}
        if(p.directory->more)button(p.body,"Weitere Ordner",14,p);status(p,p.directory->message);}
}
void renderSettings(PhotoPage& p){lv_obj_clean(p.body);lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_COLUMN);const auto& s=PhotoLibraryService::instance().settings();
    label(p.body,"Bilderrahmen · Zeitgesteuerter Start und Rückkehr bei Inaktivität. PIN und Sprechanlagenrufe haben Vorrang.");
    p.schedule=lv_checkbox_create(p.body);lv_checkbox_set_text(p.schedule,"Zeitplan aktivieren");if(s.schedule)lv_obj_add_state(p.schedule,LV_STATE_CHECKED);
    auto* row=SettingsWidgets::row(p.body,60);label(row,"Von");char text[6];snprintf(text,sizeof(text),"%02u:%02u",s.start/60,s.start%60);p.start=input(row,text,p);
    label(row,"Bis");snprintf(text,sizeof(text),"%02u:%02u",s.end/60,s.end%60);p.end=input(row,text,p);
    row=SettingsWidgets::row(p.body,52);const char* days[]={"So","Mo","Di","Mi","Do","Fr","Sa"};for(size_t i=0;i<7;++i){p.days[i]=lv_checkbox_create(row);lv_checkbox_set_text(p.days[i],days[i]);if(s.days&(1u<<i))lv_obj_add_state(p.days[i],LV_STATE_CHECKED);}
    row=SettingsWidgets::row(p.body,54);label(row,"Bilddauer in Sekunden (5–3600)");p.interval=input(row,String(s.interval).c_str(),p,false);lv_textarea_set_max_length(p.interval,4);lv_textarea_set_accepted_chars(p.interval,"0123456789");
    row=SettingsWidgets::row(p.body,54);label(row,"Maximale Renderauflösung (längste Kante)");p.quality=lv_dropdown_create(row);lv_dropdown_set_options(p.quality,"Sparsam · 800 px\nStandard · 1280 px\nQualität · 1600 px\nHoch · 1920 px");const uint16_t edges[]={800,1280,1600,1920};for(unsigned i=0;i<4;++i)if(s.renderEdge==edges[i])lv_dropdown_set_selected(p.quality,i);SettingsWidgets::darkInput(p.quality);
    p.recursive=lv_checkbox_create(p.body);lv_checkbox_set_text(p.recursive,"Unterordner einbeziehen (bis zu 4 Ebenen)");if(s.recursive)lv_obj_add_state(p.recursive,LV_STATE_CHECKED);
    p.automatic=lv_checkbox_create(p.body);lv_checkbox_set_text(p.automatic,"Bilder auf SD und USB automatisch einlesen (mit Unterordnern)");if(s.automaticFolders)lv_obj_add_state(p.automatic,LV_STATE_CHECKED);
    p.takeover=lv_checkbox_create(p.body);lv_checkbox_set_text(p.takeover,"Zum Zeitbeginn auch aus anderen Apps starten; statt Dimmung zurückkehren");if(s.takeover)lv_obj_add_state(p.takeover,LV_STATE_CHECKED);
    row=SettingsWidgets::row(p.body,54);label(row,"Rückkehr nach Sekunden (5–3600)");p.returnAfter=input(row,String(s.returnAfter).c_str(),p,false);lv_textarea_set_max_length(p.returnAfter,4);lv_textarea_set_accepted_chars(p.returnAfter,"0123456789");
    label(p.body,"Zufälliger Durchlauf ohne direkte Wiederholung. Gleiche Start-/Endzeit = ganztägig. Über Mitternacht gilt der gewählte Starttag.");
    label(p.body,"Während der Wiedergabe bleibt das Display an. Geräte-PIN und Sperrbildschirm werden nicht umgangen. Speichern benötigt entsperrte Einstellungen.");
    button(p.body,"Zeitplan speichern",15,p);
    button(p.body,"Automatik für dieses Zeitfenster pausieren",26,p);button(p.body,"Automatik fortsetzen",27,p);
}
void view(PhotoPage& p,PhotoView next){
    std::shared_ptr<ImageResourceService::Lease> preview;bool complete=false;
    if(next==PhotoView::Viewer)for(size_t i=0;i<p.shown.size();++i)if(p.shown[i]==p.selected){preview=p.thumbPins[i];complete=p.thumbComplete[i];break;}
    hideKeyboard(p);cancelImage(p);if(p.directory)p.directory->canceled=true;p.directory.reset();p.view=next;p.wanted=next;p.change=false;p.picture=nullptr;p.viewer=nullptr;p.overlay=p.heart=p.overlayMessage=nullptr;p.displayedUri="";p.zoom.reset();
    if(p.confirm){lv_obj_delete(p.confirm);p.confirm=nullptr;}p.cache.clear();p.shownLease.reset();p.imageUri="";p.prefetchUri="";p.frameNext=SIZE_MAX;
    p.fullscreen=next==PhotoView::Viewer||next==PhotoView::Frame;UiShell::instance().setAppFullscreen(p.host,p.fullscreen);
    fullscreenPower(p,p.fullscreen);
    lv_obj_set_style_pad_all(p.root,p.fullscreen?0:10,0);lv_obj_set_style_pad_row(p.root,p.fullscreen?0:8,0);
    if(p.fullscreen){lv_obj_add_flag(p.toolbar,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(p.status,LV_OBJ_FLAG_HIDDEN);}else{lv_obj_remove_flag(p.toolbar,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(p.status,LV_OBJ_FLAG_HIDDEN);}
    lv_obj_set_style_pad_all(p.body,p.fullscreen?0:12,0);lv_obj_set_style_border_width(p.body,0,0);lv_obj_clear_flag(p.body,LV_OBJ_FLAG_SCROLLABLE);lv_timer_set_period(p.timer,p.fullscreen?33:200);
    if(p.scan)p.scan->canceled=true;p.scan.reset();
    if(next!=PhotoView::Frame)PhotoLibraryService::instance().stopFrame();
    lv_obj_clean(p.body);lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_COLUMN);if(!p.fullscreen)lv_obj_add_flag(p.body,LV_OBJ_FLAG_SCROLLABLE);
    for(auto& pin:p.thumbPins)pin.reset();
    if(next==PhotoView::Library||next==PhotoView::Favorites){scanLibrary(p,next==PhotoView::Favorites);renderGrid(p);}
    else if(next==PhotoView::Albums){scanLibrary(p,false);renderAlbums(p);}
    else if(next==PhotoView::Folders){p.offset=0;directory(p);renderFolders(p);}
    else if(next==PhotoView::Settings)renderSettings(p);
    else {makeViewer(p);
        if(next==PhotoView::Frame){p.library.reset();p.bag.reset();p.nextAt=0;p.failures=0;scanLibrary(p,false);}else p.nextAt=0;
        if(preview&&p.library&&p.selected<p.library->count){p.imageUri=p.library->path(p.selected);p.cache.seed(p.imageUri,preview,complete,PhotoLibraryService::instance().settings().renderEdge);p.prefetchUri=p.library->path(neighbor(p,1));present(p,std::move(preview));}
    }
}
bool parseMinute(const char* text,uint16_t& minute){unsigned hour=0,min=0;int end=0;if(sscanf(text,"%u:%u%n",&hour,&min,&end)!=2||text[end]||hour>23||min>59)return false;minute=hour*60+min;return true;}
void saveSettings(PhotoPage& p){PhotoSettings s=PhotoLibraryService::instance().settings();if(!parseMinute(lv_textarea_get_text(p.start),s.start)||!parseMinute(lv_textarea_get_text(p.end),s.end)){status(p,"Zeit im Format HH:MM eingeben.");return;}
    char* durationEnd=nullptr;const char* durationText=lv_textarea_get_text(p.interval);const unsigned long duration=strtoul(durationText,&durationEnd,10);if(!*durationText||!durationEnd||*durationEnd||duration<5||duration>3600){status(p,"Bilddauer: 5 bis 3600 Sekunden.");return;}s.interval=duration;s.schedule=lv_obj_has_state(p.schedule,LV_STATE_CHECKED);s.recursive=lv_obj_has_state(p.recursive,LV_STATE_CHECKED);s.automaticFolders=lv_obj_has_state(p.automatic,LV_STATE_CHECKED);s.days=0;
    const uint16_t edges[]={800,1280,1600,1920};s.renderEdge=edges[std::min<unsigned>(3,lv_dropdown_get_selected(p.quality))];
    char* end=nullptr;const unsigned long seconds=strtoul(lv_textarea_get_text(p.returnAfter),&end,10);if(!end||*end||seconds<5||seconds>3600){status(p,"Rückkehrzeit: 5 bis 3600 Sekunden.");return;}s.returnAfter=seconds;s.takeover=lv_obj_has_state(p.takeover,LV_STATE_CHECKED);
    for(unsigned i=0;i<7;++i)if(lv_obj_has_state(p.days[i],LV_STATE_CHECKED))s.days|=1u<<i;
    status(p,PhotoLibraryService::instance().save(s)?"Speichere im Hintergrund …":"Nicht gespeichert. Einstellungen entsperren; mindestens einen Wochentag wählen.");}
void perform(PhotoPage& p,int action){auto& service=PhotoLibraryService::instance();
    if(p.deletion&&action!=22){status(p,"Löschen läuft; bitte kurz warten.");return;}
    if(action>=1&&action<=4){p.filter="";p.offset=0;p.wanted=static_cast<PhotoView>(action-1);p.change=true;return;}
    if(action==5){if(!service.startFrame()){status(p,"Bilderrahmen: Zeitplan, Quellordner oder Gerätesperre prüfen.");return;}p.wanted=PhotoView::Frame;p.change=true;return;}
    if(action==6||action==7){if(p.view!=PhotoView::Library&&p.view!=PhotoView::Favorites&&p.view!=PhotoView::Albums)return;
        const size_t count=p.view==PhotoView::Albums?p.albums.size():p.filtered.size();
        if(action==6)p.offset=p.offset>=12?p.offset-12:0;else if(p.offset+12<count)p.offset+=12;
        if(p.view==PhotoView::Albums)renderAlbums(p);else renderGrid(p);return;}
    if(action==8){p.album="";p.offset=0;p.wanted=PhotoView::Albums;p.change=true;return;}
    if(action==9){if(p.view==PhotoView::Library||p.view==PhotoView::Favorites||p.view==PhotoView::Albums)scanLibrary(p,p.view==PhotoView::Favorites);return;}
    if(action==12){p.uri=FileBrowserService::parent(p.uri);p.offset=0;directory(p);renderFolders(p);return;}
    if(action==13){PhotoSettings s=service.settings();bool changed=false;for(auto& f:s.folders)if(f==p.uri){f="";changed=true;break;}
        if(!changed)for(auto& f:s.folders)if(!f.length()){f=p.uri;changed=true;break;}
        status(p,changed&&service.save(s)?"Ordnerauswahl wird gespeichert …":"Maximal 8 Ordner; Einstellungen entsperren / laufendes Speichern abwarten.");return;}
    if(action==14){p.offset+=96;directory(p);return;}
    if(action==15){saveSettings(p);return;}
    if(action==16||action==17){if(!p.library||!p.library->count)return;cancelImage(p);
        p.selected=neighbor(p,action==16?-1:1);p.frameNext=SIZE_MAX;p.manualStep=true;p.nextAt=0;return;}
    if(action==18){if(p.displayedUri.length())status(p,service.toggleFavorite(p.displayedUri)?"Favoriten werden gespeichert …":"Einstellungen entsperren; maximal 32 Favoriten.");return;}
    if(action==19){UiShell::instance().requestAppOpen("settings");return;}
    if(action==20){const int slash=p.album.lastIndexOf('/');p.album=slash<0?String():p.album.substring(0,slash);p.offset=0;renderAlbums(p);return;}
    if(action==21){p.filter=p.album;p.offset=0;p.wanted=PhotoView::Library;p.change=true;return;}
    if(action==22){if(p.view==PhotoView::Frame)service.requestControl(1);p.offset=0;p.wanted=PhotoView::Library;p.change=true;return;}
    if(action>=23&&action<=25){PhotoSettings s=service.settings();if(action==25)s.descending=!s.descending;else{s.sortDate=action==24;s.descending=s.sortDate;}
        status(p,service.save(s)?"Sortierung wird gespeichert …":"Einstellungen entsperren / Speichern abwarten.");return;}
    if(action==26||action==27){service.requestControl(action==26?1:2);status(p,action==26?"Automatik für dieses Zeitfenster pausiert.":"Automatik wird fortgesetzt.");return;}
    if(action==28){showDelete(p);return;}
    if(action==29||action==30){if(!p.confirm)return;lv_obj_delete(p.confirm);p.confirm=nullptr;p.pictureSince=millis();p.nextAt=p.pictureSince+uint32_t(service.settings().interval)*1000;if(action==30){p.deletion=service.removePhoto(p.deleteUri,p.deleteRevision);status(p,"Bild wird gelöscht …");}return;}
    if(action>=1000&&action<2024){p.selected=action-1000;p.wanted=PhotoView::Viewer;p.change=true;return;}
    if(action>=300&&action<400){if(size_t(action-300)<p.sources.size()){p.uri=p.sources[action-300];p.offset=0;directory(p);renderFolders(p);}return;}
    if(action>=400&&action<408){PhotoSettings s=service.settings();s.folders[action-400]="";status(p,service.save(s)?"Entfernen wird gespeichert …":"Einstellungen entsperren.");return;}
    if(action>=6000&&size_t(action-6000)<p.albums.size()){p.album=p.albums[action-6000];p.offset=0;renderAlbums(p);return;}
    if(action>=5000&&action<6000&&p.directory&&p.directory->ready&&size_t(action-5000)<p.directory->entries.size()){p.uri=p.directory->entries[action-5000].uri;p.offset=0;directory(p);renderFolders(p);}
}
void viewerPoll(PhotoPage& p){
    auto& service=PhotoLibraryService::instance();if(!p.library||!p.library->count)return;
    const auto now=millis();
    if(!p.confirm&&!p.deletion){
        const auto touch=DisplayManager::instance().touchSnapshot();
        if(touch.sequence!=p.touchSequence&&p.picture){p.touchSequence=touch.sequence;lv_area_t area;lv_obj_get_coords(p.viewer,&area);
            const bool transformed=p.zoom.transform(touch.count,touch.span,int(touch.x)-area.x1,int(touch.y)-area.y1,int(touch.centerX)-area.x1,int(touch.centerY)-area.y1,lv_obj_get_width(p.viewer),lv_obj_get_height(p.viewer),uint32_t(p.shownLease->resource->pixels->width)*p.fitScale/256,uint32_t(p.shownLease->resource->pixels->height)*p.fitScale/256);
            if(transformed){
                if(lv_image_get_antialias(p.picture))lv_image_set_antialias(p.picture,false);
                lv_image_set_scale(p.picture,p.fitScale*p.zoom.scale/256);lv_obj_align(p.picture,LV_ALIGN_CENTER,p.zoom.panX,p.zoom.panY);p.lastGesture=now;p.lastTransform=now;
            }
            if(touch.count>=2||transformed){p.suppressTapUntil=now+300;controls(p,false);}
        }
        if(uint32_t(now-p.qualityClock)>1000){p.qualityClock=now;const auto render=UiPerformanceMonitor::instance().render();lv_timer_set_period(p.timer,render.p95Ms>60?66:render.p95Ms>30?33:20);}
        const bool due=p.view==PhotoView::Frame&&p.picture&&p.displayedUri==p.imageUri&&p.library->count>1&&int32_t(now-p.nextAt)>=0&&uint32_t(now-p.lastGesture)>750;
        if(!p.nextAt||!p.imageUri.length()||due){
            if(p.view==PhotoView::Frame&&!p.manualStep){const size_t previous=p.selected;p.selected=p.frameNext!=SIZE_MAX?p.frameNext:p.bag.next(p.library->count,[]{return esp_random();});if(p.picture&&p.selected==previous&&p.library->count>1)p.selected=p.bag.next(p.library->count,[]{return esp_random();});}
            p.frameNext=SIZE_MAX;p.manualStep=false;if(p.selected>=p.library->count)p.selected=0;
            p.imageUri=p.library->path(p.selected);p.cache.select(p.imageUri,service.settings().renderEdge);p.nextAt=UINT32_MAX;
            const auto next=neighbor(p,1);p.prefetchUri=p.library->path(next);status(p,"Bild wird im Hintergrund geladen …");
        }
    }
    p.cache.select(p.imageUri,service.settings().renderEdge);
    const bool settled=uint32_t(now-p.lastGesture)>250;
    if(settled&&p.picture&&!lv_image_get_antialias(p.picture))lv_image_set_antialias(p.picture,true);
    p.cache.poll(p.prefetchUri,settled,settled&&!p.confirm&&!p.deletion&&uint32_t(now-p.lastGesture)>600);
    if(p.confirm||p.deletion)return;
    if(settled||!p.picture||p.displayedUri!=p.imageUri)present(p,p.cache.visible());
    if(p.cache.message().length()){status(p,p.cache.message());if(p.cache.failed()&&!p.picture){service.frameFailed();p.wanted=PhotoView::Library;p.change=true;return;}}
    if(p.view==PhotoView::Frame&&p.picture&&p.frameNext==SIZE_MAX){p.frameNext=p.bag.next(p.library->count,[]{return esp_random();});if(p.library->count>1&&p.frameNext==p.selected)p.frameNext=p.bag.next(p.library->count,[]{return esp_random();});p.prefetchUri=p.library->path(p.frameNext);}
}
void poll(lv_timer_t* timer){auto& p=*static_cast<PhotoPage*>(lv_timer_get_user_data(timer));auto& service=PhotoLibraryService::instance();
    if(SecurityService::instance().tabletLocked()){cancelImage(p);p.cache.clear();fullscreenPower(p,false);return;}
    if(p.fullscreen)fullscreenPower(p,true);
    if(!service.loaded()){status(p,"Foto-Einstellungen werden geladen …");return;}
    if(!p.initialized){p.initialized=true;p.revision=service.revision();if(p.autoFrame&&service.startFrame(!p.manualFrame)){p.wanted=PhotoView::Frame;p.change=true;}p.autoFrame=false;}
    if(p.action){const int action=p.action;p.action=0;perform(p,action);}
    if(p.deletion&&p.deletion->ready){auto request=p.deletion;p.deletion.reset();status(p,request->message);if(request->success){p.cache.invalidate(request->uri);p.wanted=PhotoView::Library;p.change=true;}else controls(p,true);}
    if(p.change){view(p,p.wanted);bubble(p.root,p.keyboard);return;}
    if(p.revision!=service.revision()){p.revision=service.revision();status(p,service.status());if(p.heart)lv_obj_invalidate(p.heart);if(p.view==PhotoView::Frame&&p.picture)p.nextAt=p.pictureSince+uint32_t(service.settings().interval)*1000;if(p.view==PhotoView::Folders)renderFolders(p);else if(p.view==PhotoView::Library||p.view==PhotoView::Albums||p.view==PhotoView::Favorites||p.view==PhotoView::Frame)scanLibrary(p,p.view==PhotoView::Favorites);}
    if(p.scan&&p.scan->ready){p.library=p.scan->library;const String message=p.scan->message;p.scan.reset();p.bag.reset();
        p.frameNext=SIZE_MAX;if(p.view==PhotoView::Frame&&p.library)for(size_t i=0;i<p.library->count;++i)if(p.imageUri==p.library->path(i)){p.selected=i;break;}
        if(p.view==PhotoView::Library||p.view==PhotoView::Favorites)renderGrid(p);
        else if(p.view==PhotoView::Albums)renderAlbums(p);
        if(message.length())status(p,message);
        if((!p.library||!p.library->count)&&p.view==PhotoView::Frame){service.frameFailed();p.wanted=PhotoView::Library;p.change=true;}
    }
    if(p.directory&&p.directory->ready&&p.view==PhotoView::Folders&&p.nextAt!=UINT32_MAX){p.nextAt=UINT32_MAX;renderFolders(p);}
    if(p.view==PhotoView::Frame&&!service.frameActive()){controls(p,true);return;}
    if(!p.scan&&(p.view==PhotoView::Library||p.view==PhotoView::Albums||p.view==PhotoView::Favorites||p.view==PhotoView::Frame)&&uint32_t(millis()-p.mediaCheck)>=10000){
        p.mediaCheck=millis();if(!(p.mediaRevision==FileBrowserService::directoryRevision())){scanLibrary(p,p.view==PhotoView::Favorites);return;}}
    if(p.image&&p.image->ready){auto request=p.image;p.image.reset();
        if(request->image){auto* target=(p.view==PhotoView::Library||p.view==PhotoView::Favorites)?p.thumbs[p.imageIndex]:p.viewer;
            if(target){auto* image=lv_image_create(target);if(!showPixels(image,request->image,&p.thumbPins[p.imageIndex])){lv_obj_delete(image);status(p,"Gemeinsamer Bildspeicher belegt; letztes Bild bleibt sichtbar.");++p.failures;if(p.view==PhotoView::Frame&&p.failures>=8)service.frameFailed();p.nextAt=millis()+5000;return;}
                p.thumbComplete[p.imageIndex]=std::max(request->sourceWidth,request->sourceHeight)<=std::max(request->image->width,request->image->height);
                for(uint32_t i=0;i<lv_obj_get_child_count(target);++i){auto* child=lv_obj_get_child(target,i);if(lv_obj_check_type(child,&lv_label_class)&&!strcmp(lv_label_get_text(child),LV_SYMBOL_IMAGE))lv_obj_add_flag(child,LV_OBJ_FLAG_HIDDEN);}
                lv_obj_set_size(image,LV_PCT(100),100);lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);lv_obj_align(image,LV_ALIGN_TOP_MID,0,0);lv_obj_move_background(image);}
        }else {status(p,request->message);++p.failures;if(p.view==PhotoView::Frame&&p.failures>=8){service.frameFailed();return;}}
        p.nextAt=millis()+(request->image?uint32_t(service.settings().interval)*1000:1000);return;
    }
    if(p.image)return;
    if(p.view==PhotoView::Library||p.view==PhotoView::Favorites){if(p.thumbNext<p.shown.size()){p.imageIndex=p.thumbNext++;p.imageUri=p.library->path(p.shown[p.imageIndex]);
        if(p.imageUri.startsWith("nas")&&!ConfigService::instance().getBool("files_nas",false)){status(p,"NAS-Zugriff ist ausgeschaltet.");return;}p.image=FileBrowserService::image(p.imageUri,240,110,false,true);}return;}
    if(p.fullscreen)viewerPoll(p);
}
}
void PhotoApp::setLaunchArguments(const String& arguments){JsonDocument d;autoFrame_=!deserializeJson(d,arguments)&&bool(d["frame"]|false);manualFrame_=d["manual"]|false;
    if(page_&&autoFrame_){page_->autoFrame=true;page_->manualFrame=manualFrame_;page_->initialized=false;}}
bool PhotoApp::begin(){return ImageResourceService::instance().begin();}
void PhotoApp::open(lv_obj_t* parent){close();page_=new(std::nothrow) PhotoPage;if(!page_)return;auto& p=*page_;p.autoFrame=autoFrame_;p.manualFrame=manualFrame_;autoFrame_=false;
    p.host=parent;
    p.root=lv_obj_create(parent);lv_obj_set_size(p.root,LV_PCT(100),LV_PCT(100));UiTheme::instance().styleScreen(p.root);lv_obj_set_flex_flow(p.root,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(p.root,10,0);
    p.toolbar=SettingsWidgets::row(p.root,54);button(p.toolbar,"Fotos",1,p);button(p.toolbar,"Alben",8,p);button(p.toolbar,"Favoriten",2,p);button(p.toolbar,"Quellen",3,p);button(p.toolbar,"Zeitplan",4,p);button(p.toolbar,"Bilderrahmen",5,p);button(p.toolbar,LV_SYMBOL_REFRESH,9,p);button(p.toolbar,LV_SYMBOL_LEFT,6,p);button(p.toolbar,LV_SYMBOL_RIGHT,7,p);
    p.status=label(p.root,"Foto-Bibliothek");lv_obj_set_width(p.status,LV_PCT(100));lv_label_set_long_mode(p.status,LV_LABEL_LONG_DOT);
    p.body=lv_obj_create(p.root);UiTheme::instance().styleScreen(p.body);lv_obj_set_width(p.body,LV_PCT(100));lv_obj_set_flex_grow(p.body,1);lv_obj_set_flex_flow(p.body,LV_FLEX_FLOW_COLUMN);
    p.keyboard=lv_keyboard_create(p.root);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_FLOATING);lv_obj_align(p.keyboard,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_set_size(p.keyboard,LV_PCT(100),240);lv_keyboard_set_mode(p.keyboard,LV_KEYBOARD_MODE_NUMBER);lv_obj_add_flag(p.keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(p.root,[](lv_event_t* e){auto& p=page(e);for(auto* o=static_cast<lv_obj_t*>(lv_event_get_target(e));o;o=lv_obj_get_parent(o))if(o==p.keyboard||lv_obj_check_type(o,&lv_textarea_class))return;hideKeyboard(p);},LV_EVENT_CLICKED,&p);
    lv_obj_add_event_cb(p.keyboard,[](lv_event_t* e){if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL)hideKeyboard(page(e));},LV_EVENT_ALL,&p);
    p.timer=lv_timer_create(poll,200,&p);
}
void PhotoApp::close(){if(!page_)return;auto* p=page_;page_=nullptr;if(p->timer)lv_timer_delete(p->timer);if(p->scan)p->scan->canceled=true;if(p->directory)p->directory->canceled=true;cancelImage(*p);
    if(p->deletion)p->deletion->canceled=true;p->cache.clear();fullscreenPower(*p,false);PhotoLibraryService::instance().stopFrame();if(p->fullscreen)UiShell::instance().setAppFullscreen(p->host,false);hideKeyboard(*p);if(p->root)lv_obj_delete(p->root);p->shownLease.reset();for(auto& pin:p->thumbPins)pin.reset();ImageResourceService::instance().releaseOwner("native.photos");delete p;}
}

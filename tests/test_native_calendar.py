from pathlib import Path
import unittest
import test_system_performance

ROOT=Path(__file__).resolve().parents[1]

class NativeCalendarTests(unittest.TestCase):
    def test_keyboard_outside_tap_and_filter_queue_contract(self):
        app=(ROOT/'src/apps/calendar/CalendarApp.cpp').read_text(encoding='utf-8')
        self.assertIn('outsideKeyboardTap,LV_EVENT_PRESSED,&p',app)
        self.assertIn('LV_OBJ_FLAG_EVENT_BUBBLE',app)
        self.assertIn('lv_obj_has_class(target,&lv_textarea_class)',app)
        self.assertIn('target==p.keyboard',app)
        filters=app.split('void filters(lv_event_t *event)')[1].split('void poll(')[0]
        self.assertNotIn('if (p.controller.busy)',filters)
        self.assertLess(filters.index('c.person ='),filters.index('c.refresh(false)'))
        self.assertLess(filters.index('c.refresh(false)'),filters.index('c.action('))
        self.assertIn('p.filtersPending && !p.controller.busy',app)
        self.assertIn('reminderMinutes[reminderIndex]',app)
        self.assertIn('std::size(reminderMinutes)',app)
        self.assertIn('120, 1440, 2880, 10080',app)
        self.assertIn('lv_obj_add_flag(p.editor,LV_OBJ_FLAG_FLOATING)',app)
        self.assertIn('lv_obj_set_height(left,LV_SIZE_CONTENT)',app)
        self.assertIn('lv_obj_set_height(right,LV_SIZE_CONTENT)',app)

    def test_actual_calendar_keyboard_dismiss_policy(self):
        app=(ROOT/'src/apps/calendar/CalendarApp.cpp').read_text(encoding='utf-8')
        methods='void keyboardDismiss'+app.split('void keyboardDismiss')[1].split('bool ensureKeyboard')[0]
        code=r'''
#include <vector>
#include <cstdint>
#include <cassert>
struct lv_obj_t {lv_obj_t* parent=nullptr;std::vector<lv_obj_t*> children;unsigned flags=0;bool textarea=false;lv_obj_t* attached=nullptr;int pad=0;};
constexpr unsigned LV_OBJ_FLAG_HIDDEN=1,LV_OBJ_FLAG_EVENT_BUBBLE=2;
int lv_textarea_class=0;
struct NativeCalendarPage{lv_obj_t *keyboard=nullptr,*editorForm=nullptr,*editor=nullptr;};
struct lv_event_t{NativeCalendarPage* page;lv_obj_t* target;};
bool lv_obj_is_valid(lv_obj_t* p){return p!=nullptr;}
void lv_keyboard_set_textarea(lv_obj_t* k,lv_obj_t* t){k->attached=t;}
void lv_obj_add_flag(lv_obj_t* p,unsigned f){p->flags|=f;}
bool lv_obj_has_flag(lv_obj_t* p,unsigned f){return (p->flags&f)!=0;}
void lv_obj_set_style_pad_bottom(lv_obj_t* p,int v,int){p->pad=v;}
uint32_t lv_obj_get_child_count(lv_obj_t* p){return p->children.size();}
lv_obj_t* lv_obj_get_child(lv_obj_t* p,uint32_t i){return p->children.at(i);}
void* lv_event_get_user_data(lv_event_t* e){return e->page;}
lv_obj_t* lv_event_get_target_obj(lv_event_t* e){return e->target;}
lv_obj_t* lv_obj_get_parent(lv_obj_t* p){return p->parent;}
bool lv_obj_has_class(lv_obj_t* p,int*){return p->textarea;}
'''+methods+r'''
int main(){lv_obj_t root,form,key,input,keyChild,inputChild,choice;key.parent=&root;form.parent=&root;input.parent=&form;input.textarea=true;keyChild.parent=&key;inputChild.parent=&input;choice.parent=&form;form.pad=270;key.attached=&input;NativeCalendarPage p{&key,&form};
lv_event_t event{&p,&keyChild};outsideKeyboardTap(&event);assert(key.attached==&input);
event.target=&inputChild;outsideKeyboardTap(&event);assert(key.attached==&input);
event.target=&choice;outsideKeyboardTap(&event);assert(key.attached==nullptr);assert(key.flags&LV_OBJ_FLAG_HIDDEN);assert(form.pad==0);
outsideKeyboardTap(&event);p.keyboard=nullptr;outsideKeyboardTap(&event);
root.children={&form,&key};form.children={&input,&choice};bubbleKeyboardTaps(&root);assert(input.flags&LV_OBJ_FLAG_EVENT_BUBBLE);assert(choice.flags&LV_OBJ_FLAG_EVENT_BUBBLE);}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def native(self,case):
        code=''
        for name in ['CalendarDate.h','MonthGridModel.h','CalendarViewModel.h']:
            value=(ROOT/'src/apps/calendar'/name).read_text(encoding='utf-8')
            value=value.replace('#pragma once','').replace('#include "CalendarDate.h"','').replace('#include "MonthGridModel.h"','')
            code+=value+'\n'
        test_system_performance.SystemPerformanceTests().native(code+'\n#include <cassert>\n'+case)

    def test_native_calendar_hit_testing_and_navigation(self):
        self.native(r'''
int main(){using namespace hp::calendar;assert(monthCellAt(0,32,700,632)==0);
assert(monthCellAt(699,631,700,632)==41);assert(monthCellAt(700,40,700,632)==-1);
assert(monthCellAt(20,31,700,632)==-1);assert(monthCellAt(0,0,0,0)==-1);
auto d=moveSelection({2024,1,31},ViewMode::Month,1);assert(d.month==2&&d.day==29);
d=moveSelection({2024,1,1},ViewMode::Month,-1);assert(d.year==2023&&d.month==12);
d=moveSelection({2024,3,31},ViewMode::Week,1);assert(d.month==4&&d.day==7);
auto r=viewRange({2024,10,11},ViewMode::Month);assert(weekday(r.first.year,r.first.month,r.first.day)==0);
assert(dayNumber(r.last.year,r.last.month,r.last.day)-dayNumber(r.first.year,r.first.month,r.first.day)==42);
assert(r.to>r.from);assert(viewRange({2024,2,31},ViewMode::Day).from==0);}
''')

    def test_native_calendar_overlap_lanes(self):
        self.native(r'''
int main(){using namespace hp::calendar;std::array<TimeBlock,5> b{{{60,120,0},{90,150,1},{120,170,2},{300,330,3},{310,340,4}}};
layoutOverlaps(b,5);assert(b[0].lane==0&&b[1].lane==1&&b[2].lane==0);
assert(b[0].lanes==2&&b[2].lanes==2&&b[3].lane==0&&b[4].lane==1);
layoutOverlaps(b,0);}
''')

    def test_native_calendar_uses_existing_services_and_preserves_lua_data(self):
        app=(ROOT/'src/apps/calendar/CalendarApp.cpp').read_text(encoding='utf-8')
        controller=(ROOT/'src/apps/calendar/CalendarController.cpp').read_text(encoding='utf-8')
        header=(ROOT/'src/apps/calendar/CalendarApp.h').read_text(encoding='utf-8')
        self.assertIn('public IApp',header)
        self.assertIn('executeCalendarJob(request, result)',controller)
        self.assertIn('executeCalendarOperation(request.context.c_str()', (ROOT/'src/apps/calendar/CalendarApi.cpp').read_text(encoding='utf-8'))
        self.assertIn('LuaJobService::instance().submit',controller)
        self.assertIn('cancelOwner(owner())',controller)
        self.assertNotIn('xTaskCreate',app+controller)
        self.assertIn('request.cancelled',controller)
        self.assertIn('cursorStart_ = 0;',controller)
        self.assertIn('calendar_query',controller)
        self.assertIn('!p.controller.busy',app)
        self.assertIn('detachPageCallbacks(page->root',app)
        self.assertNotIn('http.begin',app)
        self.assertNotIn('CalendarEventStore::write',app)
        self.assertIn('LV_EVENT_DRAW_MAIN_END',app)
        self.assertIn('monthCellAt',app)
        self.assertIn('requestAppSettings("calendar")',app)
        self.assertIn('CalendarRead',header);self.assertIn('CalendarWrite',header)
        self.assertTrue((ROOT/'src/apps/lua/CalendarLuaBridge.cpp').is_file())

    def test_expanded_caldav_events_and_invalid_data(self):
        code=(ROOT/'src/apps/calendar/CalendarDate.h').read_text(encoding='utf-8').replace('#pragma once','')
        code+=(ROOT/'src/apps/calendar/CalendarImport.h').read_text(encoding='utf-8').replace('#pragma once','').replace('#include "CalendarDate.h"','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){using namespace hp::calendar;std::vector<ImportedEvent> out;std::string error;
std::string source="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:test\r\nDTSTART:20261011T100000Z\r\nDTEND:20261011T110000Z\r\nSUMMARY:Test\\, Termin\r\nDESCRIPTION:Zeile 1\\n\r\n Zeile 2\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
assert(parseExpandedCalendar(source,out,error)&&out.size()==1&&out[0].end-out[0].start==3600&&out[0].title=="Test, Termin"&&out[0].notes=="Zeile 1\nZeile 2");
assert(parseExpandedCalendar(source,out,error)&&out.size()==1);
out.clear();std::string duration="BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:duration\nDTSTART:20261011T100000Z\nDURATION:PT1H30M\nEND:VEVENT\nEND:VCALENDAR\n";
assert(parseExpandedCalendar(duration,out,error)&&out[0].end-out[0].start==5400);
for(const auto* invalid:{"PT999999H","PT1H1H","PT1M1H","P1W1D","PT1HT"}){out.clear();auto bad=duration;bad.replace(bad.find("PT1H30M"),7,invalid);assert(!parseExpandedCalendar(bad,out,error));}
out.clear();{auto bad=duration;bad.insert(bad.find("END:VEVENT"),"DTEND:20261011T120000Z\n");assert(!parseExpandedCalendar(bad,out,error));}
out.clear();std::string day="BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:day\nDTSTART;VALUE=DATE:20261011\nEND:VEVENT\nEND:VCALENDAR\n";
assert(parseExpandedCalendar(day,out,error)&&out.size()==1&&out[0].allDay&&localDate(out[0].end).day==12);
out.clear();std::string bad=source;auto pos=bad.find("DTSTART:");bad.replace(pos,8,"DTSTART;TZID=Europe/Berlin:");assert(!parseExpandedCalendar(bad,out,error));
out.clear();bad=source;pos=bad.find("END:VEVENT");bad.insert(pos,"RRULE:FREQ=DAILY\r\n");assert(!parseExpandedCalendar(bad,out,error));
out.clear();assert(!parseExpandedCalendar(source,out,error,0));assert(!parseExpandedCalendar("BEGIN:VCALENDAR\n",out,error));}
''')

    def test_actual_local_reminder_scan_deduplication_and_private_titles(self):
        source=(ROOT/'src/apps/calendar/CalendarReminderService.cpp').read_text(encoding='utf-8')
        method=source.split('void CalendarReminderService::scan()',1)[1].split('void CalendarReminderService::dismiss()',1)[0]
        date=(ROOT/'src/apps/calendar/CalendarDate.h').read_text(encoding='utf-8').replace('#pragma once','')
        code=r'''
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <functional>
using String=std::string;
uint32_t ticks=0;uint32_t millis(){return ticks;}
size_t strlcpy(char* out,const char* in,size_t n){auto size=strlen(in);if(n){auto count=std::min(n-1,size);memcpy(out,in,count);out[count]=0;}return size;}
'''+date+r'''
namespace hp {
enum EventFlags {EventDeleted=16,EventExternal=8};
enum class CalendarKind:uint8_t {Personal,Family};enum class PinKind {Settings};
struct Event {uint64_t localId=1;uint32_t calendarId=1;uint16_t flags=0;int64_t startUtc=1791712800,endUtc=1791716400;char title[96]="Privat";};
struct StoredCalendarEvent {Event event;calendar::Rules rules;};struct PersonRecord {};
struct CalendarRecord {uint32_t id=1;uint8_t kind=uint8_t(CalendarKind::Family);};
struct CalendarService {bool healthy=true;uint32_t revision=1;CalendarRecord cal;static CalendarService& instance(){static CalendarService s;return s;}bool storageHealthy(){return healthy;}uint32_t syncRevision(){return revision;}void copyMetadata(std::vector<PersonRecord>&,std::vector<CalendarRecord>& out){out={cal};}};
struct CalendarEventStore {inline static StoredCalendarEvent event;inline static bool fail=false;inline static unsigned visits=0,rows=1;static bool visit(const std::function<bool(const StoredCalendarEvent&)>& emit,String& error){++visits;if(fail){error="integrity";return false;}for(unsigned i=0;i<rows;++i){auto value=event;value.event.localId+=i;if(!emit(value))return true;}return true;}};
struct TimeService {int64_t time=1791712500;static TimeService& instance(){static TimeService s;return s;}int64_t nowUtc(){return time;}};
struct NetworkWorker {static NetworkWorker& instance(){static NetworkWorker w;return w;}unsigned priorityDepth(){return 0;}void maintain(){}};
struct SecurityService {bool locked=false;static SecurityService& instance(){static SecurityService s;return s;}bool tabletLocked(){return locked;}bool settingsAllowed(){return !locked;}bool enabled(PinKind){return true;}};
struct CalendarReminderService {struct Reminder{uint64_t id=0;int64_t occurrence=0;char title[96]{};};std::mutex mutex_;Reminder queue_[8]{},seen_[32]{};unsigned count_=0;uint32_t cachedRevision_=0;int64_t lastScanUtc_=0,nextScanUtc_=0;bool cachedMasked_=false;std::atomic<bool> dirty_{false};void scan();};
void CalendarReminderService::scan()'''+method+r'''
}
int main(){using namespace hp;CalendarReminderService service;auto& event=CalendarEventStore::event;
event.rules.reminderMinutes=5;service.scan();assert(service.count_==1&&service.dirty_);service.scan();assert(service.count_==1&&CalendarEventStore::visits==1);
service.seen_[0]=service.queue_[0];service.count_=0;service.scan();assert(service.count_==0);
service.seen_[0]={};CalendarEventStore::fail=true;++CalendarService::instance().revision;service.scan();assert(service.count_==0);CalendarEventStore::fail=false;
event.rules.reminderMinutes=-1;service.scan();assert(service.count_==0);event.rules.reminderMinutes=5;
CalendarService::instance().cal.kind=uint8_t(CalendarKind::Personal);SecurityService::instance().locked=true;service.scan();assert(service.count_==1&&!strcmp(service.queue_[0].title,"Privater Termin"));
service.count_=0;TimeService::instance().time=1791712700;service.scan();assert(service.count_==0);
// A future due time wakes the scan exactly at its boundary; cache hits do no I/O.
event.event.startUtc=1791713600;event.event.endUtc=1791717200;++CalendarService::instance().revision;
TimeService::instance().time=1791713000;service.scan();const auto reads=CalendarEventStore::visits;assert(service.count_==0);
TimeService::instance().time=1791713100;service.scan();assert(CalendarEventStore::visits==reads&&service.count_==0);
TimeService::instance().time=1791713300;service.scan();assert(CalendarEventStore::visits==reads+1&&service.count_==1);
// More due reminders than UI slots must not be hidden by the five-minute cache.
service.count_=0;CalendarEventStore::rows=10;++CalendarService::instance().revision;service.scan();assert(service.count_==8&&service.nextScanUtc_==1791713300);
service.seen_[0]=service.queue_[0];for(unsigned i=1;i<8;++i)service.queue_[i-1]=service.queue_[i];--service.count_;
TimeService::instance().time=1791713315;service.scan();assert(service.count_==8&&service.queue_[7].id==9);
}
'''
        test_system_performance.SystemPerformanceTests().native(code)

    def test_reminders_use_existing_worker_and_ui_lock_only(self):
        service=(ROOT/'src/apps/calendar/CalendarReminderService.cpp').read_text(encoding='utf-8')
        self.assertIn('submitLocal',service)
        self.assertNotIn('submitTransfer',service)
        self.assertNotIn('xTaskCreate',service)
        self.assertIn('try_to_lock',service)
        shell=(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        self.assertLess(shell.index('DisplayUiGuard uiGuard'),shell.index('CalendarReminderService::instance().loopUi(callActive)'))

if __name__=='__main__':unittest.main()

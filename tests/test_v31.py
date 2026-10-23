import os
import shutil
import subprocess
import tempfile
import re
import unittest
import test_system_performance
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LUA=ROOT/'managed_components/espressif__lua/lua'

class V31Tests(unittest.TestCase):
    def test_utf8_labels_survive_shared_ui(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='title',type='label',text='Übersicht · Grüße'}}})
for i=1,100 do if pump() then break end end
local found=false;for _,o in ipairs(objects)do if o.text=='Übersicht · Grüße' then found=true end end
assert(found)
''')

    def test_inputs_and_dropdowns_use_shared_style(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='name',type='input',text='Max'},
 {id='account',type='dropdown',options='Privat\nArbeit'}}})
for i=1,100 do if pump() then break end end
local styled=0
for _,o in ipairs(objects) do if o.inputStyled then styled=styled+1 end end
assert(styled==2)
''')

    def test_password_input_is_masked_in_ui_pump(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='password',type='input',password=true,text=''}}})
assert(writes==0)
for i=1,100 do if pump() then break end end
assert(writes==1 and masked)
assert(not pcall(ui.patch,'password',{password='yes'}))
''',r'''
writes=0
function hp_set_password_mode(o,value) writes=writes+1;masked=value end
''')

    def test_chart_cursor_is_owner_ui_queued_and_bounded(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='rain',type='chart'}}})
local labels={};for i=1,24 do labels[i]=string.format('%02d:00',i-1) end
ui.chartCursor('rain',labels,' %');labels[1]='wrong';assert(writes==0)
for i=1,100 do if pump() then break end end
assert(writes==1 and cursorLabels[1]=='00:00' and cursorLabels[24]=='23:00' and suffix==' %')
assert(not pcall(ui.chartCursor,'rain',{},' %'))
for i=25,33 do labels[i]='x' end
assert(not pcall(ui.chartCursor,'rain',labels,' %'))
foreground=false;assert(not pcall(ui.chartCursor,'rain',{'00:00'},' %'))
''',r'''
writes=0
function hp_chart_set_cursor_labels(o,labels,unit) writes=writes+1;cursorLabels=labels;suffix=unit end
''')

    def test_chart_fixed_range_is_validated_and_queued(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='rain',type='chart',min=0,max=100}}})
assert(writes==0)
for i=1,100 do if pump() then break end end
assert(writes==1 and low==0 and high==100)
assert(not pcall(ui.patch,'rain',{min=10}))
assert(not pcall(ui.patch,'rain',{min=100,max=0}))
assert(not pcall(ui.patch,'rain',{min=0.5,max=100}))
ui.patch('rain',{min=0,max=50});assert(writes==1)
for i=1,100 do if pump() then break end end
assert(writes==2 and high==50)
''',r'''
writes=0;hp_chart_create=hp_ui_container
function hp_chart_set_range(o,lo,hi) writes=writes+1;low=lo;high=hi end
''')

    def test_icon_button_is_queued_and_reuses_control(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='rain',type='icon_button',text='drop',width=48,height=34,on_click='openRain'}}})
assert(not control)
for i=1,100 do if pump() then break end end
assert(control.text=='drop' and control.callback);local count=created
ui.patch('rain',{primary=true,text='sun'})
for i=1,100 do if pump() then break end end
assert(control.text=='sun' and created==count)
foreground=false;assert(not pcall(ui.patch,'rain',{text='drop'}))
''',r'''
function hp_ui_icon_button(parent,name) control=hp_button(parent,name);return control end
function hp_ui_icon_button_set(o,name) o.text=name end
''')

    def test_semantic_icon_updates_in_shared_ui_pump(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='sky',type='icon',text='rain',width=48,height=48}}})
assert(writes==0)
for i=1,100 do if pump() then break end end
assert(icon.text=='rain');local count=created
ui.patch('sky',{text='snow'})
assert(icon.text=='rain')
for i=1,100 do if pump() then break end end
assert(icon.text=='snow' and created==count)
foreground=false;assert(not pcall(ui.patch,'sky',{text='clear'}))
''',r'''
writes=0
function hp_ui_icon(parent,name) icon=hp_ui_container(parent);icon.text=name;return icon end
function hp_ui_icon_set(o,name) writes=writes+1;o.text=name end
''')

    def test_weather_reuses_shared_effect_with_foreground_queue(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='weather',type='weather',width=96,height=64,weather_type='rain',animated=true,intensity=30}}})
assert(effects==0,'creation must wait for UI pump')
for i=1,100 do if pump() then break end end
assert(effects==1 and effect.type=='rain' and effect.enabled)
ui.patch('weather',{weather_type='snow',animated=false})
for i=1,100 do if pump() then break end end
assert(effects==1 and effect.type=='snow' and not effect.enabled)
assert(not pcall(ui.patch,'weather',{weather_type='invalid'}))
local many={};for i=1,17 do many[i]={id='w'..i,type='weather'} end
assert(not pcall(ui.mount,{id='root',children=many}))
foreground=false;assert(not pcall(ui.patch,'weather',{animated=true}))
''',r'''
effects=0
function hp_ui_weather_effect(parent,config) effects=effects+1;effect=config;return hp_ui_container(parent) end
function hp_ui_weather_effect_set(o,config) effect=config end
''')

    def lua(self,code):
        # Compile the pinned interpreter once; execute actual firmware-owned Lua.
        folder=ROOT/'.pio/host-v31';folder.mkdir(parents=True,exist_ok=True)
        exe=folder/'lua.exe'
        if not exe.exists():
            sources=[str(p) for p in LUA.glob('*.c') if p.name not in ('luac.c','onelua.c')]
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:command=[cc,'-O1','-DLUA_32BITS=1',*sources,'-o',str(exe)]
            else:
                setup=Path('C:/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
                self.assertTrue(setup.exists(),'Host compiler required')
                quoted=' '.join('"'+p+'"' for p in sources)
                command=f'cmd /d /c "call "{setup}" >nul && cl /nologo /O1 /DLUA_32BITS=1 {quoted} /Fe:lua.exe"'
            result=subprocess.run(command,cwd=folder,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout[-3000:]+result.stderr)
        result=subprocess.run([str(exe),'-'],input=code,capture_output=True,text=True,encoding='utf-8',timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def declarative(self,case,prefix=''):
        source=''.join(re.findall(r'R"lua\((.*?)\)lua"',(ROOT/'src/apps/lua/LuaDeclarativeUi.h').read_text(encoding='utf-8'),re.S))
        self.lua((ROOT/'tests/native/v31_ui.lua').read_text(encoding='utf-8')+'\n'+prefix+'\nlocal pump=assert(load([====['+source+']====]))()\n'+case)

    def test_dropdown_uses_shared_widgets_and_preserves_selection(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='country',type='dropdown',options='Systemstandard\nDeutschland\nFrankreich',value=2}}})
for i=1,100 do if pump() then break end end
assert(ui.read('country')==2)
ui.patch('country',{options='Standard\nDeutschland\nFrankreich\nItalien'})
for i=1,100 do if pump() then break end end
assert(ui.read('country')==2)
ui.patch('country',{value=0})
for i=1,100 do if pump() then break end end
assert(ui.read('country')==0)
assert(not pcall(ui.patch,'country',{options=string.rep('x',4097)}))
foreground=false;assert(not pcall(ui.read,'country'))
''')

    def test_percentage_dimensions_are_preserved_on_both_axes(self):
        self.declarative(r'''
ui.mount({id='root',type='page',children={{id='panel',type='panel',width_percent=30,height_percent=100}}})
for i=1,100 do if pump() then break end end
assert(objects[2].w==hp_ui_percent(30) and objects[2].h==hp_ui_percent(100))
ui.patch('panel',{width_percent=45})
for i=1,100 do if pump() then break end end
assert(objects[2].w==hp_ui_percent(45) and objects[2].h==hp_ui_percent(100))
assert(not pcall(ui.patch,'panel',{height_percent=101}))
assert(not pcall(ui.patch,'panel',{width_percent=-1}))
''',prefix='function hp_set_size(o,w,h) o.w=w;o.h=h end')

    def test_input_types_and_one_lazy_shared_keyboard(self):
        self.declarative(r'''
local function drain() for i=1,100 do if pump() then return end end;error('queue stuck') end
ui.mount({id='root',type='page',scrollable=true,children={
 {id='temperature',type='input',input_type='decimal',text='200',on_click='custom'},
 {id='name',type='input',text='Berlin'}}});drain()
assert(objects[2].input_type=='decimal' and not objects[3].input_type)
assert(objects[1].scrollable)
_G[objects[2].callback]();assert(keyboards==1 and target==objects[2] and customCalls==1)
_G[objects[3].callback]();assert(keyboards==1 and target==objects[3])
ui.patch('temperature',{input_type='signed_decimal'});drain()
assert(objects[2].input_type=='signed_decimal')
assert(not pcall(ui.patch,'temperature',{input_type='bad'}))
ui.remove('temperature');drain();assert(keyboardHidden)
foreground=false;assert(not pcall(ui.patch,'temperature',{input_type='integer'}))
''',prefix=r'''
keyboards=0;customCalls=0
function hp_ui_input_type(o,kind) o.input_type=kind end
function hp_ui_keyboard() keyboards=keyboards+1;return {} end
function hp_ui_keyboard_bind(k,o) target=o end
function hp_ui_keyboard_hide(k) keyboardHidden=true;target=nil end
function hp_set_scrollable(o,value) o.scrollable=value end
function custom() customCalls=customCalls+1 end
''')

    def test_four_chart_series_coalesce_256_signed_points(self):
        self.declarative(r'''
local function drain() for i=1,100 do if pump() then return end end;error('chart queue stuck') end
ui.mount({id='root',type='page',children={{id='chart',type='chart'}}});drain()
local values={};for i=1,256 do values[i]=i%2==0 and i or -i end
for i=1,4 do ui.chartSeries('chart',i,values,{name='S'..i,color=i}) end
values[1]=999;ui.chartSeries('chart',1,values,{name='S1',color=1});drain()
assert(seriesCreated==4 and seriesWrites==4 and chartPoints==256)
assert(seriesValues[1][1]==999 and seriesValues[2][1]==-1 and seriesValues[4][256]==256)
local count=created;ui.patch('chart',{hidden=true});drain();ui.patch('chart',{hidden=false});drain()
assert(created==count and seriesCreated==4)
assert(not pcall(ui.chartSeries,'chart',5,values))
values[257]=0;assert(not pcall(ui.chartSeries,'chart',1,values))
''',r'''
seriesCreated=0;seriesWrites=0;seriesValues={}
hp_chart_create=hp_ui_container
function hp_chart_resize(o,n) chartPoints=n end
function hp_chart_add_series(o,name,color) seriesCreated=seriesCreated+1;return seriesCreated end
function hp_chart_set_series(o,series,values) seriesWrites=seriesWrites+1;seriesValues[series]=values end
''')

    def test_calendar_detail_compact_and_legacy_sizes(self):
        code=(ROOT/'src/apps/calendar/EventDetails.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){using hp::EventDetailsSnapshot;
assert(EventDetailsSnapshot::compatibleSize(EventDetailsSnapshot::legacyBytes()));
assert(EventDetailsSnapshot::compatibleSize(EventDetailsSnapshot::storedBytes(1)));
assert(EventDetailsSnapshot::compatibleSize(EventDetailsSnapshot::storedBytes(128)));
assert(!EventDetailsSnapshot::compatibleSize(EventDetailsSnapshot::storedBytes(128)+1));
assert(EventDetailsSnapshot::storedBytes(1)<EventDetailsSnapshot::legacyBytes());
auto* s=new EventDetailsSnapshot;int events=42;s->count=1;s->items[0].localId=1;
s->eventsChecksum=hp::detailChecksum(&events,sizeof(events));
s->detailsChecksum=hp::detailChecksum(s->items,sizeof(hp::EventDetails));
assert(s->valid(&events,sizeof(events)));s->items[0].notes[0]='x';assert(!s->valid(&events,sizeof(events)));delete s;
}
''')

    def test_keyed_200_widgets_reuse_mutate_remove_and_stale_events(self):
        self.declarative(r'''
local function drain() for i=1,1000 do if pump() then return end end;error('queue never drained') end
ui.mount({id='root',type='page',children={{id='list',type='column'}}});drain()
local specs={};for i=1,200 do specs[i]={id='n'..i,type='button',text='item '..i,on_click='clicked'} end
ui.updateChildren('list',specs);assert(not ui.ready());drain();assert(created==202 and alive==202)
local count=created;for _,s in ipairs(specs) do s.text=s.text..' changed' end
local saved=objects[50];ui.updateChildren('list',specs);drain();assert(created==count and objects[50]==saved)
local writes=textWrites;ui.updateChildren('list',specs);drain();assert(textWrites==writes)
local cb=objects[3].callback;calls=0;function clicked() calls=calls+1 end
_G[cb]();assert(calls==1);ui.remove('n1');_G[cb]();assert(calls==1);drain();assert(_G[cb]==nil)
for i=2,200 do ui.remove('n'..i) end;drain();assert(alive==2)
for cycle=1,5 do ui.updateChildren('list',specs);drain();ui.updateChildren('list',{});drain();assert(alive==2) end
ui.insert('list',1,{id='first',type='label',text='hello'});drain();assert(alive==3)
local before=created;assert(not pcall(ui.insert,'list',1,{id='first',type='label'}));assert(created==before)
ui.patch('first',{text='world'});drain();assert(textWrites>writes)
assert(not pcall(ui.updateChildren,'list',{{id='x'},{id='x'}}));assert(ui.ready())
foreground=false;assert(not pcall(ui.remove,'first'));assert(not pcall(pump));foreground=true
ui.remove('first');drain();assert(alive==2 and maxStep<=8)
''')

    def test_virtual_window_10000_entries_has_constant_24_rows(self):
        code=(ROOT/'src/core/VirtualListWindow.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){using hp::VirtualListWindow;for(unsigned i=0;i<10000;++i){auto w=VirtualListWindow::at(i*64,800,10000);
assert(w.count<=24&&w.first+w.count<=10000&&w.first<=i);assert(w.cached(w.first,96));assert(!w.cached(w.first+1,96));}
assert(VirtualListWindow::at(0,800,0).count==0);assert(VirtualListWindow::at(10000000,800,10000).count==0);
auto end=VirtualListWindow::at(9999*64,400,10000);assert(end.first==9997&&end.count==3);
}
''')
        source=(ROOT/'src/apps/lua/LuaCollectionWidgets.cpp').read_text(encoding='utf-8')
        self.assertIn('Row rows[VirtualListWindow::kRows]',source)
        self.assertIn('ListItem items[VirtualListWindow::kPage]',source)
        self.assertIn('MALLOC_CAP_SPIRAM',source)
        self.assertNotIn('NetworkWorker',source)

    def test_virtual_api_bounded_page_callbacks_and_reuse(self):
        self.declarative(r'''
function hp_ui_virtual_list(parent) local o=hp_ui_container(parent);o.virtual=true;o.rows=0;return o end
function hp_ui_virtual_items(o,offset,items,total) o.offset=offset;o.items=items;o.total=total end
function hp_ui_virtual_callbacks(o,name,scroll) o[scroll and 'scroll' or 'select']=name end
function hp_ui_collection_status() end
function hp_ui_collection_pending(o) return o.rows<24 end
function hp_ui_collection_step(o,budget) local used=0;while o.rows<24 and used+2<=budget do o.rows=o.rows+1;used=used+2 end;return used,o.rows==24 end
-- Constructors are captured at module load; use the existing constructor stub
-- and attach native-list state to the returned object instead.
local saved=hp_ui_container
hp_ui_collection_pending=function(o) return (o.rows or 0)<24 end
hp_ui_collection_step=function(o,budget) o.rows=o.rows or 0;local used=0;while o.rows<24 and used+2<=budget do o.rows=o.rows+1;used=used+2 end;return used,o.rows==24 end
function scroll(id,offset,limit) assert(id=='v' and offset==100 and limit==96);scrolls=(scrolls or 0)+1 end
function select(id,item) assert(id=='v' and item=='stable');selected=true end
ui.mount({id='page',type='page',children={{id='v',type='virtual_list',on_scroll='scroll',on_select='select'}}})
local items={};for i=1,96 do items[i]={id='id'..i,text='entry '..i} end
ui.setItems('v',0,items,10000);for i=1,30 do if pump() then break end end;assert(ui.ready())
local object=objects[2];assert(object.total==10000 and object.rows==24);local before=created
_G[object.scroll]('',100,96);_G[object.select]('stable',0,10000);assert(scrolls==1 and selected)
for page=1,100 do ui.setItems('v',page*96,items,10000);assert(not ui.ready());for i=1,5 do if pump() then break end end end
assert(created==before and object.rows==24)
assert(not pcall(ui.setItems,'v',0,items,1));assert(not pcall(ui.setItems,'v',-1,{},10000))
items[97]={id='bad'};assert(not pcall(ui.setItems,'v',0,items,10000))
''')

    def test_prioritized_build_and_unchanged_patch(self):
        self.declarative(r'''
local specs={};for i=1,40 do specs[i]={id='slow'..i,type='label',text='data',priority='image'} end
specs[41]={id='action',type='button',text='Ready',priority='critical'}
ui.mount({id='page',type='page',children=specs});pump();assert(objects[2].text=='Ready' and not ui.ready())
for i=1,30 do if pump() then break end end;assert(ui.ready() and created==42)
local writes=textWrites;for i=1,100 do ui.patch('action',{text='Ready'}) end;assert(ui.ready());pump();assert(textWrites==writes)
''')

    def test_provider_revisions_skip_decode_and_isolate_profiles(self):
        layer=(ROOT/'src/apps/lua/LuaDataSubscriptions.h').read_text(encoding='utf-8').split('R"lua(',1)[1].split(')lua"',1)[0]
        self.lua(r'''
function hp_ui_assert_foreground() end
function hp_millis() return 100 end
function hp_json_encode() return '{}' end
function hp_json_decode() return {} end
function hp_printer_watch() end
local revision={a=1,b=1};local snapshots={a=0,b=0};local calls={a=0,b=0}
function hp_printer_revision(id) return revision[id],90,false,true end
function hp_printer_snapshot_summary(id) snapshots[id]=snapshots[id]+1;return {revision=revision[id],timestamp=90,online=true} end
function changed(id,state) calls[id]=calls[id]+1;assert(state.value.online) end
local controller=(function()
'''+layer+r'''
end)()
data.subscribe('a',{source='printer',profile='a',on_change='changed'})
data.subscribe('b',{source='printer',profile='b',on_change='changed'})
controller.step();assert(snapshots.a==1 and snapshots.b==1)
for i=1,100 do controller.event('printer.status');controller.step() end
assert(snapshots.a==1 and snapshots.b==1 and calls.a==1 and calls.b==1)
revision.a=2;controller.event('printer.status');controller.step();assert(snapshots.a==2 and snapshots.b==1 and calls.a==2)
data.refresh('b');controller.step();assert(snapshots.b==2 and calls.b==1)
assert(data.snapshot('a').age_ms==10)
''')

    def test_subscription_interval_coalesces_latest_but_not_connectivity(self):
        layer=(ROOT/'src/apps/lua/LuaDataSubscriptions.h').read_text(encoding='utf-8').split('R"lua(',1)[1].split(')lua"',1)[0]
        self.lua(r'''
function hp_ui_assert_foreground() end
local now,revision,online,calls,seen=1000,1,true,0,0
function hp_millis() return now end
function hp_json_encode() return '{}' end
function hp_json_decode() return {} end
function hp_printer_watch() end
function hp_printer_revision() return revision,now,false,online end
function hp_printer_snapshot_summary() return {revision=revision,online=online} end
function changed(id,state) calls=calls+1;seen=state.value.revision end
local controller=(function()
'''+layer+r'''
end)()
data.subscribe('a',{source='printer',profile='a',on_change='changed',min_interval_ms=500})
controller.step();assert(calls==1)
for i=2,4 do now=now+100;revision=i;controller.event('printer.status');controller.step() end
assert(calls==1)
now=1500;controller.step();assert(calls==2 and seen==4)
now=1501;online=false;controller.event('printer.status');controller.step();assert(calls==3)
for i=1,20 do controller.step() end;assert(calls==3)
assert(not pcall(data.subscribe,'bad',{source='printer',profile='a',on_change='changed',min_interval_ms=1}))
data.unsubscribe('a');now=2000;controller.step();assert(calls==3)
''')

    def test_chart_native_ring_extrema_and_memory_bound(self):
        code=(ROOT/'src/core/ChartHistoryWindow.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){hp::ChartHistoryWindow h;for(int i=0;i<10000;++i)assert(h.append(i,i%2?-1000:1000));assert(h.count==1024);
hp::ChartHistoryWindow::Sample out[256];auto n=h.range(0,10000,out,256);assert(n==256);for(unsigned i=1;i<n;++i)assert(out[i].time>out[i-1].time);
bool negative=false,positive=false;for(unsigned i=0;i<n;++i){negative|=out[i].value<0;positive|=out[i].value>0;}assert(negative&&positive);
assert(!h.append(1,0));assert(h.append(9999,-2));assert(h.count==1024);assert(h.range(9999,9999,out,256)==1&&out[0].value==-2);
assert(!h.range(5,1,out,256));assert(sizeof(h)<17000);}
''')
        self.assertIn('if(!changed)return 0;', (ROOT/'src/apps/lua/LuaChartBridge.cpp').read_text(encoding='utf-8'))
        self.assertIn('Returns a series handle, not an lv_obj_t.',(ROOT/'src/apps/lua/LuaUiBridge.cpp').read_text(encoding='utf-8'))

    def test_directory_lru_revisions_ttl_and_5000_sorted_entries(self):
        code=(ROOT/'src/core/DirectoryPageCache.h').read_text(encoding='utf-8').replace('#pragma once','')
        code+=(ROOT/'src/core/SortedPage.h').read_text(encoding='utf-8').replace('#pragma once','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
#include <vector>
#include <string>
int main(){hp::DirectoryPageCache cache;hp::DirectoryPageCache::Revision r{1,2,3};auto* p=cache.replace("nas|name|0",r,100);p->valid=true;
assert(cache.get("nas|name|0",r,101,2000));assert(!cache.get("nas|date|0",r,101,2000));assert(!cache.get("nas|name|0",{1,3,3},101,2000));assert(!cache.get("nas|name|0",r,2100,2000));
for(int i=0;i<10;++i){auto key=std::to_string(i);auto* q=cache.replace(key.c_str(),r,100);q->valid=true;}assert(!cache.get("0",r,101,2000));assert(cache.get("9",r,101,2000));assert(sizeof(cache)<64000);
int after=-1;unsigned count=0;auto less=[](int a,int b){return a<b;};
while(count<5000){hp::SortedPage<int,decltype(less)> page(193,less);for(int i=4999;i>=0;--i)if(i>after)page.add(i);auto values=page.sorted();
assert(page.retained()<=193);const unsigned size=std::min<size_t>(96,values.size());assert(size);for(unsigned i=0;i<size;++i)assert(values[i]==int(count+i));count+=size;after=values[size-1];}assert(count==5000);}
''')

    def test_partition_boundaries_and_edit_only_remove_buttons(self):
        import csv
        rows=list(csv.reader(line for line in (ROOT/'partitions_32mb.csv').read_text().splitlines() if line.strip() and not line.startswith('#')))
        areas={r[0].strip():(int(r[3],0),int(r[4],0)) for r in rows}
        self.assertEqual(areas['ota_0'],(0x20000,9*1024*1024));self.assertEqual(areas['ota_1'],(0x920000,9*1024*1024))
        self.assertEqual(areas['appfs'],(0x1220000,9*1024*1024));self.assertEqual(areas['systemdata'],(0x1B20000,0x4D0000))
        ordered=sorted(areas.values());self.assertTrue(all(a+s<=b for (a,s),(b,_) in zip(ordered,ordered[1:])))
        self.assertEqual(ordered[-1][0]+ordered[-1][1],32*1024*1024)
        shell=(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        self.assertIn('setHiddenIfChanged(remove,!desktopEditing_)',shell);self.assertIn('LV_EVENT_LONG_PRESSED',shell);self.assertIn('setDesktopEditing(false)',shell)


    def test_month_model_and_reusable_lua_grid(self):
        code=(ROOT/'src/apps/calendar/CalendarDate.h').read_text(encoding='utf-8').replace('#pragma once','')
        code+=(ROOT/'src/apps/calendar/MonthGridModel.h').read_text(encoding='utf-8').replace('#pragma once','').replace('#include "CalendarDate.h"','')
        test_system_performance.SystemPerformanceTests().native(code+r'''
#include <cassert>
int main(){hp::calendar::MonthGridModel m;assert(m.set(2024,2,false));assert(!strcmp(m.cells[0].iso,"2024-01-29"));assert(!strcmp(m.cells[31].iso,"2024-02-29"));assert(m.cells.size()==42);assert(m.set(2024,2,true));assert(!strcmp(m.cells[0].iso,"2024-01-28"));assert(m.cells[0].weekend);assert(!m.set(2024,13,false));assert(!m.validIso("2023-02-29"));assert(m.validIso("2024-02-29"));}
''')
        self.declarative(r'''
function hp_ui_month_callback(o,name) o.select=name end
function hp_ui_month_set(o,data) o.month=data end
function hp_ui_collection_pending(o) return (o.cells or 0)<42 end
function hp_ui_collection_step(o,budget) o.cells=o.cells or 0;local used=0;while o.cells<42 and used+2<=budget do o.cells=o.cells+1;used=used+2 end;return used end
function day(id,date) assert(id=='month' and date=='2024-02-29');chosen=true end
ui.mount({id='page',type='page',children={{id='month',type='month_grid',on_select='day'}}})
ui.setMonth('month',{year=2024,month=2,days={{date='2024-02-29',count=3,colors={0xff0000,0x00ff00}}}})
for i=1,50 do if pump() then break end end;assert(ui.ready());local o=objects[2];assert(o.cells==42)
local before=created;for month=1,12 do ui.setMonth('month',{year=2025,month=month});pump() end
assert(created==before and o.cells==42);_G[o.select]('2024-02-29');assert(chosen)
assert(not pcall(ui.setMonth,'month',{year=2024,month=13}));foreground=false;assert(not pcall(ui.setMonth,'month',{year=2024,month=1}))
''')

if __name__=='__main__':unittest.main()

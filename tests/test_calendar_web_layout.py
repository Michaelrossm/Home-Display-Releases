from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CalendarWebLayoutTests(unittest.TestCase):
    def test_settings_buttons_submit_and_reenable_after_failure(self):
        script = self.script()
        save = "$('savePerson').onclick=" + script.split("$('savePerson').onclick=", 1)[1].split("$('addCalendar').onclick", 1)[0]
        sync = "$('sync').onclick=" + script.split("$('sync').onclick=", 1)[1].split('hpWeb.whenVisible', 1)[0]
        self.node(r'''
const assert=require('assert');
const nodes={savePerson:{},sync:{},refresh:{},name:{value:'Max'},lastName:{value:'Mustermann'},email:{value:'max@example.org'},personColor:{value:'0'},birthday:{value:'19961224'},person:{value:''}};
const $=id=>nodes[id];let contactsLoaded=false,people=[],calls=[],failure=false;
const usersOnly=false;
const contactChoice={value:''};
const formatBirthdayInput=v=>v.replace(/\D/g,'').replace(/^(\d{4})(\d{2})(\d{2})$/,'$1-$2-$3');
const birthdayParts=v=>({year:+v.slice(0,4),month:+v.slice(5,7),day:+v.slice(8,10)});
function message(){};async function action(fn){return fn()}
const personCalendars={querySelectorAll:()=>[]},syncFeedback={};
async function loadPeople(){};async function loadCalendars(){};async function loadStatus(){};
nodes.person.onchange=()=>{};
async function job(op,args){calls.push({op,args});if(failure)throw Error('settings_locked');return op==='sync'?{accounts:[{requested:true}]}:{id:'12345678'}}
''' + save + sync + r'''
(async()=>{
 await nodes.savePerson.onclick();assert.equal(calls[0].op,'person_save');assert.equal(calls[0].args.birthday_year,1996);assert.equal(calls[0].args.birthday_day,24);assert.equal(nodes.savePerson.disabled,false);
 calls=[];await nodes.sync.onclick();assert.equal(calls[0].op,'sync');assert.equal(nodes.sync.disabled,false);
 failure=true;await assert.rejects(nodes.sync.onclick(),/settings_locked/);assert.equal(nodes.sync.disabled,false);
 await assert.rejects(nodes.savePerson.onclick(),/settings_locked/);assert.equal(nodes.savePerson.disabled,false);
})().catch(e=>{console.error(e);process.exitCode=1});
''')

    def script(self):
        source = (ROOT / 'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        return re.findall(r'<script>(.*?)</script>', source, re.S)[0]

    def node(self, source):
        node = shutil.which('node')
        self.assertIsNotNone(node)
        result = subprocess.run([node], input=source, text=True, encoding='utf-8',
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_month_week_day_and_dst_models(self):
        script = self.script()
        functions = 'function localInput(' + script.split('function localInput(', 1)[1].split('async function setupAgenda', 1)[0]
        functions += 'function shiftDay(' + script.split('function shiftDay(', 1)[1].split('function setViewRange', 1)[0]
        functions += 'function eventsOnDay(' + script.split('function eventsOnDay(', 1)[1].split('async function selectCalendarDay', 1)[0]
        self.node("const assert=require('assert');process.env.TZ='Europe/Berlin';let calendarMode='month',selectedDay=new Date(2026,1,15);" + functions + r'''
const february=monthDates(selectedDay);assert.equal(february.length,42);assert.equal(february[0].getDay(),1);
assert.equal(february.filter(d=>d.getMonth()===1).length,28);
assert.equal(monthDates(new Date(2024,1,15)).filter(d=>d.getMonth()===1).length,29);
calendarMode='week';assert.equal(visibleDates().length,7);assert.equal(visibleDates()[0].getDay(),1);
calendarMode='day';assert.equal(visibleDates().length,1);
const spring=new Date(2026,2,29),autumn=new Date(2026,9,25);
assert.equal((shiftDay(spring,1)-spring)/3600000,23);
assert.equal((shiftDay(autumn,1)-autumn)/3600000,25);
const end=shiftDay(spring,1).getTime()/1000,start=spring.getTime()/1000;
const events=[{id:1,start,end},{id:2,start:end,end:end+3600},{id:3,start:start-3600,end:start}];
assert.deepEqual(eventsOnDay(events,spring).map(e=>e.id),[1]);
''')

    def test_paginated_reads_are_bounded_and_latest_navigation_wins(self):
        script = self.script()
        function = 'async function loadAgenda(' + script.split('async function loadAgenda(', 1)[1].split('async function changePeriod', 1)[0]
        fixture = r'''
const assert=require('assert');let agendaGeneration=0,calendarLoading=false,calendarReloadQueued=false,agendaCursor=null,agendaItems=[];
const nodes={rangeFrom:{value:'2026-10-01'},rangeTo:{value:'2026-10-31'},eventFilter:{value:'0'},userFilter:{value:'0'},rangeStatus:{},moreEvents:{}};
const $=id=>nodes[id];let visible=true,queries=[];
const hpWeb={requireVisible(){if(!visible)throw Error('hidden')}};
function renderCalendarView(){};let implementation;
async function job(op,args){assert.equal(op,'query');queries.push({...args});return implementation(args)}
'''
        self.node(fixture + function + r'''
(async()=>{
 implementation=async()=>({items:Array.from({length:32},(_,id)=>({id})),more:true,next_cursor:{id:'a',start:1}});
 await loadAgenda();assert.equal(queries.length,8);assert.equal(agendaItems.length,256);assert.equal(nodes.moreEvents.hidden,false);
 assert.equal(queries[0].limit,32);assert(queries[1].cursor);assert(!calendarLoading);
 let release;queries=[];implementation=()=>new Promise(resolve=>release=resolve);
 const first=loadAgenda();await Promise.resolve();assert(calendarLoading);
 nodes.rangeFrom.value='2026-11-01';nodes.rangeTo.value='2026-11-30';await loadAgenda();
 implementation=async()=>({items:[{id:'new'}],more:false});
 release({items:[{id:'old'}],more:false});await first;
 assert.equal(queries.length,2);assert.deepEqual(agendaItems,[{id:'new'}]);assert(!calendarLoading);assert(!calendarReloadQueued);
 assert(queries[1].from>queries[0].from);
 visible=false;queries=[];await assert.rejects(loadAgenda(),/hidden/);assert(!calendarLoading);assert.equal(queries.length,0);
})().catch(error=>{console.error(error);process.exitCode=1});
''')

    def test_responsive_layout_lazy_settings_and_safe_text(self):
        script = self.script()
        page = (ROOT / 'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        for mode in ['month', 'week', 'day', 'list']:
            self.assertIn('data-mode="' + mode + '"', page)
        self.assertIn('@media(max-width:850px)', page)
        self.assertIn('@media(max-width:520px)', page)
        self.assertIn('grid-template-columns:1fr', page)
        self.assertIn('min-height:44px', page)
        self.assertIn('focus-visible', page)
        self.assertIn('settingsLoaded', script)
        bootstrap = script.split('hpWeb.whenVisible(', 1)[1].split('let settingsLoaded', 1)[0]
        self.assertIn('if(usersOnly)', bootstrap)
        self.assertIn('else await setupAgenda()', bootstrap)
        self.assertNotIn("job('region')", bootstrap)
        rendering = script.split('function renderCalendarView()', 1)[1].split('let calendarReloadQueued', 1)[0]
        self.assertNotIn('innerHTML', rendering)
        self.assertIn('title.textContent=e.title', rendering)
        self.assertIn('selectCalendarDay(date)', rendering)
        self.assertNotIn('setInterval(', script)


if __name__ == '__main__':
    unittest.main()

from pathlib import Path
import json
import unittest
import test_v31

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT / 'apps/co2_scale_professional'


@unittest.skipUnless(APP.exists(), 'Optional external CO2 app sources not present')
class Co2ScaleAppTests(unittest.TestCase):
    def run_lua(self, case, settings=False):
        source = (APP / ('settings.lua' if settings else 'main.lua')).read_text(encoding='utf-8')
        prefix = r'''
local values,requests,mounted={},{},nil
ui={mount=function(t) mounted=t end,ready=function()return true end,
 patch=function(id,v)values[id]=v end,read=function(id)return values[id] and values[id].text or '' end}
local now,epoch=0,1792252800
local store={host='192.168.1.20'}
local schedule={enabled=true,clock_valid=true,timezone='Europe/Berlin',off_start_hour=22,off_end_hour=6,source_host='192.168.1.20'}
function hp_time_epoch()return epoch end
function hp_millis()return now end
function hp_set_tick_interval()end
function hp_kv_get_string(k,default)return store[k] or default end
function hp_kv_set_string(k,v)store[k]=v;return 2 end
function hp_secret_exists()return false end
function hp_json_decode(s)if s=='schedule' then return schedule end;if s=='response' then return {schema=1,measurement={valid=true,percent=70,gas_g=420},wifi_schedule=schedule} end end
function hp_json_encode()return 'schedule' end
function hp_http_get(options)requests[#requests+1]=options;return 1 end
local response={done=true,ok=true,body='response'}
function hp_job_result()return response end
function hp_job_release()return true end
local cancelled=0
function hp_job_cancel()cancelled=cancelled+1 end
function hp_open_app_settings()end
function hp_open_app()end
function hp_secret_set()return 2 end
'''
        test_v31.V31Tests().lua(prefix + '\n' + source + '\n' + case)

    def test_read_only_poll_schedule_backoff_and_close(self):
        self.run_lua(r'''
on_open();on_tick();assert(#requests==1)
assert(requests[1].url=='http://192.168.1.20/api/v1/status')
on_tick();assert(values.level.text=='70.0 %')
now=1000;on_tick();assert(#requests==1)
-- No requests inside the cached nighttime interval, including app reopen.
store.schedule='schedule';epoch=1792278000;now=120000
on_tick();assert(#requests==1, 'night must not poll')
epoch=1792252800;on_tick();assert(#requests==2)
on_close();assert(cancelled>=1)
''')

    def test_dst_boundaries_and_wrapped_and_same_day_windows(self):
        self.run_lua(r'''
-- Compare against UTC reference values for winter, summer and EU transition.
for _,t in ipairs({{1767225600,1},{1782864000,2},{1774745940,1},{1774746000,3},{1792890000,2}})do
 assert(berlin_hour(t[1])==t[2], 'Berlin DST conversion '..t[1])
end
''')

    def test_settings_syntax_and_reject_paths(self):
        self.run_lua(r'''
on_open();values.host={text='http://192.168.1.20/admin'};save()
assert(values.status.text:find('ohne Pfad'))
values.host={text='192.168.1.20'};values.password={text=''};save();on_tick()
assert(store.host=='192.168.1.20');assert(values.status.text:find('Gespeichert'))
''', settings=True)

    def test_optional_manifest_and_no_discovery_or_write_http(self):
        manifest=json.loads((APP/'app.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['schema'],3)
        self.assertNotIn('background',manifest)
        source=(APP/'main.lua').read_text(encoding='utf-8')
        self.assertNotIn('hp_http_post',source)
        self.assertNotIn('hp_discovery',source)
        self.assertIn('hp_job_cancel',source)

    def test_layout_uses_zero_flex_basis_and_complete_weight_grid(self):
        self.run_lua(r'''
on_open()
local found={}
local function inspect(node,parent)
 assert(not found[node.id], 'Duplicate UI id '..node.id);found[node.id]=node
 if parent and (node.grow or 0)>0 then
  if parent.type=='row' then assert(node.width==0, 'Horizontal flex needs zero width: '..node.id)
  else assert(node.height==0, 'Vertical flex needs zero height: '..node.id) end
 end
 for _,child in ipairs(node.children or {}) do inspect(child,node) end
end
inspect(mounted)
assert(mounted.height_percent==100 and mounted.width_percent==100 and mounted.scrollable==false)
for _,id in ipairs({'gross','capacity','full','tare','regulator','empty'})do
 assert(found[id] and found[id..'_card'].width==0 and found[id..'_card'].height_percent==100)
end
assert(found.dashboard.height==0 and found.history.height==0)
assert(found.main.grow==4 and found.details.grow==7)
display({measurement={valid=true,percent=50,gas_g=1000,gross_g=5810,empty_g=4470,regulator_g=340,capacity_g=2000,full_total_g=6810}})
assert(values.tare.text=='4470.0 g' and values.regulator.text=='340.0 g')
assert(values.empty.text=='4810.0 g' and values.full.text=='6810.0 g')
''')

    def test_real_shared_ui_builds_main_and_settings(self):
        for name in ('main.lua', 'settings.lua'):
            source=(APP/name).read_text(encoding='utf-8')
            test_v31.V31Tests().declarative(source + r'''
function hp_set_tick_interval()end
function hp_kv_get_string(k,default)return default end
function hp_set_scrollable()end
function hp_set_password_mode()end
on_open()
for i=1,1000 do if pump() then break end end
assert(ui.ready(), 'CO2 view must finish building')
if type(display)=='function' then
  display({measurement={valid=true,percent=75,gas_g=450},time={epoch=1792252800},power={sample_age_seconds=0}})
  display({measurement={valid=true,percent=74,gas_g=444},time={epoch=1792252860},power={sample_age_seconds=0}})
  for i=1,1000 do if pump() then break end end
  local plotted=false
  for _,o in ipairs(objects)do if o.points then assert(#o.points==2 and o.points[2]==74);plotted=true end end
  assert(plotted, 'Real measurement history must reach shared chart')
end
''', r'''
hp_ui_icon=hp_label
function hp_ui_icon_set(o,name)o.icon=name end
function hp_chart_set_range(o,a,b)o.minimum=a;o.maximum=b end
function hp_ui_chart_set(o,a,b)o.points=a end
''')

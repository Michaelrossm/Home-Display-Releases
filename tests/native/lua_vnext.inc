// Runs the firmware's actual declarative Lua module on the pinned Lua VM.
L=lua_newstate(luaAppAlloc,&budget,0);assert(L);openAppLibraries(L);
const char* vnextUiDoubles=R"lua(
  created,writes,foreground=0,0,true
  function hp_ui_assert_foreground() assert(foreground,'foreground required') end
  function hp_root() return {root=true} end
  function hp_millis() return 0 end
  function hp_ui_request_work() end
  function hp_ui_move(...) end
  function hp_ui_delete(...) end
  function hp_ui_percent(n) return n end
  function hp_ui_content_size() return -1 end
  function make(_,text) created=created+1;return {text=text} end
  hp_label=make;hp_button=make;hp_ui_container=make;hp_ui_panel=make
  hp_ui_textarea=make;hp_switch=make;hp_ui_progress=make;hp_ui_spinner=make
  hp_ui_list=make;hp_ui_list_item=make;hp_ui_tabview=make;hp_ui_tab=make;hp_ui_chart=make
  function hp_set_text(o,text) if o.text~=text then writes=writes+1;o.text=text end end
  hp_ui_button_text=hp_set_text;hp_ui_textarea_set=hp_set_text
  function hp_ui_textarea_get(o) return o.text end
  function hp_checked(o) return o.checked end
  function hp_set_checked(o,value) o.checked=value end
  function hp_set_hidden(o,value) o.hidden=value end
  function hp_set_enabled(o,value) o.enabled=value end
  function hp_set_size(...) end
  function hp_ui_grow(...) end
  function hp_ui_gap(...) end
  function hp_set_padding(...) end
  function hp_ui_button_primary(...) end
  function hp_ui_text_style(...) end
  function hp_ui_style_input(o) o.inputStyled=true end
  function hp_ui_progress_set(...) end
  function hp_ui_flex(...) end
  function hp_on_click(o,callback) o.click=callback end
  function hp_on_value_changed(o,callback) o.change=callback end
  function hp_alert(...) end
  function hp_open_app() return true end
  function hp_ui_chart_set(...) end
)lua";run(L,vnextUiDoubles);
assert(luaL_loadbuffer(L,hp::kDeclarativeUi,sizeof(hp::kDeclarativeUi)-1,"ui-v3")==LUA_OK);
assert(protectedCall(L,0,1,20,1000)==LUA_OK);lua_setglobal(L,"pump");
run(L,R"lua(
  local children={}
  for i=1,64 do children[i]={id='label'..i,type='label',text='A'} end
  ui.mount{id='page',type='page',children=children}
  assert(not ui.ready() and created==0) -- mount queues, does not build.
  assert(not pcall(ui.mount,{id='second'}))
  local turns=0
  while not ui.ready() do
    local before=created;pump();assert(created-before<=8);turns=turns+1
    assert(turns<20)
  end
  assert(turns==9 and created==65 and writes==0)
  ui.patch('label1',{text='B'});ui.patch('label1',{text='C'});pump()
  assert(writes==1 and created==65)
  ui.patch('label1',{text='C'});pump();assert(writes==1)
  assert(not pcall(ui.patch,'missing',{text='X'}))
  assert(not pcall(ui.patch,'label1',{text=string.rep('x',2049)}))
  foreground=false
  assert(not pcall(ui.patch,'label1',{text='unsafe'}))
  assert(not pcall(ui.dialog,'unsafe','unsafe'))
  assert(not pcall(pump))
  foreground=true;pump();assert(writes==1)
)lua");
// Fresh module/state-style initialization must not retain previous widget IDs.
assert(luaL_loadbuffer(L,hp::kDeclarativeUi,sizeof(hp::kDeclarativeUi)-1,"ui-v3")==LUA_OK);
assert(protectedCall(L,0,1,20,1000)==LUA_OK);lua_setglobal(L,"pump");
run(L,R"lua(
  local duplicate={id='same',children={{id='same'}}}
  assert(not pcall(ui.mount,duplicate))
  local many={id='page',children={}}
  for i=1,256 do many.children[i]={id='node'..i,type='label'} end
  assert(not pcall(ui.mount,many))
  local recursive={id='loop'};recursive.children={recursive}
  assert(not pcall(ui.mount,recursive))
  local deep={id='1'};local parent=deep
  for i=2,17 do local child={id=tostring(i)};parent.children={child};parent=child end
  assert(not pcall(ui.mount,deep))
  ui.mount{id='form',type='input',text='ä ü ß'};pump();assert(ui.read('form')=='ä ü ß')
  ui.patch('form',{text='neu'});pump();assert(ui.read('form')=='neu')
)lua");
run(L,R"lua(
  hp_energy_snapshot=function() energyReads=energyReads+1;return {revision=energyRev,online=true,updated_ms=123,value=20} end
  hp_calendar_capabilities=function() return {read=true} end
  hp_calendar_query=function() calendarReads=calendarReads+1;return 42 end
  hp_job_result=function(id) assert(id==42);return {done=true,ok=true,body='{"revision":1,"items":[]}'} end
  hp_job_release=function(id) released=id end
  hp_job_cancel=function(id) canceled=id end
  hp_printer_snapshot_summary=function(profile) assert(profile=='p1');return {api_version=2,ok=true,data={revision=printerReady and 1 or 0,online=printerReady==true,pending=printerReady~=true,timestamp=456}} end
  hp_printer_watch=function(profile,enabled) assert(profile=='p1');printerWatched=enabled and printerReady;return printerReady end
  notifications,energyReads,calendarReads,energyRev=0,0,0,1
  function changed(id,snapshot) notifications=notifications+1;assert(snapshot.version==1 and snapshot.value) end
)lua");
hp::registerLuaJsonBridge(L);
assert(luaL_loadbuffer(L,hp::kDataSubscriptions,sizeof(hp::kDataSubscriptions)-1,"data-v1")==LUA_OK);
assert(protectedCall(L,0,1,20,1000)==LUA_OK);lua_setglobal(L,"subscriptions");
run(L,R"lua(
  data.subscribe('energy',{source='energy',on_change='changed'})
  data.subscribe('alias',{source='energy',on_change='changed'})
  subscriptions.step();assert(energyReads==1 and notifications==2)
  subscriptions.step();assert(energyReads==1)
  subscriptions.event('energy.updated');subscriptions.step();assert(energyReads==2 and notifications==2)
  energyRev=2;subscriptions.event('energy.updated');subscriptions.step();assert(notifications==4)
  assert(data.snapshot('energy').revision==2 and data.snapshot('alias').timestamp_ms==123)
  data.subscribe('calendar',{source='calendar',on_change='changed',query={limit=8}})
  subscriptions.step();subscriptions.event('calendar.updated');subscriptions.step();assert(calendarReads==1)
  subscriptions.event('calendar.completed',42);assert(released==42 and notifications==5)
  subscriptions.step();assert(calendarReads==2) -- One coalesced refresh requested while busy.
  data.unsubscribe('calendar');assert(canceled==42)
  data.unsubscribe('energy');energyRev=3;subscriptions.event('energy.updated');subscriptions.step();assert(notifications==6)
  assert(data.unsubscribe('alias') and not data.unsubscribe('alias'))
  printerReady=false;data.subscribe('printer',{source='printer',profile='p1',on_change='changed'});subscriptions.step()
  assert(not printerWatched and data.snapshot('printer').loading)
  printerReady=true;subscriptions.event('printer.profiles');subscriptions.step()
  assert(printerWatched and data.snapshot('printer').value.revision==1 and data.snapshot('printer').timestamp_ms==456)
  subscriptions.event('network.changed');subscriptions.step();assert(data.snapshot('printer').revision==2)
  data.unsubscribe('printer');assert(not printerWatched)
  assert(not pcall(data.subscribe,'bad',{source='unknown',on_change='changed'}))
  foreground=false;assert(not pcall(data.subscribe,'bad',{source='energy',on_change='changed'}));foreground=true
)lua");
lua_close(L);assert(budget.used==0&&hp_test_livebytes==0);
for(const char* entry:{"main.lua","settings.lua"}){
  const std::string path=std::string(argv[1])+"/apps/vnext_reference/"+entry;
  std::ifstream input(path);if(!input.good())continue;
  const std::string script((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
  L=lua_newstate(luaAppAlloc,&budget,0);assert(L);openAppLibraries(L);run(L,vnextUiDoubles);
  run(L,R"lua(
    function hp_kv_get_string(key,fallback) assert(key=='display_name');return fallback end
    function hp_kv_set_string(key,value) assert(key=='display_name');saved=value;return 42 end
    function hp_open_app_settings() navigation='settings';return true end
    function hp_open_app() navigation='app';return true end
    function hp_job_result(id) return {done=true,ok=true} end
    function hp_job_release(id) released=id end
  )lua");
  assert(luaL_loadbuffer(L,hp::kDeclarativeUi,sizeof(hp::kDeclarativeUi)-1,"ui-v3")==LUA_OK);
  assert(protectedCall(L,0,1,20,1000)==LUA_OK);lua_setglobal(L,"pump");
  run(L,script.c_str());run(L,"on_open();while not ui.ready() do pump() end");
  if(!strcmp(entry,"main.lua"))run(L,"increment();pump();on_resume();pump();settings();assert(navigation=='settings' and return_to_app==nil)");
  else run(L,"save_settings();pump();on_event('storage.changed',{job=42});pump();assert(released==42);return_to_app();assert(navigation=='app' and increment==nil)");
  lua_close(L);assert(budget.used==0&&hp_test_livebytes==0);
}

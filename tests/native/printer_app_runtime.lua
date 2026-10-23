-- Execute the real optional package against bounded, deterministic bridge mocks.
local now=0
local sequence, summaryCalls, sectionCalls, commands, seriesWrites=0,0,0,0,0
local objects, objectCount={},0
local textWrites, maxObjects, ticks=0,0,0
local buttonWrites,limitListWrites,largeList,autoThumbnail,thumbnailCalls=0,false,false,false,0
local consoleReply,consoleCalls,keyboardFocused="ok",0,false
local fileCalls,pendingFilePolls,watchStableRows,stableRowHides=0,0,false,0
local cancelled={},{}
local jobs, nextJob={},1
local actions={}
local root={x=0,y=0,w=1228,h=672,text=""}
local function object(parent,text)
  objectCount=objectCount+1
  local o={parent=parent,text=text or "",x=0,y=0,w=0,h=0,hidden=false}
  objects[#objects+1]=o
  return o
end
local function envelope(data) return {api_version=2,ok=true,data=data} end
function hp_root() return root end
function hp_millis() return now end
function hp_panel(p) return object(p) end
function hp_label(p,t) return object(p,t) end
function hp_button(p,t) return object(p,t) end
function hp_textarea(p,t) return object(p,t) end
function hp_textarea_text(o) return o.text end
function hp_align(o,_,x,y) o.x=x;o.y=y end
function hp_set_size(o,w,h) o.w=w;o.h=h end
function hp_set_text(o,t) assert(o);o.text=t;textWrites=textWrites+1 end
function hp_set_hidden(o,v)
 assert(o and not o.deleted,"deleted UI handle")
 if watchStableRows and v and not o.hidden and o.callback and o.callback:match("pc_files_item_") then stableRowHides=stableRowHides+1 end
 o.hidden=v
end
function hp_ui_delete(o)
 for _,v in ipairs(objects) do local p=v;while p do if p==o then v.deleted=true;v.hidden=true;break end;p=p.parent end end
end
function hp_set_enabled(o,v) assert(o);o.enabled=v end
function hp_on_click(o,cb) o.callback=cb end
function hp_on_value_changed(o,cb) o.callback=cb end
function hp_ui_button_text(o,t) o.text=t;buttonWrites=buttonWrites+1 end
function hp_ui_button_primary(o,v) o.primary=v end
function hp_ui_checkbox(p,t,v) local o=object(p,t);o.checked=v;return o end
function hp_ui_checkbox_checked(o) return o.checked end
function hp_set_checked(o,v) o.checked=v end
function hp_ui_dropdown(p,t) local o=object(p,t);o.value=0;return o end
function hp_ui_dropdown_value(o) return o.value end
function hp_ui_dropdown_set_value(o,v) o.value=v end
function hp_ui_dropdown_set_options(o,t) o.text=t end
function hp_ui_progress(p) return object(p) end
function hp_ui_progress_set(o,v) o.progress=v end
function hp_ui_icon(p,t) return object(p,t) end
function hp_chart_create(p) return object(p) end
function hp_theme() return {accent=0x123456,text_secondary=0x456789,warning=0x987654,success=0x345678} end
function hp_chart_add_series(_,name,color) if name=="Sensor 1" then assert(color==0x123456,"graph must use theme accent") end;return {} end
function hp_chart_set_series(_,_,v) assert(#v==60);seriesWrites=seriesWrites+1 end
function hp_ui_keyboard(p) return object(p) end
function hp_ui_keyboard_bind() keyboardFocused=true end
function hp_ui_keyboard_hide() keyboardFocused=false end
for _,name in ipairs({"hp_set_bg_color","hp_set_text_color","hp_set_radius","hp_set_border","hp_set_scrollable","hp_set_padding","hp_set_font_size","hp_ui_style_card"}) do _G[name]=function() end end
function hp_set_tick_interval(v) assert(v>=100 and v<=2000);ticks=v;return true end
function hp_bridge_contract() return {api_version=2} end
local profileListCalls=0
function hp_printer_list()
 profileListCalls=profileListCalls+1
 if now<1800 then return envelope({}) end -- stale empty cache during async KV refresh
 return envelope({{id="p1",host="duet.local",protocol="reprap",name="Duet",port=80,camera_url="http://duet.local:8080/video"},{id="p2",host="other.local",name="Other",protocol="moonraker"}})
end
function hp_printer_profile_state() return envelope({refreshing=now<1800}) end
local status={online=true,stale=false,state="idle",revision=1,progress=20,filename="test.gcode",duration=100,remaining=400}
local summaryFailure=false
function hp_printer_snapshot_summary() summaryCalls=summaryCalls+1;if summaryFailure then return {api_version=2,ok=false,error_code="printer_network"} end;local copy={};for k,v in pairs(status) do copy[k]=v end;return envelope(copy) end
function hp_printer_snapshot_section(_,kind,offset)
  sectionCalls=sectionCalls+1
  if kind=="temperatures" and offset==0 then return envelope({{id="nozzle",name="Düse",current=205,target=210},{id="bed",name="Bett",current=60,target=60}}) end
  if kind=="heaters" and offset==0 then return envelope({{id="heater_1",index=1,name="Hotend 1",category="hotend",controllable=true},{id="heater_2",index=2,name="Hotend 2",category="hotend",controllable=true},{id="heater_0",index=0,name="Bett",category="bed",controllable=true}}) end
  if kind=="tools" and offset==0 then return envelope({{id="tool_0",name="Tool 0",heaters={1}},{id="tool_1",name="Tool 1",heaters={2}}}) end
  return envelope({})
end
function hp_json_encode(t) return t.path or t.code or tostring(t.distance or t.axes or t.mode or "{}") end
function hp_json_decode(s) if s=="THUMB" then return {handle=123} end return {entries={}} end
function hp_kv_get_string() return "" end
function hp_kv_set_string() return 998 end
function hp_printer_command(_,kind,params)
  commands=commands+1;actions[#actions+1]={kind=kind,params=params};jobs[nextJob]={done=false};local id=nextJob;nextJob=nextJob+1
  return {api_version=2,ok=true,pending=true,job_id=id}
end
function hp_printer_upload(id,params) return hp_printer_command(id,"upload_file",params) end
function hp_job_result(id) return jobs[id] or {done=false} end
function hp_job_release(id) jobs[id]=nil end
function hp_job_cancel(id) cancelled[id]=true end
function hp_printer_preview()
 thumbnailCalls=thumbnailCalls+1;local id=900+thumbnailCalls
 jobs[id]=autoThumbnail and {done=true,ok=true,body="THUMB"} or {done=false}
 return {api_version=2,ok=true,pending=true,job_id=id}
end
function hp_printer_files()
 fileCalls=fileCalls+1
 if pendingFilePolls>0 then pendingFilePolls=pendingFilePolls-1;return envelope({entries={},pending=true}) end
 if not largeList then return envelope({entries={{name="test.gcode",path="0:/gcodes/test.gcode"}},pending=false,offset=0}) end
 local entries={{name="Ordner A",path="0:/gcodes/Ordner A",directory=true},{name="Ordner B",path="0:/gcodes/Ordner B",directory=true}}
 for i=1,30 do entries[#entries+1]={name=string.format("Bauteil_%02d_äöü.gcode",i),path="0:/gcodes/Teil"..i..".gcode",date=string.format("2026-10-%02dT12:00:00",i)} end
 return envelope({entries=entries,pending=false,offset=0,has_more=true,next_offset=32})
end
image={create=function(p) return object(p,"miniimage") end,setSource=function() end,release=function() end}
function hp_printer_macros() return envelope({entries={{name="macro.g",path="0:/macros/macro.g"}}}) end
function hp_printer_console() consoleCalls=consoleCalls+1;return envelope({lines={consoleReply}}) end
function hp_open_app_settings() end
local videoOpens,videoStops=0,0
function hp_video_open(o) assert(o.host=="duet.local" and o.port==8080 and o.fps==3);videoOpens=videoOpens+1;return envelope(true) end
function hp_video_stop() videoStops=videoStops+1;return envelope(true) end
function hp_video_snapshot() return envelope({active=true,frames=12}) end
function hp_video_view(p) return object(p) end
storage={list=function() jobs[999]={done=false};return 999 end}
dofile(app_path)
local function step(n)
  for _=1,n do now=now+100;local before=objectCount;local writes=buttonWrites;on_tick();maxObjects=math.max(maxObjects,objectCount-before);if limitListWrites then assert(buttonWrites-writes<=1,"list labels must be sliced, not painted in one tick") end end
end
local function has(text)
  for _,o in ipairs(objects) do if o.text:find(text,1,true) then return true end end
end
on_open();step(1);assert(root.hidden,"incomplete startup must stay hidden");step(29)
assert(not root.hidden,"completed shell must become visible")
assert(ticks==100 and maxObjects<=12,"startup must be bounded: interval="..ticks.." objects="..maxObjects)
assert(summaryCalls>0 and sectionCalls>0 and has("205.0"),"live temperatures must appear despite pending preview")
assert(profileListCalls>1,"stale empty profile cache must recover without re-saving settings")
local previousSections=sectionCalls;step(70)
assert(sectionCalls>previousSections and summaryCalls>=5,"preview must not starve status/components")
-- Unchanged summary must not repaint the entire chart every second.
assert(seriesWrites<=24,"chart redraws require a sample, not a summary revision")
-- A reconnect/error must retire cached command eligibility, even at the same
-- revision. Never exercise the physical printer: all commands here are mocks.
status.pending=true;step(15);pc_home();pc_home();assert(commands==0,"pending status must lock commands")
status.pending=nil;step(15)
summaryFailure=true;step(15);pc_home();pc_home();assert(commands==0,"failed summary must lock commands")
summaryFailure=false;status.filename="";step(15);assert(has("Kein Druckauftrag"),"empty filenames need an idle placeholder")
status.filename="test.gcode";status.revision=2;step(15)
-- Staleness transitions with the same native revision must reach command safety.
status.stale=true;step(15);pc_home();pc_home();assert(commands==0)
status.stale=false;step(15);pc_home();step(55);pc_home();assert(commands==0,"expired confirmation must not send")
pc_home();assert(commands==1);jobs[1]={done=true,ok=true};step(10)
-- Every page can be constructed/reused without errors or missing handlers.
local beforeControl=#objects
pc_control();step(1)
local controlHost
for i=beforeControl+1,#objects do if objects[i].parent==root then controlHost=objects[i] end end
assert(controlHost and controlHost.hidden,"new page must remain hidden during construction")
pc_overview();step(1);assert(controlHost.deleted,"unfinished constructor must be cancelled and removed")
local beforeRebuild=#objects;pc_control();step(1)
for i=beforeRebuild+1,#objects do if objects[i].parent==root then controlHost=objects[i] end end
assert(controlHost.hidden,"recreated page must not reveal partial controls")
step(25);assert(not controlHost.hidden,"finished page must be revealed")
for _,cb in ipairs({pc_control,pc_details,pc_files,pc_macros,pc_console,pc_history,pc_queue,pc_overview}) do cb();step(25) end
for _,o in ipairs(objects) do if o.callback then assert(type(_G[o.callback])=="function","missing handler: "..o.callback) end end
pc_files();step(10);pc_files_item_1();step(25);assert(has("Druck starten"),"reused file buttons must not invoke macros")
pc_print_file();pc_print_file();assert(actions[#actions].kind=="start_file");jobs[nextJob-1]={done=true,ok=true};step(10)
pc_macros();step(10);pc_macros_item_1();pc_macros_item_1();assert(actions[#actions].kind=="run_macro");jobs[nextJob-1]={done=true,ok=true};step(10)
farm.queues.p1={mode="confirm",jobs={{path="0:/gcodes/test.gcode",name="test"}}};farm.changed("p1")
local beforeSummary=summaryCalls;step(60);assert(summaryCalls-beforeSummary>=4,"pending KV save must not starve live status")
local before=objectCount;pc_control();step(4);pc_overview();step(4);assert(objectCount==before,"pages must be reused")
pc_files();step(10);pc_source_sd();step(4);assert(has("Quelle: SD:"),"local SD source must be explicit")
pc_source_usb();step(4);assert(has("Quelle: USB:"),"local USB source must be explicit");assert(cancelled[999],"source switch cancels old storage read")
pc_camera();step(30);assert(videoOpens==1,"existing VideoService must open once")
pc_overview();step(3);assert(videoStops>0,"hidden camera must stop")
for _,o in ipairs(objects) do
  if o.parent~=root and o.parent.w>0 then assert(o.x>=0 and o.y>=0 and o.x+o.w<=o.parent.w+1 and o.y+o.h<=o.parent.h+1,"widget outside card: "..o.text) end
  if o.text=="Temperaturen" or o.text:find("MJPEG/RTSP",1,true) then
    for _,other in ipairs(objects) do
      if other~=o and other.parent==o.parent and other.text~="" and other.w>0 and other.h>0 then
        assert(o.x+o.w<=other.x or other.x+other.w<=o.x or o.y+o.h<=other.y or other.y+other.h<=o.y,"header text overlap: "..other.text)
      end
    end
  end
end
-- Full metadata page, interrupted rendering and bounded visible thumbnails.
pc_console();step(15);local previousConsoleCalls=consoleCalls
consoleReply="DWC: externe Rückmeldung";step(15)
assert(consoleCalls>previousConsoleCalls and has(consoleReply),"console must refresh cached external replies without manual reload")
local inputY,logY
for _,o in ipairs(objects) do if o.callback=="pc_gcode_focus" and not o.deleted then inputY=o.y end;if o.text==consoleReply then logY=o.y end end
assert(inputY and logY and inputY<logY,"console input belongs above its history")
pc_gcode_focus();assert(keyboardFocused);pc_keyboard_hide();assert(not keyboardFocused,"blank-area callback must dismiss keyboard")
largeList=true;autoThumbnail=true;limitListWrites=true
pc_files();step(2);pc_source_printer();step(3);pc_overview();step(1)
local visibleOverview=false
for _,o in ipairs(objects) do
 if o.text=="Druckauftrag" then local p=o;local visible=true;while p do if p.hidden then visible=false end;p=p.parent end;visibleOverview=visibleOverview or visible end
end
assert(visibleOverview,"navigation must win over unfinished file painting")
pc_files();step(80)
assert(has("Ordner A") and has("Ordner B") and has("Bauteil_01_äöü"),"metadata page must populate all visible rows")
assert(has("\239\129\187"),"folders need the firmware-supported directory glyph")
local hideFolders,sortName,sortDate,sourcePrinter,sourceSd,sourceUsb
for _,o in ipairs(objects) do
 if not o.deleted and o.callback=="pc_hide_folders" then hideFolders=o end
 if not o.deleted and o.callback=="pc_sort_name" then sortName=o end
 if not o.deleted and o.callback=="pc_sort_date" then sortDate=o end
 if not o.deleted and o.callback=="pc_source_printer" then sourcePrinter=o end
 if not o.deleted and o.callback=="pc_source_sd" then sourceSd=o end
 if not o.deleted and o.callback=="pc_source_usb" then sourceUsb=o end
end
for _,o in ipairs(objects) do
 if o.parent==sourcePrinter.parent then
  if o.callback=="pc_hide_folders" then hideFolders=o end
  if o.callback=="pc_sort_name" then sortName=o end
  if o.callback=="pc_sort_date" then sortDate=o end
 end
end
assert(sortName.primary and not sortDate.primary,"selected sort needs theme highlighting")
assert(sourcePrinter.primary and not sourceSd.primary and not sourceUsb.primary,"active source must be highlighted")
hideFolders.checked=true;pc_hide_folders();step(15)
for _,o in ipairs(objects) do
 if not o.deleted and not o.hidden and o.callback and o.callback:match("pc_files_item_") then assert(not o.text:find("Ordner",1,true),"folder filter must hide folder rows") end
end
hideFolders.checked=false;pc_hide_folders();step(15)
local restored=false
for _,o in ipairs(objects) do if not o.deleted and not o.hidden and o.text:find("Ordner A",1,true) then restored=true end end
assert(restored,"unchecking restores directories without refetching metadata")
local beforeSortRequests=fileCalls
pc_sort_date();step(25);assert(sortDate.primary and not sortName.primary,"date sort highlight must change")
for _,o in ipairs(objects) do if not o.deleted and not o.hidden and o.callback=="pc_files_item_3" then assert(o.text:find("Bauteil_30",1,true),"date sort must reorder actual rows, not just the highlight") end end
pc_sort_name();step(25)
assert(fileCalls==beforeSortRequests,"printer sorting must use cached metadata, not trigger a fresh pending request")
watchStableRows=true;pendingFilePolls=3;pc_list_refresh();step(65);watchStableRows=false
assert(pendingFilePolls==0 and stableRowHides==0,"same-folder refresh must retain visible rows throughout pending polls")
local thumbs=0;for _,o in ipairs(objects) do if o.text=="miniimage" then thumbs=thumbs+1 end end
assert(thumbs>0 and thumbs<=6,"only visible-row thumbnail objects may be allocated")
autoThumbnail=false;pc_next();step(20);local pendingThumbnail=900+thumbnailCalls
pc_overview();step(1);assert(cancelled[pendingThumbnail],"navigation must cancel the unfinished thumbnail job")
local calls=thumbnailCalls;step(15)
assert(thumbnailCalls==calls,"hidden list must not continue thumbnail requests")
limitListWrites=false
pc_control();step(100)
local hotChoice
for _,o in ipairs(objects) do if not o.deleted and o.text:find("Tool 1 / Hotend 2",1,true) then hotChoice=o end end
assert(hotChoice,"control must discover all tool heaters")
hotChoice.value=1;pc_hot()
assert(actions[#actions].kind=="set_temperature" and actions[#actions].params.heater=="heater_2","selected hotend must reach native adapter by stable ID")
jobs[nextJob-1]={done=true,ok=true};step(15);pc_bed()
assert(actions[#actions].params.heater=="heater_0","bed must use discovered mapping")
jobs[nextJob-1]={done=true,ok=true};step(15)
status.controls={bed_mesh=true,z_tilt=true,gantry_level=false,screws_tilt=true,select_tool=true,extrude=true,set_flow_factor=true,motors_off=true,mesh_clear=true,extrusion_ready=true,homed=true,channel_idle=true}
status.revision=99
pc_maintenance();step(100)
local disabledGantry
for _,o in ipairs(objects) do if not o.deleted and o.callback=="pc_gantry_level" then disabledGantry=o end end
assert(disabledGantry and not disabledGantry.enabled,"unconfigured calibration must be disabled")
local n=commands;pc_bed_mesh();assert(commands==n,"calibration requires confirmation")
pc_bed_mesh();assert(commands==n+1 and actions[#actions].kind=="bed_mesh")
jobs[nextJob-1]={done=true,ok=true};step(15)
pc_gantry_level();pc_gantry_level();assert(commands==n+1,"unavailable action must never submit")
pc_extrude();pc_extrude();assert(actions[#actions].kind=="extrude" and actions[#actions].params.distance==1 and actions[#actions].params.tool=="tool_0")
jobs[nextJob-1]={done=true,ok=true};step(15)
status.controls.extrusion_ready=false;status.revision=100;step(20)
for _,o in ipairs(objects) do if not o.deleted and (o.callback=="pc_extrude" or o.callback=="pc_retract") then assert(not o.enabled,"cold extrusion buttons must be disabled") end end
local catalogReads,historyReads=0,0
function hp_printer_action_catalog(offset,count)
  catalogReads=catalogReads+1
  assert(count<=4,"catalog pages must stay bounded")
  return envelope({{id="bed_mesh",label="Bett vermessen",confirm=true}})
end
function hp_printer_temperature_history(profile,sensor,offset,count)
  historyReads=historyReads+1
  assert(profile and sensor and count<=16,"history must be profile/sensor scoped and paginated")
  return envelope({revision=historyReads,points={{ms=1,current=25,target=0},{ms=2,current=26,target=0}}})
end
pc_overview();step(150)
assert(catalogReads>0 and historyReads>=2,"native shared catalog and history must be consumed")
on_close()
-- Settings is a separate state in production: it must also build in bounded
-- ticks, not consume the 20 ms on_open allowance with the entire form.
objects={};objectCount=0;maxObjects=0
function hp_set_textarea_text(o,t) assert(o);o.text=t end
assert(loadfile((app_path:gsub("main.lua$","settings.lua"))))()
on_open();assert(objectCount==0,"settings on_open must only schedule work")
step(60)
assert(not root.hidden and has("3D-Drucker"),"settings form must finish and become visible")
assert(maxObjects<=6,"settings construction must be sliced")
for _,o in ipairs(objects) do
  if o.w>0 then assert(o.x>=0 and o.y>=0 and o.x+o.w<=root.w+1 and o.y+o.h<=root.h+1,"settings outside actual host: "..o.text) end
end
on_close()
print("PASS printer package runtime: startup, fairness, chart cache, commands, pages, storage, camera")

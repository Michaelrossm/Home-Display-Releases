#pragma once
namespace hp {
// Firmware-owned Lua layer reuses the existing guarded widgets. No second LVGL
// implementation. The returned pump is retained privately in the Lua registry.
inline constexpr char kDeclarativeUi[] = R"lua(
local assert_ui = hp_ui_assert_foreground
local nodes, plan, retired, mounted, initialized, serial = {}, {}, {}, false, false, 0
local turn=0
local advancedCharts=type(hp_chart_create)=='function'
local inputKeyboard
local inputTypes={text=true,integer=true,number=true,decimal=true,signed_decimal=true,phone=true,datetime=true}
local constructors = {
  page=hp_ui_container, row=hp_ui_container, column=hp_ui_container,
  panel=hp_ui_panel, label=hp_label, button=hp_button, input=hp_ui_textarea,
  switch=hp_switch, progress=hp_ui_progress, loading=hp_ui_spinner,
  list=hp_ui_list, item=hp_ui_list_item, tabs=hp_ui_tabview, tab=hp_ui_tab,
  chart=hp_ui_chart,virtual_list=hp_ui_virtual_list,month_grid=hp_ui_month_grid,image=hp_ui_image,dropdown=hp_ui_dropdown,weather=hp_ui_container,icon=hp_ui_icon,icon_button=hp_ui_icon_button
}
local containers={page=true,row=true,column=true,panel=true,tab=true}
local fields={text=true,hidden=true,enabled=true,width=true,height=true,width_percent=true,height_percent=true,
  grow=true,gap=true,padding=true,primary=true,size=true,tone=true,value=true,status=true,priority=true,input_type=true,scrollable=true,options=true,weather_type=true,animated=true,intensity=true,min=true,max=true,password=true}
local weatherTypes={clear=true,clear_night=true,partly_cloudy=true,cloudy=true,fog=true,drizzle=true,rain=true,heavy_rain=true,snow=true,storm=true}
local function properties(p)
  assert(type(p)=='table','ui properties must be a table')
  local out={}
  for key,value in pairs(p) do
    if fields[key] then
      if key=='text' then assert(type(value)=='string' and #value<=2048,'ui text limit')
      elseif key=='options' then assert(type(value)=='string' and #value<=4096,'ui dropdown options limit')
      elseif key=='input_type' then assert(inputTypes[value],'ui input type')
      elseif key=='weather_type' then assert(weatherTypes[value],'ui weather type')
      elseif key=='intensity' then assert(type(value)=='number' and value>=0 and value<=100,'ui weather intensity')
      elseif key=='width_percent' or key=='height_percent' then assert(type(value)=='number' and value>=0 and value<=100,'ui percentage limit')
      elseif key=='hidden' or key=='enabled' or key=='primary' or key=='scrollable' or key=='animated' or key=='password' then assert(type(value)=='boolean','ui boolean')
      elseif key=='tone' then assert(type(value)=='string' and #value<=16,'ui tone')
      elseif key=='priority' then assert(value=='critical' or value=='data' or value=='image','ui priority')
      elseif key=='status' then assert(value=='ready' or value=='empty' or value=='loading' or value=='offline' or value=='error','ui status')
      else assert(type(value)=='number' and value==value and value>=-4096 and value<=4096,'ui numeric limit') end
      out[key]=value
    end
  end
  if out.min~=nil or out.max~=nil then
    assert(type(out.min)=='number' and type(out.max)=='number' and out.min%1==0 and out.max%1==0 and out.min<out.max,'chart range')
  end
  return out
end
local function apply(n)
  local o,p,previous=n.object,n.pending,n.applied
  if not o or not p then return end
  local function changed(k) return p[k]~=nil and p[k]~=previous[k] end
  if changed('text') then
    if n.kind=='button' then hp_ui_button_text(o,p.text)
    elseif n.kind=='input' then hp_ui_textarea_set(o,p.text)
    elseif n.kind=='label' then hp_set_text(o,p.text)
    elseif n.kind=='icon' then hp_ui_icon_set(o,p.text) end
    if n.kind=='icon_button' then hp_ui_icon_button_set(o,p.text) end
  end
  if changed('hidden') then hp_set_hidden(o,p.hidden) end
  if changed('enabled') then hp_set_enabled(o,p.enabled) end
  if changed('scrollable') then hp_set_scrollable(o,p.scrollable) end
  if changed('input_type') and n.kind=='input' then hp_ui_input_type(o,p.input_type) end
  if changed('password') and n.kind=='input' then hp_set_password_mode(o,p.password) end
  if n.kind=='weather' and (changed('weather_type') or changed('intensity') or changed('animated')) then
    hp_ui_weather_effect_set(o,{type=p.weather_type or previous.weather_type or 'cloudy',intensity=p.intensity or previous.intensity or 40,enabled=n.desired.animated~=false})
  end
  if changed('options') and n.kind=='dropdown' then hp_ui_dropdown_set_options(o,p.options) end
  if changed('width') or changed('height') or changed('width_percent') or changed('height_percent') then
    local percent=p.width_percent or previous.width_percent
    local heightPercent=p.height_percent or previous.height_percent
    hp_set_size(o,percent and hp_ui_percent(percent) or p.width or previous.width or hp_ui_percent(100),heightPercent and hp_ui_percent(heightPercent) or p.height or previous.height or hp_ui_content_size())
  end
  if changed('grow') then hp_ui_grow(o,p.grow) end
  if n.kind=='chart' and (changed('min') or changed('max')) then
    local lo,hi=n.desired.min,n.desired.max
    assert(type(lo)=='number' and type(hi)=='number' and lo<hi,'chart range')
    hp_chart_set_range(o,lo,hi)
  end
  if changed('gap') then hp_ui_gap(o,p.gap) end
  if changed('padding') then hp_set_padding(o,p.padding) end
  if changed('primary') and (n.kind=='button' or n.kind=='icon_button') then hp_ui_button_primary(o,p.primary) end
  if changed('size') or changed('tone') then hp_ui_text_style(o,p.size or previous.size or 18,p.tone or previous.tone or 'primary') end
  if changed('value') then
    if n.kind=='progress' then hp_ui_progress_set(o,p.value)
    elseif n.kind=='switch' then hp_set_checked(o,p.value~=0) end
  end
  if n.kind=='dropdown' and (changed('value') or changed('options')) then hp_ui_dropdown_set_value(o,p.value or previous.value or 0) end
  if n.kind=='virtual_list' and (changed('status') or changed('text')) then
    local defaults={ready='',empty='Keine Eintraege',loading='Wird geladen ...',offline='Offline',error='Fehler'}
    hp_ui_collection_status(o,p.text or defaults[p.status] or '')
  end
  for k,v in pairs(p) do previous[k]=v end
  n.pending=nil
end
-- Desired state is the bounded existing UI work queue. Calls validate and
-- reconcile Lua records only; the guarded pump is the only LVGL mutator.
local function snapshot(id)
  local n=nodes[id];local s={id=id,type=n.kind}
  for k,v in pairs(n.desired) do s[k]=v end
  s.on_click,s.on_change=n.on_click,n.on_change
  s.on_scroll,s.on_select=n.on_scroll,n.on_select
  if #n.children>0 then s.children={};for _,child in ipairs(n.children) do s.children[#s.children+1]=snapshot(child) end end
  return s
end
local rootId
local function reconcile(spec)
  local records,ordered={},{}
  local weatherCount=0
  local function visit(s,parent,depth,replaceParent)
    assert(depth<=16 and #ordered<256,'ui tree budget')
    assert(type(s)=='table' and type(s.id)=='string' and #s.id>0 and #s.id<=48,'ui stable id required')
    assert(not records[s.id],'duplicate ui id')
    local kind=s.type or 'column';assert(constructors[kind],'unsupported ui component')
    if kind=='weather' then weatherCount=weatherCount+1;assert(weatherCount<=16 and type(hp_ui_weather_effect)=='function','weather effect budget per app') end
    local parentKind=parent and records[parent].kind
    if kind=='item' then assert(parentKind=='list','item requires list') end
    if kind=='tab' then assert(parentKind=='tabs','tab requires tabs') end
    if parentKind=='tabs' then assert(kind=='tab','tabs require tab children') end
    if parentKind=='list' then assert(kind=='item','list requires item children') end
    local previous=nodes[s.id]
    if previous then assert(previous.parent==parent,'cross-parent IDs require remove then insert') end
    local reusable=previous and previous.kind==kind and not replaceParent
    local n={id=s.id,parent=parent,kind=kind,props=properties(s),children={},previous=reusable and previous or nil}
    for _,key in ipairs({'on_click','on_change','on_scroll','on_select'}) do
      if s[key]~=nil then assert(type(s[key])=='string' and #s[key]<=96,'ui callback name');n[key]=s[key] end
    end
    records[n.id]=n;ordered[#ordered+1]=n
    if s.children~=nil then
      assert(type(s.children)=='table' and (containers[kind] or kind=='list' or kind=='tabs'),'invalid ui children')
      assert(#s.children<=256,'ui child budget')
      for index,child in ipairs(s.children) do
        visit(child,n.id,depth+1,previous and not reusable)
        n.children[#n.children+1]=child.id;records[child.id].index=index-1
      end
    end
  end
  visit(spec,nil,1,false)
  local removed={}
  for i=#plan,1,-1 do local old=plan[i]
    if not records[old.id] or records[old.id].previous~=old then removed[#removed+1]=old end
  end
  assert(#retired+#removed<=256,'ui mutation queue full; wait for ui.ready()')
  local nextNodes,nextPlan={},{}
  for _,record in ipairs(ordered) do
    local n=record.previous or {id=record.id,kind=record.kind,parent=record.parent,applied={}}
    n.pending=n.pending or {}
    for k,v in pairs(record.props) do if v~=n.applied[k] then n.pending[k]=v end end
    if n.object and not next(n.pending) then n.pending=nil end
    n.desired=n.desired or {};for k,v in pairs(record.props) do n.desired[k]=v end
    n.children=record.children
    for _,key in ipairs({'on_click','on_change','on_scroll','on_select'}) do
      if n[key]~=record[key] then n.bindDirty=true end;n[key]=record[key]
    end
    if n.index~=(record.index or 0) then n.move=true end
    n.index=record.index or 0
    nextNodes[n.id]=n;nextPlan[#nextPlan+1]=n
  end
  for _,n in ipairs(removed) do retired[#retired+1]=n end
  nodes,plan,rootId=nextNodes,nextPlan,spec.id
  hp_ui_request_work()
end
ui={version=3,minor_version=1}
function ui.mount(spec)
  assert_ui();assert(not mounted,'ui already mounted; use ui.patch')
  reconcile(spec);mounted=true
end
function ui.patch(id,props)
  assert_ui();local n=assert(nodes[id],'unknown ui id')
  local values=properties(props);n.pending=n.pending or {}
  for k,v in pairs(values) do n.desired[k]=v;if n.applied[k]~=v then n.pending[k]=v else n.pending[k]=nil end end
  if not next(n.pending) then n.pending=nil else hp_ui_request_work() end
end
function ui.insert(parentId,index,spec)
  assert_ui();local parent=assert(nodes[parentId],'unknown parent')
  assert(type(index)=='number' and index%1==0 and index>=1 and index<=#parent.children+1,'ui insert index is 1-based')
  assert(type(spec)=='table' and not nodes[spec.id],'duplicate ui id')
  local tree=snapshot(rootId)
  local function find(s) if s.id==parentId then return s end;for _,c in ipairs(s.children or {}) do local found=find(c);if found then return found end end end
  local p=find(tree);p.children=p.children or {};table.insert(p.children,index,spec);reconcile(tree)
end
function ui.remove(id)
  assert_ui();assert(nodes[id],'unknown ui id')
  assert(id~=rootId,'cannot remove mounted root')
  local tree=snapshot(rootId)
  local function remove(s)
    for i,c in ipairs(s.children or {}) do
      if c.id==id then table.remove(s.children,i);return true end
      if remove(c) then return true end
    end
  end
  remove(tree);reconcile(tree)
end
function ui.updateChildren(parentId,specs)
  assert_ui();assert(nodes[parentId],'unknown parent');assert(type(specs)=='table' and #specs<=256,'ui child budget')
  local tree=snapshot(rootId)
  local function update(s)
    if s.id==parentId then s.children=specs;return true end
    for _,c in ipairs(s.children or {}) do if update(c) then return true end end
  end
  update(tree);reconcile(tree)
end
)lua" R"lua(
function ui.read(id)
  assert_ui();local n=assert(nodes[id],'unknown ui id')
  if not n.object then return nil end
  if n.kind=='input' then return hp_ui_textarea_get(n.object) end
  if n.kind=='switch' then return hp_checked(n.object) end
  if n.kind=='dropdown' then return hp_ui_dropdown_value(n.object) end
end
function ui.setItems(id,offset,items,total)
  assert_ui();local n=assert(nodes[id],'unknown ui id');assert(n.kind=='virtual_list','virtual list required')
  assert(type(offset)=='number' and offset%1==0 and offset>=0 and type(total)=='number' and total%1==0 and total>=0 and total<=1000000,'list bounds')
  assert(type(items)=='table' and #items<=96 and offset+#items<=total,'list page budget')
  local copy,ids={},{}
  for i,item in ipairs(items) do
    assert(type(item)=='table' and type(item.id)=='string' and #item.id>0 and #item.id<=96 and not ids[item.id],'list stable id')
    assert(item.text==nil or type(item.text)=='string' and #item.text<=256,'list text limit')
    assert(item.enabled==nil or type(item.enabled)=='boolean','list enabled')
    ids[item.id]=true;copy[i]={id=item.id,text=item.text or '',enabled=item.enabled~=false}
  end
  n.listData={offset=offset,items=copy,total=total};hp_ui_request_work()
end
function ui.ready()
  if not mounted or #retired>0 then return false end
  for _,n in ipairs(plan) do if not n.object or n.pending or n.move or n.bindDirty or n.listData or n.monthData or n.chartData or n.cursorData or
    ((n.kind=='virtual_list' or n.kind=='month_grid') and hp_ui_collection_pending(n.object)) then return false end end
  return true
end
function ui.meaningful()
  if not mounted or not nodes[rootId].object then return false end
  for _,n in ipairs(plan) do
    if n.object and not n.pending and (n.kind=='label' or n.kind=='button') and n.desired.text and #n.desired.text>0 and n.desired.priority~='image' and n.desired.priority~='data' then
      local hidden=n.desired.hidden;local p=n.parent;while p do hidden=hidden or nodes[p].desired.hidden;p=nodes[p].parent end
      if not hidden then return true end
    end
  end;return false
end
function ui.setMonth(id,data)
  assert_ui();local n=assert(nodes[id],'unknown ui id');assert(n.kind=='month_grid','month grid required')
  assert(type(data)=='table' and type(data.year)=='number' and data.year%1==0 and data.year>=1900 and data.year<=2199 and type(data.month)=='number' and data.month%1==0 and data.month>=1 and data.month<=12,'month range')
  assert(data.week_start==nil or data.week_start=='monday' or data.week_start=='sunday','month week start')
  local copy={year=data.year,month=data.month,week_start=data.week_start,today=data.today,selected=data.selected,days={}}
  for _,key in ipairs({'today','selected'}) do assert(copy[key]==nil or type(copy[key])=='string' and #copy[key]==10,'month date') end
  assert(data.days==nil or type(data.days)=='table' and #data.days<=42,'month day budget')
  local seen={}
  for i,d in ipairs(data.days or {}) do
    assert(type(d)=='table' and type(d.date)=='string' and #d.date==10 and not seen[d.date],'month day');seen[d.date]=true
    assert(d.count==nil or type(d.count)=='number' and d.count%1==0 and d.count>=0 and d.count<=65535,'month count')
    assert(d.colors==nil or type(d.colors)=='table' and #d.colors<=4,'month color budget')
    local colors={};for j,c in ipairs(d.colors or {}) do assert(type(c)=='number' and c%1==0 and c>=0 and c<=0xffffff,'month color');colors[j]=c end
    copy.days[i]={date=d.date,count=d.count,colors=colors}
  end
  n.monthData=copy;hp_ui_request_work()
end
function ui.percent(value) return hp_ui_percent(value) end
function ui.dialog(title,text) assert_ui();hp_alert(title,text) end
function ui.back() assert_ui();return hp_open_app() end
function ui.chart(id,a,b)
  assert_ui();local n=assert(nodes[id],'unknown ui id');assert(n.kind=='chart','chart required')
  assert(type(a)=='table' and type(b)=='table' and #a<=256 and #b<=256,'chart point budget')
  if advancedCharts then
    local first,second={},{};for i=1,math.min(#a,#b) do first[i]=a[i];second[i]=b[i] end
    if #first>0 then ui.chartSeries(id,1,first,{name='A'});ui.chartSeries(id,2,second,{name='B'}) end
  else n.chartData={a=a,b=b};hp_ui_request_work() end
end
function ui.chartSeries(id,index,values,options)
  assert_ui();local n=assert(nodes[id],'unknown ui id');assert(n.kind=='chart' and advancedCharts,'advanced chart required')
  assert(type(index)=='number' and index%1==0 and index>=1 and index<=4,'chart series limit')
  assert(type(values)=='table' and #values>=1 and #values<=256,'chart point budget')
  options=options or {};assert(type(options)=='table','chart options')
  assert(options.name==nil or type(options.name)=='string' and #options.name<=96,'chart legend')
  assert(options.color==nil or type(options.color)=='number' and options.color%1==0 and options.color>=0 and options.color<=0xffffff,'chart color')
  local copy={};for i,v in ipairs(values) do assert(v==false or type(v)=='number' and v==v and v>=-1000000000 and v<=1000000000,'chart value');copy[i]=v end
  n.chartData=n.chartData or {updates={}};assert(not next(n.chartData.updates) or n.chartData.points==#copy,'chart series must have equal point counts')
  n.chartData.points=#copy;n.chartData.updates[index]={values=copy,name=options.name or ('Serie '..index),color=options.color}
  hp_ui_request_work()
end
function ui.chartCursor(id,labels,suffix)
  assert_ui();local n=assert(nodes[id],'unknown ui id');assert(n.kind=='chart' and type(hp_chart_set_cursor_labels)=='function','chart cursor unavailable')
  assert(type(labels)=='table' and #labels>=1 and #labels<=32,'cursor labels budget')
  suffix=suffix or '';assert(type(suffix)=='string' and #suffix<=16,'cursor suffix budget')
  local copy={};for i,label in ipairs(labels) do assert(type(label)=='string' and #label<=32,'cursor label');copy[i]=label end
  n.cursorData={labels=copy,suffix=suffix};hp_ui_request_work()
end
local function bind(n,key,bridge)
  local input=n.kind=='input' and key=='on_click'
  if (n[key] or input) and not n[key..'_binding'] then
    serial=serial+1;local name='_hp_v31_callback_'..serial
    _G[name]=function(...)
      if nodes[n.id]~=n or not n.object then return end
      if input then
        if not inputKeyboard then inputKeyboard=hp_ui_keyboard(hp_root()) end
        if inputKeyboard then hp_ui_keyboard_bind(inputKeyboard,n.object) end
      end
      local fn=n[key] and _G[n[key]];if type(fn)=='function' then
        if key=='on_scroll' then local _,offset,limit=...;fn(n.id,offset,limit)
        elseif key=='on_select' then local value=...;fn(n.id,value)
        else fn(...) end
      end
    end
    n[key..'_binding']=name;bridge(n.object,name)
  end
end
return function()
  assert_ui()
  if not mounted then return true end
  local start=hp_millis();local work=0
  if not initialized then hp_ui_flex(hp_root(),'column');hp_set_padding(hp_root(),0);initialized=true end
  while work<8 and hp_millis()-start<2 do
    if #retired>0 then
      local n=table.remove(retired,1)
      if n.kind=='input' and inputKeyboard then hp_ui_keyboard_hide(inputKeyboard) end
      if n.object then hp_ui_delete(n.object);n.object=nil end
      for _,key in ipairs({'on_click','on_change','on_scroll','on_select'}) do local name=n[key..'_binding'];if name then _G[name]=nil end end
      work=work+1
    else
      local selected,best
      turn=turn+1
      for _,n in ipairs(plan) do
        if (not n.object or n.pending or n.move or n.bindDirty or n.listData or n.monthData or n.chartData or n.cursorData or
            ((n.kind=='virtual_list' or n.kind=='month_grid') and hp_ui_collection_pending(n.object))) and
            (not n.parent or nodes[n.parent].object) and (n.object or n.kind~='virtual_list' or work<=5) and (n.object or n.kind~='chart' or not advancedCharts or work<=6) then
          local rank=n.desired.priority=='image' and 2 or n.desired.priority=='data' and 1 or
            ((n.kind=='chart' or n.kind=='image') and 2 or n.kind=='virtual_list' and 1 or 0)
          local hidden=n.desired.hidden;local p=n.parent
          while p do hidden=hidden or nodes[p].desired.hidden;p=nodes[p].parent end
          if hidden then rank=rank+3 end
          -- Aging bounds starvation, including hidden and low-priority work.
          local score=rank*16-(turn-(n.lastTurn or 0))
          if not best or score<best then selected=n;best=score end
        end
      end
      if not selected then break end
      local n=selected
      n.lastTurn=turn
      if not n.object then
        local parent=n.parent and nodes[n.parent].object or hp_root()
        local ctor=constructors[n.kind]
        if n.kind=='chart' and advancedCharts then n.object=hp_chart_create(parent,{points=256});n.series={};work=work+1
        elseif n.kind=='weather' then n.object=hp_ui_weather_effect(parent,{type=n.desired.weather_type or 'cloudy',width=n.desired.width or 96,height=n.desired.height or 64,intensity=n.desired.intensity or 40,enabled=n.desired.animated~=false});work=work+3
        elseif n.kind=='switch' then n.object=ctor(parent,false)
        elseif n.kind=='progress' then n.object=ctor(parent,0,100,0)
        else n.object=ctor(parent,n.kind=='dropdown' and (n.desired.options or '') or n.desired.text or '') end
        assert(n.object,'ui allocation failed')
        if n.kind=='input' or n.kind=='dropdown' then hp_ui_style_input(n.object) end
        if n.kind=='virtual_list' then work=work+2 end -- Root, extent and status.
        if containers[n.kind] then
          hp_ui_flex(n.object,n.kind=='row' and 'row' or 'column')
          hp_set_size(n.object,hp_ui_percent(100),hp_ui_content_size());hp_ui_gap(n.object,12)
          if n.kind=='page' then hp_ui_grow(n.object,1) end
        end
        n.move=true
      end
      bind(n,'on_click',hp_on_click);bind(n,'on_change',hp_on_value_changed)
      if n.kind=='month_grid' then
        bind(n,'on_select',hp_ui_month_callback)
        if n.monthData then hp_ui_month_set(n.object,n.monthData);n.monthData=nil end
      end
      if n.kind=='virtual_list' then
        bind(n,'on_scroll',function(o,name) hp_ui_virtual_callbacks(o,name,true) end)
        bind(n,'on_select',function(o,name) hp_ui_virtual_callbacks(o,name,false) end)
        if n.listData then local d=n.listData;hp_ui_virtual_items(n.object,d.offset,d.items,d.total);n.listData=nil end
      end
      n.bindDirty=nil
      if n.move then hp_ui_move(n.object,n.index);n.move=nil end
      apply(n);work=work+1
      if n.kind=='chart' and n.chartData and work<8 then
        local d=n.chartData
        if advancedCharts then
          hp_chart_resize(n.object,d.points)
          local index,update=next(d.updates)
          if index then
            -- Stable indices and legends; create at most one series per step.
            local missing;for i=1,index do if not n.series[i] then missing=i;break end end
            if missing then local spec=d.updates[missing] or {};n.series[missing]=assert(hp_chart_add_series(n.object,spec.name or ('Serie '..missing),spec.color),'chart allocation failed')
            else hp_chart_set_series(n.object,n.series[index],update.values);d.updates[index]=nil end
          end
          if not next(d.updates) then n.chartData=nil end
        else hp_ui_chart_set(n.object,d.a,d.b);n.chartData=nil end
        work=work+1
      end
      if n.kind=='chart' and n.cursorData and not n.chartData and work<8 then
        hp_chart_set_cursor_labels(n.object,n.cursorData.labels,n.cursorData.suffix);n.cursorData=nil;work=work+1
      end
      if (n.kind=='virtual_list' or n.kind=='month_grid') and work<8 then
        local used,_,offset=hp_ui_collection_step(n.object,8-work);work=work+used
        -- Invoke only after the C++ renderer has returned. Removing this
        -- widget from its page callback cannot invalidate a native stack frame.
        if offset and n.on_scroll_binding then _G[n.on_scroll_binding]('',offset,96) end
      end
    end
  end
  return ui.ready(),ui.meaningful()
end
)lua";
}

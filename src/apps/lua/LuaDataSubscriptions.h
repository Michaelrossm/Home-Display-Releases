#pragma once
namespace hp {
// Adapter of existing provider caches and owner-scoped jobs, not a provider or
// polling engine. This module is private to each Lua state and allocates no task.
inline constexpr char kDataSubscriptions[]=R"lua(
local fg=hp_ui_assert_foreground
local watches,order={},{}
local function notify(w,urgent)
  local now=hp_millis()
  if not urgent and w.interval>0 and w.lastNotify and now-w.lastNotify<w.interval then w.pendingNotify=true;return end
  w.pendingNotify=false;w.lastNotify=now
  for _,listener in ipairs(w.callbacks) do
    local callback=_G[listener.name];if type(callback)=='function' then callback(listener.id,w.state) end
  end
end
local function accept(w,value,error)
  if type(value)=='table' and value.api_version then
    error=error or value.error or value.error_code
    value=value.ok and value.data or nil
  end
  error=error or (value and value.error) or (not value and 'snapshot_unavailable') or ''
  if error=='' and type(value)=='table' and value.cloud_incomplete then error='calendar_cloud_incomplete' end
  local revision=type(value)=='table' and value.revision or nil
  local fingerprint=revision
  if w.source=='calendar' and type(value)=='table' then fingerprint=string.format('%.0f:%.0f',revision or 0,value.cloud_revision or 0) end
  local offline=value and value.online==false or false
  if w.source=='calendar' and type(hp_network_status)=='function' then offline=hp_network_status().online==false end
  local loading=value and value.pending==true or false
  local urgent=not w.delivered or w.state.error~=error or w.state.offline~=offline or w.state.loading~=loading
  local changed=not w.delivered or w.fingerprint~=fingerprint or w.state.error~=error or
    w.state.offline~=offline or w.state.loading~=loading
  w.delivered=true;w.revision=(w.revision or 0)+(changed and 1 or 0)
  w.fingerprint=fingerprint;w.providerToken=fingerprint
  local timestamp=w.source=='printer' and value and value.timestamp or value and value.updated_ms
  w.state={version=1,value=value or w.state.value,revision=w.revision,provider_revision=revision or 0,
    timestamp_ms=timestamp or hp_millis(),loading=loading,offline=offline,error=error}
  if changed then notify(w,urgent) end
end
data={version=1}
function data.subscribe(id,spec)
  fg();assert(type(id)=='string' and #id>0 and #id<=48 and not watches[id],'invalid subscription id')
  assert(type(spec)=='table' and type(spec.on_change)=='string' and #spec.on_change<=96,'subscription callback')
  assert(#order<8,'subscription limit')
  local source=spec.source;assert(source=='energy' or source=='printer' or source=='calendar','subscription source')
  local interval=spec.min_interval_ms or 0
  assert(type(interval)=='number' and interval%1==0 and interval>=0 and interval<=60000 and (interval==0 or interval>=100),'subscription interval 0 or 100..60000 ms')
  if source=='energy' then assert(type(hp_energy_snapshot)=='function','energy permission required') end
  if source=='printer' then
    assert(type(hp_printer_snapshot_summary)=='function','printer permission required')
    assert(type(spec.profile)=='string' and #spec.profile>0 and #spec.profile<=36,'printer profile required')
  end
  if source=='calendar' then assert(hp_calendar_capabilities().read,'calendar permission required') end
  local encoded=hp_json_encode(spec.query or {});assert(encoded and #encoded<=2048,'query budget')
  local key=source..':'..(spec.profile or '')..':'..encoded..':'..interval
  for _,w in ipairs(order) do if w.key==key then
    assert(#w.callbacks<4,'subscription listener limit');w.callbacks[#w.callbacks+1]={id=id,name=spec.on_change}
    watches[id]=w;return w.state
  end end
  local w={id=id,key=key,source=source,profile=spec.profile,interval=interval,query=hp_json_decode(encoded),
    callbacks={{id=id,name=spec.on_change}},dirty=true,state={version=1,loading=true,offline=false,error='',revision=0,timestamp_ms=0}}
  watches[id]=w;order[#order+1]=w;return w.state
end
function data.snapshot(id) local w=assert(watches[id],'unknown subscription');w.state.age_ms=math.max(0,hp_millis()-w.state.timestamp_ms);return w.state end
function data.refresh(id) fg();local w=assert(watches[id],'unknown subscription');w.dirty=true;w.force=true end
function data.unsubscribe(id)
  fg();local w=watches[id];if not w then return false end;watches[id]=nil
  for i=#w.callbacks,1,-1 do if w.callbacks[i].id==id then table.remove(w.callbacks,i) end end
  if #w.callbacks==0 then
    if w.job then hp_job_cancel(w.job) end
    if w.source=='printer' then hp_printer_watch(w.profile,false) end
    for i=#order,1,-1 do if order[i]==w then table.remove(order,i) end end
  end
  return true
end
local function mark() for _,w in ipairs(order) do w.dirty=true end end
return {
  resume=function()
    for _,w in ipairs(order) do if w.job then hp_job_cancel(w.job);w.job=nil end end
    mark()
  end,
  event=function(name,job)
    for _,w in ipairs(order) do
      if job and w.job==job then
        local result=hp_job_result(job)
        if result and result.done then
          w.job=nil;hp_job_release(job)
          if result.ok then local value,error=hp_json_decode(result.body);accept(w,value,error)
          else accept(w,w.state.value,result.error) end
        end
      elseif name=='network.changed' or (w.source=='energy' and name=='energy.updated') or
        (w.source=='printer' and (name=='printer.status' or name=='printer.profiles')) or
        (w.source=='calendar' and name=='calendar.updated') then w.dirty=true end
    end
  end,
  step=function()
    fg();local work=0
    for _,w in ipairs(order) do if w.dirty and not w.job and work<2 then
      w.dirty=false;work=work+1
      local probe=w.source=='printer' and hp_printer_revision or w.source=='energy' and hp_energy_revision or hp_calendar_revision
      local rev,stamp,pending,online
      if type(probe)=='function' then rev,stamp,pending,online=probe(w.profile) end
      local same=rev~=nil and w.delivered and not w.force and rev==w.providerToken
      w.force=nil
      if same then
        local changed=pending~=nil and pending~=w.state.loading or online~=nil and (not online)~=w.state.offline
        if stamp then w.state.timestamp_ms=stamp end
        if pending~=nil then w.state.loading=pending end;if online~=nil then w.state.offline=not online end
        if changed then w.state.revision=w.state.revision+1;notify(w,true) end
      else
      if w.source=='energy' then local value,error=hp_energy_snapshot();accept(w,value,error)
      elseif w.source=='printer' then hp_printer_watch(w.profile,true);local value,error=hp_printer_snapshot_summary(w.profile);accept(w,value,error)
      else
        w.job=hp_calendar_query(w.query)
        if w.job then w.state.loading=true
        else accept(w,w.state.value,'calendar_job_unavailable') end
      end
      end
    end end
    for _,w in ipairs(order) do if w.pendingNotify and hp_millis()-w.lastNotify>=w.interval then notify(w) end end
  end
}
)lua";
}

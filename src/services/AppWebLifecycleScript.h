#pragma once
namespace hp {
// Browser-owned lifetime only. No firmware task, lease timer or background app.
inline constexpr char appWebLifecycleScript[]=R"JS('use strict';
window.hpWeb=(()=>{
 let closed=false,idle=false,paused=!!document.hidden,epoch=0,idleTimer=null,tail=Promise.resolve(),queued=0;
 const reads=new Set(),resumers=new Set(),periodic=new Set(),idleMs=60000;
 const visible=()=>!closed&&!document.hidden&&!idle;
 function requireVisible(){if(!visible())throw Error('Weboberfläche pausiert. Zum Fortsetzen öffnen und aktualisieren.')}
 function stop(){epoch++;for(const controller of reads)controller.abort();reads.clear();for(const task of periodic){clearTimeout(task.timer);task.timer=null}}
 function transition(){
  if(!visible()){if(!paused){paused=true;stop()}clearTimeout(idleTimer);idleTimer=null;return}
  if(paused){paused=false;for(const fn of resumers)Promise.resolve().then(fn).catch(()=>{});for(const task of periodic)task.start()}
 }
 function activity(){if(closed||document.hidden)return;idle=false;clearTimeout(idleTimer);idleTimer=setTimeout(()=>{idle=true;transition()},idleMs);transition()}
 document.addEventListener('visibilitychange',()=>{if(document.hidden)transition();else activity()});
 for(const event of ['pointerdown','keydown','wheel'])document.addEventListener(event,activity,{passive:true});
 window.addEventListener('pagehide',()=>{closed=true;transition()});
 window.addEventListener('pageshow',()=>{closed=false;activity()});
 window.addEventListener('focus',activity);
 activity();
 async function perform(url,options,requestEpoch){
  requireVisible();if(requestEpoch!==epoch)throw Error('Anfrage vor Standby verworfen. Nicht automatisch wiederholen.');
  if(options.signal?.aborted)throw Error('Anfrage abgebrochen.');
  const method=(options.method||'GET').toUpperCase();
  // Do not pretend to cancel a server-side write after it was submitted.
  if(method!=='GET'&&method!=='HEAD')return fetch(url,options);
  const controller=new AbortController();reads.add(controller);
  const forward=()=>controller.abort();
  if(options.signal){if(options.signal.aborted)controller.abort();else options.signal.addEventListener('abort',forward,{once:true})}
  try{
   const response=await fetch(url,{...options,signal:controller.signal});
   // Consume the bounded body before dropping its cancellation controller.
   const bytes=await response.arrayBuffer();requireVisible();
   return new Response(bytes,{status:response.status,statusText:response.statusText,headers:response.headers});
  }finally{reads.delete(controller);if(options.signal)options.signal.removeEventListener('abort',forward)}
 }
 function request(url,options={}){
  if(!visible())return Promise.reject(Error('Weboberfläche pausiert. Zum Fortsetzen öffnen und aktualisieren.'));
  if(queued>=16)return Promise.reject(Error('Zu viele Anfragen. Bitte kurz warten.'));
  const requestEpoch=epoch;queued++;
  // One bounded request at a time per tab; do not occupy all TLS slots with
  // a single browser's concurrent polling. Writes are never retried.
  const pending=tail.then(()=>perform(url,options,requestEpoch));
  tail=pending.catch(()=>{});return pending.finally(()=>{queued--});
 }
 function onResume(fn){resumers.add(fn);return ()=>resumers.delete(fn)}
 function every(fn,ms){
  const task={timer:null,busy:false,start(){
   if(!visible()||task.busy||task.timer!==null)return;
   task.timer=setTimeout(async()=>{task.timer=null;if(!visible())return;task.busy=true;
    try{await fn()}catch(e){if(visible())console.error(e)}finally{task.busy=false;task.start()}
   },Math.max(1000,ms));
  }};
  periodic.add(task);task.start();return ()=>{clearTimeout(task.timer);periodic.delete(task);task.start=()=>{}};
 }
 function whenVisible(fn){
  if(visible()){Promise.resolve().then(fn).catch(console.error);return}
  const off=onResume(()=>{off();return fn()});
 }
 function upload(url,body,{headers={},signal,onProgress,timeout=120000}={}){
  if(!visible()||signal?.aborted)return Promise.reject(new DOMException('Upload abgebrochen oder pausiert','AbortError'));
  if(queued>=16)return Promise.reject(Error('Zu viele Anfragen.'));const requestEpoch=epoch;queued++;
  const work=tail.then(()=>{requireVisible();if(requestEpoch!==epoch)throw Error('Upload vor Standby verworfen. Nicht automatisch wiederholen.');return new Promise((resolve,reject)=>{
   const xhr=new XMLHttpRequest();let settled=false;
   const finish=(error,value)=>{if(settled)return;settled=true;signal?.removeEventListener('abort',abort);error?reject(error):resolve(value)};
   const abort=()=>xhr.abort();xhr.open('POST',url);xhr.timeout=timeout;
   for(const [key,value] of Object.entries(headers))xhr.setRequestHeader(key,value);
   xhr.upload.onprogress=e=>{if(e.lengthComputable)onProgress?.(e.loaded/e.total)};
   xhr.onload=()=>{if(xhr.status>=200&&xhr.status<300)finish(null,xhr.responseText);else{const error=Error(xhr.responseText||('HTTP '+xhr.status));error.status=xhr.status;finish(error)}};
   xhr.onerror=()=>finish(Error('Netzwerkfehler; Ergebnis prüfen, nicht automatisch wiederholen.'));
   xhr.ontimeout=()=>finish(Error('Zeitlimit; Ergebnis möglicherweise bereits gespeichert. Bitte prüfen.'));
   xhr.onabort=()=>finish(new DOMException('Upload abgebrochen','AbortError'));
   if(signal?.aborted){finish(new DOMException('Upload abgebrochen','AbortError'));return;}signal?.addEventListener('abort',abort,{once:true});xhr.send(body);
  })});tail=work.catch(()=>{});return work.finally(()=>queued--);
 }
 let batchBusy=false;
 async function batch(items,execute,{signal,onResult,max=128}={}){
  if(batchBusy)throw Error('Ein Auftrag läuft bereits.');if(!items.length||items.length>max)throw Error('1 bis '+max+' Dateien auswählen.');
  batchBusy=true;const results=[];let stopped=false;
  try{for(let index=0;index<items.length;++index){if(signal?.aborted||!visible()){stopped=true;break;}
   let result;try{const value=await execute(items[index],index,items.length);result={index,ok:true,value}}catch(error){result={index,ok:false,error};if(error.name==='AbortError'||error.status===401||error.status===403)stopped=true;}
   results.push(result);onResult?.(result,items.length);if(stopped)break;
  }}finally{batchBusy=false;}return {results,stopped};
 }
 let regionCatalog,regionAt=0;
 async function countrySelect(select,value='',{inherit=true}={}){
  const initial=select.value;
  if(Date.now()-regionAt>1000){regionCatalog=null;regionAt=Date.now()}
  try{const catalog=await (regionCatalog??=(async()=>{const r=await request('/api/region',{cache:'no-store'});if(!r.ok)throw Error('Länderliste nicht verfügbar');return r.json()})());
   if(!select.isConnected)return;const current=select.value!==initial?select.value:value;
   const match=catalog.countries.find(c=>c.code===current||c.name===current),country=match?.code||current;
   select.replaceChildren();if(inherit)select.add(new Option('Systemstandard · '+(catalog.countries.find(c=>c.code===catalog.country)?.name||catalog.country),''));
   for(const item of catalog.countries)select.add(new Option(item.name,item.code));
   if(country&&!catalog.countries.some(c=>c.code===country))select.add(new Option('Gespeichert: '+country,country));
   select.value=country|| (inherit?'':catalog.country);
  }catch(error){regionCatalog=null;select.replaceChildren(new Option(value||'Systemstandard',value));throw error}
 }
 return {visible,requireVisible,fetch:request,upload,batch,whenVisible,onResume,every,countrySelect};
})();
)JS";
}

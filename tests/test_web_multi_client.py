from pathlib import Path
import unittest
import test_app_web_idle
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = (ROOT/'src/services/AppWebLifecycleScript.h').read_text(encoding='utf-8').split('R"JS(',1)[1].split(')JS"',1)[0]


class WebMultiClientTests(unittest.TestCase):
    def test_actual_client_counter_and_free_slots_use_same_capacity(self):
        source=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        method='size_t WebServerService::activeClients()'+source.split('size_t WebServerService::activeClients()',1)[1].split('void WebServerService::stop()',1)[0]
        header=(ROOT/'src/services/WebServerService.h').read_text(encoding='utf-8')
        limits='static constexpr size_t socketCapacity()'+header.split('static constexpr size_t socketCapacity()',1)[1].split('private:',1)[0]
        fixture=r'''
#include <cassert>
#include <cstddef>
constexpr int ESP_OK=0;unsigned clients=0;bool failed=false;
int httpd_get_client_list(void*,size_t* count,int*){assert(*count>=clients);if(failed)return 1;*count=clients;return ESP_OK;}
struct WebServerService{void* server_=(void*)1;size_t activeClients()const;
'''+limits+'};\n'+method+r'''
int main(){WebServerService server;for(clients=0;clients<=6;clients++){assert(server.activeClients()==clients);assert(server.freeSockets()==6-clients);}clients=0;failed=true;assert(server.activeClients()==0);server.server_=nullptr;assert(server.activeClients()==0);}
'''
        test_system_performance.SystemPerformanceTests().native(fixture)

    def node(self, code):
        test_app_web_idle.AppWebIdleTests().node(code)

    def test_three_browsers_serialize_their_own_requests(self):
        self.node(r'''
const assert=require('assert'),vm=require('vm');
let active=0,peak=0,calls=0,releases=[];
function fetch(url,options){
 ++calls;++active;peak=Math.max(peak,active);
 return Promise.resolve({status:200,statusText:'OK',headers:{},arrayBuffer:()=>new Promise(resolve=>releases.push(()=>{active--;resolve(new ArrayBuffer(0))}))});
}
function browser(){
 const listeners={},timers=new Map();let id=0;
 const ctx={fetch,AbortController,Response,console,Promise,
  setTimeout:(f,ms)=>{timers.set(++id,{f,ms});return id},clearTimeout:i=>timers.delete(i),
  window:{addEventListener:(e,f)=>listeners[e]=f},
  document:{hidden:false,addEventListener:(e,f)=>listeners[e]=f}};
 vm.createContext(ctx);vm.runInContext('''+repr(SCRIPT)+r''',ctx);return ctx.window.hpWeb;
}
(async()=>{
 const browsers=[browser(),browser(),browser()],requests=[];
 for(const b of browsers)for(let i=0;i<3;i++)requests.push(b.fetch('/status'));
 for(let round=0;round<3;round++){
  await new Promise(setImmediate);assert.equal(active,3);assert.equal(releases.length,3);
  const batch=releases;releases=[];for(const finish of batch)finish();
 }
 await Promise.all(requests);assert.equal(peak,3);assert.equal(calls,9);assert.equal(active,0);
})().catch(e=>{console.error(e);process.exitCode=1});
''')

    def test_standby_cancels_reads_discards_unsent_writes_and_resumes_once(self):
        self.node(r'''
const assert=require('assert'),listeners={},timers=new Map();let timerId=0,calls=[],finish;
global.setTimeout=(f,ms)=>{timers.set(++timerId,{f,ms});return timerId};
global.clearTimeout=id=>timers.delete(id);
global.window={addEventListener:(e,f)=>listeners[e]=f};
global.document={hidden:false,addEventListener:(e,f)=>listeners[e]=f};
global.fetch=(url,options)=>{
 calls.push(url);
 if(url==='/slow')return new Promise((resolve,reject)=>options.signal.addEventListener('abort',()=>reject(Error('aborted'))));
 if(url==='/write')return new Promise(resolve=>{finish=()=>resolve(new Response('{}'))});
 return Promise.resolve(new Response('{}'));
};
'''+SCRIPT+r'''
(async()=>{
 const hp=window.hpWeb;let resumes=0,polls=0;const draft={name:'unsaved'},cookie='unchanged';
 hp.onResume(()=>resumes++);const cancel=hp.every(async()=>{polls++},1500);
 const reading=hp.fetch('/slow');reading.catch(()=>{});await new Promise(setImmediate);
 const unsent=hp.fetch('/must-not-write',{method:'POST'});unsent.catch(()=>{});
 document.hidden=true;listeners.visibilitychange();document.hidden=false;listeners.visibilitychange();
 await assert.rejects(()=>reading);await assert.rejects(()=>unsent);
 await new Promise(setImmediate);assert.deepEqual(calls,['/slow']);assert.equal(resumes,1);
 const writing=hp.fetch('/write',{method:'POST'});await new Promise(setImmediate);
 document.hidden=true;listeners.visibilitychange();finish();await writing;
 assert.equal(calls.filter(v=>v==='/write').length,1);
 assert.equal([...timers.values()].filter(t=>t.ms===1500).length,0);
 document.hidden=false;listeners.visibilitychange();await new Promise(setImmediate);
 assert.equal(resumes,2);assert.equal(draft.name,'unsaved');assert.equal(cookie,'unchanged');
 const idle=[...timers.entries()].find(([id,t])=>t.ms===60000);assert(idle);
 timers.delete(idle[0]);idle[1].f();assert.equal(hp.visible(),false);
 await assert.rejects(()=>hp.fetch('/idle'));assert.equal(polls,0);
 assert.equal([...timers.values()].filter(t=>t.ms===1500).length,0);
 listeners.pointerdown();await new Promise(setImmediate);assert.equal(hp.visible(),true);assert.equal(resumes,3);
 assert.equal([...timers.values()].filter(t=>t.ms===1500).length,1);
 cancel();assert.equal([...timers.values()].filter(t=>t.ms===1500).length,0);
})().catch(e=>{console.error(e);process.exitCode=1});
''')

    def test_shared_socket_budget_and_idle_tls_buffers(self):
        config=(ROOT/'sdkconfig.defaults').read_text(encoding='utf-8')
        self.assertIn('CONFIG_LWIP_MAX_SOCKETS=24',config)
        self.assertIn('CONFIG_MBEDTLS_DYNAMIC_BUFFER=y',config)
        self.assertIn('CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=n',config)
        source=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
        self.assertIn('cfg.max_open_sockets=socketCapacity()',source)
        header=(ROOT/'src/services/WebServerService.h').read_text(encoding='utf-8')
        self.assertIn('socketCapacity() { return 6; }',header)
        self.assertIn('n<socketCapacity()?socketCapacity()-n:0',header)
        diagnostics=(ROOT/'src/services/DiagnosticsService.cpp').read_text(encoding='utf-8')
        self.assertIn('WebServerService::socketCapacity()',diagnostics)
        self.assertNotIn('web80_max_sockets=2',diagnostics)
        self.assertIn('cfg.lru_purge_enable=true',source)
        self.assertNotIn('setInterval(',source)
        portal=(ROOT/'src/services/OAuthWebRoutes.cpp').read_text(encoding='utf-8')
        self.assertIn('hpWeb.every(update,1500)',portal)

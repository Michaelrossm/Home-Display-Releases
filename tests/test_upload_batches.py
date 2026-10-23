from pathlib import Path
import unittest
import test_app_web_idle as browser
import test_system_performance as native

ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8')
SCRIPT=read('src/services/AppWebLifecycleScript.h').split('R"JS(',1)[1].split(')JS"',1)[0]
SETUP=r'''
const assert=require('assert'),listeners={};
const originalTimeout=setTimeout;global.setTimeout=(fn,ms)=>{const t=originalTimeout(fn,ms);t.unref();return t};
global.window={addEventListener:(e,f)=>listeners[e]=f};global.document={hidden:false,addEventListener:(e,f)=>listeners[e]=f};
'''

class UploadBatchTests(unittest.TestCase):
    def test_shared_batch_serializes_continues_errors_and_stops_after_abort(self):
        browser.AppWebIdleTests().node(SETUP+SCRIPT+r'''
(async()=>{const hp=window.hpWeb;let active=0,peak=0,calls=[],rows=[];
 const first=await hp.batch(['a','bad','c'],async(item)=>{active++;peak=Math.max(peak,active);calls.push(item);await new Promise(setImmediate);active--;if(item==='bad')throw Error('invalid');return item},{onResult:r=>rows.push(r)});
 assert.equal(peak,1);assert.deepEqual(calls,['a','bad','c']);assert.deepEqual(first.results.map(r=>r.ok),[true,false,true]);assert.equal(rows.length,3);
 const controller=new AbortController();calls=[];
 const stopped=await hp.batch(['one','two'],async item=>{calls.push(item);controller.abort();return 'committed'},{signal:controller.signal});
 assert.deepEqual(calls,['one']);assert.equal(stopped.results[0].value,'committed');assert(stopped.stopped);
 await assert.rejects(()=>hp.batch(Array(129).fill('x'),async()=>{}));
 let release;const running=hp.batch(['x'],()=>new Promise(resolve=>release=resolve));await Promise.resolve();await assert.rejects(()=>hp.batch(['other'],async()=>{}));release();await running;
 document.hidden=false;const hidden=await hp.batch(['x','y'],async()=>{document.hidden=true;return true});assert.equal(hidden.results.length,1);assert(hidden.stopped);
})().catch(e=>{console.error(e);process.exitCode=1});
''')

    def test_upload_progress_abort_and_no_parallel_socket_with_reads(self):
        browser.AppWebIdleTests().node(SETUP+r'''
let active=0,peak=0,requests=[],held;
global.fetch=async()=>{active++;peak=Math.max(peak,active);return {status:200,statusText:'OK',headers:{},arrayBuffer:()=>new Promise(resolve=>held=()=>{active--;resolve(new ArrayBuffer(0))})}};
global.XMLHttpRequest=class {
 constructor(){this.upload={};this.headers={}}open(method,url){this.url=url}setRequestHeader(k,v){this.headers[k]=v}
 send(body){requests.push(this);active++;peak=Math.max(peak,active);if(this.url==='/hold')return;this.upload.onprogress?.({lengthComputable:true,loaded:5,total:10});active--;this.status=this.url==='/bad'?400:200;this.responseText=this.status===200?'OK':'bad_file';setImmediate(()=>this.onload())}
 abort(){if(active)active--;this.onabort()}
};
'''+SCRIPT+r'''
(async()=>{const hp=window.hpWeb,progress=[];
 const reading=hp.fetch('/status');await new Promise(setImmediate);
 const upload=hp.upload('/upload',new Uint8Array(10),{headers:{'X-HP-CSRF':'token'},onProgress:f=>progress.push(f)});await new Promise(setImmediate);assert.equal(requests.length,0);held();await reading;await upload;assert.equal(peak,1);assert.deepEqual(progress,[.5]);assert.equal(requests[0].headers['X-HP-CSRF'],'token');
 const abort=new AbortController();const pending=hp.upload('/hold',new Uint8Array(10),{signal:abort.signal});await new Promise(setImmediate);abort.abort();await assert.rejects(()=>pending,e=>e.name==='AbortError');
 const bad=await hp.batch(['/upload','/bad','/upload'],url=>hp.upload(url,new Uint8Array(1)));assert.deepEqual(bad.results.map(r=>r.ok),[true,false,true]);assert.equal(active,0);
})().catch(e=>{console.error(e);process.exitCode=1});
''')

    def test_real_install_transfer_lease_blocks_other_browser_and_wraps(self):
        native.SystemPerformanceTests().native(read('src/services/InstallerTransferLease.h')+r'''
#include <cassert>
int main(){hp::InstallerTransferLease lease;const char* a="aaaaaaaaaaaaaaaa",*b="bbbbbbbbbbbbbbbb";
 assert(!lease.claim("app","short",1));assert(lease.claim("app",a,1));assert(!lease.claim("app",b,2)&&!lease.claim("other",a,2));
 assert(!lease.release("app",b)&&!lease.release("other",a));assert(lease.claim("app",a,3));assert(lease.release("app",a));
 assert(lease.claim("other",b,4));assert(!lease.claim("app",a,100));assert(lease.claim("app",a,120005));assert(lease.release("app",a));
 assert(lease.claim("wrap",a,UINT32_MAX-100));assert(!lease.claim("another",b,50));assert(lease.claim("another",b,120000));
}
''')

    def test_app_batch_runs_same_installer_once_per_file_and_continues(self):
        source=read('src/services/WebServerService.cpp')
        function='async function installAppBatch('+source.split('async function installAppBatch(',1)[1].split('function bindAppInstaller(',1)[0]
        browser.AppWebIdleTests().node(SETUP+SCRIPT+r'''
const elements={};const $=id=>elements[id]??={disabled:false,value:'',checked:false,textContent:'',append(){},replaceChildren(){}};
document.createElement=()=>({textContent:''});let preparedAppZip=null,appZipFiles=[{name:'weather.zip'},{name:'bad.zip'},{name:'radio.zip'}],appZipBatchRunning=false,appZipController=null,lastAppZipError='broken ZIP';
let calls=[],active=0,peak=0;const appInstallProgress=()=>{};
async function prepareAppZip(file){if(file.name==='bad.zip')return null;return {id:file.name,mode:'Neuinstallation'}}
async function installPreparedApp(){active++;peak=Math.max(peak,active);calls.push('install');await new Promise(setImmediate);active--;return 'Installed'}
const hpWeb=window.hpWeb;
'''+function+r'''
(async()=>{await installAppBatch();assert.equal(calls.length,2);assert.equal(peak,1);assert.equal(appZipFiles.length,0);assert(!appZipBatchRunning&&!appZipController);assert.equal(elements.appZipStop.disabled,true);assert.match(elements.appZipStatus.textContent,/2 erfolgreich; 1 fehlgeschlagen/);})().catch(e=>{console.error(e);process.exitCode=1});
''')

    def test_photo_batch_uses_existing_atomic_endpoint_and_retains_successes(self):
        page=read('src/services/PhotoWebPage.h');script=page.split("<script>'use strict';",1)[1].split('</script>',1)[0]
        browser.AppWebIdleTests().node(SETUP+r'''
const elements={},rows=[];
document.getElementById=id=>elements[id]??={value:'',checked:false,disabled:false,options:[],style:{},append(row){rows.push(row.textContent)},replaceChildren(){rows.length=0},click(){}};
document.querySelectorAll=()=>[];document.createElement=()=>({append(){},textContent:''});document.createTextNode=x=>x;
'''+SCRIPT+r'''
const hpWeb=window.hpWeb;hpWeb.whenVisible=()=>{};
'''+script+r'''
(async()=>{
 token='csrf';$('target').value='usb';$('album').value='';let writes=[],callbacks;
 const actualBatch=hpWeb.batch;hpWeb.batch=async(...args)=>{const result=await actualBatch(...args);callbacks=result;return result};
 hpWeb.upload=async(url,body,options)=>{if(url==='/photos/upload'){writes.push([url,options.headers['X-Path'],options.headers['X-HP-CSRF']]);options.onProgress?.(.5);if(decodeURIComponent(options.headers['X-Path']).includes('bad.jpg')){const e=Error('disk');e.status=500;throw e}}return 'OK'};
 choosePhotos([{name:'one.jpg',size:10},{name:'bad.jpg',size:10},{name:'three.png',size:10}]);await $('upload').onclick();
 assert.equal(writes.length,3);assert(writes.every(x=>x[0]==='/photos/upload'&&x[2]==='csrf'));assert.deepEqual(callbacks.results.map(r=>r.ok),[true,false,true]);assert.equal(photoFiles.length,0);assert.equal($('stopUpload').disabled,true);assert.match($('status').textContent,/2 gespeichert; 1 fehlgeschlagen/);
})().catch(e=>{console.error(e);process.exitCode=1});
''')
        transfer=read('src/services/FileTransferService.cpp')
        self.assertIn('fs->rename(temp,rel)',transfer)
        self.assertIn('if(!ok)fs->remove(temp)',transfer)
        self.assertIn('507 Insufficient Storage',transfer)
        self.assertIn('notifyMediaChanged()',transfer)

    def test_commit_is_not_cancelled_or_repeated_and_cleanup_is_owner_scoped(self):
        source=read('src/services/WebServerService.cpp')
        self.assertIn("signal:url==='/apps/lua/commit'?undefined:appZipController?.signal",source)
        self.assertEqual(source.count("await uploadPart('/apps/lua/commit'"),1)
        self.assertIn("'X-Install-Release':'1'",source)
        self.assertIn("if(p.mode==='Downgrade')",source)
        transfer=read('src/services/FileTransferService.cpp')
        for function in ('appsLuaFileHandler','appsLuaCommitHandler','appsLuaCancelHandler'):
            body=transfer.split('esp_err_t FileTransferService::'+function,1)[1].split('\nesp_err_t ',1)[0]
            self.assertIn('luaTransferLease.claim(appId,owner,millis())',body)

if __name__=='__main__':unittest.main()

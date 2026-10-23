import json
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


class FirmwareWebUiTests(unittest.TestCase):
    def run_ui(self, body):
        source=(ROOT/'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        script=source[source.index("const ff=document.getElementById('fwFile')"):source.index("const lf=document.getElementById('luaFile')")]
        harness=r'''
const assert=require('node:assert/strict');
const elements=new Map();
globalThis.document={getElementById(id){if(!elements.has(id))elements.set(id,{disabled:false,style:{},classList:{toggle(){}},value:'',checked:false,textContent:''});return elements.get(id)}};
let confirmResult=true;globalThis.window={addEventListener(){},confirm(){return confirmResult}};
globalThis.location={href:''};const CSRF='test-csrf';
function uiP(bar,status,value,message){status.textContent=message;bar.value=value}
function bindDrop(){}
Object.defineProperty(globalThis,'crypto',{value:{subtle:{async digest(_,bytes){return new Uint8Array(32).fill(new Uint8Array(bytes)[0]).buffer}}}});
function file(name='firmware.bin',value=7,size=2048){const b=new Blob([new Uint8Array(size).fill(value)]);b.name=name;return b}
const meta={ok:true,relation:'newer',version:'1.2.3',runningVersion:'1.2.2',project:'home_display',chipId:18,maxBytes:9437184,pendingValidation:false};
function response(value=meta,status=200){return {ok:status>=200&&status<300,status,async json(){return value}}}
let calls=0;globalThis.fetch=async()=>{++calls;return response()};
let uploads=0,uploadReply=null;
async function xhrUpload(url,data,headers,progress){++uploads;assert.equal(url,'/firmware/upload');assert.equal(headers['X-SHA256'],fwHash);progress(1);return uploadReply??JSON.stringify({ok:true,slot:'ota_0',sha256:fwHash})}
'''
        code=harness+script+'\n(async()=>{'+body+'})().catch(e=>{console.error(e);process.exit(1)});'
        node=shutil.which('node')
        self.assertIsNotNone(node)
        result=subprocess.run([node,'-'],input=code,text=True,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_actual_file_validation_and_stale_inspection(self):
        self.run_ui(r'''
await fwInspect(file('firmware.factory.bin'));assert.equal(calls,0);assert.equal(firmware,null);
globalThis.fetch=async()=>response({},401);await fwInspect(file());assert.ok(fs.textContent.includes('erneut anmelden'));
globalThis.fetch=async()=>response({ok:false,error:'wrong_chip_revision'},400);await fwInspect(file());assert.ok(fs.textContent.includes('Hardware-Revision'));
globalThis.fetch=async()=>response({...meta,maxBytes:1024});await fwInspect(file());assert.equal(firmware,null);
globalThis.fetch=async()=>response({...meta,pendingValidation:true});await fwInspect(file());assert.equal(firmware,null);
const pending=[];globalThis.fetch=()=>new Promise(resolve=>pending.push(resolve));
const a=file('a.bin',1),b=file('b.bin',2);const first=fwInspect(a),second=fwInspect(b);
pending[1](response());await second;pending[0](response());await first;
assert.equal(firmware,b);assert.equal(fwHash,'02'.repeat(32));assert.equal(fu.disabled,false);
assert.ok(fm.textContent.includes('b.bin'));assert.ok(!fm.textContent.includes('a.bin'));
''')

    def test_actual_confirmation_upload_response_and_restart_failure(self):
        self.run_ui(r'''
await fwInspect(file());confirmResult=false;await fu.onclick();assert.equal(uploads,0);
confirmResult=true;await fu.onclick();assert.equal(uploads,1);assert.equal(fwReady,true);assert.equal(fr.disabled,false);assert.equal(ff.disabled,true);
await fu.onclick();assert.equal(uploads,1);
globalThis.fetch=async()=>response({},409);await fr.onclick();assert.equal(fwBusy,false);assert.equal(location.href,'');assert.ok(fs.textContent.includes('409'));
fwReady=false;await fwInspect(file()); // blocked by deliberate HTTP conflict
globalThis.fetch=async()=>response();await fwInspect(file());uploadReply='{}';await fu.onclick();
assert.equal(fwReady,false);assert.equal(firmware,null);assert.equal(fu.disabled,true);assert.equal(ff.value,'');assert.ok(fs.textContent.includes('Nicht automatisch'));
''')

    def test_backend_inspection_exposes_limits_and_rejects_wrong_revision(self):
        code=(ROOT/'src/services/FileTransferService.cpp').read_text(encoding='utf-8')
        self.assertIn('bootloader_common_check_chip_revision_validity(image, true)',code)
        self.assertIn('memchr(app.project_name',code)
        inspect=code.split('esp_err_t FileTransferService::firmwareInspectHandler')[1].split('esp_err_t FileTransferService::firmwareUploadHandler')[0]
        self.assertIn('webAdminAuthorized',inspect)
        self.assertIn('maxBytes',inspect)
        self.assertIn('pendingValidation',inspect)
        self.assertNotIn('esp_ota_begin',inspect)

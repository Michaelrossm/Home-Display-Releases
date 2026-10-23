from pathlib import Path
import shutil
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[1]

class AppWebIdleTests(unittest.TestCase):
    def node(self,code,check=False):
        node=shutil.which('node');self.assertIsNotNone(node)
        result=subprocess.run([node]+(['--check'] if check else []),input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_all_browser_scripts_parse_and_share_lifecycle(self):
        for file in ['WebServerService.cpp','CalendarWebPage.h','IntercomWebPage.h']:
            source=(ROOT/'src/services'/file).read_text(encoding='utf-8')
            self.assertIn('src="/app-web-lifecycle.js"',source)
            script=source.split('<script>',1)[1].split('</script>',1)[0]
            self.node(script,True)
            self.assertIn('hpWeb.fetch',script)
            self.assertIn('hpWeb.whenVisible',script)

    def test_closed_and_hidden_pages_do_not_fetch_or_replay_writes(self):
        script=(ROOT/'src/services/AppWebLifecycleScript.h').read_text().split('R"JS(',1)[1].split(')JS"',1)[0]
        self.node(script,True)
        self.node("""
const assert=require('assert');const listeners={};let calls=[],running;
const originalTimeout=setTimeout;global.setTimeout=(fn,ms)=>{const t=originalTimeout(fn,ms);t.unref();return t};
global.window={addEventListener:(name,fn)=>listeners[name]=fn};
global.document={hidden:false,addEventListener:(name,fn)=>listeners[name]=fn,removeEventListener:(name)=>delete listeners[name]};
global.fetch=(url,options)=>{calls.push([url,options]);if(url==='/slow')return new Promise((resolve,reject)=>{running=()=>resolve(new Response('{}'));options.signal.addEventListener('abort',()=>reject(Error('aborted')))});return Promise.resolve(new Response('{}'))};
"""+script+"""
(async()=>{
 const hp=window.hpWeb;
 document.hidden=true;listeners.visibilitychange();await assert.rejects(()=>hp.fetch('/unused'));assert.equal(calls.length,0);
 let boot=0;hp.whenVisible(()=>boot++);assert.equal(boot,0);
 document.hidden=false;listeners.visibilitychange();await Promise.resolve();assert.equal(boot,1);
 // Re-register visibility listener after the one-shot initial-load listener.
 document.hidden=false;await hp.fetch('/read');assert.equal(calls.length,1);
 const reading=hp.fetch('/slow');await new Promise(setImmediate);listeners.pagehide();await assert.rejects(()=>reading);
 await assert.rejects(()=>hp.fetch('/closed'));assert.equal(calls.length,2);
 listeners.pageshow();document.hidden=false;
 await hp.fetch('/write',{method:'POST'});assert.equal(calls.length,3);
 listeners.pagehide();listeners.pageshow();assert.equal(calls.length,3);
 document.hidden=true;await assert.rejects(()=>hp.fetch('/write',{method:'POST'}));assert.equal(calls.length,3);
})().catch(e=>{console.error(e);process.exitCode=1});
""")

    def test_static_vector_icons_used_on_home_and_dock(self):
        for file,token in [('phonebook/PhonebookApp.h','@contacts'),('intercom/IntercomApp.h','@intercom')]:
            self.assertIn(token,(ROOT/'src/apps'/file).read_text())
        shell=(ROOT/'src/ui/UiShell.cpp').read_text()
        self.assertIn('createAppIcon(b,symbol,32',shell)
        self.assertIn('createAppIcon(button',shell)
        icon=(ROOT/'src/ui/NativeAppIcon.h').read_text()
        self.assertIn('LV_EVENT_DRAW_MAIN',icon)
        self.assertNotIn('lv_timer_create',icon)
        self.assertNotIn('lv_anim_start',icon)

if __name__=='__main__':unittest.main()

from pathlib import Path
from html.parser import HTMLParser
import shutil
import subprocess
import unittest
import test_system_performance as native

ROOT=Path(__file__).resolve().parents[1]

class PhotoViewerTests(unittest.TestCase):
    def test_zoom_is_bounded_and_continues_after_finger_release(self):
        native.SystemPerformanceTests().native((ROOT/'src/apps/photos/PhotoZoom.h').read_text()+r'''
#include <cassert>
int main(){hp::PhotoZoom z;assert(z.update(100)==256);assert(z.update(200)==512);
 assert(z.update(1000)==1024);assert(z.update(0)==1024);
 assert(z.update(200)==1024);assert(z.update(100)==512);assert(z.update(1)==512);
 assert(z.update(100)==512);assert(z.update(20)==256);z.reset();assert(z.scale==256&&z.span==0);
 for(unsigned i=0;i<65535;++i){auto scale=z.update(i);assert(scale>=256&&scale<=1024);}
}
''')

    def test_library_form_has_no_time_validation_or_schedule_patch(self):
        page=(ROOT/'src/services/PhotoWebPage.h').read_text(encoding='utf-8')
        html=page.split('R"PHOTOS(',1)[1].split(')PHOTOS"',1)[0]
        class Forms(HTMLParser):
            def __init__(self):super().__init__();self.form=None;self.fields={}
            def handle_starttag(self,tag,attrs):
                a=dict(attrs)
                if tag=='form':self.form=a['id'];self.fields[self.form]=[]
                if tag=='input' and self.form:self.fields[self.form].append(a)
            def handle_endtag(self,tag):
                if tag=='form':self.form=None
        forms=Forms();forms.feed(html)
        self.assertFalse(any(x.get('type')=='time' for x in forms.fields['libraryForm']))
        self.assertTrue(any(x.get('id')=='automaticFolders' and 'checked' in x for x in forms.fields['libraryForm']))
        script=html.split('<script>\'use strict\';',1)[1].split('</script>',1)[0]
        code=r'''
const assert=require('assert'),elements={};
const document={getElementById(id){return elements[id]??={value:'',checked:false,options:[],append(){}}},querySelectorAll(){return []},createElement(){return {append(){}}},createTextNode(x){return x}};
const hpWeb={whenVisible(){}};
'''+script+r'''
let patch;run=fn=>fn();save=async p=>{patch=p};
$('automaticFolders').checked=true;$('folders').value='usb:/Fotos';$('sortDate').value='date';$('descending').value='true';
$('renderEdge').value='1280';
elements.start={value:''};elements.end={value:''};
elements.libraryForm.onsubmit({preventDefault(){}});
assert.deepEqual(patch,{automaticFolders:true,recursive:false,sortDate:true,descending:true,folders:['usb:/Fotos'],renderEdge:1280});
'''
        run=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(run.returncode,0,run.stderr)

    def test_fullscreen_lifecycle_icons_and_existing_touch_path(self):
        app=(ROOT/'src/apps/photos/PhotoApp.cpp').read_text(encoding='utf-8')
        shell=(ROOT/'src/ui/UiShell.cpp').read_text(encoding='utf-8')
        display=(ROOT/'src/hardware/DisplayManager.cpp').read_text(encoding='utf-8')
        self.assertIn('setAppFullscreen(p.host,p.fullscreen)',app)
        self.assertIn('setAppFullscreen(p->host,false)',app)
        self.assertIn('controls(p,false)',app)
        self.assertIn('viewerTap,LV_EVENT_SHORT_CLICKED',app)
        self.assertIn('LV_ALIGN_TOP_RIGHT,-18,18',app)
        self.assertIn('drawHeart,LV_EVENT_DRAW_MAIN',app)
        self.assertIn('toggleFavorite(p.displayedUri)',app)
        self.assertIn('LV_IMAGE_ALIGN_CENTER',app) # CONTAIN would ignore lv_image_set_scale
        self.assertIn('setHiddenIfChanged(top_,enabled)',shell)
        self.assertIn('setHiddenIfChanged(edge_,enabled)',shell)
        self.assertIn('callbacks.custom_touch_read=readTouchWithPinch',display)
        self.assertEqual(display.count('esp_lcd_touch_read_data('),1)
        self.assertNotIn('xTaskCreate',display+app)
        self.assertIn('next==PhotoView::Viewer||next==PhotoView::Frame',app)
        self.assertIn('p.interval=input(row,String(s.interval).c_str(),p,false)',app)
        self.assertNotIn('lv_dropdown_get_selected(p.interval)',app)
        self.assertIn('p.nextAt=p.pictureSince+uint32_t(service.settings().interval)*1000',app)

if __name__=='__main__':unittest.main()

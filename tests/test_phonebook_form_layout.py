from pathlib import Path
import subprocess,shutil,re,unittest
ROOT=Path(__file__).resolve().parents[1]
class PhonebookFormTests(unittest.TestCase):
    def test_actual_birthday_and_save_reads_current_dom(self):
        page=(ROOT/'src/services/PhonebookWebPage.h').read_text(encoding='utf-8')
        script=re.search(r'<script>(.*?)</script>',page,re.S).group(1)
        helpers=script.split('function formatContactBirthday(',1)[1].split('function values(',1)[0]
        code="const assert=require('assert');\nfunction formatContactBirthday("+helpers+r'''
function row(label,value){return {querySelector:s=>({value:s.includes('label')?label:value})}}
let draft={firstName:'Old',emails:[],phones:[],addresses:[]};
const basic=[{dataset:{key:'firstName'},value:'Michael'},{dataset:{key:'lastName'},value:'Mustermann'},{dataset:{key:'birthday'},value:'19961224'}];
const nodes={basic:{querySelectorAll:()=>basic},assignment:{value:'00000001'},phones:{querySelectorAll:()=>[row('Privat','+49123')]},emails:{querySelectorAll:()=>[row('Arbeit','m@example.org')]},addresses:{querySelectorAll:()=>[{querySelectorAll:()=>[{dataset:{key:'label'},value:'Privat'},{dataset:{key:'street'},value:'Hauptstraße'},{dataset:{key:'houseNumber'},value:'12a'}]}]}};
const $=id=>nodes[id];
assert.equal(formatContactBirthday('19961224'),'1996-12-24');
assert.equal(formatContactBirthday('--0229'),'--02-29');
assert.equal(formatContactBirthday('199612'),'1996-12');
const saved=contactForSave();assert.equal(saved.birthday,'1996-12-24');assert.equal(saved.firstName,'Michael');
assert.deepEqual(saved.emails,[{label:'Arbeit',value:'m@example.org'}]);assert.equal(saved.addresses[0].houseNumber,'12a');
assert.equal(draft.firstName,'Old');
nodes.emails.querySelectorAll=()=>[row('m@example.org','')];assert.deepEqual(contactForSave().emails,[{label:'Privat',value:'m@example.org'}]);
nodes.emails.querySelectorAll=()=>[row('Unvollständige Eingabe','')];assert.throws(contactForSave,/E-Mail-Adresse/);
nodes.emails.querySelectorAll=()=>[row('Privat','')];assert.equal(contactForSave().emails.length,0);
'''
        result=subprocess.run([shutil.which('node')],input=code,text=True,encoding='utf-8',capture_output=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_layout_and_immediate_fields(self):
        page=(ROOT/'src/services/PhonebookWebPage.h').read_text(encoding='utf-8')
        self.assertIn("$('detail').prepend(connectDetails)",page)
        self.assertNotIn("$('detail').before(connectDetails)",page)
        for key in ('phones','emails','addresses'):
            self.assertIn('if(!draft.'+key+'.length)',page)
        self.assertIn("bindBirthday(input)",page)
        self.assertIn("'E-Mail-Adresse':'Telefonnummer'",page)
        self.assertIn('minmax(180px,1fr)',page)

if __name__=='__main__':unittest.main()

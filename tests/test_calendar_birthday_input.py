from pathlib import Path
import unittest
import test_system_performance

ROOT = Path(__file__).resolve().parents[1]

class BirthdayInputTests(unittest.TestCase):
    def test_native_input(self):
        code = '#include "BirthdayInput.h"\n' + r'''
#include <cassert>
int main(){
 using namespace hp::calendar;
 assert(birthdayInput("19961224",false)=="1996-12-24");
 assert(birthdayInput("24121996",true)=="24-12-1996");
 assert(birthdayInput("1996-12-24",false)=="1996-12-24");
 assert(birthdayInput("1996",false)=="1996");
 assert(birthdayInput("19961",false)=="1996-1");
 assert(birthdayText(1996,12,24,true)=="24-12-1996");
 assert(birthdayText(0,2,29,false)=="0000-02-29");
 int y=0,m=0,d=0;
 assert(parseBirthday("24-12-1996",true,y,m,d)&&y==1996&&m==12&&d==24);
 assert(parseBirthday("1996-12-24",false,y,m,d)&&y==1996&&m==12&&d==24);
 assert(!parseBirthday("19961224",false,y,m,d));
 assert(!parseBirthday("1996-xx-24",false,y,m,d));
}
'''
        test_system_performance.SystemPerformanceTests.native(self, code, [ROOT/'src/apps/calendar'])

    def test_preferences_and_feedback(self):
        page=(ROOT/'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        self.assertIn("$('savePerson').onclick",page)
        self.assertIn("$('sync').onclick",page)
        self.assertIn('result.accounts.filter(a=>a.requested)',page)
        self.assertIn('syncFeedback.textContent',page)
        self.assertIn("job('person_contacts'",page)
        self.assertIn("job('person_preferences'",page)

if __name__=='__main__': unittest.main()

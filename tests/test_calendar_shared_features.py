from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
class SharedFeatureTests(unittest.TestCase):
    def test_web_features_not_personal_calendar_slots(self):
        source=(ROOT/'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
        self.assertIn("agendaCalendars.filter(c=>c.kind!==3&&c.kind!==5)",source)
        self.assertIn('sharedFeatures',source)
        self.assertIn('Gemeinsame Funktionen für alle',source)
        self.assertIn("if(c.kind!==3&&c.kind!==5)$('eventFilter')",source)

    def test_native_separate_features_and_compatible_storage(self):
        source=(ROOT/'src/apps/calendar/CalendarController.cpp').read_text(encoding='utf-8')
        self.assertIn('features.push_back(c);else calendars.push_back(c)',source)
        source=(ROOT/'src/apps/calendar/CalendarApp.cpp').read_text(encoding='utf-8')
        self.assertIn('Gemeinsame Funktionen · für alle',source)
        self.assertIn('p.controller.features[i].id',source)
        source=(ROOT/'src/apps/calendar/CalendarProfiles.cpp').read_text(encoding='utf-8')
        self.assertIn('views.size()>=4',source)
        self.assertIn('if(found->kind==3||found->kind==5)continue',source)
        self.assertIn('views.add(key(calendar))',source)

if __name__=='__main__':unittest.main()

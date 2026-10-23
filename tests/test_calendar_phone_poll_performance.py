"""Targeted source integration checks for cheap idle UI polling."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CalendarPhonePollPerformance(unittest.TestCase):
    def test_phonebook_checks_revision_every_two_seconds(self):
        source=(ROOT/'src/apps/phonebook/PhonebookApp.cpp').read_text(encoding='utf-8')
        self.assertIn('lv_timer_create(PhonebookPage::tick,200,&p)',source)
        self.assertIn('int32_t(millis()-p.nextRevisionCheckAt)>=0',source)
        self.assertIn('p.nextRevisionCheckAt=millis()+2000',source)
        self.assertIn('if(p.seenRevision!=PhonebookService::instance().revision())',source)
        self.assertIn('if(app.done_.exchange(false,std::memory_order_acquire))',source)
        self.assertIn('p.pollPostal();',source)
        self.assertIn('millis()-p.searchChangedAt>=250',source)
        self.assertIn('if(p->timer)lv_timer_delete(p->timer)',source)

    def test_calendar_keeps_region_cache_between_revisions(self):
        source=(ROOT/'src/apps/calendar/CalendarController.cpp').read_text(encoding='utf-8')
        header=(ROOT/'src/apps/calendar/CalendarController.h').read_text(encoding='utf-8')
        self.assertIn('regionRefreshPending_ = true;',source)
        self.assertIn('regionRefreshPending_ = false;',source)
        self.assertIn('bool regionRefreshPending_ = true;',header)
        self.assertIn('if(bootstrap_==4&&(usersOnly_||!regionRefreshPending_))bootstrap_=5;',source)
        self.assertIn('region = doc;',source)
        self.assertIn('bootstrap_ = 2;',source)
        self.assertIn('"calendars", "people", "region",',source)

if __name__=='__main__':unittest.main()

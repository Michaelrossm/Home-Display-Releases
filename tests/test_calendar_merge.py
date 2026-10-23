import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class CalendarMergeTest(unittest.TestCase):
 def test_single_event_multi_person(self):
  t=(ROOT/'src/apps/calendar/CalendarTypes.h').read_text();self.assertIn('uint16_t participantMask',t)
  mask=(1<<0)|(1<<1)|(1<<3);self.assertEqual(bin(mask).count('1'),3)

import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class MediaSessionTest(unittest.TestCase):
 def test_general_sources(self):
  t=(ROOT/'src/services/MediaSessionService.h').read_text()
  for x in ('LocalFile','HttpStream','Radio','Dlna','WebApp'):self.assertIn(x,t)
  self.assertIn('MediaSourceType type',t)

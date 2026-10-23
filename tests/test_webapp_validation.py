import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class WebAppValidationTest(unittest.TestCase):
 def test_guards(self):
  t=(ROOT/'src/apps/browser/WebAppManager.cpp').read_text()
  for x in ('Credentials in URL nicht erlaubt','Nur lokale Webseiten erlaubt','Icon-Pfad'):self.assertIn(x,t)
  self.assertIn('schema!=2',t)

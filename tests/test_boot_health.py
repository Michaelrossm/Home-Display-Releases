import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class BootHealthTest(unittest.TestCase):
 def test_health_not_time_based(self):
  o=(ROOT/'src/services/OtaService.cpp').read_text();self.assertIn('commitIfHealthy',o);self.assertNotIn('30000',o);self.assertNotIn('kHealthWindow',o)
 def test_external_services_not_required(self):
  h=(ROOT/'src/core/BootHealthCheck.cpp').read_text();self.assertNotIn('Weather',h);self.assertNotIn('CalendarSync',h);self.assertNotIn('Internet',h)

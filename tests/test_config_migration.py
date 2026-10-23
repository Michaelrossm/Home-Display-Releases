import unittest,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ConfigMigrationTest(unittest.TestCase):
 def test_stepwise_schema(self):
  t=(ROOT/'src/infrastructure/ConfigMigration.cpp').read_text()
  self.assertIn('case 1',t);self.assertIn('v1ToV2',t);self.assertNotIn('V1ToV7',t)
  b=(ROOT/'include/BuildInfo.h').read_text();self.assertRegex(b,r'kConfigSchema = 2')

import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class RecoveryTest(unittest.TestCase):
 def test_scoped_resets(self):
  t=(ROOT/'src/core/RecoveryManager.h').read_text()
  for x in ('ResetNetwork','ResetUi','ResetWebApps','ResetCalendarAccounts','ResetMedia','FullFactoryReset'):self.assertIn(x,t)
 def test_asset_restore_verifies_before_erase(self):
  t=(ROOT/'src/services/UpdateService.cpp').read_text();self.assertLess(t.find('backup_hash_invalid'),t.find('restore_erase'))
 def test_factory_requires_confirmation(self):
  t=(ROOT/'src/core/RecoveryManager.cpp').read_text();self.assertIn('if(!confirmed)return false',t)

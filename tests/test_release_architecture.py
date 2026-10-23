import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ArchitectureTest(unittest.TestCase):
 def test_no_duplicate_mdns(self):
  texts='\n'.join(p.read_text(errors='ignore') for p in (ROOT/'src').rglob('*') if p.is_file())
  self.assertNotIn('ESPmDNS',texts)
 def test_main_is_orchestration(self):
  lines=(ROOT/'src/main.cpp').read_text().splitlines()
  # Includes/comments are not orchestration complexity; retain the code limit.
  code=[line for line in lines if line.strip() and not line.lstrip().startswith(('#include','//'))]
  self.assertLess(len(code),120)
 def test_ir_specific_meter_not_imported(self):
  names={p.name for p in (ROOT/'src').rglob('*') if p.is_file()}
  self.assertNotIn('SmlParser.cpp',names);self.assertNotIn('MeterAutoCommissioning.cpp',names)

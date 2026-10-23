import unittest,importlib.util,tempfile,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('asset_manifest',ROOT/'tools/asset_manifest.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class AssetManifestTest(unittest.TestCase):
 def test_hash_validation(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/'a.txt').write_text('abc');doc=m.generate(p,'0.3.0');self.assertEqual(m.validate(p,doc),[]);(p/'a.txt').write_text('abd');self.assertTrue(m.validate(p,doc))

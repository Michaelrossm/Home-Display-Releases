import unittest
class AtomicModel:
 def __init__(self,target=b'old'):self.target=target;self.tmp=None;self.bak=None
 def write(self,new,cut=None):
  self.tmp=new
  if cut=='tmp':return
  self.bak=self.target
  if cut=='backup':return
  self.target=None
  if cut=='rename':return
  self.target=self.tmp;self.tmp=None;self.bak=None
 def recover(self):
  if self.target is not None:self.tmp=self.bak=None;return
  if self.bak is not None:self.target=self.bak;self.bak=None;self.tmp=None;return
  if self.tmp is not None:self.target=self.tmp;self.tmp=None
class StorageAtomicWriteTest(unittest.TestCase):
 def test_powercuts_keep_old_or_new(self):
  for cut in (None,'tmp','backup','rename'):
   m=AtomicModel();m.write(b'new',cut);m.recover();self.assertIn(m.target,(b'old',b'new'))

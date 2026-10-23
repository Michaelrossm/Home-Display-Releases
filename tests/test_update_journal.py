import unittest,zlib,struct
class J:
 FMT='<IHHB3xIIII'; MAGIC=0x48444A31
 def __init__(self):self.slots=[None,None];self.seq=0
 def enc(self,state,seq):
  raw=struct.pack(self.FMT,self.MAGIC,1,seq,state,0,0,0,0);crc=zlib.crc32(raw[:-4])&0xffffffff;return raw[:-4]+struct.pack('<I',crc)
 def valid(self,b):return b and len(b)==struct.calcsize(self.FMT) and struct.unpack(self.FMT,b)[0]==self.MAGIC and (zlib.crc32(b[:-4])&0xffffffff)==struct.unpack(self.FMT,b)[-1]
 def append(self,state,torn=False):
  self.seq+=1;b=self.enc(state,self.seq);self.slots[self.seq&1]=b[:7] if torn else b
 def latest(self):
  v=[b for b in self.slots if self.valid(b)];return max(v,key=lambda b:struct.unpack(self.FMT,b)[2]) if v else None
class UpdateJournalTest(unittest.TestCase):
 def test_torn_new_record_does_not_destroy_old(self):
  j=J();j.append(1);old=j.latest();j.append(2,True);self.assertEqual(j.latest(),old)
 def test_power_cut_every_stage(self):
  for stage in range(1,10):
   j=J();j.append(stage);j.append(min(stage+1,9),True);self.assertIsNotNone(j.latest())

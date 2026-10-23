import unittest
class Retry:
 def __init__(self,b=1000,m=60000):self.b=b;self.m=m;self.a=0
 def fail(self):
  s=min(self.a,6);self.a+=1;return min(self.m,self.b<<s)
class NetworkRetryTest(unittest.TestCase):
 def test_backoff_bounded(self):
  r=Retry();v=[r.fail() for _ in range(10)];self.assertEqual(v[:4],[1000,2000,4000,8000]);self.assertLessEqual(max(v),60000)

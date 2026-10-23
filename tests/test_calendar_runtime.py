"""Production calendar date/recurrence code and integration contracts."""
import re
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class CalendarRuntimeTest(unittest.TestCase):
 def test_native_date_recurrence_regions(self):
  with tempfile.TemporaryDirectory(prefix='hp-calendar-') as directory:
   folder=Path(directory);exe=folder/('calendar.exe' if os.name=='nt' else 'calendar')
   source=ROOT/'tests/native/calendar_date.cpp';compiler=shutil.which('g++') or shutil.which('clang++')
   if compiler: command=[compiler,'-std=c++17','-I',str(ROOT),str(source),'-o',str(exe)]
   else:
    vswhere=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.exists(): self.skipTest('Native compiler missing')
    install=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    vcvars=Path(install)/'VC/Auxiliary/Build/vcvars64.bat'
    command=f'cmd /d /c "call "{vcvars}" >nul && cl /nologo /utf-8 /EHsc /std:c++17 /I"{ROOT}" "{source}" /Fe:calendar.exe"'
   r=subprocess.run(command,cwd=folder,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_native_worker_security_storage_contract(self):
  bridge=(ROOT/'src/apps/lua/CalendarLuaBridge.cpp').read_text(encoding='utf-8')
  self.assertIn('job.kind=5',bridge);self.assertIn('job.owner=app->id()',bridge);self.assertIn('job.required=perm',bridge)
  self.assertIn('BackgroundExecuting',bridge)
  for forbidden in ['xTaskCreate','lv_obj_','readOAuthRecord','access_token','refresh_token']:
   self.assertNotIn(forbidden,bridge)
  store=(ROOT/'src/apps/calendar/CalendarEventStore.cpp').read_text(encoding='utf-8')
  self.assertIn('recoverAtomicWrite',store);self.assertIn('writeAtomic',store);self.assertNotIn('FILE_APPEND',store)
  service=(ROOT/'src/apps/calendar/CalendarOperations.cpp').read_text(encoding='utf-8')
  self.assertIn('revision_conflict',service);self.assertIn('Settings',service.replace('settingsAllowed','Settings'))
  web=(ROOT/'src/services/WebServerService.cpp').read_text(encoding='utf-8')
  handler=web.split('esp_err_t WebServerService::calendarJobHandler',1)[1].split('esp_err_t WebServerService::calendarResultHandler',1)[0]
  for check in ['writeAuthorized(req)','settingsAllowed()','tabletLocked()','LuaJobService::instance().submit']:
   self.assertIn(check,handler)
  self.assertIn('job.owner="@calendar-web"',handler) # Not a valid installable Lua app ID.
 def test_calendar_web_script_syntax(self):
  node=shutil.which('node')
  if not node:self.skipTest('Node missing')
  page=(ROOT/'src/services/CalendarWebPage.h').read_text(encoding='utf-8')
  script=re.search(r'<script>(.*?)</script>',page,re.S).group(1)
  r=subprocess.run([node,'--check'],input=script,capture_output=True,text=True)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()

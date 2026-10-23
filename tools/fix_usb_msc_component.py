#!/usr/bin/env python3
"""Verify USB MSC dependency and create its IDF registry manifest; never fake USB availability."""
from pathlib import Path
root=Path.cwd()
p=root/'src/idf_component.yml'
expected='espressif/usb_host_msc: "1.1.4"'
if p.exists():
 s=p.read_text(encoding='utf-8')
 if 'espressif/usb_host_msc' not in s:
  if 'dependencies:' not in s:raise SystemExit('Please merge dependency manually: malformed src/idf_component.yml')
  s=s.replace('dependencies:', 'dependencies:\n  '+expected,1)
  p.write_text(s,encoding='utf-8')
else:
 p.write_text('dependencies:\n  '+expected+'\n',encoding='utf-8')
print('IDF dependency declared:',expected)
print('If usb/msc_host.h is still absent, reconfigure the IDF components with PlatformIO; the USB driver itself is NOT bundled.')

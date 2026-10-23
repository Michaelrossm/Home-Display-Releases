from pathlib import Path
from zipfile import ZipFile
import re

root=Path(__file__).resolve().parent.parent
svc=(root/'src/services/NasService.cpp').read_text(encoding='utf-8')
header=(root/'src/services/NasService.h').read_text(encoding='utf-8')
ui=(root/'src/apps/settings/SettingsApp.cpp').read_text(encoding='utf-8')
uih=(root/'src/apps/settings/SettingsApp.h').read_text(encoding='utf-8')
cm=(root/'src/CMakeLists.txt').read_text(encoding='utf-8')
for code,name in ((svc,'NasService.cpp'),(header,'NasService.h'),(ui,'SettingsApp.cpp'),(uih,'SettingsApp.h')):
    assert code.count('{')==code.count('}'),f'Unbalanced braces: {name}'
assert 'sahlberg__libsmb2' in cm
assert 'smb2_connect_share(' in svc
assert 'smb2_opendir(' in svc and 'smb2_readdir(' in svc
assert 'smb2_read(' in svc and 'fs->rename(tempPath, targetPath)' in svc
assert 'xTaskCreate(smbWorkerThunk' in svc
assert 'smb2_set_password(ctx, password.c_str())' in svc
assert 'if (fs->exists(targetPath)' in svc or 'fs && fs->exists(targetPath)' in svc
assert 'nasProtocol_=lv_dropdown_create' in ui
assert 'buildNasBrowser' in ui and 'nasPollCb' in ui
assert 'nasTimer_' in uih
print('PASS: 10 structural SMB/browse/download safeguards checked.')
print('NOTE: This is not a compiler test or a live NAS/PCM audio test.')

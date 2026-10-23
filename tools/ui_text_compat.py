"""Adapt supported punctuation in known firmware UI *literal* strings.

Only selected C++ UI files are scanned. Runtime values, Lua applications,
HTTP templates, and other firmware modules remain untouched.
"""
from pathlib import Path

# ASCII fallbacks for unsupported Unicode glyphs.
SUB = {
    'Ä': 'Ae', 'Ö': 'Oe', 'Ü': 'Ue', 'ä': 'ae', 'ö': 'oe', 'ü': 'ue', 'ß': 'ss',
    '·': ' - ', '•': ' - ', '–': '-', '—': '-', '→': '->', '←': '<-',
    '„': "'", '“': "'", '”': "'", '’': "'", '…': '...', '\u00a0': ' ',
}


def _literal_safe(literal):
    if not any(ch in literal for ch in SUB):
        return False
    lower = literal.lower()
    # Do not change hard-coded protocol identifiers, URLs or file paths.
    if any(token in lower for token in (
        'http:', 'https:', 'mqtt:', '://', 'application/', 'charset=',
        '.json', '.lua', '.html', '.css', '.js', '.bin', '/homepanel/',
        'ssid=', 'password=', 'wifi:', 'content-type', 'user-agent',
        'x-api-', 'authorization', 'bearer ', 'app.json',
    )):
        return False
    return True


def convert(source):
    """Convert ordinary quoted strings only, preserving comments and raw strings."""
    import re
    output = []
    i = 0
    n = len(source)
    while i < n:
        if source.startswith('//', i):
            j = source.find('\n', i)
            if j < 0:
                output.append(source[i:])
                break
            output.append(source[i:j+1]); i = j+1; continue
        if source.startswith('/*', i):
            j = source.find('*/', i+2)
            if j < 0:
                output.append(source[i:]); break
            output.append(source[i:j+2]); i=j+2; continue
        if source.startswith('R"',i):
            m=re.match(r'R"([^()\\\s]{0,16})\(',source[i:])
            if m:
                closing=')'+m.group(1)+'"'
                j=source.find(closing, i+m.end())
                if j>=0:
                    output.append(source[i:j+len(closing)])
                    i=j+len(closing)
                    continue
        if source[i]=='"':
            j=i+1
            while j<n:
                if source[j]=='\\': j+=2; continue
                if source[j]=='"': j+=1; break
                j+=1
            literal=source[i:j]
            if _literal_safe(literal):
                body=literal[1:-1]
                for glyph, fallback in SUB.items():
                    body=body.replace(glyph, fallback)
                literal='"'+body+'"'
            output.append(literal); i=j; continue
        if source[i]=="'":
            j=i+1
            while j<n:
                if source[j]=='\\': j+=2; continue
                if source[j]=="'": j+=1; break
                j+=1
            output.append(source[i:j]); i=j; continue
        output.append(source[i]); i+=1
    return ''.join(output)


def selected(root):
    apps=root/'src/apps'
    if apps.is_dir():
        for path in apps.rglob('*.cpp'):
            rel=path.relative_to(apps).as_posix()
            if rel.startswith(('lua/','browser/')): continue
            if path.name.endswith('App.cpp') or path.name=='DynamicAppSettings.cpp':
                yield path
    for path in (root/'src/ui/UiShell.cpp', root/'src/services/NetworkService.cpp'):
        if path.is_file(): yield path


def update_ui_text(root):
    root=Path(root)
    changed=[]
    for path in selected(root):
        try:
            before=path.read_text(encoding='utf-8')
        except (OSError, UnicodeDecodeError):
            continue
        after=convert(before)
        if after!=before:
            path.write_text(after,encoding='utf-8',newline='\n')
            changed.append(path.relative_to(root).as_posix())
    if changed:
        print('[Home-Display UI] Converted unsupported UI literals:')
        for rel in changed: print('  ~',rel)

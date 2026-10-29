"""Build the standalone EO_WeatherControl plugin and its authored native pages."""
from pathlib import Path
import subprocess,json,sys,os
from xml.sax.saxutils import escape
from version_resource import version_resource
src=Path(__file__).resolve().parent
root=src.parents[2]
zig=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'work/toolchain/zig-x86_64-windows-0.15.2/zig.exe'
cache=src.parent/'build';cache.mkdir(exist_ok=True)
env=os.environ.copy()
for key,name in [('ZIG_GLOBAL_CACHE_DIR','global'),('ZIG_LOCAL_CACHE_DIR','local'),('TEMP','tmp'),('TMP','tmp')]:
    path=cache/name;path.mkdir(exist_ok=True);env[key]=str(path.resolve())
output=src.parent/'release/Extra Options - Weather Control'
plugins=output/'plugins';plugins.mkdir(parents=True,exist_ok=True)
media=plugins/'EO_WeatherControl/Media/UI/Scenes/Anthem';media.mkdir(parents=True,exist_ok=True)
catalog=json.loads((src/'translations.json').read_text(encoding='utf8'))
keys=['TITLE','LOCK','HOUR','WEATHER','LOCK_DESC','HOUR_DESC','WEATHER_DESC']
template=(src/'weather_page.template.xaml').read_text(encoding='utf8')
header=['// Generated from translations.json; includes every EO language.\n',
        'typedef struct {const char *code;const wchar_t *prompt;} Translation;\n',
        'static const Translation translations[]={\n']
for code,values in catalog.items():
    text=template
    for key,value in zip(keys,values):text=text.replace('@'+key+'@',escape(value,{'"':'&quot;'}))
    (media/f'EO_WeatherControl_{code}.xaml').write_text(text,encoding='utf-8-sig')
    # JSON escapes are valid C escapes, but retain Unicode for the wide literal.
    header.append('{'+json.dumps(code)+',L'+json.dumps(values[0],ensure_ascii=False)+'},\n')
header.append('};\n');(src/'translations.h').write_text(''.join(header),encoding='utf8')
res=src/'version.res';res.write_bytes(version_resource())

# Compile/link directly, using only import libraries for Windows' own DLLs.
# Avoids bundling a second MinGW runtime into a game plugin.
import struct
for name in ('kernel32','user32','msvcrt'):
    data=(Path(os.environ['WINDIR'])/'System32'/(name+'.dll')).read_bytes()
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24;section=opt+struct.unpack_from('<H',data,pe+20)[0]
    def offset(rva):
        for i in range(struct.unpack_from('<H',data,pe+6)[0]):
            _,va,size,raw=struct.unpack_from('<IIII',data,section+40*i+8)
            if va<=rva<va+size:return raw+rva-va
        raise ValueError(hex(rva))
    e=offset(struct.unpack_from('<I',data,opt+112)[0]);names=offset(struct.unpack_from('<I',data,e+32)[0]);exports=[]
    for i in range(struct.unpack_from('<I',data,e+24)[0]):
        at=offset(struct.unpack_from('<I',data,names+4*i)[0]);exports.append(data[at:data.index(b'\0',at)].decode())
    definition=cache/(name+'.def');definition.write_text('LIBRARY '+name+'.dll\nEXPORTS\n'+'\n'.join(exports)+'\n')
    subprocess.run([str(zig),'dlltool','-m','i386:x86-64','-d',str(definition),'-l',str(cache/(name+'.lib'))],check=True)
units=['weather_control.c','config.c','clock_hold.c','weather_hold.c','settings_page.c','entry_hook.c','compat.c']
for test in (False,True):
    out=(cache/'EO_WeatherControl-test.dll') if test else (plugins/'EO_WeatherControl.dll');objects=[]
    for name in units:
        obj=cache/(name+('.test.obj' if test else '.obj'));objects.append(str(obj))
        args=[str(zig),'clang','-target','x86_64-w64-windows-gnu','-c','-ffreestanding','-fno-builtin',
              '-fno-stack-protector','-fms-extensions','-funwind-tables','-O2','-D__USE_MINGW_ANSI_STDIO=0',
              '-Wall','-Wextra','-Wno-missing-field-initializers','-Werror',
              '-isystem',str(zig.parent/'lib/include'),'-isystem',str(zig.parent/'lib/libc/include/any-windows-any'),
              '-include',str(src/'compat.h')]
        if test:args+=['-DWEATHER_CONTROL_TEST=1']
        subprocess.run(args+[str(src/name),'-o',str(obj)],check=True)
    subprocess.run([str(zig),'lld-link','/dll','/entry:DllMain','/machine:x64','/nodefaultlib',
                    '/dynamicbase','/nxcompat','/Brepro','/out:'+str(out),*objects,str(res),
                    *[str(cache/(n+'.lib')) for n in ('kernel32','user32','msvcrt')]],check=True)
    print(out)

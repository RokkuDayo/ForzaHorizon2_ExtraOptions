"""Read-only FH2 clock capture. No game calls, memory writes or save changes."""
import ctypes as c
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import time

ROOT=Path(sys.argv[1]) if len(sys.argv)>1 else Path('E:/Games/Azimuth')
REPORT=Path(__file__).with_name('clock-state.json')
def main():
    report={'read_only':True,'version':1,'samples':[],'installation':str(ROOT)}
    k=c.WinDLL('kernel32',use_last_error=True)
    k.OpenProcess.argtypes=[c.c_ulong,c.c_int,c.c_ulong];k.OpenProcess.restype=c.c_void_p
    k.ReadProcessMemory.argtypes=[c.c_void_p,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
    k.CloseHandle.argtypes=[c.c_void_p]
    h=None
    try:
        pid=int(re.findall(r'session \S+-(\d+) began',(ROOT/'data/logs/azimuth.log').read_text(errors='replace'))[-1])
        report['pid']=pid;h=k.OpenProcess(0x1010,False,pid)
        if not h:raise OSError(c.get_last_error(),'Keep FH2 running with the affected save loaded.')
        def read(a,n):
            if a<0x10000 or n>0x1000:raise ValueError('Invalid address')
            b=c.create_string_buffer(n);got=c.c_size_t()
            if not k.ReadProcessMemory(h,a,b,n,c.byref(got)) or got.value!=n:raise OSError('Unavailable memory: '+hex(a))
            return b.raw
        def ptr(a):return struct.unpack('<Q',read(a,8))[0]
        base=0x140000000
        if read(base+0x2e1520,7)!=bytes.fromhex('488b8198000000'):raise ValueError('Expected FH2 image not found.')
        report['time_update_entry']=read(base+0x197b170,24).hex()
        settings=[]
        for folder in (ROOT/'mods',ROOT/'plugins'):
            if not folder.exists():continue
            for path in folder.rglob('EO_WeatherControl.ini'):
                settings.append({'path':str(path),'text':path.read_text(errors='replace')[:16384]})
                dll=path.with_suffix('.dll')
                if dll.exists():settings[-1]['dll_sha256']=hashlib.sha256(dll.read_bytes()).hexdigest()
                log=path.with_suffix('.log')
                if log.exists():settings[-1]['log_tail']=log.read_text(errors='replace')[-12000:]
        report['plugin_settings']=settings
        print('Return to offline free roam now. Keep the game focused and do not open Photo Mode or pause.')
        print('Recording begins in five seconds; no settings or save data will be changed.')
        time.sleep(5)
        print('RECORDING (20 seconds)')
        for _ in range(21):
            sample={'elapsed':len(report['samples'])}
            try:
                manager=ptr(base+0x34aba60);simulation=ptr(manager+0x48)
                world=ptr(simulation+0x98);clock=ptr(world+0x480)
                state=read(clock,0x30)
                sample.update(world=hex(world),clock=hex(clock),simulation_paused=read(simulation+0x28,1)[0],
                    internal_seconds=struct.unpack_from('<f',state,0x10)[0],
                    speed=struct.unpack_from('<f',state,0x18)[0],
                    stopped=state[0x24],fixed=state[0x25],stop_locked=state[0x27],has_target=state[0x28],
                    realtime=read(clock+0x361,1)[0],clock_header=state.hex())
            except Exception as e:sample['error']=str(e)
            report['samples'].append(sample)
            if len(report['samples'])<21:time.sleep(1)
        active=[s for s in report['samples'] if 'error' not in s and not s['simulation_paused']]
        report['summary']={'unpaused_samples':len(active),'clocks':len({s['clock'] for s in active}),
            'stopped_samples':sum(bool(s['stopped']) for s in active),
            'zero_speed_samples':sum(s['speed']==0 for s in active),
            'fixed_samples':sum(bool(s['fixed']) for s in active),
            'time_changed':any(a['clock']==b['clock'] and a['internal_seconds']!=b['internal_seconds'] for a,b in zip(active,active[1:]))}
        print('Capture complete. This report does not repair or alter the save.')
    except Exception as e:report['error']=str(e);print('Capture error:',e)
    finally:
        if h:k.CloseHandle(h)
        REPORT.write_text(json.dumps(report,indent=2),encoding='utf8')
        print('Report saved:',REPORT)
if __name__=='__main__':main()

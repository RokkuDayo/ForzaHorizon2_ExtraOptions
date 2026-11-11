"""Regression checks for a session-only lock that never writes native stop flags."""
import ctypes as c
from pathlib import Path
import sys
import struct
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'source/development/tests'))
from test_media_overrides import NativeFiles

def main():
    core,plugin,game,scratch=map(lambda p:Path(p).resolve(),sys.argv[1:5])
    scratch.mkdir(exist_ok=False)
    n=NativeFiles(core,game,scratch)
    d=c.WinDLL(str(plugin));v=c.c_void_p
    d.WeatherTestConfig.argtypes=[c.c_wchar_p]
    d.WeatherTestClockBind.argtypes=[v,v]
    d.WeatherTestClockUpdate.argtypes=[v,c.c_float,v,v]
    d.WeatherTestClockUpdate.restype=c.c_ubyte
    d.WeatherTestClockEnabled.argtypes=[c.c_int]
    ini=scratch/'TimeLock.ini'
    ini.write_text('[TimeLock]\nEnabled=1\nHour=18.25\n')
    d.WeatherTestConfig(str(ini))
    assert d.WeatherTestValue(0)==0 and 'Enabled=0' in ini.read_text()
    deltas=[]
    def advance(tod,dt,out,unused):
        deltas.append(dt)
        if not c.c_ubyte.from_address(tod+0x24).value:
            c.c_float.from_address(tod+0x10).value+=dt*60
        return 93
    fn=c.WINFUNCTYPE(c.c_ubyte,v,c.c_float,v,v)(advance);n.callbacks.append(fn)
    n.stub(0x197a880,c.c_float,[v,c.c_int],lambda t,r:c.c_float.from_address(t+0x10).value)
    def set_time(t,seconds,remap):c.c_float.from_address(t+0x10).value=seconds
    n.stub(0x197aec0,None,[v,c.c_float,c.c_int],set_time)
    d.WeatherTestClockBind(n.image,c.cast(fn,v))
    tod=n.allocate(0x400)
    c.c_float.from_address(tod+0x10).value=300
    assert d.WeatherTestClockUpdate(tod,1,None,None)==93 and deltas[-1]==1
    d.WeatherTestClockEnabled(1)
    for _ in range(3):d.WeatherTestClockUpdate(tod,1,None,None)
    assert deltas[-1]==0 and c.c_float.from_address(tod+0x10).value==18.25*3600
    assert c.string_at(tod+0x24,5)==b'\0'*5
    assert 'Enabled=0' in ini.read_text(),'Enabled lock must never be persisted'
    d.WeatherTestClockEnabled(0);d.WeatherTestClockUpdate(tod,1,None,None)
    assert deltas[-1]==1 and c.c_float.from_address(tod+0x10).value==18.25*3600+60
    d.WeatherTestClockEnabled(1)
    for flag in (0x24,0x25,0x27,0x28):
        c.c_ubyte.from_address(tod+flag).value=1
        before=c.string_at(tod+0x24,5)
        d.WeatherTestClockUpdate(tod,0.5,None,None)
        assert deltas[-1]==0.5 and c.string_at(tod+0x24,5)==before
        c.c_ubyte.from_address(tod+flag).value=0
    new_tod=n.allocate(0x400)
    c.c_ubyte.from_address(new_tod+0x24).value=1
    d.WeatherTestClockUpdate(new_tod,1,None,None)
    assert deltas[-1]==1 and c.c_ubyte.from_address(new_tod+0x24).value==1
    d.WeatherTestConfig(str(ini));assert d.WeatherTestValue(0)==0
    d.WeatherTestClockUpdate(tod,1,None,None);assert deltas[-1]==1
    class Frame(c.Structure):
        _fields_=[('size',c.c_uint),('input',c.c_uint),('car',v),('buttons',c.c_uint),('dt',c.c_float),('flash',v)]
    d.WeatherTestBind.argtypes=[v];d.WeatherTestBind(n.image)
    d.FH2PluginVehicleUpdate.argtypes=[c.POINTER(Frame)]
    frame=Frame(c.sizeof(Frame),1,None,0,0.25,None)
    hud=n.allocate(16);context=n.allocate(0x80)
    n.pointer(n.image+0x34c0f40,hud)
    def get_context(manager,out):n.pointer(out,context);n.pointer(out+8,0);return out
    n.stub(0x2129a0,v,[v,v],get_context)
    session=n.allocate(16);n.pointer(n.image+0x34d6348,session)
    online=[0];n.stub(0xa2c920,c.c_ubyte,[v,c.c_int],lambda session,which:online[0])
    manager=n.allocate(0x50);simulation=n.allocate(0xa0);world=n.allocate(0x490)
    n.pointer(n.image+0x34aba60,manager);n.pointer(manager+0x48,simulation)
    n.pointer(simulation+0x98,world);n.pointer(world+0x480,tod)
    c.c_float.from_address(tod+0x18).value=48
    c.c_int.from_address(context+0x60).value=2
    c.c_ubyte.from_address(tod+0x24).value=1
    ini.write_text('[TimeLock]\nEnabled=0\n')
    d.WeatherTestConfig(str(ini))
    def tick_many():
        for _ in range(16):d.FH2PluginVehicleUpdate(c.byref(frame))
    frame.input=0;tick_many();assert c.c_ubyte.from_address(tod+0x24).value==1
    frame.input=1
    d.WeatherTestClockEnabled(1);tick_many()
    assert c.c_ubyte.from_address(tod+0x24).value==1
    d.WeatherTestClockEnabled(0)
    for mode in (0,1,3,4,5,-1):
        c.c_int.from_address(context+0x60).value=mode;tick_many()
        assert c.c_ubyte.from_address(tod+0x24).value==1
    c.c_int.from_address(context+0x60).value=2
    online[0]=1;tick_many();assert c.c_ubyte.from_address(tod+0x24).value==1
    online[0]=0
    for flag in (0x25,0x27,0x28):
        c.c_ubyte.from_address(tod+flag).value=1;tick_many()
        assert c.c_ubyte.from_address(tod+0x24).value==1
        c.c_ubyte.from_address(tod+flag).value=0
    c.c_ubyte.from_address(simulation+0x28).value=1;tick_many()
    assert c.c_ubyte.from_address(tod+0x24).value==1
    c.c_ubyte.from_address(simulation+0x28).value=0
    before=c.string_at(tod,0x400)
    for _ in range(11):d.FH2PluginVehicleUpdate(c.byref(frame))
    assert c.c_ubyte.from_address(tod+0x24).value==1
    d.FH2PluginVehicleUpdate(c.byref(frame))
    assert c.c_ubyte.from_address(tod+0x24).value==0
    after=c.string_at(tod,0x400)
    assert [i for i,(a,b) in enumerate(zip(before,after)) if a!=b]==[0x24]
    assert '[Recovery]' not in ini.read_text(),'Automatic recovery must not require or write an INI request'
    c.c_ubyte.from_address(tod+0x24).value=1;tick_many()
    assert c.c_ubyte.from_address(tod+0x24).value==1,'Recovery must not repeat'
    # Exercise the same logger setup used by plugin initialization, including
    # the UTF-16 INI supplied for recovery. Logging must never gate the repair.
    d.WeatherTestLogging.argtypes=[c.c_wchar_p,c.c_wchar_p]
    for encoding in ('utf-8','utf-16'):
        for logging in (0,1):
            ini.write_text('[TimeLock]\nEnabled=0\nHour=16.75\n'
                           '[Recovery]\nResumeStoppedFreeRoamClock=0\n'
                           f'[Logging]\nEnabled={logging}\n',encoding=encoding)
            d.WeatherTestLogging(str(scratch),str(ini))
            d.WeatherTestConfig(str(ini))
            c.c_ubyte.from_address(tod+0x24).value=1
            before=c.string_at(tod,0x400)
            for _ in range(11):d.FH2PluginVehicleUpdate(c.byref(frame))
            assert c.c_ubyte.from_address(tod+0x24).value==1
            d.FH2PluginVehicleUpdate(c.byref(frame))
            after=c.string_at(tod,0x400)
            assert [i for i,(a,b) in enumerate(zip(before,after)) if a!=b]==[0x24],(encoding,logging)
            assert 'ResumeStoppedFreeRoamClock=0' in ini.read_text(encoding=encoding)
            if logging:
                assert 'startup clock check completed' in (scratch/'EO_WeatherControl.log').read_text()
            c.c_ubyte.from_address(tod+0x24).value=1;tick_many()
            assert c.c_ubyte.from_address(tod+0x24).value==1
    ini.write_text('[Logging]\nEnabled=0\n')
    d.WeatherTestLogging(str(scratch),str(ini))
    # A healthy startup consumes the check too: a later native stop is untouched.
    d.WeatherTestConfig(str(ini))
    c.c_ubyte.from_address(tod+0x24).value=0
    before=c.string_at(tod,0x400);tick_many()
    assert c.string_at(tod,0x400)==before
    c.c_ubyte.from_address(tod+0x24).value=1;tick_many()
    assert c.c_ubyte.from_address(tod+0x24).value==1

    ini.write_text('[Weather]\nMode=Storm\n[Logging]\nEnabled=0\n')
    d.WeatherTestConfig(str(ini))
    assert d.WeatherTestValue(2)==0 and 'Mode=Dynamic' in ini.read_text()
    assert d.WeatherTestSetWeather(4) and d.WeatherTestValue(2)==4
    assert 'Mode=Dynamic' in ini.read_text(),'Weather selections must be session-only'
    d.WeatherTestConfig(str(ini))
    assert d.WeatherTestValue(2)==0

    d.WeatherTestWeatherBind.argtypes=[c.c_size_t]
    assert d.WeatherTestWeatherBind(0x3a18000), 'Native weather recovery signatures must match FH2'
    weather=n.allocate(0x300);n.pointer(world+0x488,weather)
    options=n.allocate(0x3b80);prologue=n.allocate(16)
    script_lock=c.c_ubyte.from_address(options+0x3b7d)
    stage=c.c_int.from_address(prologue+8)
    auto_change=c.c_ubyte.from_address(weather+0x211)
    pins=[c.c_ubyte.from_address(weather+0xe0+flag) for flag in (0x10,0x18,0x20)]
    release_calls=[]
    def release(manager):
        release_calls.append(manager)
        for pin in pins:pin.value=0
    n.stub(0x2959a0,v,[],lambda:options)
    n.stub(0xa65ca0,v,[],lambda:prologue)
    n.stub(0x1981c80,None,[v],release)
    for pin in pins:pin.value=1
    def update_weather():
        d.FH2PluginVehicleUpdate(c.byref(frame))
        d.WeatherTestClockUpdate(tod,0.25,None,None)
    stage.value=3
    for _ in range(12):update_weather()
    assert auto_change.value==0 and not release_calls
    stage.value=4;script_lock.value=1
    for _ in range(12):update_weather()
    assert auto_change.value==0 and not release_calls
    # A stale request must be discarded if driving stops before the native update.
    frame.input=0;script_lock.value=0;update_weather()
    assert auto_change.value==0 and not release_calls
    frame.input=1
    for _ in range(11):update_weather()
    assert auto_change.value==0 and not release_calls
    update_weather()
    assert auto_change.value==1 and release_calls==[weather] and not any(pin.value for pin in pins)
    auto_change.value=0
    for pin in pins:pin.value=1
    for _ in range(16):update_weather()
    assert auto_change.value==0 and release_calls==[weather], 'Weather recovery must not repeat'
    d.WeatherTestWeatherPageVerify.argtypes = [v, c.c_size_t]
    # The fixture patches the two guarded allocator entries. Verify the
    # native bytes for the duration of this check, then restore the stubs.
    pe = struct.unpack_from('<I', n.data, 0x3c)[0]
    text = pe + 24 + struct.unpack_from('<H', n.data, pe + 20)[0]
    _, text_rva, _, text_raw = struct.unpack_from('<IIII', n.data, text + 8)
    allocator_stubs = {rva: c.string_at(n.image + rva, 12) for rva in (0x3229c0, 0x31c7b0)}
    for rva in allocator_stubs:
        offset = text_raw + rva - text_rva
        n.write(rva, n.data[offset:offset + 12])
    assert d.WeatherTestWeatherPageVerify(n.image, 0x3a18000)
    for rva, code in allocator_stubs.items():
        n.write(rva, code)
    assert [d.WeatherTestWeatherRow(i, 0) for i in range(6)] == [0, 1, 2, 3, 4, -1]
    assert [d.WeatherTestWeatherRow(i, 1) for i in range(9)] == [0, 1, 5, 2, 6, 3, 4, 7, -1]

    # Minimal native map layout, including a heap-backed StormTropicalFog key.
    def preset_table(names):
        table, sentinel = n.allocate(0x28), n.allocate(0x40)
        n.pointer(table + 0x18, sentinel)
        c.c_ubyte.from_address(sentinel + 0x19).value = 1

        def tree(keys):
            if not keys:
                return sentinel
            mid = len(keys) // 2
            node, key = n.allocate(0x40), keys[mid]
            n.pointer(node, tree(keys[:mid]))
            n.pointer(node + 0x10, tree(keys[mid + 1:]))
            storage = node + 0x20
            if len(key) >= 16:
                storage = n.allocate(len(key) + 1)
                n.pointer(node + 0x20, storage)
            c.memmove(storage, key, len(key))
            c.c_size_t.from_address(node + 0x30).value = len(key)
            c.c_size_t.from_address(node + 0x38).value = max(15, len(key))
            return node

        n.pointer(sentinel + 8, tree(sorted(names)))
        return table

    mainland = preset_table([b'Clear', b'StormTropicalFoggy', b'ThreateningStormCCloudyA'])
    island = preset_table([b'CloudyFog', b'ThreateningFog', b'StormTropicalFog'])
    calls = []

    def native_option(manager, output, option):
        assert manager == weather
        calls.append(option)
        c.c_float.from_address(output + 0x18).value = 0.75
        c.c_int.from_address(output + 0x1c).value = 12
        c.c_float.from_address(output + 0x20).value = 0.5

    def set_control(manager, control):
        assert manager == weather
        c.memmove(weather + 0xe0, control, 0x3c)

    n.stub(0xd6f110, None, [v, v, c.c_int], native_option)
    n.stub(0x1981860, None, [v, v], set_control)
    n.stub(0x1981c80, None, [v], lambda manager: c.memset(manager + 0xe0, 0, 0x3c))
    c.memset(weather + 0xe0, 0, 0x3c)
    n.pointer(weather + 0x158, mainland)
    assert not d.WeatherTestHasFogPresets()
    d.WeatherTestSetWeather(7)
    update_weather()
    assert calls == []
    n.pointer(weather + 0x158, island)
    assert d.WeatherTestHasFogPresets()
    update_weather()
    assert calls == [7]
    assert c.c_int.from_address(weather + 0xe4).value == 12
    assert c.c_float.from_address(weather + 0xfc).value == 0.75
    assert c.c_float.from_address(weather + 0x104).value == 0.5
    for mode in (5, 6):
        d.WeatherTestSetWeather(mode)
        update_weather()
    assert calls == [7, 3, 4]
    n.pointer(weather + 0x158, mainland)
    update_weather()
    assert calls == [7, 3, 4] and not c.c_ubyte.from_address(weather + 0xe0).value
    print('PASS: Native weather list signatures and indices, exact preset lookup, and fog weather across map changes.')
    print('PASS: Dynamic startup, session-only weather presets, prologue and scripted weather guards, and one-shot recovery with logging off.')
    print('PASS: automatic one-shot recovery with no Recovery section or legacy request=0; identical with Logging=0/1 and UTF-8/UTF-16 INIs; healthy startup consumes the check without modifying the clock.')
    print('PASS: unlocked startup, session-only persistence, unchanged native stop flags, normal delta after unlock, stopped-world exclusion and world transitions; recovery guards, three-second delay and single-byte/one-shot recovery. Live save/GPU not tested.')
if __name__=='__main__':main()

# Category zero-volume behavior — 0.2.206

The original UI's Audio_Min1 slider minimum is 0.1. EO already allowed 0, but
the authored AudioOptions automation retains an audible floor. Master gain was
already correctly silent at 0 and is unchanged.

Vanilla MasterBank.strings.bank provides these five VCA names:
HZ2_AOPT_Music, HZ2_AOPT_SFX, HZ2_AOPT_VO, HZ2_AOPT_Cars, HZ2_AOPT_Tires.
Native option floats at offsets 0xc..0x1c map to those groups in that order.
Use native mixer-strip lookup1a98300(system,GUID,mode0,out) on each change;
no retained FMOD pointers. Send programmatic fader0 for zero and1 when raised.
Audio options/automation continue unchanged. Startup load failures retry from
the snapshot manager's native update, with a500ms backoff, only while pending.

Legacy FMOD MixerStrip setMute is bus-only and does not mute VCAs. The engine
version's documented solution is fader0:
https://qa.fmod.com/t/mixerstrip-setmute-not-working/11580
This is why the command uses existing setFaderLevel vtable2347198.

Native enqueue follows ParameterInstance::setValue1a99b60 with the appropriate
fader command. Scope lock/unlock, queue capacity/reservation and command dispatch
remain native; the UI never invokes the audio-thread handler on a live strip.
Private fixtures run the actual1aea6e0 dispatcher and1af0000 handler against fixture strip objects and
verify gain storage, locks, both reservation paths, five groups, restoration,
unchanged option values and retry. They do not produce audio or prove live output.

## 0.2.207 crash correction

0.2.206 omitted the command argument to1aea6e0(queue,command). Its first
instruction loads the vtable throughRDX, matching the reported null-read crash
at game+1aea6e0. The earlier fixture stubbed this dispatcher, concealing the
ABI error. The revised fixture preserves both the real dispatcher and handler;
206 reproduces the null-read and207 passes using both arguments. The boundary
mock now covers only final serialization/publication, asserting its queue and
command pointers, the native gain write and balanced queue lock.

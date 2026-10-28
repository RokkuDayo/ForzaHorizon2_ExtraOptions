"""Machine code + memory layout for the voice-line subtitle hooks (assembled with keystone, position independent).

features/voice_subtitles.c copies this blob (embedded by gen_voice_code.py) into the game process once, next to the game
image, and writes the hook jumps: all logic runs inside the hooks.

  Hooks (rcx/rdx/r8/r9 arguments of the original functions are preserved):
    ctor / prep / prep3d  PlaySpeech / PreparePlayVO / PreparePlay3DVO parameter constructors. rdx = std::string* folder,
                          r8 = std::string* event name (r9 = third std::string* for 3D). The name is copied to a ring log,
                          its LAST PATH COMPONENT is hashed (FNV-1a 64) and looked up in `table`; on a hit the text pointer is put
                          into one of two fake text objects, `mb_until` = now + duration, and `mb_seq` is published.
    update                SubtitleDataSource::Update (UI thread, every frame). If mb_seq changed, calls
                          ds->iface(+0x38)->SetText(&obj); when `now >= mb_until` shows the empty object instead.

  `now` is KUSER_SHARED_DATA.InterruptTime (0x7FFE0008, 100 ns units, monotonic), so the display time does not depend
  on the frame rate. Table durations are in the same 100 ns units.
    parser                DialogueScript parser, counter only.

  Fake text object (AVUI object protocol): vtable slot2 AddRef / slot3 Release = ret, slot7 (+0x38) GetWide = [obj+0x38]
  (pointer to a NUL-terminated UTF-16 string in `pool`).

  Layout (offsets asserted in build()):  0x00 header/counters, 0x80 vtable, 0x100 obj_a, 0x140 obj_b, 0x180 obj_empty,
  0x1C0 empty_wstr, 0x200 ring (64 x 256 B), then table (24 B entries: hash, text ptr, frames), pool (UTF-16 text), code.
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "lib"))  # optional local copy; else pip install keystone-engine
import keystone  # noqa: E402

IMAGE_BASE = 0x140000000
SET_TEXT_VA = 0x140C3A4B0          # ICinematicSubtitlesDisplay::SetText(iface, Ptr<Object>*)
RING_SLOTS, RING_SLOT_SIZE = 64, 256
TABLE_MAX = 2048
POOL_SIZE = 0x40000
CAVE_SIZE = 0x52000

HOOKS = {
    "ctor": (0x140F50EE0, 5, bytes.fromhex("4c894c2420")),       # PlaySpeech param ctor    (kind 1)
    "prep": (0x140F51100, 5, bytes.fromhex("4c894c2420")),       # PreparePlayVO param ctor (kind 2)
    "prep3d": (0x140F512A0, 5, bytes.fromhex("4c894c2420")),     # PreparePlay3DVO ctor     (kind 3, third string in r9)
    "update": (0x140C3DF10, 5, bytes.fromhex("4055535657")),     # SubtitleDataSource::Update
    "parser": (0x14085AFA0, 6, bytes.fromhex("405556574154")),   # DialogueScript parser
}

FNV_OFFSET, FNV_PRIME = 0xCBF29CE484222325, 0x100000001B3
INTERRUPT_TIME = 0x7FFE0008        # KUSER_SHARED_DATA.InterruptTime, 100 ns units

ASM = """
magic:        .ascii "FH2S"
              .long 4
cnt_update:   .quad 0
cnt_ctor:     .quad 0
cnt_parser:   .quad 0
ring_seq:     .quad 0
mb_seq:       .quad 0
mb_seen:      .quad 0
mb_until:     .quad 0
table_count:  .quad 0
              .fill 0x38, 1, 0
vtable:       .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad getwide
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
              .quad stub_ret
obj_a:        .quad vtable
              .fill 0x30, 1, 0
              .quad 0
obj_b:        .quad vtable
              .fill 0x30, 1, 0
              .quad 0
obj_empty:    .quad vtable
              .fill 0x30, 1, 0
              .quad empty_wstr
empty_wstr:   .quad 0
              .fill 0x38, 1, 0
ring:         .fill 16384, 1, 0
table:        .fill 49152, 1, 0
pool:         .fill 0x40000, 1, 0

stub_ret:
    ret

getwide:
    mov rax, qword ptr [rcx+0x38]
    ret

lookup_name:
    push rbx
    push r12
    push r13
    push r14
    push r15
    lea rcx, [r10+32]
    mov rbx, rcx
ln_find:
    movzx eax, byte ptr [rcx]
    test eax, eax
    jz ln_end
    cmp eax, 0x2f
    jne ln_next
    lea rbx, [rcx+1]
ln_next:
    inc rcx
    jmp ln_find
ln_end:
    mov rax, {fnv_offset:#x}
    mov r12, {fnv_prime:#x}
ln_hash:
    cmp rbx, rcx
    jae ln_hashed
    movzx r13d, byte ptr [rbx]
    xor rax, r13
    imul rax, r12
    inc rbx
    jmp ln_hash
ln_hashed:
    lea r13, [rip+table]
    mov r14, qword ptr [rip+table_count]
ln_scan:
    test r14, r14
    jz ln_done
    cmp qword ptr [r13], rax
    je ln_match
    add r13, 24
    dec r14
    jmp ln_scan
ln_match:
    mov rax, qword ptr [rip+mb_seq]
    inc rax
    lea r12, [rip+obj_a]
    test al, 1
    jz ln_pick
    lea r12, [rip+obj_b]
ln_pick:
    mov r15, qword ptr [r13+8]
    mov qword ptr [r12+0x38], r15
    mov r15d, dword ptr [r13+16]
    add r15, qword ptr [{now:#x}]
    mov qword ptr [rip+mb_until], r15
    mov qword ptr [rip+mb_seq], rax
ln_done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret

{logging_hooks}

copy_str:
    mov rcx, qword ptr [rsi+0x10]
    cmp rcx, r11
    jbe cs_ok
    mov rcx, r11
cs_ok:
    cmp qword ptr [rsi+0x18], 16
    jb cs_inline
    mov rsi, qword ptr [rsi]
cs_inline:
    rep movsb
    ret

hook_update:
    push rbp
    push rbx
    push rsi
    push rdi
    inc qword ptr [rip+cnt_update]
    mov rax, qword ptr [rip+mb_seq]
    cmp rax, qword ptr [rip+mb_seen]
    jne upd_publish
    mov rax, qword ptr [rip+mb_until]
    test rax, rax
    jz upd_done
    cmp qword ptr [{now:#x}], rax
    jb upd_done
    mov qword ptr [rip+mb_until], 0
    lea rdx, [rip+obj_empty]
    jmp upd_call
upd_publish:
    mov qword ptr [rip+mb_seen], rax
    lea rdx, [rip+obj_a]
    test al, 1
    jz upd_call
    lea rdx, [rip+obj_b]
upd_call:
    mov rbx, rcx
    sub rsp, 0x28
    mov qword ptr [rsp+0x20], rdx
    lea rdx, [rsp+0x20]
    lea rcx, [rbx+0x38]
    call {set_text:#x}
    add rsp, 0x28
    mov rcx, rbx
upd_done:
    jmp {update_back:#x}

hook_parser:
    push rbp
    push rsi
    push rdi
    push r12
    inc qword ptr [rip+cnt_parser]
    jmp {parser_back:#x}
"""

LOG_HOOK = """
hook_{name}:
    mov qword ptr [rsp+0x20], r9
    push rax
    push rcx
    push rsi
    push rdi
    inc qword ptr [rip+cnt_ctor]
    mov rax, qword ptr [rip+ring_seq]
    and eax, 63
    shl eax, 8
    lea rdi, [rip+ring]
    add rdi, rax
    mov r10, rdi
    xor eax, eax
    mov ecx, 32
    rep stosq
    mov rdi, r10
    mov rsi, rdx
    mov r11d, 31
    call copy_str
    lea rdi, [r10+32]
    mov rsi, r8
    mov r11d, 159
    call copy_str
{third}    mov byte ptr [r10+255], {kind}
    call lookup_name
    inc qword ptr [rip+ring_seq]
    pop rdi
    pop rsi
    pop rcx
    pop rax
    jmp {{{name}_back:#x}}
"""
THIRD = """    lea rdi, [r10+192]
    mov rsi, r9
    mov r11d, 31
    call copy_str
"""


def logging_hooks():
    out = []
    for name, kind, third in (("ctor", 1, ""), ("prep", 2, ""), ("prep3d", 3, THIRD)):
        out.append(LOG_HOOK.format(name=name, kind=kind, third=third))
    return "\n".join(out)


LABELS = ("magic", "cnt_update", "cnt_ctor", "cnt_parser", "ring_seq", "mb_seq", "mb_seen", "mb_until", "table_count", "vtable",
          "obj_a", "obj_b", "obj_empty", "empty_wstr", "ring", "table", "pool",
          "hook_ctor", "hook_prep", "hook_prep3d", "hook_update", "hook_parser")


def fnv1a64(data: bytes) -> int:
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def key_of(name: str) -> bytes:
    """What the hook hashes: the last path component of the event name."""
    return name.replace("\\", "/").rsplit("/", 1)[-1].encode("latin1")


def build(cave, base):
    """Assemble for a cave at absolute address `cave` in an image loaded at `base`. Returns (blob, {label: offset})."""
    ks = keystone.Ks(keystone.KS_ARCH_X86, keystone.KS_MODE_64)
    delta = base - IMAGE_BASE
    backs = {k + "_back": v[0] + delta + v[1] for k, v in HOOKS.items()}
    text = ASM.replace("{logging_hooks}", logging_hooks()).format(
        fnv_offset=FNV_OFFSET, fnv_prime=FNV_PRIME, now=INTERRUPT_TIME, set_text=SET_TEXT_VA + delta, **backs)
    enc, _ = ks.asm(text, cave)
    blob = bytes(enc)
    probe_text = text + "\nprobe:\n" + "".join(f"    .quad {n}\n" for n in LABELS)
    probe, _ = ks.asm(probe_text, cave)
    vals = struct.unpack_from("<%dQ" % len(LABELS), bytes(probe), len(blob))
    offs = {n: v - cave for n, v in zip(LABELS, vals)}
    assert len(blob) < CAVE_SIZE, len(blob)
    expect = {"mb_seq": 0x28, "vtable": 0x80, "obj_a": 0x100, "obj_b": 0x140, "obj_empty": 0x180, "empty_wstr": 0x1C0, "ring": 0x200,
              "table": 0x200 + 16384, "pool": 0x200 + 16384 + 49152}
    for k, v in expect.items():
        assert offs[k] == v, (k, hex(offs[k]), hex(v))
    return blob.ljust(CAVE_SIZE, b"\0"), offs


def table_blob(lines, cave, offs, frames_for):
    """lines: {event name: text}. Returns (table bytes, pool bytes, count). frames_for(name) -> display time, 100 ns units."""
    pool = bytearray()
    entries = bytearray()
    seen = {}
    n = 0
    for name, text in lines.items():
        k = key_of(name)
        if not k or not text.strip() or n >= TABLE_MAX:
            continue
        h = fnv1a64(k)
        if h in seen:
            continue
        seen[h] = name
        raw = text[:1023].encode("utf-16-le") + b"\0\0"
        if len(pool) + len(raw) > POOL_SIZE:
            break
        ptr = cave + offs["pool"] + len(pool)
        pool += raw
        entries += struct.pack("<QQII", h, ptr, int(frames_for(name)), 0)
        n += 1
    return bytes(entries), bytes(pool), n

# Offline pause tabs

The pause page computes connectivity/privilege state and passes it to the native
tile-group setter at RVA 0xdd8fd0. State 1 disables a group; state 0 enables it.
The existing Marketplace fix replaces its disable assignments with state 0.
This build adds the same treatment for PART_GROUP_Social (three sites),
PART_GROUP_Clubs (four), and PART_GROUP_Rivals (seven).

Each site is the exact six-byte `mov r8d, 1` preceding the group's name and call.
The replacement is `xor r8d, r8d` plus three NOPs, preserving instruction length.
The manifest records raw offsets, RVAs and original bytes. No category is hidden
or removed and no global connectivity state is changed. Server-dependent actions
retain their normal service calls. Each group has its own Frontend INI switch,
defaulting to enabled. Byte signatures, independent combinations, protection
restoration and conflict refusal are checked in the runtime fixture.

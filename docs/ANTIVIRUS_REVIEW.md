# Antivirus investigation

The supplied VirusTotal screenshot reports 22/71 detections for Extra Options
0.2.74 azimuth-tls.dll, SHA-256
4e22f6d58b29f0fa0eded0d7d25a50cbfaf931a9dc3532cef66155b0a1ba9a52.
Examples include Microsoft Trojan:Win32/Sabsik.FL.A!ml,
BitDefender Gen:Variant.Yogi.97830 and Avast/AVG Win64:Evo-gen [Trj].
These are malware detections, not merely an unknown-download reputation prompt.

A clean build of the supplied 0.2.74 source using the existing Zig 0.15.2 compiler
reproduced every PE section of that DLL byte for byte: .text, .rdata, .data,
.pdata, .tls and .reloc. This provides no evidence of an extra payload appended
by the build or distribution step. Source review found game/host memory patches,
local file access, configured save-marker repair and keyboard/controller input.
No network client, remote-process injection, persistence installation or packer
is present in the build. This review does not replace an antivirus vendor's
independent determination, and does not establish why a particular rule matched.

The previous binary was unsigned, lacked product/version resources, replaced a
TLS helper and scanned process allocations. Self-modifying hooks and unsigned
DLL proxies are plausible heuristic inputs; this is an inference, not a confirmed
cause of the reported Trojan detections. The screenshot's behavioral tags alone
are not a reliable account of the mod's source behavior.

0.2.75 leaves the original helper intact, removes the process-wide scan and heavy
initialization in DllMain, limits the temporary host hook to the exact supported
executable and restores it before installing game patches. The release includes
accurate product/version resources, repeatable unsigned builds, source and hashes.
It does not obfuscate, encrypt or pack the code, suppress antivirus, request an
exclusion or change browser protection. The DLL still patches game memory and
is still unsigned. A clean detection result is not promised or claimed.

## Release review

No file has been uploaded or submitted to an external scanner or vendor by this
agent. During validation, the user reported a Defender sample request for the
new winmm.dll and chose to send it. That request alone contains no final malware
verdict or promise of reclassification. Its result is not available here. The submitted development build may differ
from the final release; a manual vendor report should identify the exact final
DLL and its checksum. Local Defender history access was denied, so its current detection status
could not be independently checked. Browser download alerts and engine detections
need to be tracked separately.

The user subsequently reported a clean VirusTotal result for 0.2.75 and confirmed
that winmm.dll loads when placed beside azimuth.exe. Version 0.2.81 retains that
loader; its changes concern wheel-path storage/lookup and first-person drift
tracking. No external scan of
the new binaries has been performed by this agent.

For an incorrectly detected release, submit the exact final DLL as a software
developer at https://www.microsoft.com/en-us/wdsi/filesubmission and include the
source, SHA-256, this audit and a concise explanation of the game/layer hooks.
Use the corresponding vendor review portals for other detecting engines. Preserve
submission IDs and their final determinations; a new scan/hash alone is not proof
that the underlying issue was resolved.

Microsoft's developer FAQ recommends developer submissions for disputed malware
detections and a consistent trusted signing identity:
https://learn.microsoft.com/en-us/defender-xdr/developer-faq

Authenticode signing with a legitimate publisher certificate would establish
publisher identity. No certificate was supplied and the release is not signed.
Signing does not guarantee a clean malware verdict or immediate SmartScreen
reputation. Self-signing would not establish public trust:
https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation

Sign only the final validated binaries, then regenerate their release checksums.
Keep a stable release filename, source tag and reproducible build instructions.

## 27 October 2026: v0.3.2 Defender report and v0.3.3 metadata

User supplied a Defender screenshot identifying Program:Win32/Contebrew.A!ml,
category Settings Modifier, affected file winmm.dll. The subsequently supplied
installed DLL is version 0.3.2, SHA-256
`bdee5227492fc7235cce567483d4bf73876aa8bad11b3db380674f12a5496908`, matching
this task's recorded 0.3.2 release. The screenshot itself has no version/hash.
No third-party modification is evident from this exact file comparison. This
is not an independent antivirus determination of the file's behavior.

Microsoft's description publishes no technical detection details:
https://www.microsoft.com/en-us/wdsi/threats/malware-encyclopedia-description?Name=Program%3AWin32%2FContebrew.A%21ml
Therefore the detection name alone cannot establish the exact triggering code.

Both 0.3.3 adapters retain their existing version/product/original-filename and
copyright fields and add CompanyName (the existing author identity), an
adapter-specific description and honest Comments identifying this unofficial
FH2 in-process mod. No fake Authenticode certificate, packing, obfuscation,
antivirus exclusions or security-setting changes were introduced. Genuine
publisher signing remains unavailable; both files remain unsigned. Independent
build directories produce identical binaries. These metadata changes improve
traceability, not a guaranteed Defender outcome. With explicit user approval, both final 0.3.3 DLLs were uploaded to VirusTotal.
No direct vendor dispute has been submitted.

Garage Engine Rev 1.0.3 separately has a completed authorized VirusTotal result:
1/68, Cynet Malicious (score: 100); McAfee and Microsoft report Undetected.
That verdict applies only to plugin hash
`ba6b6df6446c1c0d9f6045759b57e215ba00fe26b9d40e075907dffd56e98031`,
not winmm.dll or xg.dll. No further plugin runtime changes were made in this
update, and the Cynet detection remains unresolved pending vendor review.

### Completed v0.3.3 scans

Final Azimuth winmm.dll: **0/71**, Microsoft and Cynet Undetected.
SHA-256 `149f6e1a2079a8366ffcd644b8c7c6a0621c60714916cb19926cda9876a7d975`.
https://www.virustotal.com/gui/file/149f6e1a2079a8366ffcd644b8c7c6a0621c60714916cb19926cda9876a7d975

Final Mezcal xg.dll: **1/71**, Cynet Malicious (score: 100), Microsoft Undetected.
SHA-256 `77782e9d8e17a6e914ba1ed1b373b09f63c6913c6f902d27a2ff764cfb63bbdb`.
https://www.virustotal.com/gui/file/77782e9d8e17a6e914ba1ed1b373b09f63c6913c6f902d27a2ff764cfb63bbdb

Both scans completed on 27 October 2026 without scanner timeouts; four mobile
engines reported unsupported file type. These are observed scan results for
these exact files, not proof that metadata caused a verdict change, a vendor
allowlisting decision, or a guarantee of all users' local Defender behavior.
The core source and binary checksums remain available for disputed detections.

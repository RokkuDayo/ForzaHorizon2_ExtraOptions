# Garage Engine Rev antivirus review — 27 October 2026

## Report inspected

Original DLL SHA-256:
`eb48f446f22eca24d6cc93f25d6a70bd60c387a8d3ef675921cc3ab1d1bc5e17`

The supplied VirusTotal report displayed 2/71 detections: Cynet
`Malicious (score: 100)` and McAfee Scanner `Ti!EB48F446F22E`.
Microsoft, Acronis (Static ML) and SentinelOne (Static ML) displayed Undetected.
ML describes the detection approach, not code included in this plugin.
Report: https://www.virustotal.com/gui/file/eb48f446f22eca24d6cc93f25d6a70bd60c387a8d3ef675921cc3ab1d1bc5e17

## Local evidence

A clean build of the 1.0.2 source reproduced the supplied DLL byte for byte.
The source contains four in-process vtable hooks for the known game's menu/input
interfaces, local INI/log access, and foreground-process checks to gate throttle.
Imports: CreateFileW, FlushFileBuffers, GetCurrentProcessId,
GetPrivateProfileStringW, GetTickCount64, VirtualProtect, WriteFile,
GetForegroundWindow, GetWindowThreadProcessId. No network, remote-process
injection, persistence installer, packer, encrypted payload or ML model is present.
DllMain does no work. No anti-debugging or sleep calls exist in the source;
VirusTotal behavior tags alone do not establish that this plugin calls them.
The DLL's in-process hooks and unsigned build are plausible heuristic inputs;
the exact detection causes have not been established by either vendor.

## 1.0.3 changes and verification

Added accurate Windows version/product/original-filename metadata. Rebuilt twice:
identical SHA-256 `ba6b6df6446c1c0d9f6045759b57e215ba00fe26b9d40e075907dffd56e98031`. The executable .text section and imports are byte-for-byte
unchanged from 1.0.2. No obfuscation, packing, antivirus exclusions, security
setting changes, hook concealment or signing substitute was added.

Native regression fixtures and all three logging modes passed. Fixtures do not
run live gameplay or FMOD playback. The DLL remains unsigned; no genuine signing
certificate is available in this task. The completed, user-authorized 1.0.3 VirusTotal scan reports 1/68: Cynet
Malicious (score: 100). McAfee Scanner and Microsoft report Undetected.
Skyhigh (SWG), Trellix ENS and TrendMicro timed out.
Report: https://www.virustotal.com/gui/file/ba6b6df6446c1c0d9f6045759b57e215ba00fe26b9d40e075907dffd56e98031.
These changes improve release identification and auditing; they do not establish
that the detection is corrected. A new hash alone is not proof of a resolution.

## Vendor review

VirusTotal states that false-positive determinations should be disputed with the
detecting vendors: https://docs.virustotal.com/docs/false-positive
McAfee's software-vendor review form:
https://www.mcafee.com/en-us/consumer-support/dispute-detection-allowlisting.html
Include the exact affected DLL/hash, source, this review and the detection name.
Do not claim the file is allowlisted until a vendor confirms its decision.
With explicit user approval, the 1.0.3 DLL was uploaded to VirusTotal on
27 October 2026. No vendor dispute or message has been submitted.
The remaining Cynet verdict has not been resolved.

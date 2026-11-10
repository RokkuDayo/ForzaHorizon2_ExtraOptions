# Drivatar livery loading

Local saved designs retain their existing native display path and selected livery quality. The player livery is not changed.

During native race and free-roam loading waits, texture preparation is advanced round-robin with at most one display request per update. Once all selected resources are ready, new appearances are attached one car per update. Free roam now performs these initial attachments before StartDriving, rather than leaving them for gameplay. The existing native readiness predicate and 15-second fallback remain in place.

During gameplay, selection examines only the resident design indices (at most MaxDrivatarLiveries, maximum 32) rather than repeatedly searching the entire local design library. Newly spawned cars can reuse ready handles, but cannot trigger design enumeration, container reads or texture preparation. Attaching a cached appearance can still require native renderer work; this does not guarantee elimination of all GPU stalls at 4K and 4x MSAA.

Validation: test_local_liveries.py runs native handle construction, cloning and car application in a private executable image, checks one request/attachment per loading update, initial free-roam assignments before driving, cache caps, player exclusion, cancellation, timeout, profile changes and reference lifetimes. It does not measure live GPU performance.

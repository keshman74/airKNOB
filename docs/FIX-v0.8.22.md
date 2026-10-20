# KNOB v0.8.22 — A97/A98 volume responsiveness

## Problem and resulting behavior
A97/A98 encoder volume previously waited for a new HTTPS connection and response in the UI loop. A31/A33 already use their separately verified TCP paths.

A97/A98 HTTPS absolute-volume sends now use the existing single background worker. The UI draws the encoder target immediately. One request may be in flight; all further turns replace one latest target. Completion of an older target cannot clear or delay a newer value. Requests are limited to one dispatch per120ms while turning; no extra idle poll frequency is introduced.

The worker retains one HTTPS connection for volume and getPlayerStatus when the server permits HTTP/1.1 keep-alive. It honours server closure and closes on errors, zone/IP/port/epoch changes, before artwork/other chip work, or after1500ms without another job. It never automatically replays a failed HTTP request. The existing bounded UI retry always uses the current absolute target.

The worker retains snapshot/epoch isolation across zone switches and OTA. A successful volume acknowledgement does not overwrite displayed volume with the old sent target, nor become stranded by unrelated artwork/control epoch changes.

## Preserved behavior / scope
- A31 TCP8899 and A33 TCP1234 volume paths unchanged.
- A97 confirmed PlayQueue listing and HTTPS MCUKeyShortClick launch unchanged.
- A98 PlayQueue/AVTransport subscriptions, getAllRoutines and preset correlation unchanged.
- Status fallback remains4s and group/artwork polling intervals are unchanged.
- Foreground menu/transport HTTP paths are unchanged. This release moves encoder volume to the worker; it does not claim every menu operation is asynchronous or every foreground request shares the retained worker socket.
- No A97 event capability or new RenderingControl event parsing is assumed.
- Host tests mock the network adapter; keep-alive support and speedup require measurements on the actual A97/A98.

## Diagnostics
/health:
- background_job: volume / artwork / status
- wiim_volume_sends: worker volume attempts since boot
- wiim_volume_last_ms, wiim_volume_max_ms: duration of volume requests, including TLS when needed
- wiim_https_requests: retained-worker HTTPS request attempts (volume and player status)
- wiim_https_reused: successful requests started on an already connected worker socket

Counters aggregate across zones until reboot, not per-device metrics.

## Validation
New host tests execute the actual volume dispatch/run/commit code and retained HTTPS adapter. They cover latest-target coalescing, old-zone rejection, unrelated epoch changes, bounded failure retries, artwork cancellation, offline/OTA/task allocation cases, A31/A33 route preservation, host/port/epoch session isolation, server close, errors/incomplete bodies/API rejection and absence of automatic replay.
Existing regression suites and PlatformIO build run in GitHub Actions.

Hardware baseline: user reported improved A31 and good A33 control/feedback before this change. A97/A98 optimization is implemented for hardware comparison; no measured speedup is claimed yet.

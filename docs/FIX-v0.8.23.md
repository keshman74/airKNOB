# KNOB0.8.23 — A97 volume via RenderingControl

## Evidence
On tested A97 Кухня192.168.0.99, description.xml advertises RenderingControl on ports49152 and59152. The user tested the port49152 SCPD and actions:
- GetVolume inputs InstanceID,Channel.
- SetVolume inputs InstanceID,Channel,DesiredVolume.
- GetVolume durations12,23,15ms, all returning94.
- SetVolume with the same94 returned a valid successful SOAP response in15ms.
- GetVolume readback14ms returned94; script reported OK.

These are Mac-to-player measurements, not ESP32 timing. They confirm reading and acceptance of same-level SetVolume; they do not prove a changed audible level or KNOB latency. Earlier KNOB0.8.22 A97 volume attempts took1221–1305ms, with no additional successful HTTPS reuse in the reported interval.

## Change
A97 encoder volume now first sends HTTP SOAP SetVolume to:
http://<selected-IP>:49152/upnp/control/rendercontrol1
SOAPAction: "urn:schemas-upnp-org:service:RenderingControl:1#SetVolume"
Arguments: InstanceID0, ChannelMaster, DesiredVolume0..100.

Only A97 encoder volume on the existing worker uses this route. No preceding GetVolume or following readback is added per turn. The existing latest-value coalescing and120ms minimum dispatch interval are retained.

A bounded complete response must contain SetVolumeResponse and no SOAP Fault. HTTP200 alone is insufficient. A failed SOAP attempt falls back to the existing selected HTTPS volume endpoint. Further SOAP attempts are suppressed for30s for this zone generation. IP/zone-epoch changes reset that cache. Normal idle operation adds no capability polling. A31/A33, A98, presets, metadata and all other HTTP API transport remain unchanged.

The worker closes retained TLS before the A97 SOAP attempt to release its resources. One worker job remains in flight; background status and artwork cannot start alongside it. A rare SOAP failure may be followed by the same absolute target via HTTPS within that job. This is an idempotent fallback, not an accumulated queue of intermediate values. Firmware retains existing old-zone/OTA completion rejection.

## Diagnostics
/health adds:
- wiim_volume_path: upnp / https / none (last/in-progress volume attempt)
- a97_soap_attempts: SOAP attempts since boot
- a97_soap_ok: accepted valid SOAP responses since boot
- a97_soap_http: most recent SOAP HTTP result
Existing wiim_volume_last_ms/max_ms measure the whole worker attempt, including fallback when present. All counters aggregate across zones until reboot.

## Validation and limits
Actual C++ adapter and dispatch tests passed locally: endpoint/action/body, levels0/100, bounds, SOAP Fault under HTTP200, missing/wrong response, incomplete read, network/HTTP/begin failures, 30s backoff, zone/IP isolation, millis wrap, A97 SOAP bypass of HTTPS, A98 isolation and prior latest-target/error/OTA/legacy tests.
Full regression and firmware compilation run in GitHub Actions.
Hardware KNOB verification is pending. No claim of a 15ms ESP32 command or full A97 performance resolution is made yet. Periodic A97 getPlayerStatus remains HTTPS and may occupy the shared worker before a new encoder request arrives; this release specifically removes HTTPS from normal A97 volume sends.

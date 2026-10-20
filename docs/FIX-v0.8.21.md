# KNOB0.8.21 — A97 PlayQueue preset reading

Hardware readback on A97 Кухня192.168.0.99 confirms advertised PlayQueue GetKeyMapping at HTTP49152 and59152:3538-byte replies, MaxNumber21 and six named Key1..Key6 entries. HTTPS443 getPresetInfo returns unknown command on this product.

A97 now tries GetKeyMapping49152 first, then59152 on failure. A successful empty map is authoritative; it does not trigger JSON fallback. WiiM getPresetInfo remains a final compatibility fallback only when both PlayQueue reads fail. All reads stay deferred/serialized as in0.8.20. No continuous retry/preset polling is introduced.

The shared bounded XML parser accepts MaxNumber1..64 but stores/displays only Key1..Key12 for A97 and Key1..Key20 for A31. Original IDs are retained and observed timestamp suffixes are stripped for display. The A31 SOAP implementation is extracted into the shared helper with the same port49152 and timeouts; A98 keeps its separate routine-aware loader; A33 keeps its native API.

Normal A97 control and MCUKeyShortClick:N launch still use the selected HTTPS443 endpoint. The terminal experiment confirmed list reading only; launch on the tested A97 and0.8.21 panel rendering still require hardware testing.

Health adds preset_source and preset_port to distinguish PlayQueue from the selected command endpoint. Regression tests compile the actual complete reader/parser and exercise the observed six-key shape, MaxNumber21, alternate-port fallback, partial/malformed responses, A97/A31 array bounds, HTTPS JSON compatibility fallback, rejection and A31/A33/A98 routing isolation.

Canonical hardware record: https://github.com/keshman74/airControl-Protocol-KB/blob/main/protocols/upnp/playqueue/A97-PRESETS.md .
No raw metadata, service tokens or user serial are committed.

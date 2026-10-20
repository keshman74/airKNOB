# KNOB0.8.20 — A97 preset menu support

A97 already had device classification, normal player control and WiiM HTTP preset list/launch commands. This release extends the0.8.19 serialized deferred preset-menu read to A97 as well as A98. Reads wait for the periodic worker to finish, run outside the button stack, and cancel on zone/menu/chip changes. A31 and A33 retain their hardware-confirmed preset paths.

A97 list: getPresetInfo through the selected HTTP80/HTTPS443 endpoint. Preserve original sparse slot numbers1..12 and display names. Launch: MCUKeyShortClick:N. Do not invent slots or derive active slot from vendor/service. The A98-only PlayQueue/routine/input active-slot protocol is not applied to A97 without hardware evidence.

A97 read diagnostics now report actual HTTP status, received body bytes and separate http_or_body/json_parse/json_structure errors. /health adds wiim_presets_pending; the a98_presets_pending field remains as an A98-specific compatibility alias.

Tests exercise deferred reads for both A97 and A98, including busy-worker/OTA/volume/scan gating, cancellation and chip isolation. The actual A97 loader is exercised with HTTP80 and HTTPS443 snapshots, sparse slot3/12, Cyrillic, transport failure, malformed JSON and empty list, plus existing1..12 launch-routing tests.

Status: IMPLEMENTED; consult CI linked to the source commit for test/build results. A97 hardware verification remains pending. This is not a claim that every A97 product supports the WiiM HTTP API or that A97 external active-preset highlighting is implemented.

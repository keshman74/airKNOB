# KNOB 0.8.18 — A31 MaxNumber=21 preset parsing

The user-provided A31 GetKeyMapping response reports MaxNumber=21, Key0..Key21 and populated Key1..Key4. Firmware 0.8.17 rejected every maximum greater than20 before reading any preset name, explaining the captured HTTP200 / xml_structure result.

Accept bounded map maxima1..64, but traverse only Key1..Key20 to fit the existing20-entry storage. Preserve original sparse slot IDs and timestamp-stripped labels. Key0 and records above20 remain excluded; A31 launch support still stays1..10 pending hardware evidence.

The regression fixture reconstructs the observed SOAP/escaped-KeyList/nested-Metadata structure with the four station names. It contains no user serial, service token or private URL. It also checks a fully populated21-key map cannot overflow storage, plus invalid maximum rejection.

The separate A98 restart has not been diagnosed or fixed by this patch. /health adds numeric ESP reset_reason and raw loop/worker stack high-water marks for the next hardware capture. The counters are diagnostics, not proof of a particular crash cause. A prior HTTP200 key-map response followed by uptime2 confirms a KNOB restart, not a player restart. User reports A33 presets normal.

Validation: see the source commit's CI run for actual parser regression and ESP32 build results. Hardware confirmation of0.8.18 remains pending.

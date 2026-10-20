# KNOB multi-chip protocol reference — 2026-10-06

Source of API mappings: keshman74/airControl docs/PROTOCOL_COMMAND_REGISTRY.md (2026-10-05), src/main.jsx; existing KNOB A31 behavior.

Evidence levels: HW VERIFIED refers to prior user device tests of the protocol. NEW KNOB ADAPTER requires new tests on each real chip. A successful compiler check does not confirm device support.

| Function | A31 | A97 ALLWINNER/R328 | A98 AMLOGIC/A113 | A33 CLOUDYX |
|---|---|---|---|---|
| Discovery | SSDP + bounded local /24 checks | Same, ports 443/80 | Same, ports 443/80 | SSDP + UDP 53308 broadcast + ports 8000/8443 |
| Identity | getStatusEx hardware/project | getStatusEx hardware/project | getStatusEx hardware/project | getStatusEx ProjectName/DevVolumeL |
| API | HTTP 80 /httpapi.asp?command= | Runtime HTTP/HTTPS Linkplay endpoint | Runtime HTTP/HTTPS Linkplay endpoint | HTTP 8000 or HTTPS 8443 /?Instruct= |
| Volume/play/mute | MCU TCP 8899, HTTP fallback | API, no assumed TCP 8899 | API, no assumed TCP 8899 | TCP JSON 1234, ACS2 fallback |
| State | TCP notifications + periodic HTTP | Periodic HTTPS | Periodic HTTPS | TCP events + getStatusEx periodic fallback |
| Artwork | UPnP GetPositionInfo, image download | getMetaInfo then UPnP | getMetaInfo then UPnP | AllMate/AltMate.TrackImage |
| Presets | MCU+KEY+001..010 / MCUKeyShortClick | MCUKeyShortClick via API, test required | Same, test required | setPlayerCmd:playPreset:N (desktop mapping, test required) |
| Sources | Native source modes | Modes via Linkplay API | Modes via Linkplay API | Text source names via ACS2 |
| Multiroom | JoinGroup / LeaveGroup | Linkplay group | Linkplay group | setHost / setSlave:IP / disconnectSlave / breakUp |
| EQ | MCU EQ presets/tone | EQGetBand/SetBand probed at runtime; support NOT ASSUMED | EQGetBand/SetBand/Load; desktop implementation | getEqInfo:1/:2; setEqHighAndLowFrequencies; setPresetEq; eqEnable |
| Virtual Bass / Mid tone | Verified MCU RAKOIT | No verified command in registry | No verified command in registry | No verified separate commands in registry |

Cross-family native grouping A33 ↔ Linkplay is rejected. A97 8899 is unknown/unverified and is not used. A33 native 23040 framing is not introduced in this firmware; its verified controls use TCP JSON 1234 and ACS2.

Source capability DevFunction is used when present. Unconfirmed inputs are not advertised as physical USB support on A97/A98. Graphic EQ availability is checked by reading native band arrays before editing; failure displays EQ API UNAVAILABLE. Virtual Bass remains A31 only.

TLS local devices use self-signed certificates; certificate validation is disabled for the local player transport, matching the existing desktop approach. Online updater still downloads GitHub using browser HTTPS.

This version is an experimental branch. Stable main/latest.json remains v0.7.3 until hardware verification. Test: discover all four chips in display and web; select and reboot; volume/play/mute; phone feedback; presets; all physically present sources; artwork; same-family grouping/leave and Slave indicator; EQ read/write/readback; unavailable EQ paths; OTA file installation.

# v0.8.4

Hardware report: v0.8.3 discovers one A31, but Office 192.168.0.35 still missing. Device-specific cause unconfirmed without its getStatusEx response. Added final on-demand port-80 retry sweep for missing hosts with 250ms connect allowance; found zones are skipped. Search takes longer; UI shows retry pass as R. No constant scan added.

KNOB INFO order: VERSION, IP:, address on own line, RAM total/used/free, firmware size. Five-row scrolling viewport retained. Published firmware named airControl-KNOB-v<version>.bin; manifest points to versioned file. Legacy firmware.bin alias retained. Browser-mediated online upload uses versioned filename.

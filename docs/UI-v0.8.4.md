# v0.8.4

Hardware report: v0.8.3 discovers one A31, but Office 192.168.0.35 still missing. Device-specific cause unconfirmed without its getStatusEx response. Added final on-demand port-80 retry sweep for missing hosts with 250ms connect allowance; found zones are skipped. Search takes longer; UI shows retry pass as R. No constant scan added.

KNOB INFO order: VERSION, IP:, address on own line, RAM total/used/free, firmware size. Five-row scrolling viewport retained. Published firmware named airControl-KNOB-v<version>.bin; manifest points to versioned file. Legacy firmware.bin alias retained. Browser-mediated online upload uses versioned filename.

## Database cross-check

Read current airControl docs/PROTOCOL_COMMAND_REGISTRY.md and electron/main.cjs. Current progressive discovery validates getStatusEx, extracts both datagram sender and LOCATION IPv4, and network-source discovery listens for passive multicast NOTIFY on 1900. Ported LOCATION parsing and temporary NOTIFY listener to KNOB; every candidate is still local-subnet filtered and API validated. Multicast listener stops on completion/cancel. Historical DISCOVERY_V4.1.1.md says fallback scan only when SSDP found nothing; newer actual code v4.2.2 runs progressive SSDP and scan concurrently. No speculative Linkplay UDP discovery packet added.

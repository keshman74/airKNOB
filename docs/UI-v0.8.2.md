# v0.8.2

Hardware report: v0.8.1 volume queue replay caused continued motion after encoder stopped. Fixed by draining all pending detents into one clamped target per frame; A31 volume pacing now nonblocking (220ms retained). Menus retain direction-preserving individual steps.

XHR progress for both local-file and browser-mediated online firmware upload. Browser shows bytes transferred then waits for server verification; LCD shows Update.write bytes, verification, success/failure. Optional X-Firmware-Size header supplies payload size. Failed/aborted updates are not marked complete.

KNOB INFO adds firmware version, last line remains IP, six lines use five-row scrolling viewport. Network save responds with monochrome restart page, replaces /save in browser history, then redirects to root/new static IP after 7 seconds. DHCP reassignment to an unknown new IP cannot be inferred before reboot; user may need to find the new DHCP address. Hardware validation pending.

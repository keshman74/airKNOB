# v0.8.1

Monochrome WiFiManager captive portal. Interrupt-driven encoder with 127-detent direction-preserving queue (4 edges/detent). Volume commands coalesced at up to 12.5/s, no wait for rotation to stop. Arc and volume label painted in final colours without black/grey erase or full frame allocation. Two-second play/pause icon and SELECTED feedback after successful commands. Host tests cover actual ISR: 60 queued steps, reversal, bounce, invalid transitions. Hardware validation pending.

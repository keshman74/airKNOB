# KNOB 0.8.19 — A98 preset menu hardening

## Hardware evidence
A310.8.18 preset fix was confirmed by the user. On A98, KNOB0.8.18 restarted around preset-menu entry. Subsequent health returned uptime3, reset_reason4, loop watermark3808 and worker watermark4012. These post-boot stack numbers do not measure the prior failing call.

ESP-IDF4.4.7 esp_reset_reason_t enumerates UNKNOWN0, POWERON1, EXT2, SW3, PANIC4, INT_WDT5, TASK_WDT6, WDT7. Therefore4 means panic/exception; it does not identify an interrupt watchdog or establish stack exhaustion. Reference: https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_system/include/esp_system.h

## Changes
A98 menu entry and Refresh enqueue a read rather than running networking inside pollButton -> chooseMenuItem -> setMenu. The UI loop drains any background response first and performs the read only after the button stack unwinds and the worker is idle. No new periodic background job or A98 subscription maintenance is started while the read is pending. Back/zone changes cancel an obsolete request; OTA, volume and discovery defer it. Failed attempts update the activity timestamp and pace metadata retry.

The temporary12-key A98 model uses persistent UI-task-only scratch storage. mapping() clears it before reuse. This removes the second model from the nested loop-task stack. A31 and A33 retain their existing menu-read paths; A31 MaxNumber21 fix is preserved.

## Validation and limits
Host regression tests exercise actual deferred-reader functions: no network read while worker busy, one-shot execution, cancel on menu/zone/chip changes, OTA/volume/scan deferral, failure retry pacing, and A31/A33 isolation. Existing A98 routine/model tests cover repeated model loads, preserving active input and unresolved routines.

These changes address concrete concurrency and stack-depth risks. They are NOT proof that the observed PANIC is fixed or that either risk caused it. If A98 still restarts, capture the serial Guru Meditation/Error/Backtrace from the same build and the subsequent /health response. Do not increase/disable watchdogs or factory-reset settings as a substitute for diagnosis.

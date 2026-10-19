# Mode 4: acknowledgement recovery and measured delivery speed

Revision: `adaptive-goodput-1`, 17 October 2026.

**Build correction, 18 October:** the policy method is now named `applyGroupCap`. Its previous name, `constrain`, collided with Arduino's three-argument macro and prevented compilation. The host Arduino stub now includes the same macro to catch this conflict. This correction changes the method name, not the transfer policy.

## Changes

1. **Check the receiver before shrinking.** The first missing bitmap reply sends another query and leaves the chosen group size in place. The sender keeps the original group timer, so the recovery wait counts in its measured speed. Two consecutive unanswered requests allow a smaller fallback. Four unanswered requests retain the existing pause and manifest recovery.
2. **Choose sizes using measured speed.** The sender divides newly confirmed image bytes by the group's elapsed time, including queueing, transmissions, retries, listening gaps and acknowledgement waits. It tries another size and adopts it only when the measured rate improves by more than 5%. It rechecks alternatives after three measured groups at the preferred size. A bitmap reporting missing data prompts a smaller trial.

The initial size is still two chunks. The existing 8-second data-airtime cap allows five full chunks with the current radio settings. SOS/chat/resolution traffic keeps its priority; groups interrupted by it do not train the speed policy. Short final groups and manifest replies also do not train it. A route change or a receiver bitmap rollback clears the rate estimate. Each photo learns independently.

Mode 4 continues using wire protocol 4 and the existing durable bitmap/CRC completion acknowledgement. Modes 1–3 retain their transfer behaviour. The photo bytes, radio parameters, page controls and stored record format are unchanged.

## Upload and try it

1. Open `C:\Users\cyftl\Documents\Arduino\testing\testing.ino` in Arduino IDE.
2. In `TransferConfig.h`, select `TRANSFER_ADAPTIVE` (mode 4).
3. In `Config.h`, select `ROLE_CIVILIAN`, choose the civilian board's current port, and upload.
4. Select `ROLE_RESCUER`, choose the rescuer board's current port, and upload. Use the same version and mode on any relays, with the civilian role for a relay.
5. Check each startup log for `[FIRMWARE] adaptive-goodput-1`, `mode=4 (adaptive)`, and `protocol=4`.
6. Send the same prepared JPEG used for mode 3. Measure until the civilian receives image-completion acknowledgement and verify the finished photo on the rescuer.

Port numbers can change after reconnecting USB. Select each board by its role/node identity. Upload normally with the existing partition layout; no erase or filesystem formatting is needed.

## Read the new log

The civilian prints one `[ADAPT]` line when it accepts a data group's bitmap:

| Field | Meaning |
| --- | --- |
| `chunks` | Selected group size for the measurement |
| `confirmed_bytes` | Newly acknowledged bytes from this group, excluding bytes already recorded as received |
| `elapsed_ms` | Time from group planning through saving its bitmap reply |
| `bytes_per_s` | Confirmed bytes divided by elapsed seconds, rounded down |
| `next` | Selected size for the next full group; it can be a trial |
| `missing` | Whether the returned bitmap omits any chunk in this group |
| `ack_timeouts` | Consecutive unanswered requests before this reply |
| `used_for_policy` | Whether this sample was eligible to update the size policy |

Use the existing `[EXP]` `START`/`DELIVERED` events for the whole-photo result. A group rate is not the complete image transfer time. Label old and new mode-4 results separately using the firmware revision. Trial groups can be slower, so this change does not guarantee a speedup over mode 3.

## Verification

The final host suite passed all eight combinations of modes 1–4 and civilian/rescuer roles. Targeted checks cover missed replies, confirmed missing chunks, larger/slower trials, smaller/faster trials, timeout bounds, partial JPEG chunks, duplicate replies and clock rollover. The existing suite also exercises persistence, CRC recovery and urgent traffic.

These are host simulations. An ESP32 firmware build and physical radio timing were not run for this update, and no board was flashed. Arduino IDE will compile the selected role during upload; hardware timing remains for the user's board test.

Backup before this change:
`C:\Users\cyftl\Documents\Arduino\backups\testing-before-adaptive-goodput-20261017-193801.zip`.

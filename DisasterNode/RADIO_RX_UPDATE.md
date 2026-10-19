# Radio reception update: radio-rx-guard-2

17 October 2026.

## What the supplied capture establishes

The rescuer ran role rescuer, mode 3, protocol 4, with zero stored records and zero Wi-Fi clients. Its maximum web service time was 1 ms and its maximum gap between radio service calls was 29 ms. It completed beacon transmissions but recorded no accepted incoming packets. HTTPS blocking on that rescuer does not explain this capture.

The previous counters could not distinguish silence from failed radio reads: `readData()` errors and short frames were dropped before the application counters. Most calls to `startReceive()` also ignored their return value. Consequently, `radio=1` could remain visible after a failed receive restart. The available capture does not establish which of these, a different radio setting, or an RF/link problem caused the missing SOS.

## Changes

- Check actual RX_DONE/TX_DONE flags before handling an interrupt. A leftover channel-scan interrupt cannot be treated as a received frame or a completed transmission.
- Poll completion flags every 10 ms when interrupts have not already triggered processing. This also runs while the node is idle, without restarting an in-progress reception.
- Check every receive restart. Failed restarts suspend new radio work and retry once per second. `radio=0` reports this pending recovery; queued and saved messages remain available.
- Report failed reads, receive/header errors, short frames and failed restarts separately. Diagnostic messages are limited after the first few errors.
- Print the selected radio parameters at startup and additional counters every 30 seconds. The last error is retained after recovery; zero indicates that no receive error has been recorded since boot.

The packet formats, transfer modes, radio settings, camera controls and stored-record layout are unchanged. This update addresses receive handling and missing diagnostics; it is not a verified resolution of the reported hardware failure.

## Upload and capture

1. Upload the updated sketch to both boards with the appropriate role in `Config.h` and mode 3 in `TransferConfig.h`.
2. Check for `[FIRMWARE] radio-rx-guard-2` on each board. Compare both `[RADIO_CONFIG]` lines.
3. Keep the rescuer phone disconnected for this check. Keep both boards powered and the civilian phone connected.
4. Send a fresh text-only SOS or choose Retry pending. Capture the rescuer's `[RADIO_CONFIG]`, `[RADIO]`, `[HEALTH]` and `[RADIO_HEALTH]` lines, plus the civilian's transmission and health lines from the same attempt.

Counters in `[RADIO_HEALTH]`:

| Field | Meaning |
| --- | --- |
| `rx_raw` | RX_DONE events handled, before read/CRC/application validation. |
| `rx_read_errors` | Radio read, CRC/header or receive timeout errors. |
| `rx_length_errors` | Completed frames outside the application's permitted length. |
| `rx_start_errors` | Receive-start commands that returned an error. |
| `polled_events` | RX/TX completions handled without a software interrupt flag already set; this can include a race with the callback. |
| `last_rx_error` | Last recorded RadioLib receive error (`-6` timeout, `-7` CRC/header error). |

If both raw reception and receive-error counters remain zero during a confirmed civilian transmission, compare the two radio configuration lines and investigate the radio link. If application rejection counters increase, their version/authentication messages identify a different stage of failure. `SOS_ACK_QUEUED` means an ACK was queued; the civilian must still process that ACK before showing delivery.

## Validation status

Initially this update received source inspection only, following the user's request to skip tests. The later request to investigate and repair the failure authorized further checks. On 17 October 2026, the native suite passed for all four modes and both roles. Physical radio checks and the restored mode-2 application were then exercised on both boards. Reception returned after the antennas were reconnected and the boards power-cycled. See [RADIO_LINK_CHECK.md](RADIO_LINK_CHECK.md) for the results and their limits; modes 1, 3 and 4 have not been validated on hardware in this repair session.

Pre-change files: `C:\Users\cyftl\Documents\Arduino\backups\testing-before-radio-receive-20261017-155530.zip`.

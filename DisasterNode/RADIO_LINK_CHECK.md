# Radio link check — 17 October 2026

## Findings

Both boards originally ran `radio-rx-guard-2`, mode 2, protocol 3, with matching radio settings. The civilian completed transmissions, but neither board recorded raw received packets. Both remained running with available heap and stack, including with no phones connected.

A temporary radio-only sketch reproduced the failure without the website, storage, mesh routing or transfer policies. It received no packets in either direction at 2 dBm or 22 dBm. Raising the oscillator supply from 1.6 V to 1.8 V and enabling boosted receive gain did not restore reception in that check.

After the user's second antenna reconnection and power cycle, the same test sketch received **8 of 8 packets**: two packets in each direction at each power setting. Each packet was 238 bytes. All reads succeeded; packet RSSI ranged from approximately -53 to -38 dBm. This successful run began with the original 1.6 V oscillator setting.

This associates the recovery with the physical reconnection/power cycle. It does not identify a particular loose connector, prove overheating, or establish that either radio is damaged. No application source changes were needed to obtain this recovery.

## Restored application and board identity

The original application from each board was restored and its flash contents verified by the upload tool. Both run **mode 2 (tuned), protocol 3, revision `radio-rx-guard-2`**. Only the application partition was written; stored reports, messages and photos were retained.

Windows reassigned the serial ports after reconnection:

| Role | Full node ID | Current port | Port before reconnection |
| --- | --- | --- | --- |
| Civilian | `000098d50404a7ac` | COM5 | COM9 |
| Rescuer | `000048120604a7ac` | COM8 | COM5 |

Choose firmware roles by board identity. A COM number can change and does not assign the board's role.

The restored rescuer received all six pending SOS reports and queued acknowledgements. The civilian logged `DELIVERED` for acknowledged reports and then for the queued 2,320-byte photo. The photo completion event was at civilian uptime 173,669 ms, with chunk index 12 and bitmap `0000000000001fff` (all 13 chunks). The receiver's completion acknowledgement follows image validation in the application. Its health log showed seven records: six SOS reports plus the photo.

The captured health reports showed no read, receive-start, frame-validation, authentication or storage errors, and no reboot/panic occurred during the restored application capture. Some queued reports required retries while the backlog was being sent. The roughly 174 seconds since boot includes that backlog and must not be presented as the isolated transfer time for this photo.

## Validation limits

- The native test suite passed for modes 1–4 with both roles.
- The temporary radio sketch compiled successfully with ESP32 core 2.0.17 and RadioLib 7.8.1.
- Physical application validation in this repair uses mode 2. Other modes and phone interactions require their own checks.
- These are recovery checks with an existing queue, not a controlled image-speed benchmark.
- Both serial capture sessions ended and released their ports. No diagnostic sketch remains installed.

## Recovery copies and captures

Full 8 MB flash backups are in `C:\Users\cyftl\Documents\Arduino\backups`. Their filenames use the **old** port numbers: `before-link-repair-COM9-20261017.bin` is the civilian and `before-link-repair-COM5-20261017.bin` is the rescuer. Separate application-only copies are in the same directory. These backups include local firmware credentials and stored data; keep them private.

The source backup is `testing-before-link-repair-20261017-164141.zip` in that directory.

Serial captures are in `C:\Users\cyftl\AppData\Local\Temp`:

- `heltec-probe-after-second-reconnect.log`
- `heltec-restored-civilian-COM5.log`
- `heltec-restored-rescuer-COM8.log`

The temporary test sketch is in `C:\Users\cyftl\AppData\Local\Temp\heltec-link-repair-20261017\radio_probe`. It is no longer installed on either board.

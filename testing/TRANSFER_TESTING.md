# Four-mode implementation: verification and handoff

**Second hardware finding, 16 October:** a new trace (`23fdaf6e637231fb`) confirms that the combined SOS/web/radio/filesystem call chain overflowed the 8 KB loop-task stack. The source now allocates 16 KB and moves large request buffers out of the stack. See [LOOP_STACK_FIX.md](LOOP_STACK_FIX.md) for verification and retesting. The earlier host results below did not establish ESP32 runtime stack headroom.

**Later hardware finding, 16 October:** the civilian crash log was decoded against its exact ELF and traced to the HTTPS library using a freed TLS connection. That library is now patched; 2,000 simulated disconnect cycles and the civilian mode-1 firmware build passed. See [HTTPS_DISCONNECT_FIX.md](HTTPS_DISCONNECT_FIX.md) for the fix and required upload/retest. The earlier build history below is retained.

16 October 2026. The four modes and test helpers are implemented. No physical board was flashed or measured during this work. The user chose to proceed with hardware testing instead of further automated build runs.

## Completed checks

- Final production C++ host tests passed for all **eight mode/role combinations**. These use simulated Arduino, LittleFS, RadioLib and HMAC interfaces.
- Covered persistence and failed commits, cache hints after failed saves, reordered/duplicate chunks, CRC restart, interrupted finalisation repaired by a bitmap query, lost bitmap replies, selective retransmission, stale/invalid bitmap rejection, sender/receiver restarts, a completion reply after the fourth request, queue eviction before transmission, SOS priority and the receive-in-progress guard.
- Five Python tests passed for serial parsing, incomplete transfers, restart segments, clock wrap, the preparation-page generator, exact JPEG-byte upload and reuse of the report ID after a simulated failed response.
- The offline preparation page passed a real Chromium test at phone width: missing-file feedback, JPEG compression within 8192 bytes and 160 × 120, byte-for-byte download verification, no horizontal overflow and no page JavaScript errors. Its screenshot was visually inspected.
- The original `NetworkSecrets.h`, certificate/key files, portal routing, embedded web assets and `data/` page files were verified byte-for-byte against the backup. Radio settings, open Wi-Fi, the rescuer role and disabled automatic formatting were preserved.

These checks exercise software state transitions. They do not establish actual RF throughput, flash behaviour during a physical power cut, battery use, peak heap or SOS latency on the boards.

## ESP32 compilation status

Target: `esp32:esp32:heltec_wifi_lora_32_V3`, ESP32 core 2.0.17, RadioLib 7.8.1, ArduinoJson 6.21.5 and ESP32_HTTPS_Server 1.0.0 with the existing compatibility patch.

An initial complete matrix of **all eight mode/role builds passed**. Subsequent changes refined measurement logging, receive-state handling and RAM-only scheduling hints. All final host tests passed after those changes.

The final matrix rebuild completed **mode 1 / civilian**:

| Flash | Static RAM |
| --- | --- |
| 1,242,317 bytes (37%) | 63,352 bytes (19%) |

The next build, mode 1 / rescuer, reached firmware packaging but stopped because the generated temporary `build/partitions.csv` file was missing. This was a build-artifact error, not a reported C++ source diagnostic. The remaining final matrix builds were not run after the user chose hardware testing. **Do not treat the other seven earlier binaries as verified builds of the final source.**

Private artifacts and logs are under `C:\Users\cyftl\AppData\Local\Temp\heltec-transfer-validation-20261016`. That folder contains both initial-build outputs and the attempted final rebuild; consult the status above. Binaries contain the mesh key and dashboard password.

Use Arduino IDE **Verify**, followed by **Upload**, for the selected role and mode. If the IDE reports a missing temporary `partitions.csv`, retain the compiler output and rebuild with a fresh build directory/restarted IDE. Keep the existing board and partition selection; do not format LittleFS or erase all flash to resolve a compiler temporary-file problem.

## Hardware tests to do next

Follow `EXPERIMENTS.md` for the full procedure and measurement tools.

1. Start with mode 1 on one civilian and one rescuer. Verify and upload each role. Keep antennas, positions and radio settings fixed.
2. Confirm SOS delivery, coordinates and chat in both directions.
3. Send the same prepared JPEG and measure until the sender receives image-completion acknowledgement. Confirm that the rescuer displays the correct finished image.
4. Repeat with modes 2, 3 and 4, using the same mode on **every** participating node. Modes 3/4 use protocol 4; modes 1/2 use protocol 3.
5. Repeat each comparison several times, rotating mode order. Record compressed byte count, completion time, success/failure and whether relays were used.
6. During an image transfer, submit another SOS and record its acknowledged-delivery delay.
7. Test an intermediate relay and temporary disconnections. With disposable test reports, interrupt/restart the sender and receiver separately and check that transfers recover with a valid complete JPEG.

For controlled comparisons, use the provided uploader to send identical prepared JPEG bytes. The CSV distinguishes queue-to-ACK time from first-radio-transmission-to-ACK time. Both are acknowledged-delivery measurements, not a one-way arrival timestamp. Restart downtime needs an external clock. Keep incomplete runs in the delivery-success calculation.

Current source selection: **mode 1 (baseline), ROLE_RESCUER**. The current local portal uses HTTPS with HTTP redirection; this transport update preserved that configuration.

Original project backup: `C:\Users\cyftl\Documents\Arduino\backups\testing-before-four-modes-20261016-135419.zip`.

# Four versions of the image transfer protocol

The sketch contains four selectable modes. Image compression, JPEG bytes, radio parameters, durable storage, authentication and the application stay shared. Select the mode in `TransferConfig.h` and each board's role in `Config.h`. The page header and startup log show the values actually running on the connected board.

The original project is saved in `C:\Users\cyftl\Documents\Arduino\backups\testing-before-four-modes-20261016-135419.zip`. It contains private keys. Mode 1 in the working project has measurement logging added; the archive preserves the original files.

## 1. Choose a mode and upload every node

Open `TransferConfig.h`. Change just this selection:

```cpp
#define TRANSFER_MODE TRANSFER_BASELINE
```

| Version | Selection | Behaviour | Radio protocol |
| --- | --- | --- | --- |
| 1 | `TRANSFER_BASELINE` | Original stop-and-wait: 300 ms after image ACKs, 200 ms planning interval, 15 s ACK timer plus jitter, original retry cooldown | 3 |
| 2 | `TRANSFER_TUNED` | Stop-and-wait: 30 ms after image ACKs, 20 ms planning interval, reduced image packet jitter, timeout based on airtime and estimated path length | 3 |
| 3 | `TRANSFER_FIXED` | Four chunks per group, then a bitmap query and one bitmap reply; retransmit missing chunks | 4 |
| 4 | `TRANSFER_ADAPTIVE` | Compare measured confirmed bytes/second for different group sizes; query before shrinking after a missing reply; retain urgent priority and airtime cap | 4 |

Set `CURRENT_NODE_ROLE` in `Config.h` for each upload: civilian for the sender and intermediate relay, rescuer for the destination. **Use the same transfer mode on every participating node for a test.** Modes 3/4 cannot communicate with mode 1/2 radios. The mode is printed at startup and available from `/api/status` as `transferMode` and `transferName`.

Upload normally using the existing Heltec LoRa V3 board settings. Keep the partition layout and formatting setting unchanged. Stored `/mesh3` records use the same layout in every mode, so changing the mode does not erase reports or photos. Finish pending experiments before switching modes, or label the next run as a recovery run.

The portal files currently in this project use **HTTPS at https://192.168.4.1/** with HTTP redirection. This transport update preserves that configuration, the open Wi-Fi networks, coordinates, dashboard login and browser photo compression.

## 2. Prepare a photo once

From this sketch folder, run:

```powershell
C:\Python314\python.exe tools\experiment.py prepare
```

Open the generated `prepare-test-photo.html` in your browser, choose a photo and download `test-photo.jpg`. The page reuses the application's actual browser compression function. Save the resulting JPEG once and reuse that exact file for all modes. Choose several photos of different compressed sizes for a later campaign.

The experiment uploader below sends those bytes unchanged. The normal phone form still compresses each selected original photo, so use the uploader for controlled comparisons.

## 3. Record Serial output

Connect the civilian board to the computer. Close Arduino Serial Monitor, then open a PowerShell window in the sketch folder:

```powershell
.\tools\capture_serial.ps1 -Port COM5 -OutputFile mode1-direct-run1.log -Minutes 5
```

Replace `COM5` with the civilian's actual port. Capture starts before submitting the test. The script keeps normal startup messages and CSV measurement events. It does not overwrite an existing log. Stop with Ctrl+C if needed. For relay diagnostics, record those nodes in separate windows/files too.

If opening the serial port resets your board, wait for its startup to finish before submitting. Do not count boot-up as a normal transfer test. Keep logging enabled in all four modes; logging adds overhead and must be consistent.

## 4. Send identical image bytes

Connect the computer's Wi-Fi to the civilian node. In a second PowerShell window run:

```powershell
C:\Python314\python.exe tools\experiment.py upload --jpeg test-photo.jpg --mode 1 --scenario direct-clear --out mode1-direct-run1.json --insecure
```

`--insecure` accepts this board's local self-signed HTTPS certificate. If you later configure plain HTTP, use `--base http://192.168.4.1` and omit that option. The helper checks the node role, readiness and mode. It creates a fresh test SOS, then uploads the JPEG. Retries within the command reuse the same IDs. Keep the JSON file private: it includes the test's browser access token as well as the JPEG SHA-256 and CRC32.

The returned message means **saved on the civilian node**. Wait for a `DELIVERED` event with `kind=3` for that report ID, and check the finished photo on the rescuer. The source marks completion only after the rescuer verifies the complete JPEG and saves its completion state.

Repeat with modes 2, 3 and 4, changing the mode on **all nodes** and using a fresh log and JSON filename. Change `--mode` to match. Keep antennas, placement, transmit power, spreading factor, bandwidth, coding rate and the image file unchanged within each comparison. Rotate mode order across repetitions to reduce bias from changing RF conditions.

Each node currently stores at most **8 photos and 96 records**. Check capacity before a campaign. Export the needed logs/results before explicitly using **Clear all** between test batches; it deletes that board's reports, messages and photos. Clear the civilian first, then the rescuer and any relay holding pending test reports. See [CLEAR_ALL.md](CLEAR_ALL.md). Routine transfers never automatically erase history.

## 5. Export the measurements

```powershell
C:\Python314\python.exe tools\experiment.py summarise mode1-direct-run1.log mode2-direct-run1.log mode3-direct-run1.log mode4-direct-run1.log --out comparison.csv
```

Open `comparison.csv` in Excel. Each row represents one source-side transfer during one boot:

| Field | Meaning |
| --- | --- |
| `kind` | 1 = SOS, 2 = chat, 3 = image, 5 = resolution |
| `bytes`, `crc32` | Logical payload size and checksum; use the trial JSON's SHA-256 to confirm identical JPEGs |
| `elapsed_ms` | From local save/first observation of pending work to the sender's durable delivery update; an image can include waiting for its SOS ACK |
| `radio_elapsed_ms` | From the first local radio transmission to the sender's durable delivery update; excludes browser compression and Wi-Fi upload |
| `tx_frames` | Successfully completed local transmissions for this logical item, including manifests and queries |
| `repeated_chunks`, `repeated_data_bytes` | Repeated image chunk transmissions within this boot, including repeats needed after a CRC restart |
| `timeouts` | Expired retry timers observed by the scheduler; mode 1 starts its timer when queued, optimized image modes start after transmission |
| `tx_airtime_ms` | Calculated airtime of successful local transmissions, not a measurement of energy or total network airtime |
| `max_group` | Largest selected data group; an adaptive group can close early for urgent traffic |
| `status` | `delivered` or `incomplete`; missing completions are retained |
| `resumed` | Loaded from pending storage at startup; this boot segment is not a fresh complete-transfer trial |

Uptime clocks cannot measure downtime between reboots. Recovery experiments need an external stopwatch or timestamped capture in addition to the segment CSV. Do not discard incomplete trials when computing delivery success. Report median and 95th-percentile completion/SOS delays across repeated trials, alongside success rate and retransmissions. Measure energy and peak heap separately on hardware.

## What the adaptive mode actually does

- Revision `adaptive-goodput-1` starts at two chunks. A complete measured group establishes a delivery-rate estimate, then the policy tries a larger group. It keeps a trial only if its measured rate exceeds the preferred size's rate by more than 5%. A slower trial returns to the preferred size. After three measured groups at that size it probes again, alternating larger and smaller trials. Sizes are doubled/halved, within the airtime cap and a minimum of one.
- Delivery rate is **newly confirmed image bytes / elapsed seconds** for the group. The clock starts before enqueueing its first chunk and ends after the sender saves the bitmap reply. It includes queueing, retransmission attempts, listening gaps, bitmap queries and lost-reply waits. Partial final chunks contribute their actual byte counts; duplicate replies do not add bytes.
- One missing bitmap reply retries the **query**, increases the timeout and preserves the selected group size. Two consecutive unanswered requests permit a smaller fallback. A returned bitmap with missing chunks triggers a smaller trial whose rate must still improve on the preferred size. After four unanswered requests the existing pause/manifest recovery remains in effect.
- Manifests, groups shortened at the end of a photo, and groups interrupted by SOS/chat/resolution traffic do not train the size policy. A route change or receiver bitmap rollback discards the sample and resets its rate estimate. Learning is held in RAM for the current image, and starts afresh on another image or reboot.
- Uses a smoothed request/reply delay and its variation to set an ACK timeout, with an airtime/path-based minimum and a 60 s maximum. These defaults are initial engineering estimates to validate on your topology.
- Slow ACKs influence the measured delivery rate and timeout without directly limiting a group to two chunks. Recent SOS/resolution activity or queued urgent traffic limits the next group to one; a partly sent group can close at the next packet boundary.
- Limits data airtime per group to 8 seconds. At the current SF9 settings a full 180-byte chunk needs about 1.574 seconds, so the effective cap is five full chunks even though the desired maximum is eight.
- Gives the radio at least 200 ms of listening time after each local data packet, with extra time for the estimated relay path. ACK TTL estimates the return-path hop count. Set `TRANSFER_MIN_PATH_HOPS` to a larger known bound for asymmetric paths. This is a conservative spacing heuristic, not a mesh reservation protocol.

Mode 4 prints `[ADAPT]` lines with the attempted `chunks`, `confirmed_bytes`, `elapsed_ms`, `bytes_per_s`, the `next` selected size, `missing`, `ack_timeouts`, and `used_for_policy`. These explain individual decisions; the existing `[EXP]` events and CSV remain the measurements for whole-transfer comparisons. Probe groups can be slower, so a speedup over mode 3 is not guaranteed. Label earlier mode-4 results separately from revision `adaptive-goodput-1`.

Modes 2–4 share the same guard against starting channel assessment while a valid packet is already being received. ACK/SOS/chat priority remains active. A packet already being transmitted cannot be interrupted, and collisions/hidden nodes remain possible. The implementation provides opportunities to receive urgent traffic; it does not establish a guaranteed SOS latency bound.

The faster scheduler in modes 2–4 uses RAM-only delivery hints to skip already delivered history without reopening its files every 20 ms. These hints update only after a successful storage commit and are rebuilt on startup; the on-flash record layout remains unchanged.

## ACKs, persistence and restart behaviour

Each chunk still passes the same write, flush/close, read-back and metadata-save path in all modes. Bitmap ACKs only include committed chunks. The receiver does not acknowledge a chunk just because it reached RAM. Storage checkpoint frequency is deliberately held constant for this comparison; adaptive storage batching is not implemented.

The group receiver answers the initial image manifest and later queries with a full 64-bit bitmap plus a completion flag. Every answer echoes the exact request packet ID. The sender rejects stale responses, bits beyond the actual image and completion flags without all chunks. It saves the new bitmap before advancing. A fresh manifest after restart recovers the receiver's stored bitmap, including any reset caused by a failed CRC.

Lost bitmap replies cause bounded query retries; after four unanswered requests, the sender pauses and later retries the manifest. The original source JPEG remains stored. A query also repairs an interruption between final JPEG rename and saving completion metadata. Completion still requires the full JPEG CRC32, boundary checks and saved completion state.

These are experimental firmware policies. Grouping and bitmap ACKs already exist in research; the adaptive policy is a contribution to evaluate, not a demonstrated novelty or speedup. Test against the tuned and fixed-group baselines before making a research claim.

## Software checks

```powershell
.\tools\run_native_tests.cmd
C:\Python314\python.exe -m unittest discover -s tests -p test_experiment.py
.\tools\build_transfer_matrix.ps1
```

The matrix builder compiles all eight mode/role combinations from a private temporary snapshot. It preserves the working role/mode settings and writes compiler logs, binaries and source hashes. The binaries contain the mesh key and dashboard password. See `TRANSFER_TESTING.md` for the recorded results. Host tests simulate storage and radio state; real RF speed, half-duplex collisions, flash power loss, peak heap and battery use require board tests.

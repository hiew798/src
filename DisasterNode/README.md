# Heltec SOS, photos and two-way chat

**Mode 4 update, 17 October 2026:** firmware `adaptive-goodput-1` retries a missing bitmap reply before shrinking the group, and compares group sizes using confirmed image bytes per second. See [ADAPTIVE_UPDATE.md](ADAPTIVE_UPDATE.md) for upload steps and the new `[ADAPT]` log fields.

**Radio reception update:** firmware `radio-rx-guard-2` checks receive restarts, ignores unrelated interrupts and polls for completed radio events. On 17 October, the native tests passed for all four modes and both roles. After the antennas were reconnected and the boards power-cycled, eight radio test packets arrived successfully. The restored mode-2 application received SOS acknowledgements and confirmed the queued photo's delivery. See [RADIO_RX_UPDATE.md](RADIO_RX_UPDATE.md) and [RADIO_LINK_CHECK.md](RADIO_LINK_CHECK.md) for the hardware evidence and current port mapping.

**17 October 2026:** both dashboards now have **Clear all** for the connected board's reports, messages and photos. Rebuild/upload each board, then reload its page. See [CLEAR_ALL.md](CLEAR_ALL.md) for deletion scope, recovery and the fixed-mode acknowledgement check.

**16 October 2026 experiment update:** four selectable image-transfer modes: original, tuned stop-and-wait, fixed groups and adaptive groups. Read **[EXPERIMENTS.md](EXPERIMENTS.md)** for selection, uploads and measurement steps. Select the role in `Config.h` and the mode in `TransferConfig.h`; check the uploaded values in the page header or startup log.

**Stack-overflow fix:** the SOS/web/radio call chain exceeded the default 8 KB main-task stack. The sketch now uses 16 KB and shares fixed request buffers outside the stack. See **[LOOP_STACK_FIX.md](LOOP_STACK_FIX.md)** for the confirmed crash, upload marker and hardware retest. Rebuild and upload to apply it.

The local portal files were already configured for **HTTPS** when this transport update began. That configuration is preserved: open **https://192.168.4.1/**; port 80 redirects there. Wi-Fi remains open with no connecting password.

The integrated sketch is **`C:\Users\cyftl\Documents\Arduino\testing\testing.ino`**.
Open this file in Arduino IDE. Keep **all headers beside it**. Do not copy only the `.ino` file into Downloads.

## What this version includes

- Civilian SOS form: people count, trapped/injured/water/medicine/unconscious flags, name, room/location and text.
- Visible latitude and longitude inputs and report values, including valid zero coordinates. Enter coordinates manually when automatic location is unavailable; reports without them show **Not supplied**.
- One small photo per SOS. JavaScript in the phone's browser makes a grayscale JPEG, up to 160 × 120, preserving its aspect ratio. Firmware rejects uploads above 8 KB.
- Rescuer incident list, verified photos and a separate **Mark resolved** action.
- **Two-way text chat** linked to each report: civilian → rescuer and rescuer → civilian.
- Open Wi-Fi on both roles, local HTTPS on port 443, wildcard DNS and HTTP redirects. Phone captive-page behaviour depends on the browser and its acceptance of the local certificate. All page assets are compiled into the firmware; no internet or separate filesystem upload is needed.
- Automatic node IDs from the full device MAC, distinct random message IDs, authenticated radio packets, bounded relay queues and durable pending messages.
- Automatic rescuer beacons and a manual **Call nearby nodes** button.

The feature reference was [hiew798/src, commit cb88553](https://github.com/hiew798/src/tree/cb88553d10082b3374b14d7a4f48013c77505c6d). The SOS form, role split, incident list, photo workflow and resolve action were integrated into this sketch's radio/storage architecture. Two-way chat and the acknowledged transfer protocol are additions. The repository's separate `WiFiAccessPoint` LED example is an unrelated demo.

## Start here: upload and try two boards

### 1. Open the right sketch

Open `C:\Users\cyftl\Documents\Arduino\testing\testing.ino`.
The folder must also contain `Config.h`, `TransferConfig.h`, `TransferPolicy.h`, `PacketDefs.h`, `StorageManager.h`, `LoRaMeshManager.h`, `WebServerManager.h`, `WebAssets.h`, `PortalRouting.h`, `NetworkSecrets.h`, `cert_der.h` and `key_der.h`. The current HTTPS server uses the certificate headers.

### 2. Select the tested board package and libraries

In Arduino IDE:

1. Boards Manager: **esp32 by Espressif Systems, version 2.0.17**.
2. Select **Heltec WiFi LoRa 32(V3) / Wireless shell(V3) / Wireless stick lite (V3)**, FQBN `esp32:esp32:heltec_wifi_lora_32_V3`.
3. Select the connected board's COM port. If serial output is missing, check the board's USB CDC On Boot setting and port after reset.
4. Library Manager: **RadioLib 7.8.1**, **ArduinoJson 6.21.5**, **ESP32 HTTPS Server 1.0.0** (folder `ESP32_HTTPS_Server`).
5. The local HTTPS library already has the ESP32-S3 SHA compatibility patch. After reinstalling that library, run `tools\apply_https_compatibility.ps1` in PowerShell 7, then compile again.
6. The local HTTPS library also has the disconnect recovery fix. After reinstalling it, run `C:\Python314\python.exe tools\apply_https_disconnect_fix.py` from this sketch folder before compiling. This prevents a dropped browser connection from crashing the node. See [HTTPS_DISCONNECT_FIX.md](HTTPS_DISCONNECT_FIX.md).

Do not select `heltec_wifi_kit_32_V3` for this radio sketch. The tested setup uses the Espressif package, not Heltec's separate 3.x board package.

### 3. Upload the civilian board

In `Config.h`, set:

```cpp
#define CURRENT_NODE_ROLE ROLE_CIVILIAN
```

Verify, then Upload. Open Serial Monitor at **115200 baud**.
The log should show storage mounted, radio ready, the selected transfer mode, portal DNS ready and HTTPS ready.

**Only if this is a new, empty board and LittleFS fails to mount:** set `FORMAT_FILESYSTEM_ON_MOUNT_FAILURE` to `true`, upload and boot once. Then change it back to `false` and upload again. Formatting erases that partition. Do not enable it to troubleshoot a board that contains reports.

Connect a phone to **`Heltec-LoRa-xxxxxx`**. **No Wi-Fi password is required.** If this network was saved with the earlier password, forget it on the phone and join it again. Stay connected when the phone says there is no internet.

The phone may show **Sign in to Wi-Fi / Sign in to network**; tap that prompt. If no page appears, open **https://192.168.4.1/** in the browser using the existing local certificate setup. The board cannot force every phone or desktop to launch its browser.

### 4. Upload the rescuer board

Change the same role line to:

```cpp
#define CURRENT_NODE_ROLE ROLE_RESCUER
```

Select the second board's port and upload. Keep **the same `NetworkSecrets.h` on all nodes**.

Connect another phone/browser to **`Heltec-Rescuer-xxxxxx`**, also with **no Wi-Fi password**. Open the captive page or **https://192.168.4.1/**. The rescuer dashboard still has its separate login: use `RESCUER_PASSWORD` from `NetworkSecrets.h`. This password is for the dashboard only. The API username is `rescuer`.

Return `Config.h` to `ROLE_CIVILIAN` before uploading another civilian node. Relaying is automatic on both roles; an additional civilian node can act as an intermediate relay.

### 5. Send a small test report

1. On the civilian page, enter a location and message. Optionally enter **both latitude and longitude** in decimal degrees. Send a text-only SOS first.
2. Confirm the civilian page says **Saved · awaiting device acknowledgement** initially, then **Received by device**.
3. Confirm the report and its latitude/longitude appear on the rescuer page. Empty coordinates must say **Not supplied**, not zero. Reply to it there.
4. Confirm the civilian sees the reply and can send a message back.
5. Attach a photo and watch the acknowledged chunk count. The rescuer sees the photo only after the complete file passes verification.
6. Turn off the rescuer, send another message, and restart the civilian. Reconnect using the same browser. The pending message should still be there. Turn the rescuer back on and wait for delivery.
7. Mark the test incident resolved on the rescuer page. Verify that the update reaches the civilian separately from ordinary delivery ACKs.

Use the detailed checklist in `TESTING.md` before relying on the radio behaviour.

## Exactly what acknowledgements mean

| Item | When the receiving node sends an ACK | What the sender does |
| --- | --- | --- |
| SOS | Rescuer has saved and verified the report record | Marks the report received and remembers that rescuer for the conversation |
| Chat | Addressed node has saved the message in the matching report | Marks that message received |
| Photo manifest | Rescuer has saved its size and checksum | Begins acknowledged chunk transfer |
| Photo progress | Rescuer has written, read back and recorded the chunks; modes 1/2 ACK individually, modes 3/4 return a bitmap on request | Advances only the durably acknowledged progress and retries missing chunks |
| Completed photo | All bytes pass CRC32 and JPEG boundary checks, and completion is saved | Marks the photo received |
| Resolve update | Civilian node has saved the separate resolution record | Marks the resolution update received |

A node ACK means **stored by that device**. It is not a human read receipt or confirmation of rescue.

The source saves SOS/chat before trying the radio. A lost ACK causes retransmission of the same logical message with a fresh packet ID; the receiver deduplicates it and acknowledges it again. After four attempts, retries pause for approximately two minutes and then resume. Pending data is retained through restart. A beacon or the Retry pending button can wake deferred work. Retries are not a promise of delivery when the destination cannot be reached.

ACKs, SOS and resolution updates take priority over chat; chat takes priority over photos. Photo chunks are 180 bytes. Modes 1/2 use stop-and-wait; modes 3/4 use grouped transfer with bitmap replies. Radio TX is nonblocking; channel assessment is brief and synchronous. A whole photo can take several minutes, especially over relays or with interference. See EXPERIMENTS.md for the exact policies and timing measurements.

## Storage and older data

- New records live in a separate **`/mesh3`** directory in LittleFS. Records use a checksum and a write/verify/rename update. The old record is not deleted before replacement.
- Limit: **96 records total**, including SOS, chat, photo metadata and resolution records; **8 photos total**, each at most **8192 bytes**. These are history limits, not limits per incident. The node reports a storage error when it cannot save a new item; it does not report successful delivery or silently evict pending data.
- Old `/unsent_sos.bin` entries from the restored 124-byte record format are imported on civilian startup and retried using the selected mode's radio protocol. Their original file remains intact. Press **Recover earlier queued reports** to associate these imported reports with your current browser. The first browser to claim them receives access; the old format had no browser ownership.
- Old `/sos_history.bin` remains untouched. It is not replayed as new SOS reports or displayed in the new dashboard. Unknown or damaged legacy formats are retained and reported in Serial Monitor rather than guessed.
- A normal firmware upload should preserve LittleFS when the partition layout stays unchanged and flash erase is disabled. Changing partition schemes, choosing erase-all, or uploading a filesystem image can erase stored reports.
- Keep the originating node powered for pending chat/photo transfers. Relays also retain SOS copies for later forwarding, but photos/chat rely on the source's durable copy.
- History is retained, including resolved incidents. There is no automatic garbage collection or dashboard delete command. Archive device data before intentionally resetting storage when it fills.

## Portal, coordinates and browser sessions

The current page is **https://192.168.4.1/**. The server on port 80 redirects to HTTPS. DNS resolves network connectivity-check hostnames to the board. Opening automatically is controlled by the phone and can be affected by the local certificate; use the full URL manually when necessary.

**Automatic phone GPS requires browser support, a secure context and permission.** Manual latitude and longitude remain available, for example copied from the phone's maps app. The firmware retains, relays and displays those coordinates. It never fills missing coordinates with fabricated values. See the [browser Geolocation requirements](https://developer.mozilla.org/en-US/docs/Web/API/Geolocation/getCurrentPosition).

Both coordinate fields may be empty. If either is filled, both are required: latitude -90 to 90 and longitude -180 to 180. Values such as `(0, 0)` are valid. All four transfer modes retain these fields in the SOS payload.

`cert_der.h` and `key_der.h` are used by the current HTTPS server. The existing files, `heltec-sos.cer` and their generation tool were preserved; no new certificate or key was generated for the transfer experiment.

`NetworkSecrets.h` still holds the shared radio authentication key and separate rescuer dashboard login. Keep the same file on every node. If it is missing, generate it once with `python tools\generate_network_secrets.py`. Both Wi-Fi networks themselves are open; the current application connection uses HTTPS.

Radio packets still use HMAC-SHA256 with an 8-byte authentication tag. This authenticates participating nodes that know the shared key; it does not encrypt the radio contents or verify an individual rescuer's identity.

Civilian reports are associated with a random access token in the browser's local storage. Keep the same browser profile. If a captive window blocks storage, the page remains usable for that visit and tells you to use a normal browser for continued access. Captive windows and normal browsers may have separate storage.

### Historical note: existing reports when moving from HTTPS to HTTP

Stored reports are retained on the node. Browsers keep HTTP and HTTPS site storage separately, so an old civilian browser token does not transfer automatically. The rescuer dashboard continues to show stored reports after login.

To keep a civilian browser's access, save the value of `localStorage.getItem("heltec-session")` from the old HTTPS page using its browser developer console before updating. On the new HTTP page, run `localStorage.setItem("heltec-session", "YOUR_SAVED_32_CHARACTER_VALUE")`, then reload. Keep this value private. If the firmware was already updated, the pre-HTTP sketch backup can be used temporarily to access the old HTTPS browser session without formatting LittleFS. The **Recover earlier queued reports** button applies only to imported legacy reports that never had an owner token.

## Compatibility and practical limits

Use the **same transfer mode on all participating nodes**, the same `NetworkSecrets.h` and identical radio parameters. Modes 1/2 use protocol 3; modes 3/4 use protocol 4. Stored records keep their existing version-3 layout in every mode. These modes cannot exchange messages with the old v1 sketch or the repository's v2 protocol.

Radio settings retained: SX1262, 915 MHz, 125 kHz bandwidth, SF9, coding rate 4/7, sync word 0x12, 22 dBm and a three-hop frame TTL. Pins are NSS8, DIO1=14, RESET12, BUSY13, SCK9, MISO11, MOSI10, Vext36 and LED35. Use settings suitable for the actual radio hardware and deployment location.

For the initial test, use **one rescuer node**. The first rescuer ACK selects the civilian report's destination for chat and photos. Coordinated assignment/failover between multiple rescuer nodes is not implemented. Do not interpret a second rescuer's unacknowledged reply as delivered.

No software can guarantee zero data loss: physical flash failure, erasing a device, exhausted storage, unavailable power and unreachable destinations remain limitations. The changes address the previous missing ACK/retry paths and prevent ordinary radio failures from being treated as successful delivery.

## Editing and testing

- Edit `data/index.html`, `data/app.js` and `data/style.css`, then run **`python tools\embed_web_assets.py`**. This regenerates `WebAssets.h`. No LittleFS asset upload is required.
- `tools\run_native_tests.cmd` checks portal routing and runs the production protocol/storage/radio logic against host hardware doubles using installed MSVC Build Tools. The test doubles do not test real flash timing, radio propagation or HMAC cryptographic implementation.
- `tests\browser_tests.cjs` uses Playwright on an insecure HTTP origin with a mock node API. It checks manual, zero and missing coordinates, blocked browser storage, phone layout, text rendering, lost-response retry, photos, chat and rescuer controls.
- See `TESTING.md` for recorded checks and the physical-board checklist.

The pre-HTTP backup is `C:\Users\cyftl\Documents\Arduino\backups\testing-before-http-20261015-225226.zip`. It includes the earlier HTTPS implementation.

The pre-integration sketch backup is `C:\Users\cyftl\Documents\Arduino\backups\testing-before-integration-20261015-200954.zip`. It contains private TLS material; keep it local.

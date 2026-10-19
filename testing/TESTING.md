# Verification and hardware checklist

**Historical HTTP snapshot:** the results below describe the earlier HTTP build. The current four-mode transport implementation and HTTPS configuration are covered by `TRANSFER_TESTING.md`; use `EXPERIMENTS.md` for its upload and comparison steps.

Date: 16 October 2026. These are software checks on this computer. **No physical board was flashed or tested during this update.**

## Final ESP32 builds

Target: `esp32:esp32:heltec_wifi_lora_32_V3`, Espressif ESP32 core **2.0.17**. RadioLib **7.8.1**, ArduinoJson **6.21.5**, ESP32_HTTPS_Server **1.0.0** with the existing SHA compatibility patch.

| Role | Result | Flash | Static RAM |
| --- | --- | --- | --- |
| Civilian | Pass | 1,127,957 bytes (33%) | 58,948 bytes (17%) |
| Rescuer | Pass | 1,128,617 bytes (33%) | 58,948 bytes (17%) |

Both builds used the final headers and embedded page assets. Build copies were compared with the sources, allowing for Arduino's generated line annotations, and source hash manifests were saved for each role. Config.h is restored to ROLE_RESCUER, preserving the user's selected role, with automatic formatting disabled. Static RAM excludes runtime Wi-Fi and HTTP allocations and does not measure live throughput or battery life.

Full compiler logs and private compiled firmware are under `C:\Users\cyftl\AppData\Local\Temp\heltec-http-validation-20261016`. The final logs are `civilian-final-build.log` and `rescuer-build.log`; each role's binaries and source manifest are in its matching subfolder. Firmware contains mesh and dashboard secrets; keep the binaries private. Certificate and private-key headers are no longer included in the HTTP build.

## Automated checks that passed

### Production C++ with simulated hardware: both roles

Run `tools\run_native_tests.cmd`. It uses local MSVC Build Tools and the production protocol, storage and radio code with test doubles for Arduino, LittleFS, RadioLib and HMAC.

- Valid frames, wrong protocol versions, truncated frames, invalid GPS and chunk bounds.
- CRC32 reference vector and 255-byte radio frame size limits.
- Persistence across reconstructed storage instances; failed writes and failed replacement preserve the prior record.
- Full record capacity and failed mount return errors instead of successful saves.
- Delivery ACK leaves an incident unresolved; a separate resolution record survives restart.
- Out-of-order image chunks and duplicate chunk zero do not truncate a photo.
- Partial photo metadata resumes after restart; a completed JPEG is recovered after interruption between file rename and completion metadata.
- CRC mismatch requests restart; failed chunk persistence produces no ACK.
- Pending SOS retry, wrong authentication tag rejection and duplicate logical message suppression.
- Rescuer ACK is withheld when storage fails; a saved SOS is acknowledged again on retry.
- Incoming chat requires a matching report and is deduplicated.
- A photo ACK after the fourth attempt still counts; delayed manifest ACK does not interrupt a chunk wait.
- Lost CRC-restart ACK cannot trap a sender with a full bitmap and no completion ACK.
- Legacy 124-byte unsent records import once, remain pending, can be claimed by a browser session, and keep the original file intact.
- Portal routing accepts the canonical IP (including explicit port 80), preserves assets and API paths, redirects Android/Apple/Windows connectivity checks and unknown hosts/paths, and handles HEAD probes. POST requests are not redirected. Lookalike hosts and HTTPS-port hosts are not accepted as the canonical site.

The filesystem double is in memory. These checks exercise state transitions and error paths, not real flash power-failure behaviour. The HMAC double tests acceptance/rejection handling; the real SHA-256 implementation is supplied by mbedTLS in the linked ESP32 firmware.

### Browser checks: Chromium with mock API

`tests\browser_tests.cjs` passed at a 390-pixel phone width and for a rescuer browser. The browser used `http://192.168.4.1/` with intercepted requests to the mock API; `window.isSecureContext` was verified to be false. The rendered phone page was also visually inspected.

- SOS form submission and state display.
- Visible latitude/longitude fields; one missing coordinate is rejected, while a valid zero latitude is sent and displayed on both roles. Reports without coordinates show **Not supplied** for both fields.
- On HTTP, the location button explains the browser restriction without requesting GPS or clearing manually entered coordinates.
- A newly submitted second report stays selected instead of reverting to the previous report.
- When localStorage is blocked, the page still loads and can submit a report using temporary storage; a visible notice explains that the captive window cannot remember the session.
- Retry after a simulated 504 response following a successful save reuses the report ID and creates one report.
- User-supplied HTML is displayed literally.
- Photo compression produces a JPEG under 8192 bytes; upload and chunk progress display work.
- Civilian sends chat; a rescuer reply appears; rescuer can sign in and reply.
- Resolve confirmation updates the report and closes the input form.
- No page JavaScript errors occurred.

These checks use a mock API, so they do not establish HTTP or LoRa performance on a board. The native portal tests exercise routing decisions; real phone captive-portal detection still needs the hardware check below.

JavaScript and Python helper syntax checks passed. WebAssets.h was regenerated from data/. The original certificate files and generation helpers are retained for reference and are unused by the HTTP sketch.

## Test this on the boards next

Start with one civilian and one rescuer using README.md. Fit the appropriate antennas before transmitting.

1. **Startup:** both boards show storage, radio, portal DNS and HTTP ready at 115200 baud. Preserve the existing partition layout and stored data. Check the radio error code if startup fails.
2. **Browser:** forget the previously saved Wi-Fi network, then join each AP with no password. Check whether the phone opens the page or shows **Sign in to Wi-Fi**. Stay connected without internet; if needed, open **http://192.168.4.1/** manually. Sign in separately on the rescuer dashboard. Try the actual Android/iOS devices intended for use; automatic opening depends on the phone.
3. **SOS and coordinates:** submit text with a known pair of manual coordinates. It must appear once on the rescuer, with the same coordinates, and change from pending to received on the civilian. It must remain unresolved. Repeat with no coordinates (both values must say **Not supplied**) and with `(0, 0)` (both must display zero). HTTP does not provide automatic browser GPS access.
4. **Chat:** send in each direction. Confirm the exact text and received status for each message.
5. **Offline retry:** turn off the rescuer, queue a civilian message and restart the civilian. Confirm it remains pending using the same browser. Restore the rescuer and confirm delivery without duplicates. Repeat for a rescuer reply while the civilian is offline.
6. **Photo:** attach a small photo. Interrupt either node midway, restore power and confirm completion without a truncated image. Allow several minutes.
7. **Resolve:** resolve an incident with the civilian offline. Confirm the local resolved state and pending update. Restore the civilian and confirm the resolution arrives and is acknowledged.
8. **Relay:** add a third node between endpoints, then test where direct reception is unavailable. Verify both forward and return paths for ACKs and chat.
9. **Legacy queue:** if present, check startup imports and claim earlier reports with the recovery button. Confirm reports do not multiply after reboot.
10. **Load:** test the expected number of phones and message rate. Watch Serial Monitor for resets and /api/status for free heap and storage. Test capacity errors with disposable test data, preserving real reports first.

A successful build or host simulation cannot establish RF range, actual power-cut durability, battery life, live HTTP memory usage or phone auto-opening behaviour. Check README.md's browser-session migration instructions before replacing an HTTPS deployment that contains civilian reports.

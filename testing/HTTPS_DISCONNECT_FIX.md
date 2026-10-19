# Civilian restart when sending text or opening the camera

16 October 2026.

## Confirmed cause

The supplied log shows `LoadProhibited`, followed by `Rebooting...` and a new civilian boot. That restart drops the access point and causes the phone to prompt for Wi-Fi sign-in again. Both text and photos use the affected web server.

The original Arduino IDE ELF matches the log's SHA-256 prefix `a0299dba530d2b1a`. Decoding its addresses gives:

```text
SSL_pending
HTTPSConnection::pendingByteCount
HTTPConnection::pendingBufferSize
HTTPRequest::requestComplete
HTTPConnection::loop
HTTPServer::loop
WebServerManager::update / loop
```

`pendingBufferSize()` calls `updateBuffer()`. A receive error inside that call closes the connection and frees its TLS context. The next operation called `SSL_pending()` with the now-null context. This matches the log's fault address `0x18`.

Failed SSL handshakes are separate from this confirmed crash. The supplied log does not identify why every handshake failed; HTTPS probes redirected to this device and certificate rejection remain possible. The patch prevents the identified teardown crash, but does not make the certificate trusted or give the Wi-Fi network internet access.

## Changes installed

The fix is installed in `C:\Users\cyftl\Documents\Arduino\libraries\ESP32_HTTPS_Server\src` and applies to both roles and all four transfer modes:

- Guard TLS reads, writes and pending-data checks after the context has been freed.
- Stop request processing after the connection has closed, including errors detected while draining the body.
- Keep request headers alive until the connection object is destroyed, so the request destructor cannot access freed headers.
- Stop draining an interrupted POST when closed, yield while waiting, and bound incomplete bodies by the library's connection timeout.
- Avoid checking a closed socket for readable data.

Original versions of the four modified library files are retained alongside them with the suffix `.before-heltec-disconnect-fix`. The sketch's certificates, HTTPS address, Wi-Fi settings, roles, transfer policy and stored records were not changed.

To reinstall the fix after replacing the library, run from the sketch folder:

```powershell
C:\Python314\python.exe tools\apply_https_disconnect_fix.py
C:\Python314\python.exe tools\apply_https_disconnect_fix.py --check
```

The tool checks for ESP32_HTTPS_Server 1.0.0 and validates all source replacements before writing. It is safe to run again. The separate SHA compatibility patch described in README is still needed after a fresh library installation.

## Verification and board test

The regression harness reproduces the original null TLS access. It compiles unchanged methods extracted from the installed library against simulated TLS and sockets; it does not emulate radio hardware or perform a real TLS handshake.

Run it with:

```powershell
C:\Python314\python.exe tools\run_https_disconnect_tests.py
```

It checks repeated receive-error teardown, calls after closure, header lifetime, interrupted POST bodies, EOF timeout, and complete POST bodies.

Recorded result: the original sources failed at the simulated `SSL_pending(NULL)` call. With the fix, all cases passed, including 2,000 teardown cycles with no remaining simulated TLS/header allocations. Reapplying the patch to copies of the four original files also passed; backup preservation and a second, unchanged application were checked.

The civilian mode-1 firmware also compiled successfully for `esp32:esp32:heltec_wifi_lora_32_V3`: 1,242,657 bytes of flash (37%) and 63,352 bytes of static RAM (19%). The build log and firmware are in `C:\Users\cyftl\AppData\Local\Temp\heltec-https-disconnect-fix-20261016`. The matching original crash ELF was retained there. No physical board was flashed by Codex.

After verification, rebuild and upload the civilian firmware in Arduino IDE. Uploading is required: a library edit on the computer does not change firmware already on a board. Keep the existing partition settings and leave flash erasure disabled. Then reconnect to `Heltec-LoRa-...` and open `https://192.168.4.1/` in the normal browser.

Keep Serial Monitor at 115200 baud. Send a text SOS and chat reply, then take/attach a photo. Also briefly switch away from the browser or disconnect the phone during a request, then reconnect and retry. Success means no panic/repeated boot, continued Wi-Fi availability, and normal delivery or a recoverable request error. Apply the same rebuilt library to the rescuer firmware as well.

This patch still needs physical retesting. A photo that never finished uploading must be attached again; an already saved SOS remains a separate stored record.

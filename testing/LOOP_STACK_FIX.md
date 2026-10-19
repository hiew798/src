# Fix for loopTask stack overflow

16 October 2026.

## Confirmed failure

The board reported `Stack canary watchpoint triggered (loopTask)` and rebooted. The log's ELF prefix `23fdaf6e637231fb` matches the saved Arduino IDE civilian build exactly. Its decoded call chain is:

```text
loopTask / loop / WebServerManager::update
  HTTPServer::loop / HTTPConnection::loop / middleware
    WebServerManager::handle / submitSOS / json
      LoRaMeshManager::update / plan
        StorageManager::at / readFile
          LittleFS open / stat / lfs_dir_find
```

This particular crash occurred while reading an SOS request and servicing the radio scheduler, even though it was reported after an image attempt. The original 8192-byte Arduino loop stack was insufficient for those nested calls. LittleFS was where the guard triggered; this does not establish filesystem corruption or exhausted storage. The boot log showed 40,960 bytes used out of 1,572,864.

This is a separate failure from the previously repaired `SSL_pending(NULL)` crash. That library fix remains installed. The four-mode firmware needed an adequate stack budget for the integrated web/radio/storage workload. Host radio tests and successful compilation did not measure that workload's ESP32 stack use. The precise change or workload difference that made the older version appear stable has not been isolated by an on-board comparison.

## Changes

- Allocate 16,384 bytes for `loopTask` through Arduino's `SET_LOOP_TASK_STACK_SIZE` hook.
- Keep the 1536-byte JSON workspace and 1025-byte request-body buffer in the global web-server object's fixed storage. The handlers reuse and clear the workspace sequentially, rather than placing large documents and the body buffer on nested call frames. This does not allocate a fresh heap document for every request.
- Keep cooperative radio servicing during web requests so long uploads do not suspend mesh processing.
- Print a build marker and minimum remaining stack space at startup, then stack/heap/uptime/client diagnostics every 30 seconds. ESP32's `uxTaskGetStackHighWaterMark` reports the minimum remaining stack in bytes, not the current usage.
- Expose diagnostic values through `/api/status`: `stackFix`, `stackBytes`, `stackMinFree`, `heapMinFree`, `uptimeMs`, `wifiClients`.

The larger stack reserves an additional 8192 bytes from available RAM. The shared workspace also uses fixed global RAM. The compiler's static-RAM summary excludes the dynamically allocated task stack and peak TLS usage; on-board readings are needed to assess those.

## Verification

The civilian mode-1 firmware compiled successfully for `esp32:esp32:heltec_wifi_lora_32_V3`, using ESP32 core 2.0.17. The build uses 1,243,049 bytes of flash and 65,968 bytes of static RAM. The fixed ELF SHA256 is `16fc28c0911123068782797c54e519d36d3a13cae4a820e6612fd21dcb009c83`.

Disassembly of the final ELF confirms that the sketch overrides `getArduinoLoopTaskStackSize()` and returns 16,384. Comparing function stack allocations against the exact crashing ELF gives:

| Function | Before (bytes) | After (bytes) |
| --- | ---: | ---: |
| Web request routing (`handle`) | 1344 | 832 |
| SOS submission (`submitSOS`) | 2432 | 848 |
| JSON body reading (`json`) | 1104 | 112 |
| State response (`state`) | 2160 | 608 |
| Chat / resolution (`chatOrResolve`) | 2000 | 944 |
| Photo handler (`photo`) | 1248 | 1248 |

The three nested SOS handler frames total 1792 bytes, down from 4880: a reduction of 3088 bytes on the reported crash path. These totals exclude library calls and the radio scheduler. The scheduler's `update` and `plan` frames remain 352 and 992 bytes. This check verifies the intended stack reduction; it does not substitute for runtime measurements under all four modes.

## Upload and retest

1. Verify and upload the civilian firmware from this sketch. It is currently set to civilian, mode 1. Leave flash erasure disabled and preserve the partition settings.
2. Confirm Serial Monitor at 115200 prints `[STACK] fix=1 | loop=16384 bytes`.
3. Send several text-only SOS reports and chat messages, then attach a photo while other delivery is pending.
4. Save the subsequent `[HEALTH]` lines. `stack_min` is the lowest remaining loop stack space observed since this boot; it should retain comfortable headroom. A value below 2048 bytes warrants investigation before longer tests. Record heap readings as well.
5. Rebuild/upload the rescuer with its role selected, then test both directions. The fix applies to all four transfer modes; use one matching mode on all participating nodes.

This source fix requires physical retesting. A successful compilation or reduced compiled stack frame does not establish a worst-case runtime bound. Do not erase stored records to address this error.

The pre-fix source files are backed up in `C:\Users\cyftl\Documents\Arduino\backups\testing-before-loop-stack-fix-20261016-225123.zip`. Matching original crash symbols, compiler output and fixed firmware are kept privately under `C:\Users\cyftl\AppData\Local\Temp\heltec-loop-stack-fix-20261016`.

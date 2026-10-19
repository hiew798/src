# Clear all on a civilian or rescuer node

17 October 2026.

## Use

1. Rebuild and upload the updated sketch to each board, selecting its civilian or rescuer role and the same transfer mode on all nodes.
2. Connect to that board's Wi-Fi and reload its page. The current portal is `https://192.168.4.1/`.
3. On a rescuer node, sign in first. Under the reports heading, choose **Clear all** and confirm the named node.
4. The button permanently deletes every SOS report, chat message, resolution, photo and unfinished upload stored on that board, including other users' records and pending deliveries.
5. To reset both sides for testing, clear the civilian first, then the rescuer. Clear any relay that still holds pending reports as well. Each action affects only its connected board. Other nodes can send their retained pending reports again.

The operation preserves firmware, Wi-Fi settings, transfer mode, credentials, certificates and this browser's access token. It does not format the whole filesystem. Browser selections, loaded photos, form inputs and drafts associated with the displayed reports are cleared after confirmed success. Existing packet counters remain lifetime counters for the current boot.

## Implementation

- `POST /api/clear` uses the existing session/origin validation and rescuer authentication. It requires JSON containing `confirm: "CLEAR_ALL"` and the connected node's exact ID. A local civilian browser can clear all records on its civilian node; this includes records owned by other browser sessions, as stated in the confirmation.
- The radio is placed in standby, its interrupt callback is detached, and queued packets, active transmission state, retry slots, group transfers and duplicate caches are reset before deletion. Reception is restarted afterward. If standby fails, no storage deletion is attempted.
- Storage deletes files only from the flat `/mesh3` message directory, plus `/unsent_sos.bin`, which otherwise could reimport deleted legacy reports. Unrelated filesystem files are retained.
- `/mesh3-clear.pending` records a confirmed clear in progress. If a removal fails, normal storage writes are blocked and the marker remains. Retry **Clear all**, or reboot to finish deletion before loading records. An error may mean that some data has already been removed; no rollback is promised.
- A successful deletion with a failed radio restart is reported as cleared with the radio unavailable. The page asks for a restart before sending again.
- The browser prevents overlapping mutations during clear and ignores state/photo responses started before the clear. A failed or lost response is shown as an unconfirmed clear, with instructions to reconnect and check the board.

## Mode 3 acknowledgement check

The page header and footer now display the live protocol. For fixed mode, both boards should report:

```text
[TRANSFER] mode 3 (fixed), protocol 4
```

Modes 1/2 use protocol 3; modes 3/4 use protocol 4. `parseFrame` rejects packets with a different protocol version, including SOS packets. Always upload the same selected mode to every participating board, including relays, and select the correct civilian/rescuer role for each.

An SOS receives an ACK only after the rescuer saves it. The limits are 96 records and 8 photos; failure to save or a full receiver can also prevent acknowledgement. These are conditions to check, not a confirmed diagnosis of the user's fixed-mode test. Its physical startup logs have not yet been provided.

## Verification

- Production C++ tests passed for all eight mode/role combinations, including clearing a full record store, photo/temporary files, unrelated-file preservation, queue cancellation, deletion failures, recovery after restart, repeated clear, and new transmissions afterward.
- Chromium tests passed for both roles at phone width using a mock API: confirmation/cancellation, failed deletion, login gating, stale state/photo responses, preserved session, cleaned drafts and a fresh SOS after clearing.
- The browser test is a UI/API simulation. Actual filesystem power-loss behaviour, radio standby/restart, and both-board deletion still require hardware testing. No board was erased or flashed by the assistant.

## Latest changes and test status

- **Take photo** is restored for a new civilian SOS and for attaching a photo to an existing report. Choosing an existing image remains available. Both paths use the existing compression and upload process. The embedded `WebAssets.h` has been regenerated.
- Large history responses now batch JSON writes using 512 bytes of shared scratch space. This avoids sending individual JSON punctuation bytes through TLS once the library's response cache fills. It does not change the four radio transfer policies.
- Startup now identifies firmware `clear-radio-diag-1`, compilation time, role, mode and protocol. Radio diagnostics distinguish protocol/authentication rejection, a failed SOS save, and an ACK queued for transmission. The health line also reports radio/storage state and maximum service delays. An ACK being queued is not confirmation that the civilian received it.
- COM numbers identify USB connections, not node roles. Civilian on COM5 and rescuer on COM9 is valid when the corresponding firmware is uploaded to each board.
- Native tests for all eight mode/role combinations passed after the buffered output and diagnostic changes. An earlier ESP32 fixed/rescuer build passed for Clear all (1,253,621 bytes flash; 65,968 bytes static RAM), before those later changes.
- At the user's request, the follow-up ESP32 build was stopped and no tests or builds were run after restoring the camera controls. The latest source still needs the user's build and hardware test.

Pre-change sources are backed up at `C:\Users\cyftl\Documents\Arduino\backups\testing-before-clear-all-20261017-132259.zip`.

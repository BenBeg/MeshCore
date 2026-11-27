# Three-Device Repeater + Room Server Lab Protocol

Status: prepared for bench execution; **no hardware has been flashed and no RF test has been performed**.

Target: Seeed SenseCAP P1-Pro / SenseCAP Solar combined Repeater + Room Server.
Control devices: two ThinkNode M5 units.
Purpose: compare the stock MeshCore Repeater behavior with the combined firmware while keeping both client devices on unmodified MeshCore firmware.

## 1. Test objectives

Prove with repeatable evidence that:

1. The SenseCAP's original Repeater identity and ordinary Repeater behavior remain intact.
2. The combined firmware also exposes a separate standard MeshCore Room identity.
3. Both ThinkNode clients can discover and use the Repeater and Room services with unmodified MeshCore client firmware.
4. Room traffic shares the radio/dispatcher without starving or corrupting Repeater forwarding.
5. Room posts, contacts/ACL state and sync progress survive reboot.
6. A Room storage/service failure does not take down the Repeater.
7. Sleep/wake behavior remains functional on the solar device.

This protocol distinguishes source/build checks from actual over-the-air evidence. A successful build is not proof of radio interoperability.

## 2. Hardware and roles

| ID | Hardware | Role in both phases |
|---|---|---|
| D1 | SenseCAP P1-Pro / SenseCAP Solar | First stock Repeater, then combined Repeater + Room Server |
| D2 | ThinkNode M5 #1 | MeshCore client A; originates test messages and Room posts |
| D3 | ThinkNode M5 #2 | MeshCore client B; receives/forwards test messages and reads Room posts |

D2 and D3 must run official, unmodified MeshCore client/companion firmware for the entire comparison. The current factory Meshtastic firmware on either M5 is not a valid MeshCore interoperability control; convert them to official MeshCore firmware only after confirming the exact M5 hardware revision and supported target. Record firmware version, build date, device identity prefix and radio settings for each.

Use the same D2/D3 firmware, region, frequency, bandwidth, spreading factor, coding rate, transmit-power setting and antenna arrangement in both phases. Do not change client settings between baseline and combined-firmware runs.

A phone running the official MeshCore app is the user interface. Connect it to D2 or D3 using the supported BLE/USB method. One phone can be used sequentially; two phones make simultaneous A/B interaction easier but are optional.

## 3. RF and Canadian compliance gate

**Do not begin transmitting tests until the exact P1-Pro/Wio-SX1262 configuration, antenna, frequency, bandwidth, output power and Canadian ISED authorization/operating conditions have been verified.** The firmware's 910.525 MHz / 62.5 kHz / SF7 / CR5 profile is a MeshCore interoperability preset, not proof of Canadian regulatory authorization. The configured 22 dBm capability is not permission to transmit at that power.

Important baseline trap: the unmodified base-branch SenseCAP configuration inherits a generic 869.618 MHz default. Do not flash or transmit with that default in Canada. The stock-Repeater comparison must use the stock upstream Repeater source with a reviewed, Canada-appropriate RF configuration. Keep the RF profile identical between stock and combined firmware.

For an RF-isolated bench, use only a properly rated 50-ohm conducted setup with suitable attenuation and a verified RF load, and only if the device exposes a suitable RF connector and the setup is designed by someone competent in RF testing. Never transmit into an open connector, improvised cable, or an attenuator/load with inadequate power rating. Do not disconnect an antenna while transmitting. If a compliant, isolated setup is unavailable, defer RF transmission until the compliance gate is cleared.

## 4. Software and bench setup

### Host computer

- Laptop/desktop with current PlatformIO Core and the repository cloned.
- USB data cable for the SenseCAP and each M5 (one at a time is acceptable).
- Official MeshCore mobile app.
- Three labelled USB ports/cables if available; avoid charging-only cables.
- A text editor and a folder for raw logs, screenshots and result sheets.

Create a dated evidence folder, for example:

`lab/2026-11-27/`
- `00_inventory.md`
- `01_stock_sensecap_serial.txt`
- `02_m5_a_serial.txt`
- `03_m5_b_serial.txt`
- `10_stock_app_screenshots/`
- `20_combined_sensecap_serial.txt`
- `21_combined_m5_a_serial.txt`
- `22_combined_m5_b_serial.txt`
- `30_combined_app_screenshots/`
- `results.csv`
- `notes.md`

Do not publish private keys, QR codes containing secrets, admin passwords, full identity exports or unredacted contact databases. Identity prefixes are sufficient for correlating logs.

### Serial capture

The repository's PlatformIO configuration specifies `monitor_speed = 115200`. Use 115200 baud, 8 data bits, no parity, 1 stop bit unless the specific device firmware documentation says otherwise. Capture raw serial output with timestamps if the terminal supports them. Record the selected COM/tty port and device ID at the top of each log.

The current SenseCAP target has `MESH_PACKET_LOGGING` and `MESH_DEBUG` commented out. Start with the normal production-like build and capture whatever boot/CLI diagnostics it actually emits. Do not assume packet-level logs exist. Before enabling either compile-time debug macro, inspect its implementation and output volume, then make a separate diagnostic build and rerun CI; debug builds are not the acceptance firmware.

If a device exposes no useful serial output, capture the app's event/status screens and note that the corresponding serial evidence is unavailable. Never invent or infer packet events from an app screenshot.

### Record before each run

- Date/time and operator.
- Board model/revision and printed hardware identifiers (redact serial numbers before sharing publicly).
- Firmware source commit, environment name and binary SHA-256.
- Radio profile and antenna.
- Device identity prefix for each device.
- App version and phone OS version.
- Power source, battery state and approximate device temperature.
- Physical positions/distances and whether the two M5s can hear each other directly.
- Any configuration changes, resets or unexpected events.

## 5. Network layout and avoiding false repeater results

A three-node test only proves that D1 repeated a packet if D2 and D3 cannot communicate directly during that test. If D2 and D3 hear each other, a successful message may have travelled directly and does not prove D1 forwarding.

Preferred setup:
- Place D1 between D2 and D3.
- Establish and record a repeatable geometry where D2-to-D3 direct communication fails but D2-to-D1 and D1-to-D3 work.
- Confirm the direct-path failure with D1 powered off, then restore D1 and repeat.
- Do not rely on walls, distance or RSSI assumptions alone; record the actual negative control.
- Do not use illegal power levels, antenna disconnection or unqualified RF attenuation to force the topology.

If the test location cannot create a reliable one-hop relay path, mark repeater-forwarding validation **NOT TESTED**, not PASS. A separate compliant RF-isolated setup or a fourth relay/client device would then be required for definitive forwarding proof.

## 6. Phase A — stock MeshCore baseline

### Firmware baseline

- D1: upstream/vanilla MeshCore SenseCAP Solar Repeater source at the PR base commit `75b04ef06def54b348400cb1b138afb6749ffd4f`, built with a reviewed Canada-appropriate RF profile.
- D2 and D3: official, unmodified MeshCore client/companion firmware for the exact M5 revision.
- No Room Server service on D1.
- Keep a recoverable copy of the original D1 firmware and configuration before any firmware change. Do not erase or format storage as part of baseline setup.

The base-branch target is `SenseCap_Solar_repeater`. Its inherited generic radio default is not suitable for Canadian testing; explicitly verify the effective RF build flags before producing any test binary. Do not use a prebuilt binary unless its target, commit, region and RF settings are known.

### Baseline procedure

1. Boot D1 and record its serial startup log, Repeater identity prefix, firmware commit and RF configuration.
2. Boot D2 and D3. Record their firmware versions, identity prefixes and radio settings.
3. Connect the official app to D2. Confirm D1 appears as a Repeater using its expected identity.
4. Connect to D1's Repeater service using the normal MeshCore workflow. Record discovery, connection and any route/path details exposed by the app.
5. Send uniquely labelled messages in both directions: `BASE-A-TO-B-001` and `BASE-B-TO-A-001`.
6. For a relay claim, establish the direct-path negative control: power D1 off and confirm the same D2/D3 message fails; power D1 on and confirm it succeeds. Repeat at least three times.
7. Observe for 15 minutes with ordinary low-rate traffic. Record resets, lost messages, unexpected queueing and any abnormal power behavior.
8. Reboot D1 normally. Confirm the Repeater identity and ordinary Repeater service return.
9. Record the stock behavior as the reference. Room discovery/login/post tests are **not applicable** in this phase.

### Baseline expected behavior

- Exactly the stock Repeater role is advertised by D1.
- D2 and D3 discover the Repeater and exchange ordinary MeshCore traffic.
- With a verified no-direct-path topology, traffic succeeds only when D1 is online and forwarding.
- No Room Server identity/service is advertised.
- Reboot returns D1 to the same Repeater identity, assuming its identity store was not deliberately erased.

## 7. Phase B — combined Repeater + Room firmware

Only proceed after the baseline is complete, backed up and reviewed. Flashing is a separate operator decision; this document does not authorize flashing.

- D1: combined firmware from the feature branch, target `SenseCap_Solar_repeater`.
- D2 and D3: unchanged official MeshCore client firmware from Phase A.
- Preserve the exact same RF profile, physical positions, antennas, app version and traffic patterns as Phase A.

### B1. Boot and dual discovery

1. Capture D1's complete boot log.
2. Record both identity prefixes. Confirm the Repeater identity matches the baseline identity; confirm the Room identity is distinct.
3. From the official app, discover the Repeater and Room roles separately.
4. Confirm the Repeater is advertised as a Repeater and Room as a Room Server; record the identity prefix associated with each.
5. Repeat discovery after a normal reboot.

Expected:
- Both roles are discoverable using standard MeshCore discovery.
- The Repeater identity is unchanged from baseline.
- Room has a separate identity.
- No repeated reboot, storage-format loop or Room failure that disables the Repeater.
- Two advert roles do not imply two independent radios; D1 remains one physical radio and one shared scheduler.

### B2. Repeater regression

Repeat the Phase A ordinary messaging and direct-path negative-control tests without changing D2/D3 firmware or RF setup.

Expected:
- Repeater discovery, connection and ordinary messages behave as in Phase A.
- The same verified D2-to-D3 relay path works with D1 on and fails with D1 off.
- Flood forwarding remains associated with the Repeater identity.
- Room adverts and Room activity do not cause measurable loss of ordinary Repeater traffic.
- No unexpected identity changes, packet duplication, reboot or watchdog reset.

### B3. Room service and interoperability

Use the official MeshCore app and unmodified clients; do not use a custom test client.

1. From D2, discover and connect to D1's Room identity.
2. Complete the standard Room login/authentication flow.
3. Submit a unique post: `ROOM-A-POST-001`.
4. Confirm the post is acknowledged only after the server has accepted/persisted it; record the visible result and any serial evidence.
5. From D3, connect to the same Room identity and synchronize/read the post.
6. Submit a reply or second unique post from D3: `ROOM-B-POST-001`.
7. Return to D2 and synchronize; confirm the second post is visible.
8. Disconnect/reconnect each client and repeat sync. Confirm no duplicate replay and no missing accepted post.
9. Leave both clients idle for at least five minutes; record whether normal Repeater traffic remains responsive.

Expected:
- Both unmodified clients discover and use the Room Server.
- Login succeeds only for authorized contacts.
- A valid post appears on the other client after synchronization.
- ACK is not falsely issued for a post that was not durably accepted.
- Reconnect/sync does not create duplicate posts or lose previously accepted posts.
- Repeater discovery and ordinary messaging continue throughout.

### B4. Identity and ACL isolation

1. Record Repeater and Room identity prefixes before and after reboot.
2. Add one test contact through the Room workflow if supported by the official app.
3. Verify the Room contact/ACL state survives reboot.
4. Verify the Repeater's pre-existing contacts/configuration remain intact.
5. Confirm Room identity/ACL actions do not replace or mutate the Repeater identity or Repeater contact list.

Expected:
- `_main.id` remains the Repeater identity; `_room.id` is independent.
- Room ACL state is separate from Repeater contacts.
- Room storage changes do not erase or rewrite Repeater settings.

Do not deliberately corrupt flash or interrupt writes during this user-device test. Power-loss and corruption injection should first be covered by automated/sacrificial hardware tests with a recovery image available.

### B5. Persistence and reboot

1. Create at least two uniquely identifiable Room posts and one Room contact/ACL entry.
2. Record the latest visible post and identity prefixes.
3. Perform a normal software reboot.
4. Confirm both identities return and the Repeater remains discoverable.
5. Reconnect a client to Room and synchronize; confirm posts and ACL state remain.
6. Repeat once more to catch first-reboot-only behavior.

Expected:
- Repeater and Room identities persist.
- Accepted Room posts and Room ACL/contact state persist.
- Sync resumes without replaying already-synchronized content.
- Repeater configuration and contacts remain intact.

This is a normal-reboot persistence check, not proof of power-failure atomicity.

### B6. Solar sleep/wake and coexistence

1. With Room idle and no pending work, observe D1's documented sleep/low-power behavior using serial logs and, if available, a USB power meter.
2. Trigger a Room request/post and observe wake, processing and return to idle.
3. Repeat with normal Repeater traffic.
4. Run a mixed test: alternate one ordinary Repeater message and one Room post/sync request for 15 minutes.

Expected:
- Future Room timers do not hold the device continuously awake.
- Due Room work wakes/executes when required.
- Room traffic uses the shared queue at lower priority; ordinary Repeater forwarding/control remains responsive.
- No persistent wake loop, starvation, reset or unexplained battery drain.

A USB power meter can affect power delivery; use a stable supply and compare identical wiring between stock and combined runs. A bench test does not replace outdoor solar validation.

## 8. Logging and evidence rules

For every test, preserve:
- Raw serial text, not only screenshots.
- App screenshots or short screen recordings showing discovery, message/post contents, ACK/sync outcome and timestamps.
- Firmware commit and binary SHA-256.
- Test geometry and direct-path negative-control result.
- A one-line result: PASS / FAIL / NOT TESTED / INCONCLUSIVE.
- If a failure occurs, preserve the first failing log before rebooting or reflashing.

Suggested event labels to use in your own notes (these are test annotations, **not firmware log strings**):
- `BOOT_D1`, `REPEATER_DISCOVERED`, `ROOM_DISCOVERED`
- `A_TO_B_SENT`, `A_TO_B_RECEIVED`, `DIRECT_PATH_NEGATIVE_CONTROL`
- `ROOM_LOGIN_OK`, `ROOM_POST_SUBMITTED`, `ROOM_POST_VISIBLE_ON_PEER`
- `REBOOT_START`, `REBOOT_COMPLETE`, `POSTS_RESTORED`, `ACL_RESTORED`
- `SLEEP_OBSERVED`, `WAKE_ON_ROOM_WORK`, `MIXED_TRAFFIC_COMPLETE`

Do not treat the suggested labels as machine-parsed events; enter them as annotations beside the actual log timestamps.

## 9. Simple pass/fail scorecard

| ID | Test | PASS means |
|---|---|---|
| A-01 | Stock Repeater discovery | D1 stock Repeater discovered by both vanilla clients |
| A-02 | Stock direct messaging | Bidirectional messages work |
| A-03 | Stock relay proof | D2/D3 direct path fails with D1 off and works with D1 on |
| B-01 | Dual discovery | Repeater and Room roles both discoverable |
| B-02 | Repeater identity | Repeater identity unchanged from baseline |
| B-03 | Repeater regression | Baseline messaging/relay behavior preserved |
| B-04 | Room login | Vanilla client completes standard Room login |
| B-05 | Room post | Post from D2 appears on D3 |
| B-06 | Room reverse post | Post from D3 appears on D2 |
| B-07 | Room sync | Reconnect/sync has no missing or duplicate accepted posts |
| B-08 | ACL isolation | Room ACL and Repeater contacts remain separate |
| B-09 | Reboot persistence | Both identities, posts and Room ACL survive normal reboot |
| B-10 | Mixed traffic | Room activity does not break/starve Repeater traffic |
| B-11 | Sleep/wake | Idle sleep and due-work wake behavior are correct |
| B-12 | Failure isolation | Room storage/service failure does not disable Repeater (requires controlled fault test) |

Overall status is **NOT READY** until A-01 through A-03 and B-01 through B-11 have evidence. B-12 requires a separate controlled fault-injection test and should not be attempted by corrupting the user's only device.

## 10. Copy/paste run record

```text
Run ID:
Date/time:
Operator:
Phase: STOCK / COMBINED
D1 hardware revision:
D1 firmware commit:
D1 binary SHA-256:
D1 Repeater identity prefix:
D1 Room identity prefix (combined only):
D2 hardware revision / firmware / identity prefix:
D3 hardware revision / firmware / identity prefix:
App version / phone OS:
RF profile (frequency / BW / SF / CR / TX power):
ISED/compliance approval reference:
Antennas:
Power source / battery state:
D1-D2 distance:
D1-D3 distance:
D2-D3 direct path verified unavailable? YES / NO
Serial log filenames:
App evidence filenames:

Test ID:
Expected:
Observed:
Result: PASS / FAIL / NOT TESTED / INCONCLUSIVE
Evidence:
Notes:
```

## 11. Stop conditions and recovery

Stop the run immediately if:
- The RF configuration or Canadian authorization is uncertain.
- Device identity changes unexpectedly or a key/ACL store appears to be overwritten.
- The device repeatedly reboots, overheats, reports flash errors or enters a format loop.
- Repeater forwarding stops when Room activity begins.
- A firmware image's board target, commit or SHA-256 is unknown.

Before any flash:
1. Confirm the exact hardware revision and correct target.
2. Save the currently installed firmware and all recoverable configuration/identity data using a verified method.
3. Confirm a known-good official recovery image and physical recovery procedure.
4. Verify the test binary's board target, source commit, RF flags and SHA-256.
5. Keep the original M5 client firmware images and known-good SenseCAP recovery image offline.
6. Do not erase InternalFS/ExtraFS unless the test specifically requires it and the data backup/recovery has been validated.

## 12. Current readiness

- Native unit tests: passed on PR head `2cc4dbbe5015c3f67d7c256294d3d3f813bdb040`.
- Full firmware matrix: running at the time this protocol was added; verify the run before recording final CI status.
- Physical test execution: not started.
- Firmware flashed: no.
- RF compliance gate: unresolved; no over-the-air transmission authorized by this protocol.
- Official client interoperability: not yet demonstrated.

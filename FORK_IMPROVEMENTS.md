# Fork Improvements

This fork adds a SenseCAP Solar–focused combined Repeater + Room Server implementation while retaining MeshCore's original Repeater as the primary application. The Room Server is a service inside that application—not a second Mesh engine—and shares the radio, dispatcher, packet pool and transmit queue.

## Implemented

- **Dual local identities:** preserves the existing Repeater identity and stores a separate Room identity; receive dispatch tries matching identities and uses packet authentication to select the correct service.
- **Identity-aware packet handling:** adds explicit-identity packet creation, path returns and receive hooks while keeping existing APIs Repeater-compatible.
- **Standard discovery:** advertises both Repeater and Room roles with their respective identities and supports Room discovery responses.
- **Independent access control:** gives Room its own ACL and storage path, separate from the original Repeater contacts.
- **Recoverable Room ACL storage:** adds versioned, checksummed, two-slot persistence with wrap-safe generation selection, verified writes, legacy migration and retry after failed saves. Recovery retries the older validated slot if the preferred slot fails its second read, stages without a large embedded-stack allocation, and rechecks payload checksum, trailing bytes and encoded route length before publishing contacts.
- **Persistent Room posts:** adds a bounded two-slot LittleFS journal with record integrity checks, recovery of valid records after a damaged tail, verified compaction, and read-back verification of each newly appended record before the server acknowledges the post. Journal loading prefers the newest valid generation and falls back if its second-pass load fails. An I/O read failure with a full appended record remaining now rejects that slot instead of being mistaken for a damaged tail. If both journal slots are unreadable but either file exists, startup preserves both files and disables Room rather than creating an empty replacement. SafeRoomExtraFS refuses CustomLFS's default erase-on-any-mount-failure behavior: only a fully blank first-use partition is formatted; nonblank mount failures preserve data and disable Room while keeping Repeater available.
- **Shared, prioritized radio scheduling:** routes Room traffic through the existing Repeater dispatcher and queue at lower priority, with bounded enqueue/pool use so Room traffic cannot displace essential Repeater work.
- **Synchronization recovery:** persists Room sync progress and learned peer routes (including route invalidation after flood logins), normalizes route-buffer tails before comparison, and retries failed ACL writes.
- **Solar power behavior:** schedules Room work around the Repeater's sleep path so future timers do not continuously prevent sleep.
- **Receive and storage hardening:** validates local identity keypairs, reserves safe receive-buffer terminator space, validates decrypted and persisted encoded path lengths, sanitizes malformed legacy routes for rediscovery, rejects undersized ACK/data headers, and tests encoded path-length boundaries. SafeRoomExtraFS refuses destructive auto-format on nonblank mount failure; new identities are read-back verified and unreadable existing keys are preserved; IdentityStore existence checks now honor the configured directory; atomic ACL clear commits an empty generation before retiring the prior slot; Room journal and ACL generation ordering and Room post-sequence recovery use wrap-safe uint32 arithmetic, including journals whose first retained sequence is already above 0x7FFFFFFF.
- **SenseCAP build configuration:** adds the dedicated ExtraFS partition for Room data while retaining the original InternalFS for Repeater settings and identities, plus the North American MeshCore radio preset.
- **CI maintenance:** adjusts PlatformIO build configuration, runs reproducible firmware matrices, and cancels superseded PR build/test runs so obsolete commits do not consume CI capacity.

## Development log — change-size snapshot (2026-11-27)

This snapshot records where the fork's accumulated changes have gone, measured against upstream `main` at base commit `75b04ef06def54b348400cb1b138afb6749ffd4f`, through fork commit `a8a62950049d790c4c3e4056ac36015863c5c362`.

| Area | Files changed | Lines added | Lines removed | What the work went into |
|---|---:|---:|---:|---|
| Repeater/Room integration and shared firmware source (`examples/`, `src/`, `variants/`) | 15 | 1,875 | 136 | Room service integration, shared identity-aware packet handling, ACL and journal persistence/recovery, radio scheduling, receive validation, sleep coordination, and SenseCAP build configuration |
| Documentation | 5 | 602 | 0 | Integration design, implementation log, storage failure-injection plan, and the three-device hardware lab protocol |
| Automated native tests | 4 | 530 | 0 | Atomic ACL recovery, packet path-length validation, Room journal recovery, and Room sequence rollover |
| CI workflows | 2 | 19 | 23 | Build/test workflow maintenance and cancellation of superseded runs |
| **Total** | **26** | **3,026** | **159** | **2,867 net lines added** |

The line counts are GitHub's per-file comparison totals; they are a measure of source change, not compiled firmware size. The largest production-file additions are `examples/simple_repeater/MyMesh.cpp` (+875), `src/helpers/ClientACL.cpp` (+313), `src/helpers/RoomJournalCore.h` (+223), and `examples/simple_repeater/main.cpp` (+102).

**Interpretation:** the bulk of production work is in integrating the Room service and hardening its persistence while retaining the existing Repeater. A substantial additional portion is test coverage and documentation; those files do not become part of the deployed firmware image. The current source-change total alone cannot establish flash usage or RAM headroom. The next engineering checkpoint is to record actual linked image size, static RAM, runtime heap/stack high-water marks, and packet/TX queue peaks for the combined SenseCAP build before treating resource fit as proven.

This snapshot should be refreshed as the implementation changes materially, with the comparison base and head commit recorded each time so the totals remain reproducible.


## Development log — resource-audit tooling (2026-11-27)

**Change:** added reproducible resource-audit build jobs to the existing `.github/workflows/pr-build-check.yml` (commit `10d4642ac53c45f71a95e7ed5b47610ea3d3eaf5`). A standalone workflow draft was removed so the audit runs under the already-established PR workflow.

The audit is designed to build three comparable SenseCAP images and preserve their build outputs:

1. **Upstream stock Repeater:** checked out at baseline commit `75b04ef06def54b348400cb1b138afb6749ffd4f`, before the fork's combined integration.
2. **Fork Room-only:** `SenseCap_Solar_room_server` from the pull-request revision.
3. **Fork combined Repeater + Room:** `SenseCap_Solar_repeater` from the pull-request revision.

For each target, the workflow captures PlatformIO's build report, ELF section sizes from `arm-none-eabi-size -A`, and available ELF/BIN/HEX/MAP outputs as GitHub Actions artifacts. This gives us a reproducible first comparison of flash and static section usage. It does not measure runtime heap fragmentation, stack high-water marks, packet-pool peaks, radio load, or power consumption; those remain separate runtime/hardware measurements.

**Important comparison detail:** upstream `main` still defines `SenseCap_Solar_repeater` as the stock Repeater. The fork repurposes that environment for the combined image and adds the ExtraFS linker layout. Therefore, the baseline must be built from the pinned upstream revision, while Room-only and combined images are built from the fork revision.

**First source-level RAM finding:** the combined `MyMesh` instance adds a second 32-client ACL, a fixed 32-entry Room post cache, a second 32-entry peer-index array, and increases the packet pool from 32 to 36 objects. The precise byte cost remains pending target ELF and runtime measurements; the formulas and evidence are recorded in [the resource audit](./docs/RESOURCE_AUDIT.md).

**Status at this log entry:** PR workflow updated; no audit run or build-size result is yet available in the retrieved CI records. Do not infer a pass from the workflow change existing. The first successful run and its artifacts will be reviewed and summarized here.

Full measurement protocol and result tables: [docs/RESOURCE_AUDIT.md](./docs/RESOURCE_AUDIT.md).

## Development log — target-specific resource instrumentation (2026-11-27)

**Changes committed:** `3dd56a8c` adds opt-in serial footprint diagnostics to `examples/simple_repeater/main.cpp`; `a3b3359c` adds the bench-only `SenseCap_Solar_repeater_resource_audit` PlatformIO environment; `23b44fd0` adds that instrumented image to the PR resource-audit build; `4b10fef5`, `45df8642`, `d699ab53` and `b1400233` document the measurement procedure, linker memory regions and four-image result table.

**What the diagnostics measure:** target-compiler `sizeof` values for `MyMesh`, `ClientInfo`, `ClientACL`, `RoomPostInfo`, `mesh::Packet` and `StaticPoolPacketManager`; the Room post cache; one ACL client array; all 36 packet objects; and the backing arrays for the three packet queues. The diagnostic is guarded by `RESOURCE_AUDIT` and does not add serial output to the production image.

**Important linker finding:** the ExtraFS linker script reserves an application FLASH region of 708,608 bytes and an application RAM region of 237,568 bytes. The latter is the usable RAM region defined for the application by this linker script, not the nRF52840's full 256 KB physical SRAM. This distinction is now recorded in `docs/RESOURCE_AUDIT.md`.

**CI observation:** a prior native run on commit `df6f04f9` passed 80/80 tests. A later run on `bcbd534a` reported 80 successes and one test compile failure (`test_atomic_slot_recovery` included `unity.h`, which is unavailable in the GoogleTest native harness). The current branch version of that test includes `gtest/gtest.h`, but this correction and the new resource-audit targets still require a fresh run. The latest retrieved Actions list did not contain a run for the current branch head; no build sizes or object-size figures are claimed yet.

**Next gate:** trigger the PR Build Check workflow on the current branch, inspect all four resource images and the serial diagnostics, then add measured flash/static RAM values to `docs/RESOURCE_AUDIT.md`. Runtime free heap, stack high-water marks, packet-pool peaks and three-device load tests remain separate work.

## Development log — compiler stack-frame reporting (2026-11-27)

**Changes:** `c15cb877` enables GCC `-fstack-usage` in the instrumented SenseCAP build; `cf4ce147` archives the generated `.su` reports with the resource-audit artifacts; `0061534d` documents how to interpret them.

This adds a static stack-frame view to the audit without changing the production firmware target. The `.su` files can identify functions with unusually large frames and guide targeted runtime testing. They do not establish worst-case call-chain depth, interrupt nesting or actual stack high-water marks. Runtime validation remains mandatory.

## Verification status

- **Passed:** native unit tests 80/80 and all 16 firmware targets on code commit `524fce7c21009c3e4f029f5d08a10a187cab0816`. [Native run](https://github.com/BenBeg/MeshCore/actions/runs/46082374667) · [Firmware matrix](https://github.com/BenBeg/MeshCore/actions/runs/46082374675).
- **Passed:** all 16 firmware targets on `bcbd534a3021bfa4bed72495aff8ea7c637c62de`, including SenseCAP Solar Repeater and ThinkNode M5 BLE companion. The native test run on that commit had 80 successful tests but the new ACL recovery test did not compile because it used Unity headers instead of the repository's GoogleTest harness.
- **Corrected, pending CI:** ACL recovery tests were converted to GoogleTest in `9fe734a4`. Subsequent commits `229bc2bd`, `391c0f76`, `5c99278d`, `2a1c1e43`, `26487871`, `4f941ba9`, `947418e1`, `fdaa249c`, and `bb6d2967` fix identity-path detection and harden ACL/journal recovery (stack use, second-pass integrity checks, portable generation ordering, journal fallback and fail-closed handling of existing unreadable journals). The lab protocol was updated in `003df990` to include a sacrificial-storage validation case. The current branch head has not yet received a fresh CI run, so these latest changes are not claimed as verified.
- **Resource audit tooling added, results pending:** the new SenseCAP Resource Audit workflow builds upstream stock Repeater, fork Room-only and fork combined images, then captures linker section sizes and build artifacts. No successful run has yet been observed; flash/RAM measurements remain unverified.
- **Still required:** production-integrated ACL filesystem load/migration/reboot tests; identity and ExtraFS failure injection; sacrificial-device power-loss testing; real-device sleep, storage and radio tests; official-client interoperability; Canadian ISED authorization verification.
- **Not done:** no device has been flashed and the pull request has not been merged.

## Storage failure-injection plan

[ROOM_STORAGE_FAILURE_INJECTION.md](./docs/ROOM_STORAGE_FAILURE_INJECTION.md) defines the deterministic host-test and sacrificial-device matrix. The current host harness already covers Room journal scan/append/compaction behavior, ACL inactive-slot write ordering, ACL clear ordering, and the shared ACL slot-selection/fallback policy. The ACL recovery helper tests are not yet a substitute for testing the complete production filesystem load/migration path or reboot recovery. Identity/ExtraFS fault injection and physical power-loss tests remain outstanding.

## Hardware validation protocol

[THREE_DEVICE_LAB_PROTOCOL.md](./docs/THREE_DEVICE_LAB_PROTOCOL.md) defines the three-device stock-versus-combined lab using the SenseCAP P1-Pro and two unmodified MeshCore ThinkNode M5 clients. It includes device roles, RF compliance gate, bench setup, serial/app evidence capture, expected behavior, relay negative control, reboot/persistence tests and a pass/fail scorecard.

See [the integration design and verification notes](./docs/SENSECAP_REPEATER_ROOM_INTEGRATION.md) for architecture and remaining checks, and [the Three-Device Lab Protocol](./docs/THREE_DEVICE_LAB_PROTOCOL.md) for the practical hardware validation plan.

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


## Development log — CI rerun diagnosis (2026-11-28)

**Observed:** the manually requested rerun was a retry of the previously failed **Run Unit Tests** workflow run `46083362430`, attempt 2. It tested the older PR merge revision `bcbd534a3021bfa4bed72495aff8ea7c637c62de`, not the current branch head `dfb95eb35583ce2210b91cc1da5930a36cfd3e18`.

**Result:** 80 of 81 native test cases succeeded. The sole build error was `test/test_atomic_slot_recovery/test_atomic_slot_recovery.cpp:1:10: fatal error: unity.h: No such file or directory`. This is the known old-revision harness mismatch, not a new runtime test assertion failure. The current branch version of that exact file begins with `#include <gtest/gtest.h>` (blob `797dba2b`), so the correction is present in the branch but was not exercised by this rerun.

**Build workflow:** the PR Build Check for the same older revision completed successfully, including all 16 firmware matrix targets. That run did not contain the newly added resource-audit jobs and produced no resource artifacts. It therefore does not provide flash, static RAM or stack-frame measurements.

**Next verification action:** start a new `PR Build Check` and `Run Unit Tests` workflow run from branch `feature/sensecap-repeater-room` (rather than using **Re-run failed jobs** on the older run). The resource audit is still pending. No firmware resource-fit claim is made from these results.


## Development log — packet-manager heap accounting (2026-11-28)

**Source audit:** inspected `src/helpers/StaticPoolPacketManager.cpp` and `.h` against the new target-size diagnostics. `StaticPoolPacketManager` constructs three `PacketQueue` objects; each queue dynamically allocates a packet-pointer array, priority array and schedule array sized to the pool. The manager also allocates every `mesh::Packet` object individually. At pool size 36, the diagnostic's separate packet-object and three-queue-array formulas correctly account for requested payload bytes, but `sizeof(StaticPoolPacketManager)` alone is only the manager/queue-object size and excludes those heap allocations.

**Documentation update:** `docs/RESOURCE_AUDIT.md` now explicitly distinguishes linker-visible static RAM from packet-pool heap payload and notes allocator metadata, alignment and fragmentation as additional costs. This is source-level accounting only; target compiler output and runtime free-heap measurements are still required for actual resource acceptance.


## Development log — CI resource-audit and native-runner corrections (2026-11-28)

**Observed on commit `e9e0a989f663f79f8970eb9377131d306c9f7ed1`:** the SenseCAP production combined image and Room-only image compiled, and all 16 firmware matrix targets passed. The native suite executed 88 test cases: 87 passed; `test_atomic_slot_recovery` failed at link time because its GoogleTest source had no `main()` entry point. The source was corrected to use the same explicit GoogleTest runner pattern as the other native test targets.

**Resource-audit findings:** the upstream stock Repeater built successfully and PlatformIO reported 34,876 bytes RAM and 365,396 bytes flash against that baseline's 235,520-byte RAM and 811,008-byte flash limits. These are PlatformIO's environment-specific usage figures, not yet a normalized apples-to-apples comparison with the fork's ExtraFS linker layout. The stock report collection then failed because the workflow assumed `arm-none-eabi-readelf` was beside `arm-none-eabi-size`. The report step now treats readelf as optional while retaining the required `arm-none-eabi-size` section reports and binary/ELF capture.

The fork's Room-only image built, and the combined production image built with PlatformIO reporting 47,564 bytes RAM and 384,696 bytes flash against its 235,520-byte RAM and 708,608-byte flash limits. The diagnostic build failed before compilation because its PlatformIO environment did not explicitly extend the `env:SenseCap_Solar_repeater` environment. Its inheritance reference has been corrected. The figures above are preliminary build summaries; the resource audit remains incomplete until the instrumented image and artifact collection succeed.

**Next verification:** rerun native tests and PR Build Check on the corrected branch head, confirm all four images and artifacts, then record ELF sections and stack-usage data in `docs/RESOURCE_AUDIT.md`. No hardware has been flashed and PR #1 remains draft.


## Development log — resource artifact collector correction (2026-11-28)

**Observed on PR Build Check run `46100040146`, branch head `be73fcd40dd53d8c15248c2fc9604bd1a48140a3`:** all 16 firmware matrix targets passed, including the SenseCAP Solar Repeater and ThinkNode M5 BLE target. The stock Repeater, Room-only, combined production, and instrumented diagnostic images all compiled. Both resource-audit jobs then failed in the capture step because the script required a `.bin` file that PlatformIO did not emit in these environments (it emitted ELF/HEX/ZIP outputs).

**Correction:** the capture steps now use the target toolchain's `arm-none-eabi-objcopy` to generate a raw binary from each ELF when a pre-existing `.bin` is absent. The collector still requires the ELF and `arm-none-eabi-size`, and preserves the ELF, section report, generated binary, HEX/MAP and available `.su` files. This changes CI artifact collection only; it does not alter firmware source or production build flags.

**Verification status:** pending a fresh PR Build Check after this workflow correction. The prior run's successful firmware compilation remains valid, but no resource artifact is claimed until both upload jobs complete and the artifacts are inspected.


## Development log — first successful resource audit (2026-11-28)

**Verified:** PR Build Check run [46100886098](https://github.com/BenBeg/MeshCore/actions/runs/46100886098) completed successfully on branch head `92b52fd0fb597390c9e667f47032721f0892e835`. Native unit tests also passed in run [46100886090](https://github.com/BenBeg/MeshCore/actions/runs/46100886090). All 16 firmware matrix targets passed, both resource-audit jobs passed, and both artifact archives were uploaded and inspected.

**Measured production image comparison:**

| Metric | Upstream stock Repeater | Fork combined Repeater + Room |
|---|---:|---:|
| Firmware BIN | 365,396 B | 384,696 B |
| `.text` + `.ARM.exidx` | 363,536 B | 382,832 B |
| `.data` | 1,860 B | 1,864 B |
| `.bss` | 33,016 B | 45,700 B |
| `.data` + `.bss` | 34,876 B | 47,564 B |
| Linker `.heap` reservation | 200,644 B | 187,956 B |

The combined image is 19,300 bytes larger in BIN form (+5.28%). Its linked RAM use (`.data` + `.bss`) is 12,688 bytes higher. The linker correspondingly reduces the reserved heap by 12,688 bytes; the total application RAM region accounting remains 235,520 bytes. The fork's application flash region is 708,608 bytes, leaving 323,912 bytes of BIN-based headroom for the combined production image. Stock and fork linker flash regions differ, so their percentages must not be compared using a shared denominator.

The Room-only image is 359,540 B BIN; the combined diagnostic image is 385,528 B BIN. The diagnostic artifact includes compiler `.su` reports. The largest individual reported static stack frame is 2,352 bytes in `ge_double_scalarmult_vartime`; this is not a complete call-chain or runtime high-water measurement. The target `sizeof` serial diagnostic is compiled into the diagnostic image, but still needs to be captured on hardware.

**Engineering interpretation:** firmware size is within the linker region with substantial flash headroom. RAM is the more important remaining risk: the combined integration consumes an additional 12.7 KB of statically allocated RAM and reduces the linker's heap reservation by the same amount. We cannot call the device resource-safe until actual free-heap/largest-block, stack high-water, packet-pool/TX queue peaks and forwarding latency are measured under concurrent Repeater + Room load.

**Next actions:** add or confirm low-overhead runtime heap/stack/queue instrumentation; inspect target-specific stack-frame hotspots and call paths; then perform the documented three-device bench protocol. No hardware has been flashed. PR #1 remains open and draft; no merge was performed.


## Development log — diagnostic runtime packet/queue high-water counters (2026-11-28)

Added opt-in runtime telemetry to the resource-audit build only. `StaticPoolPacketManager` now records peak checked-out packet count, peak outbound queue depth and peak delayed inbound queue depth when `RESOURCE_AUDIT` is defined. The SenseCAP diagnostic firmware prints a `RESOURCE_RUNTIME` sample after 10 seconds and every 60 seconds thereafter, including current free packet count and current outbound queue depth. These diagnostic-only fields and branches are not compiled into production firmware.

**Scope limitation:** this is packet-pool/queue pressure telemetry, not a heap monitor. It does not report allocator free bytes, largest free block, heap fragmentation, stack high-water or worst-case call-chain stack. Those APIs have not yet been added because the exact nRF52 framework allocator and stack boundaries must be confirmed first. No runtime measurement exists until the diagnostic image is flashed and exercised on the target.

**Verification:** pending CI for the instrumentation commits. Next: confirm diagnostic and production targets compile, then inspect the pinned nRF52 framework/toolchain for a safe allocator/stack measurement method before expanding instrumentation. Hardware remains unflashed; PR remains draft.


## Development log — nRF52 loop-task stack high-water diagnostic (2026-11-28)

Inspected the exact pinned Adafruit nRF52 core revision used by this PlatformIO environment (`d541301665b40959682252911e57b11df3ee651a`). Its Arduino `loop()` runs in a FreeRTOS task created with a 1,024-word stack, and the core enables `INCLUDE_uxTaskGetStackHighWaterMark`. The audit diagnostic now reports `uxTaskGetStackHighWaterMark(NULL) × sizeof(StackType_t)` as minimum-ever free bytes for the calling loop task. This is a true task stack high-water measurement for the loop task, not a complete system-wide stack measurement; the BLE callback task and interrupt stack are separate and remain outside this sample.

The same source inspection found the selected FreeRTOS memory manager is `heap_3`, which delegates allocation to newlib malloc. Although FreeRTOS `portable.h` declares `xPortGetFreeHeapSize()` and `xPortGetMinimumEverFreeHeapSize()`, `heap_3.c` does not implement those functions. The diagnostic will not call these unavailable APIs. Reliable free-heap/largest-block measurement remains open and needs a compatible newlib allocator-statistics method or explicit allocation accounting.

**Verification:** latest diagnostic/stack instrumentation commit is pending CI. No device has been flashed; no runtime sample has yet been captured.

# SenseCAP Firmware Resource Audit

## Purpose

Establish whether the combined SenseCAP Solar Repeater + Room Server image fits the nRF52840 target with defensible flash, RAM, stack and runtime headroom. A successful compile alone is not a resource-safety result.

## Build comparison

The GitHub Actions workflow `.github/workflows/pr-build-check.yml` (resource-audit jobs) builds three images:

| Image | Source revision | Purpose |
|---|---|---|
| Stock SenseCAP Repeater | Pinned upstream baseline `75b04ef06def54b348400cb1b138afb6749ffd4f` | Reference cost of the original Repeater |
| SenseCAP Room Server | Current fork PR revision | Reference cost of the standalone Room application |
| Combined Repeater + Room | Current fork PR revision | Actual production target image under evaluation |
| Instrumented combined image | Current fork PR revision, `RESOURCE_AUDIT=1` | Prints target-compiler object sizes at boot; diagnostic only, not the production image |

The upstream baseline is intentionally pinned so results remain reproducible. If the project rebases or changes its baseline, update this document and the workflow together.

## Target memory regions

The nRF52840 has 256 KB physical RAM and 1 MB internal flash. The actual ExtraFS linker script, `boards/nrf52840_s140_v7_extrafs.ld`, defines an application FLASH region at origin `0x27000` with length `0xAD000` (708,608 bytes), and an application RAM region at origin `0x20006000` with length `0x3A000` (237,568 bytes). The lower RAM range is reserved outside this application region for the SoftDevice/system configuration. The PlatformIO upload ceiling matches the FLASH region. It is a limit, not a measurement of the linked image or remaining headroom. External QSPI flash is persistent storage and does not add working RAM.

## Build-time measurements

For every image, retain the exact source SHA, PlatformIO build log, ELF, available BIN/HEX/MAP files, and ELF section report.

Record:

| Metric | Stock Repeater | Room-only | Combined |
|---|---:|---:|---:|
| Build result | Pending | Pending | Pending |
| Firmware BIN bytes | Pending | Pending | Pending | Pending |
| Instrumented image BIN bytes | — | — | — | Pending |
| ELF total bytes | Pending | Pending | Pending |
| Flash ceiling / linker region | Confirm | Confirm | 708,608-byte configured upload ceiling |
| `.text` + `.rodata` | Pending | Pending | Pending |
| `.data` | Pending | Pending | Pending |
| `.bss` | Pending | Pending | Pending |
| Other RAM-resident sections | Pending | Pending | Pending |
| Static RAM estimate | Pending | Pending | Pending | Pending |
| Target `sizeof` inventory | — | — | — | Pending |
| Remaining flash headroom | Pending | Pending | Pending | Pending |

Use PlatformIO's final build summary and `arm-none-eabi-size -A` output. Do not add the external QSPI filesystem capacity to available MCU RAM or to the internal application flash budget. Confirm the linker script's actual memory regions before calculating headroom.

## Source-level RAM inventory (preliminary, not a measurement)

A first source inspection identifies the main fixed-memory costs that the ELF report must confirm:

- **Second Room ACL:** `MyMesh` owns both the original `ClientACL acl` and a separate `ClientACL room_acl`. Each ACL contains `ClientInfo clients[MAX_CLIENTS]`; the default `MAX_CLIENTS` is 32. The additional fixed storage is therefore `32 × sizeof(ClientInfo)`, before the Room ACL's own runtime allocations.
- **Room post cache:** `MyMesh` contains `RoomPostInfo room_posts[ROOM_MAX_UNSYNCED_POSTS]`; the default is 32. Each entry contains an author identity, two 32-bit values plus a 32-bit timestamp, and a 152-byte text array. The exact footprint is `32 × sizeof(RoomPostInfo)`; compiler padding and the exact identity layout must be taken from the target ELF.
- **Second peer-index array:** the Room service adds `room_matching_peer_indexes[MAX_CLIENTS]` alongside the existing Repeater matching array. At the default 32 clients and 32-bit `int`, this is another 128 bytes.
- **Packet pool increase:** when `ROOM_EXTRA_FS` is enabled, the Repeater's `StaticPoolPacketManager` is constructed with 36 packets instead of 32. Each packet has fixed path and payload arrays (`MAX_PATH_SIZE=64`, `MAX_PACKET_PAYLOAD=184`). The four additional packet objects plus their three queue-table entries increase dynamic pool/queue memory; exact target `sizeof(mesh::Packet)`, allocator overhead and total heap impact still need build/runtime confirmation.

These observations show why the resource audit must not look only at firmware flash size. The Room service adds meaningful fixed RAM and heap pressure even though it shares the Repeater's radio and packet manager. Do not treat the formulas above as final byte counts; use the target compiler's ELF section report and runtime measurements for acceptance.

## Opt-in target-compiler object-size diagnostics

A bench-only PlatformIO environment named `SenseCap_Solar_repeater_resource_audit` enables `RESOURCE_AUDIT=1`. On boot it prints target-compiler `sizeof` values for `MyMesh`, `ClientInfo`, `ClientACL`, `RoomPostInfo`, `mesh::Packet` and `StaticPoolPacketManager`, plus the Room post cache, one ACL client array, all 36 packet objects and the three packet-queue backing arrays.

These are compiler-derived object sizes, not estimates. They exclude allocator metadata, heap fragmentation and stack use. The diagnostic build is not the production firmware. At 115200 baud, capture the complete boot output beginning with `=== SENSECAP RESOURCE AUDIT: COMPILE-TIME FOOTPRINT ===` and record the exact firmware SHA and build artifact. This is a static inventory, not a free-heap measurement.

## Runtime measurements

Build-time section sizes do not reveal heap fragmentation, worst-case stack use, or queue pressure. These require runtime instrumentation and hardware tests.

Capture the following on the SenseCAP P1-Pro:

- Minimum free heap and largest allocatable block, if the selected allocator exposes both reliably.
- Main-loop/task stack high-water mark, plus any separate task/callback stack that can execute Room code.
- Peak packet-pool occupancy and TX queue occupancy.
- Maximum simultaneous Room records/buffers and temporary storage buffers.
- Repeater forwarding latency and packet loss while Room posts/synchronization are active.
- Wake frequency, time spent awake/asleep, and current consumption in idle and loaded states.
- Behavior during journal compaction, ACL save/recovery, ExtraFS mount failure, and repeated reboot/power interruption.

Do not add allocator-specific or RTOS-specific APIs until the actual nRF52 runtime and allocator used by this firmware have been confirmed from the pinned framework/toolchain. Instrumentation must be low-overhead and disabled or bounded in production builds.

## Three-device runtime scenarios

Use the existing [Three-Device Lab Protocol](./THREE_DEVICE_LAB_PROTOCOL.md). Keep both ThinkNode M5 clients on identical unmodified official Companion firmware throughout baseline and combined phases.

| Scenario | Repeater requirement | Room requirement | Evidence |
|---|---|---|---|
| Idle / sleep | Remains available and enters expected sleep state | No unnecessary wakeups | Serial timestamps and current draw |
| Repeater forwarding only | Baseline throughput/latency | Room inactive | Client observations and logs |
| Room discovery/login | Forwarding remains responsive | Discovery and authentication succeed | Serial logs and app captures |
| Room post + sync | No starvation of essential forwarding | Post persisted and synchronized | Client captures, journal state |
| Concurrent load | No unexplained forwarding regression | Room work remains bounded | Queue/heap/stack peaks |
| Storage recovery | Repeater remains operational | Valid data recovered or Room fails closed | Fault-injection logs |
| Reboot / wake | Repeater identity and forwarding recover | Room identity, ACL and journal recover | Before/after logs |

Record every case as **PASS**, **FAIL**, **NOT TESTED**, or **INCONCLUSIVE**. A client-visible success alone is not proof that a record was durably committed; correlate it with server logs and post-reboot state where practical.

## Acceptance gates

Do not claim resource suitability until all of the following are satisfied:

1. All three comparison builds complete and their exact artifacts are retained.
2. Combined image fits the actual linker/upload region with an explicit flash reserve.
3. Static RAM and measured worst-case runtime heap/stack leave explicit reserves.
4. Packet pool and TX queue remain bounded during concurrent Repeater and Room activity.
5. Essential Repeater forwarding remains responsive under Room load and storage recovery.
6. Sleep/wake behavior remains compatible with the solar power objective.
7. Results are repeated after material changes to the integration, packet scheduling, storage, or framework.

The team must choose numerical reserve thresholds after seeing the first build and runtime measurements; do not invent a threshold before the baseline is known.

## Current status

- Workflow implementation: committed; first successful run not yet confirmed.
- Binary sizes and ELF section measurements: pending.
- Runtime heap/stack/queue measurements: pending.
- Three-device performance and sleep tests: pending.
- Hardware has not been flashed as part of this audit.

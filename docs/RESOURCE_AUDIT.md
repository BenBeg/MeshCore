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

| Metric | Stock Repeater | Room-only | Combined production | Combined diagnostic |
|---|---:|---:|---:|---:|
| Build result | PASS | PASS | PASS | PASS |
| Firmware BIN bytes | 365,396 | 359,540 | 384,696 | 385,528 |
| ELF `.text` + `.ARM.exidx` | 363,536 | 357,696 | 382,832 | 383,664 |
| `.data` | 1,860 | 1,844 | 1,864 | 1,864 |
| `.bss` | 33,016 | 36,752 | 45,700 | 45,700 |
| Linker `.heap` reservation | 200,644 | 196,924 | 187,956 | 187,956 |
| `.data` + `.bss` (PlatformIO RAM use) | 34,876 | 38,596 | 47,564 | 47,564 |
| Total `.data` + `.bss` + `.heap` region | 235,520 | 235,520 | 235,520 | 235,520 |
| Flash ceiling | 811,008 | 708,608 | 708,608 | 708,608 |
| BIN-based flash headroom | 445,612 | 349,068 | 323,912 | 323,080 |
| Target `sizeof` inventory | — | — | — | Built; serial capture pending |

**Artifact source:** PR Build Check run [46100886098](https://github.com/BenBeg/MeshCore/actions/runs/46100886098), branch SHA `92b52fd0fb597390c9e667f47032721f0892e835`. The stock report is from the pinned upstream baseline; the other three images are from the fork. BIN-based headroom is linker-region length minus BIN size and is a useful first estimate, not a substitute for checking load segments, bootloader reservations or deployment packaging.

**Interpretation:** the combined production image is 19,300 bytes (5.28%) larger than the stock Repeater BIN. Its linked `.data` + `.bss` rises by 12,688 bytes, from 34,876 to 47,564 bytes. The linker correspondingly reserves 12,688 fewer bytes for `.heap`, leaving the total RAM region allocation unchanged at 235,520 bytes. This does **not** prove runtime heap sufficiency: it means the integration consumes more of the RAM region that the linker previously made available as heap. Combined BIN flash headroom is 323,912 bytes (about 45.7% of the fork's 708,608-byte application region).

The stock and fork flash ceilings differ because their linker layouts differ; compare absolute bytes and the actual image-specific region, not percentages against a shared assumed ceiling. The generated raw BIN is useful for image size/headroom, but ELF section and linker data remain authoritative for understanding RAM.

## Source-level RAM inventory (preliminary, not a measurement)

A first source inspection identifies the main fixed-memory costs that the ELF report must confirm:

- **Second Room ACL:** `MyMesh` owns both the original `ClientACL acl` and a separate `ClientACL room_acl`. Each ACL contains `ClientInfo clients[MAX_CLIENTS]`; the default `MAX_CLIENTS` is 32. The additional fixed storage is therefore `32 × sizeof(ClientInfo)`, before the Room ACL's own runtime allocations.
- **Room post cache:** `MyMesh` contains `RoomPostInfo room_posts[ROOM_MAX_UNSYNCED_POSTS]`; the default is 32. Each entry contains an author identity, two 32-bit values plus a 32-bit timestamp, and a 152-byte text array. The exact footprint is `32 × sizeof(RoomPostInfo)`; compiler padding and the exact identity layout must be taken from the target ELF.
- **Second peer-index array:** the Room service adds `room_matching_peer_indexes[MAX_CLIENTS]` alongside the existing Repeater matching array. At the default 32 clients and 32-bit `int`, this is another 128 bytes.
- **Packet pool increase:** when `ROOM_EXTRA_FS` is enabled, the Repeater's `StaticPoolPacketManager` is constructed with 36 packets instead of 32. Each packet has fixed path and payload arrays (`MAX_PATH_SIZE=64`, `MAX_PACKET_PAYLOAD=184`). The four additional packet objects plus their three queue-table entries increase dynamic pool/queue memory; exact target `sizeof(mesh::Packet)`, allocator overhead and total heap impact still need build/runtime confirmation.

These observations show why the resource audit must not look only at firmware flash size. The Room service adds meaningful fixed RAM and heap pressure even though it shares the Repeater's radio and packet manager. Do not treat the formulas above as final byte counts; use the target compiler's ELF section report and runtime measurements for acceptance.

### Packet-manager allocation detail (source-verified)

`StaticPoolPacketManager` does not embed its packet objects or queue tables inside the manager object. Its constructor creates `pool_size` individual `mesh::Packet` objects with `new`, and each of its three `PacketQueue` instances allocates three parallel arrays with `new`: packet pointers, 8-bit priorities and 32-bit scheduled times. With the combined target's pool size of 36, the payload bytes requested by these allocations are therefore:

- Packet objects: `36 × sizeof(mesh::Packet)`.
- Queue arrays: `3 queues × 36 entries × (sizeof(mesh::Packet*) + sizeof(uint8_t) + sizeof(uint32_t))`.

Consequently, `sizeof(StaticPoolPacketManager)` reports only the manager and queue-object members; it does **not** include these heap allocations. The diagnostic prints both heap payload formulas separately. Allocator metadata, alignment overhead, fragmentation and other dynamic allocations remain additional costs. This distinction is important when reconciling ELF `.bss` with runtime free-heap measurements.


## Opt-in target-compiler object-size diagnostics

A bench-only PlatformIO environment named `SenseCap_Solar_repeater_resource_audit` enables `RESOURCE_AUDIT=1`. On boot it prints target-compiler `sizeof` values for `MyMesh`, `ClientInfo`, `ClientACL`, `RoomPostInfo`, `mesh::Packet` and `StaticPoolPacketManager`, plus the Room post cache, one ACL client array, all 36 packet objects and the three packet-queue backing arrays.

These are compiler-derived object sizes, not estimates. They exclude allocator metadata, heap fragmentation and stack use. The diagnostic build is not the production firmware. At 115200 baud, capture the complete boot output beginning with `=== SENSECAP RESOURCE AUDIT: COMPILE-TIME FOOTPRINT ===` and record the exact firmware SHA and build artifact. This is a static inventory, not a free-heap measurement. The diagnostic target also compiles with GCC `-fstack-usage`; the workflow archives the generated `.su` files so we can rank individual function stack-frame estimates. These per-function figures are not a complete worst-case call-chain or interrupt-stack measurement and must be validated against runtime stack use.

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

- Workflow implementation: verified on PR Build Check run `46100886098`; both resource artifact archives uploaded successfully.
- Stock, Room-only, combined production and combined diagnostic BIN/ELF section measurements: recorded above.
- Instrumented `.su` reports archived; largest individual compiler-reported frame is 2,352 bytes (`ge_double_scalarmult_vartime`). This is a per-function frame, not a worst-case call-chain or runtime stack high-water measurement.
- Diagnostic firmware now emits a RESOURCE_RUNTIME line every 60 seconds after the first 10 seconds, with current free packet count, peak packet-pool occupancy, current/peak TX queue and peak delayed-RX queue. Counters are compiled only under RESOURCE_AUDIT; production firmware layout/behavior is unchanged by these counters.
- The diagnostic sample also reports FreeRTOS loop-task minimum-ever free stack bytes using the pinned nRF52 core's enabled uxTaskGetStackHighWaterMark API. This covers the Arduino loop task only, not BLE callback/ISR stacks. The diagnostic also attempts to report newlib mallinfo totals (`arena`, `uordblks`, `fordblks`) because the pinned core's heap_3 delegates to newlib malloc. These values are aggregate allocator accounting, not the largest contiguous free block; fragmentation can make the largest requestable allocation smaller. The FreeRTOS xPortGetFreeHeapSize declarations are not implemented by heap_3 and are not called. Compilation and runtime plausibility of the mallinfo fields must be confirmed by CI and a target capture.
- Three-device performance and sleep tests: pending.
- Hardware has not been flashed as part of this audit.


## Runtime diagnostic implementation notes

The diagnostic build tracks packet-manager high-water values in StaticPoolPacketManager only when RESOURCE_AUDIT is defined. It reports:
- current unused packet count;
- peak number of packets checked out of the pool since boot;
- current and peak outbound queue depth;
- peak delayed inbound queue depth.

The diagnostic serial sample is emitted at 115200 baud every 60 seconds, starting after 10 seconds. These counters add a small amount of diagnostic-only state and branch work; they are absent from the production build. Peak values are cumulative since boot and reset only on reboot. They expose packet-pool pressure and queue pressure, but do not measure allocator fragmentation, free heap, largest allocatable block, call-chain stack high-water or interrupt stack use. Do not infer those values from packet counts.

The loop-task stack high-water value is reported in bytes (FreeRTOS stack words multiplied by sizeof(StackType_t)). It is the minimum free stack observed since task creation, and is therefore more useful than a one-time stack-pointer snapshot. The BLE callback task has a separate stack and must be measured separately before a whole-device stack reserve can be claimed.


The heap fields are aggregate newlib allocator statistics. `heap_free_total_bytes` must not be presented as the largest available allocation. A fragmented heap can have substantial total free bytes but no sufficiently large contiguous block for a particular request. The allocator's current arena size is not the full theoretical RAM headroom up to the linker heap limit.

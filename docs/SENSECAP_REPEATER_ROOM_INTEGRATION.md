# SenseCAP Solar Repeater + Room Server Integration

Status: source-audit design note; implementation has not started.

Target branch: `feature/sensecap-repeater-room`
Target PlatformIO environment: `SenseCap_Solar_repeater`
Base: upstream-compatible MeshCore `main`

## Objective

Keep the complete upstream Repeater application and add a standard MeshCore Room Server service to it. The combined device must use one SX1262 radio, one Dispatcher, one Mesh transport engine, one packet pool/queue, and the existing Repeater main loop. It must operate without Internet access and remain compatible with unmodified MeshCore clients and repeaters.

## Verified source facts

- The SenseCAP Solar variant defines the SX1262 wrapper and pins in `variants/sensecap_solar/platformio.ini` and `target.h`.
- The two existing target environments compile separate source sets: `examples/simple_repeater/*.cpp` and `examples/simple_room_server/*.cpp`.
- Both application classes derive from `mesh::Mesh`; both instantiate their own `StaticPoolPacketManager` and both call `Mesh::begin/loop`. They cannot simply be linked together.
- `Mesh::onRecvPacket()` currently uses the single public `self_id` for direct destination matching and anonymous login decryption. `routeRecvPacket()` appends `self_id` to flood paths. `createDatagram()` and `createPathReturn()` encode `self_id` as sender.
- `createAdvert(const LocalIdentity&, ...)` already accepts an explicit signing identity.
- The Room implementation has a separate `ClientACL`, and the ACL persistence format is currently hard-coded to `/s_contacts`. Its union contains Room-specific sync/ACK state, so the Room ACL cannot share the Repeater ACL instance.
- Room posts are currently held only in a cyclic RAM array (`MAX_UNSYNCED_POSTS`, default 32); they are lost on reboot.
- Room responses and sync posts use standard MeshCore payloads (`PAYLOAD_TYPE_TXT_MSG`, `PAYLOAD_TYPE_REQ`, `PAYLOAD_TYPE_RESPONSE`, signed-plain text). No new client packet format is needed.
- Repeater discovery currently responds only as `ADV_TYPE_REPEATER`; combined firmware must also support standard Room discovery as `ADV_TYPE_ROOM`.
- Dispatcher selects one outbound packet by priority from one queue. The shared packet manager therefore provides the correct single radio scheduler, but Room service work must be bounded and use lower priority than Repeater forwarding/control traffic.
- `IdentityStore` stores files as `<dir>/<name>.id`; the current main identity is `_main.id`. On NRF52, `save()` removes the target before opening it for write, so it is not an atomic replacement operation.

## Identity and packet-routing design

1. Preserve the Repeater's existing `_main.id` and its public identity as the primary identity.
2. Load or generate a separate Room keypair in `_room.id`. Never overwrite or repurpose `_main.id`.
3. Keep both identities continuously receive-active. Do not swap `self_id` around callbacks and do not alternate receive identities.
4. Add explicit identity context to the core receive path:
   - For `PAYLOAD_TYPE_PATH/REQ/RESPONSE/TXT_MSG`, test each local destination identity that matches the destination hash; attempt peer lookup and authenticated decryption against that identity's ACL; dispatch only the successfully authenticated service.
   - For `PAYLOAD_TYPE_ANON_REQ`, test each matching local identity and derive/decrypt with that identity. Deliver the authenticated login to that service only.
   - If truncated hashes collide, try each candidate independently and dispatch only the candidate whose MAC/authentication succeeds. Never dispatch one packet twice.
5. Make local packet construction identity-explicit. Room-generated datagrams and path returns use the Room identity; Repeater-generated packets use the Repeater identity. Keep the existing default overloads bound to the Repeater identity for backward compatibility with the Repeater code.
6. Keep flood forwarding identity fixed to the Repeater identity. A single physical relay must append only the Repeater identity to flood paths, not alternate path identities. This is essential to preserve Repeater transport behavior.
7. Keep ACK ownership service-specific. ACK payloads do not encode a destination identity; only the Room service may consume ACKs matching a pending Room push, while unrelated ACKs continue through the Repeater's normal handling.
8. Sign separate standard advertisements using the explicit identity: `ADV_TYPE_REPEATER` and `ADV_TYPE_ROOM`. Alternate advertisement transmissions on a timer, never radio receive identity. Add Room support to node-discovery request/response handling.

## Service composition

- Retain the upstream Repeater `MyMesh` as the single `mesh::Mesh` subclass and retain its full routing, neighbour, rate-limiting, region, bridge, telemetry, power-saving, watchdog and CLI behavior.
- Integrate Room logic as a service owned by the Repeater application, not as a second `mesh::Mesh` instance.
- Keep one shared `NodePrefs`, `CommonCLI`, radio parameter set, region map/default flood scope, and advertisement scheduling source. Do not instantiate two independent preference/CLI stacks that could overwrite the same persisted settings.
- Give Room its own `ClientACL` instance and a distinct persistence file (proposed `/room_contacts`); retain Repeater contacts at `/s_contacts`.
- Refactor the Room application loop into a service tick that does not call `Mesh::loop()`. The Repeater main loop remains the only place that runs Dispatcher/radio processing. Call the Room service tick once per main-loop iteration.
- Bound Room post-push work per tick and enqueue Room-generated packets at a lower priority than direct Repeater forwarding and essential control traffic. Share the same packet pool and queue; never create a second Dispatcher.

## Persistent Room posts

The current RAM-only cyclic queue is insufficient. Implement a bounded, recoverable append journal only after confirming the SenseCAP Solar InternalFS capacity, filesystem semantics and write endurance. Records should include a format/version marker, monotonically increasing sequence, timestamp, author public key, bounded text length/text, and integrity check. Recovery must stop safely at a truncated/corrupt tail and retain earlier valid records. Compaction must be bounded and power-loss-safe; do not erase or rewrite the only valid copy before a replacement is verified. Room-post persistence failure must not damage `_main.id`, `_room.id`, contacts, or Repeater configuration.

## Repeater behavior that must remain unchanged

- Full existing flood and direct forwarding, duplicate suppression, route-path handling, retransmit timing, airtime budgeting, CAD and rate limiting.
- Existing Repeater ACL, discovery, neighbour tracking, region/scope filtering, CLI, telemetry, bridge hooks and power management.
- Existing boot order, watchdog servicing, board housekeeping, sensors, RTC and SenseCAP Solar sleep/power-off behavior.
- Repeater identity, existing `_main.id`, and existing client-facing packet formats.

## Required audit before implementation

- Verify current branch head and compare it with upstream `main`; keep all changes on this feature branch.
- Inspect exact SenseCAP Solar InternalFS implementation, partition/available capacity, file write/flush/rename/remove semantics, and the installed Adafruit nRF52 core version. Do not infer usable filesystem size from the linker script alone.
- Inspect all Room methods and identify every use of `self_id`, ACL state, `CommonCLI`, prefs, region/scope, packet send priority and ACK processing before extraction.
- Inspect the full Repeater main loop and `hasPendingWork()` to preserve sleep correctness with Room service work and pending Room posts.
- Confirm standard discovery protocol details and validate whether official clients tolerate two distinct advertised identities from one physical device.
- Confirm Canadian 915 MHz regional parameters and legal TX limits from authoritative Canadian sources before changing radio defaults. Do not use the source's 869.618 MHz default for this deployment.

## Acceptance tests

1. Unmodified MeshCore clients discover and connect to the Repeater identity.
2. Existing repeaters forward through the combined device exactly as through the unmodified Repeater.
3. Unmodified MeshCore clients discover and connect to the Room identity.
4. Standard Room login, post submission, reply, ACK and post synchronization work.
5. Both identities receive traffic continuously; no identity swapping or packet loss caused by advert alternation.
6. Repeater forwarding remains under the Repeater identity.
7. Repeater and Room traffic coexist without starving Repeater forwarding/control.
8. ACLs, both identities and Room posts survive normal reboot; power-loss recovery preserves all valid journal records.
9. Deliberately colliding short identity hashes do not cause wrong-service delivery or duplicate dispatch.
10. Corrupt/missing Room storage does not damage Repeater identity, contacts or forwarding.
11. Build both the untouched upstream Repeater baseline and the combined target reproducibly before any device flash.
12. Back up existing device firmware/data and verify a documented recovery path before hardware flashing.

## Implementation progress (2026-11-27)

Implemented on the feature branch:
- Core Mesh now enumerates local identities and tries each matching destination identity; authenticated MAC validation selects the service. Short-hash collisions are tried without duplicate dispatch.
- Packet construction has explicit sender-identity overloads. Existing APIs continue to use the Repeater identity by default.
- The Repeater application loads the existing `_main.id` unchanged and a separate `_room.id`.
- Room and Repeater use independent ACL instances and files. Repeater ACL remains on InternalFS at `/s_contacts`; Room ACL is on the dedicated ExtraFS at `/room_contacts`.
- Both standard advert types are signed by their respective identities; Room discovery responses are supported and Room advertisements are staggered from Repeater advertisements.
- Room login, standard request handling, text posts, ACK handling, CLI replies and post synchronization have been integrated into the Repeater application.
- All Room service transmissions, including ACKs, use queue priority 4, below normal Repeater forwarding/control traffic (priorities 0–3). The shared packet manager and Dispatcher remain the only TX scheduler.
- Room outbound enqueue is capped when the shared queue has four or more packets or fewer than four packet buffers remain free. The SenseCAP target pool is increased from 32 to 36, preserving the Repeater's original 32-buffer capacity while reserving a small bounded allowance for Room traffic.
- Room post storage and Room contacts use the dedicated 100 KiB ExtraFS; Repeater settings, Repeater contacts and both identity files remain on the original InternalFS.
- Room posts use a two-slot LittleFS journal on ExtraFS with per-record checksums, generation selection, bounded compaction at 48 KiB, recovery of valid records before a corrupt/truncated tail, and no ACK until the new post has been written. The service retains the latest 32 posts for client synchronization.
- The SenseCAP Solar Repeater target reserves and mounts the 100 KiB ExtraFS region at `0xD4000`–`0xED000`, using `nrf52840_s140_v7_extrafs.ld`. The existing 28 KiB InternalFS at `0xED000` remains available for the Repeater's existing settings and identities. Room contacts and posts use ExtraFS.
- The SenseCAP Solar target now uses the MeshCore USA/Canada recommended interoperability preset: 910.525 MHz, SF7, 62.5 kHz bandwidth, CR5.

Storage source audit:
- Adafruit InternalFS reserves seven 4 KiB pages (28,672 bytes).
- MeshCore's nRF52 ExtraFS configuration reserves 0x19000 bytes (102,400 bytes) at 0xD4000, with 128-byte blocks.
- CustomLFS uses LittleFS and automatically formats its configured region if mounting fails. The ExtraFS region is excluded from the application image by the alternate linker script.
- The ExtraFS region is a reserved portion of the nRF52840 internal flash. It is not the separate 2 MB onboard flash listed in Seeed's board specification.
- If ExtraFS fails to mount/format, the firmware starts as a Repeater only; the Room identity is not receive-active and Room storage is not touched. This isolates Room storage faults from core Repeater availability.
- The ExtraFS configuration must be verified by the SenseCAP target build before flashing.

## Remaining implementation and verification

- Build the SenseCap_Solar_repeater target and fix any compiler/linker issues.
- Verify the exact P1-Pro radio certification and Canadian ISED operating conditions. The 910.525 MHz / 62.5 kHz preset is a MeshCore network interoperability setting, not proof of regulatory compliance. RSS-247 Issue 4 specifies a 500 kHz minimum 6 dB bandwidth for its 902–928 MHz DTS route; the MeshCore preset is 62.5 kHz, so it cannot be assumed to qualify under that DTS clause. RSS-210 Annex B has a separate 902–928 MHz field-strength route. The P1-Pro product specification lists FCC and CE certifications; no ISED identifier has yet been verified. Do not treat the 22 dBm hardware TX capability as an approved Canadian operating power.
- Confirm official Android/iOS clients can retain both identities from one physical device and validate Room discovery/login/post synchronization over the air.
- Test power-loss recovery, full ExtraFS behavior, ACL persistence, and simultaneous Repeater/Room traffic.
- Back up existing device firmware/data and verify a documented recovery route before hardware flashing.

No build has yet been verified, and no firmware has been flashed.

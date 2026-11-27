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

## Current implementation status

No source code has been changed yet. The branch contains this design note only. Firmware has not been built or flashed.

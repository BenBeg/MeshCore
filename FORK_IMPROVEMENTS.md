# Fork Improvements

This fork adds a SenseCAP Solar–focused combined Repeater + Room Server implementation while retaining MeshCore's original Repeater as the primary application. The Room Server is a service inside that application—not a second Mesh engine—and shares the radio, dispatcher, packet pool and transmit queue.

## Implemented

- **Dual local identities:** preserves the existing Repeater identity and stores a separate Room identity; receive dispatch tries matching identities and uses packet authentication to select the correct service.
- **Identity-aware packet handling:** adds explicit-identity packet creation, path returns and receive hooks while keeping existing APIs Repeater-compatible.
- **Standard discovery:** advertises both Repeater and Room roles with their respective identities and supports Room discovery responses.
- **Independent access control:** gives Room its own ACL and storage path, separate from the original Repeater contacts.
- **Recoverable Room ACL storage:** adds versioned, checksummed, two-slot persistence with generation selection, verified writes, legacy migration and retry after failed saves.
- **Persistent Room posts:** adds a bounded two-slot LittleFS journal with record integrity checks, recovery of valid records after a damaged tail, and verified compaction. SafeRoomExtraFS now refuses CustomLFS's default erase-on-any-mount-failure behavior: only a fully blank first-use partition is formatted; nonblank mount failures preserve data and disable Room while keeping Repeater available.
- **Shared, prioritized radio scheduling:** routes Room traffic through the existing Repeater dispatcher and queue at lower priority, with bounded enqueue/pool use so Room traffic cannot displace essential Repeater work.
- **Synchronization recovery:** persists Room sync progress and learned peer routes (including route invalidation after flood logins), normalizes route-buffer tails before comparison, and retries failed ACL writes.
- **Solar power behavior:** schedules Room work around the Repeater's sleep path so future timers do not continuously prevent sleep.
- **Receive and storage hardening:** validates local identity keypairs, reserves safe receive-buffer terminator space, validates decrypted and persisted encoded path lengths, sanitizes malformed legacy routes for rediscovery, rejects undersized ACK/data headers, and tests encoded path-length boundaries.
- **SenseCAP build configuration:** adds the dedicated ExtraFS partition for Room data while retaining the original InternalFS for Repeater settings and identities, plus the North American MeshCore radio preset.
- **CI maintenance:** adjusts PlatformIO build configuration, runs reproducible firmware matrices, and cancels superseded PR build/test runs so obsolete commits do not consume CI capacity.

## Verification status

- **Passed:** native unit tests and all 15 firmware targets on PR head `2cc4dbbe5015c3f67d7c256294d3d3f813bdb040`; the missing GoogleTest entry point in the path-length suite is fixed.
- **Requires fresh CI:** SafeRoomExtraFS now scans the reserved region and only formats it when every byte is erased; the new code is on the current branch head and its native/build workflows are running.
- **Still required:** fault-injection/power-loss testing; real-device sleep, storage and radio tests; interoperability testing with official clients; Canadian ISED compliance verification.
- **Not done:** no device has been flashed and the pull request has not been merged.

## Hardware validation protocol

[THREE_DEVICE_LAB_PROTOCOL.md](./docs/THREE_DEVICE_LAB_PROTOCOL.md) defines the three-device stock-versus-combined lab using the SenseCAP P1-Pro and two unmodified MeshCore ThinkNode M5 clients. It includes device roles, RF compliance gate, bench setup, serial/app evidence capture, expected behavior, relay negative control, reboot/persistence tests and a pass/fail scorecard.

See [the integration design and verification notes](./docs/SENSECAP_REPEATER_ROOM_INTEGRATION.md) for architecture and remaining checks, and [the Three-Device Lab Protocol](./docs/THREE_DEVICE_LAB_PROTOCOL.md) for the practical hardware validation plan.

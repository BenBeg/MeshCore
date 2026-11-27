# Fork Improvements

This fork adds a SenseCAP Solar–focused combined Repeater + Room Server implementation while retaining MeshCore's original Repeater as the primary application. The Room Server is a service inside that application—not a second Mesh engine—and shares the radio, dispatcher, packet pool and transmit queue.

## Implemented

- **Dual local identities:** preserves the existing Repeater identity and stores a separate Room identity; receive dispatch tries matching identities and uses packet authentication to select the correct service.
- **Identity-aware packet handling:** adds explicit-identity packet creation, path returns and receive hooks while keeping existing APIs Repeater-compatible.
- **Standard discovery:** advertises both Repeater and Room roles with their respective identities and supports Room discovery responses.
- **Independent access control:** gives Room its own ACL and storage path, separate from the original Repeater contacts.
- **Recoverable Room ACL storage:** adds versioned, checksummed, two-slot persistence with generation selection, verified writes, legacy migration and retry after failed saves.
- **Persistent Room posts:** adds a bounded two-slot LittleFS journal with record integrity checks, recovery of valid records after a damaged tail, verified compaction, and read-back verification of each newly appended record before the server acknowledges the post. SafeRoomExtraFS now refuses CustomLFS's default erase-on-any-mount-failure behavior: only a fully blank first-use partition is formatted; nonblank mount failures preserve data and disable Room while keeping Repeater available.
- **Shared, prioritized radio scheduling:** routes Room traffic through the existing Repeater dispatcher and queue at lower priority, with bounded enqueue/pool use so Room traffic cannot displace essential Repeater work.
- **Synchronization recovery:** persists Room sync progress and learned peer routes (including route invalidation after flood logins), normalizes route-buffer tails before comparison, and retries failed ACL writes.
- **Solar power behavior:** schedules Room work around the Repeater's sleep path so future timers do not continuously prevent sleep.
- **Receive and storage hardening:** validates local identity keypairs, reserves safe receive-buffer terminator space, validates decrypted and persisted encoded path lengths, sanitizes malformed legacy routes for rediscovery, rejects undersized ACK/data headers, and tests encoded path-length boundaries. SafeRoomExtraFS refuses destructive auto-format on nonblank mount failure; identity files are read-back verified and unreadable existing keys are preserved; atomic ACL clear commits an empty generation before retiring the prior slot; Room journal generation ordering handles uint32 rollover.
- **SenseCAP build configuration:** adds the dedicated ExtraFS partition for Room data while retaining the original InternalFS for Repeater settings and identities, plus the North American MeshCore radio preset.
- **CI maintenance:** adjusts PlatformIO build configuration, runs reproducible firmware matrices, and cancels superseded PR build/test runs so obsolete commits do not consume CI capacity.

## Verification status

- **Passed:** native unit tests and all 16 firmware targets on code commit `b84d188954dc0e79a9c2a73628be834e754d33dc`, including SenseCAP Solar and ThinkNode M5 BLE companion.
- **Passed:** Room post appends now reopen the journal and verify the exact record after flush/close before acknowledging the client. Native unit tests and all 16 firmware targets passed on `566e59cf0ba2e7356c1e653958374b5126391d5f`.
- **Still required:** implement and run the automated storage fault-injection harness described in `docs/ROOM_STORAGE_FAILURE_INJECTION.md`, then perform sacrificial-device power-loss testing; real-device sleep, storage and radio tests; interoperability testing with official clients; Canadian ISED compliance verification.
- **Not done:** no device has been flashed and the pull request has not been merged.

## Storage failure-injection plan

[ROOM_STORAGE_FAILURE_INJECTION.md](./docs/ROOM_STORAGE_FAILURE_INJECTION.md) defines the deterministic host-test and sacrificial-device matrix for interrupted compaction, short writes, corrupt/truncated tails, full storage, ACL slot failures, identity preservation, blank/nonblank mount failures and generation rollover. This is a test plan only; the fault-injection harness and physical tests are not yet implemented.

## Hardware validation protocol

[THREE_DEVICE_LAB_PROTOCOL.md](./docs/THREE_DEVICE_LAB_PROTOCOL.md) defines the three-device stock-versus-combined lab using the SenseCAP P1-Pro and two unmodified MeshCore ThinkNode M5 clients. It includes device roles, RF compliance gate, bench setup, serial/app evidence capture, expected behavior, relay negative control, reboot/persistence tests and a pass/fail scorecard.

See [the integration design and verification notes](./docs/SENSECAP_REPEATER_ROOM_INTEGRATION.md) for architecture and remaining checks, and [the Three-Device Lab Protocol](./docs/THREE_DEVICE_LAB_PROTOCOL.md) for the practical hardware validation plan.

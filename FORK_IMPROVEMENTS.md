# Fork Improvements

This fork adds a SenseCAP Solar–focused combined Repeater + Room Server implementation while retaining MeshCore's original Repeater as the primary application. The Room Server is a service inside that application—not a second Mesh engine—and shares the radio, dispatcher, packet pool and transmit queue.

## Implemented

- **Dual local identities:** preserves the existing Repeater identity and stores a separate Room identity; receive dispatch tries matching identities and uses packet authentication to select the correct service.
- **Identity-aware packet handling:** adds explicit-identity packet creation, path returns and receive hooks while keeping existing APIs Repeater-compatible.
- **Standard discovery:** advertises both Repeater and Room roles with their respective identities and supports Room discovery responses.
- **Independent access control:** gives Room its own ACL and storage path, separate from the original Repeater contacts.
- **Recoverable Room ACL storage:** adds versioned, checksummed, two-slot persistence with generation selection, verified writes, legacy migration and retry after failed saves.
- **Persistent Room posts:** adds a bounded two-slot LittleFS journal with record integrity checks, recovery of valid records after a damaged tail, and verified compaction.
- **Shared, prioritized radio scheduling:** routes Room traffic through the existing Repeater dispatcher and queue at lower priority, with bounded enqueue/pool use so Room traffic cannot displace essential Repeater work.
- **Synchronization recovery:** persists Room sync progress and learned peer routes, and retries failed ACL writes.
- **Solar power behavior:** schedules Room work around the Repeater's sleep path so future timers do not continuously prevent sleep.
- **Receive and storage hardening:** validates local identity keypairs, reserves safe receive-buffer terminator space, validates decrypted path bounds and rejects undersized ACK/data headers.
- **SenseCAP build configuration:** adds the dedicated ExtraFS partition for Room data while retaining the original InternalFS for Repeater settings and identities, plus the North American MeshCore radio preset.
- **CI maintenance:** adjusts PlatformIO build configuration and workflow behavior for reproducible targeted builds.

## Verification status

- **Passed:** native unit tests and the complete 15-target firmware build matrix on commit `36f9b80f6d74b21eed5c65ca7323b347f4f250e0`.
- **Still required:** fault-injection/power-loss testing; real-device sleep, storage and radio tests; interoperability testing with official clients; Canadian ISED compliance verification.
- **Not done:** no device has been flashed and the pull request has not been merged.

See [the integration design and verification notes](./docs/SENSECAP_REPEATER_ROOM_INTEGRATION.md) for architecture and remaining checks.

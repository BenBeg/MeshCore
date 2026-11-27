# Room Storage Failure-Injection Plan

Status: test design only. No hardware fault injection has been performed. The journal/ACL recovery claims below are source-level expectations and must not be treated as measured results.

## Scope

Exercise the dedicated SenseCAP ExtraFS Room data area without touching the Repeater's InternalFS. Use a sacrificial device or a reproducible flash-image backup/restore process. Never inject power loss into the only copy of valuable Room data.

Test the two-slot Room post journal (`/room_posts.a`, `/room_posts.b`), the two-slot Room ACL store, identity-file preservation, and SafeRoomExtraFS mount behavior.

## Required instrumentation

For automated tests, add compile-time-only fault hooks around the Room filesystem operations. Hooks must be disabled in production builds and must not change normal timing or persistence semantics when disabled.

Each hook should support:
- fail before an operation;
- simulate a short write after N bytes;
- fail a read or seek;
- simulate reset immediately after an operation;
- record operation name, path, byte count and return value.

A host-side fake filesystem is preferred for deterministic tests. Hardware power-cut tests should follow only after the host fault matrix passes.

## Journal test matrix

| ID | Injection point | Expected recovery after restart |
|---|---|---|
| J01 | Before creating inactive compaction slot | Previous active journal remains authoritative |
| J02 | During header write | Incomplete target rejected; old slot remains usable |
| J03 | During base-record copy | Target rejected; old slot remains usable |
| J04 | During header rewrite at offset zero | Target checksum/header rejected; old slot remains usable |
| J05 | After target flush/close, before read-back | On reboot, select target only if its complete header/base records verify |
| J06 | After read-back verification, before RAM active-slot switch | Both slots may be valid; select newer generation |
| J07 | After active-slot switch, before deleting old slot | Select newer valid generation |
| J08 | During/after old-slot deletion | New active journal remains usable |
| J09 | Short append write | Do not ACK; recover valid prefix and compact damaged tail |
| J10 | Full append write, then read-back failure | Do not ACK; preserve previous valid records and compact before next append |
| J11 | Corrupt final appended record checksum | Recover preceding valid records; mark journal for compaction |
| J12 | Truncate final appended record at every byte boundary | Recover preceding valid records; do not expose partial record |
| J13 | Corrupt one base record or base checksum | Reject that slot and fall back to the other valid slot |
| J14 | Both slots invalid | Start Room with an empty journal only after recording that prior Room posts were unrecoverable; Repeater must remain operational |
| J15 | Fill journal to compaction threshold / filesystem full | Refuse new post without ACK; existing journal remains readable |
| J16 | Generation rollover from 0xFFFFFFFF to 0 | Select generation 0 as newer than 0xFFFFFFFF |

## ACL and identity matrix

| ID | Injection point | Expected recovery |
|---|---|---|
| A01 | Interrupt inactive ACL slot write | Previous valid ACL slot remains active |
| A02 | Corrupt newest ACL slot checksum | Fall back to previous valid slot |
| A03 | Interrupt empty-generation ACL clear | Never resurrect revoked contacts from an older slot once the empty generation was committed |
| A04 | Fail ACL read-back | Do not report save success; retry according to service policy |
| I01 | Existing identity file unreadable/corrupt | Preserve file; do not silently generate a replacement identity |
| I02 | First-time identity write interrupted | Do not activate identity unless write/read-back verification succeeds |
| I03 | Existing valid identity | Reboot retains exactly the same public identity |

## SafeRoomExtraFS mount matrix

| ID | Partition state | Expected behavior |
|---|---|---|
| F01 | Entire region erased (0xFF) | First-use format and mount permitted |
| F02 | Valid filesystem | Mount without formatting |
| F03 | Nonblank region with invalid filesystem metadata | Do not format; disable Room and keep Repeater running |
| F04 | Read failure during blank-region scan | Do not format; disable Room and keep Repeater running |
| F05 | Power interruption during first-use format | On next boot, do not assume blank; preserve nonblank region and fail closed |

## Evidence to capture

For every test, retain:
1. firmware commit SHA and build artifact;
2. test ID and exact injected operation/byte offset;
3. serial log from boot through recovery;
4. journal/ACL slot filenames, sizes and checksums before and after;
5. whether a client received a Room post ACK;
6. whether previously committed posts/contacts remain available;
7. whether ordinary Repeater discovery and forwarding still work.

A test passes only when observed behavior matches the expected recovery and the Repeater remains operational. A client-visible ACK for a post that is missing after restart is an automatic failure.

## Execution order

1. Implement host fake-filesystem tests and deterministic operation hooks.
2. Run every J/A/I/F case under native CI; include short writes at each byte boundary for fixed-size journal records and headers.
3. Add randomized reset/failure sequences and run them repeatedly.
4. Validate the same high-value checkpoints on a sacrificial SenseCAP with a controlled power switch.
5. Only after those pass, perform the three-device interoperability protocol.

## Current known limitations

- The current production code has read-back verification for new appended records and for compacted journal base records.
- The current repository does not yet contain the fake filesystem/fault-hook harness described above.
- The CI green status verifies compilation and existing unit tests; it does not validate the failure matrix.
- Power-loss atomicity, flash-controller behavior, and LittleFS behavior under real supply collapse remain unverified.

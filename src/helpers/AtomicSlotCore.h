#pragma once
#include <stdint.h>
#include <stddef.h>

// Shared atomic-slot transaction used by firmware and host fault-injection tests.
// The inactive slot is never made authoritative here; callers switch generation
// and active-slot metadata only after this helper reports verified success.
template <typename Storage, typename Header, typename Record, typename GetRecord, typename Verify>
bool atomicSlotWriteVerified(Storage& storage, const char* path,
    const Header& header, uint16_t count, GetRecord get_record, Verify verify) {
  if (storage.exists(path) && !storage.remove(path)) return false;
  if (!storage.openWrite(path)) return false;

  bool success = storage.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  Record record;
  for (uint16_t i = 0; success && i < count; i++) {
    success = get_record(i, record) &&
      storage.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  }

  bool flushed = storage.flush();
  bool closed = storage.close();
  if (!success || !flushed || !closed) return false;
  return verify(path, header, count);
}


// Commit an empty generation before retiring old ACL snapshots. If the
// empty generation commits but cleanup fails, it remains authoritative.
template <typename Storage, typename CommitEmpty, typename ClearMemory>
bool atomicSlotClearVerified(Storage& storage, const char* stale_path,
    const char* legacy_path, CommitEmpty commit_empty, ClearMemory clear_memory) {
  if (!commit_empty()) return false;
  clear_memory();
  bool success = true;
  if (storage.exists(stale_path) && !storage.remove(stale_path)) success = false;
  if (storage.exists(legacy_path) && !storage.remove(legacy_path)) success = false;
  return success;
}

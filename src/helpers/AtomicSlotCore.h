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

// Return the newer valid slot using wrap-safe serial-number ordering.
// Equal generations deterministically prefer slot 0.
inline uint8_t atomicSlotNewest(bool valid0, uint32_t generation0,
                                bool valid1, uint32_t generation1) {
  if (!valid0) return valid1 ? 1 : 0;
  if (!valid1) return 0;
  return (int32_t)(generation1 - generation0) > 0 ? 1 : 0;
}

// Try the preferred validated slot, then the other validated slot if the
// second-pass read fails. The callback must stage data and publish only when
// it returns true, so a failed attempt cannot leak partial state.
template <typename Load>
bool atomicSlotLoadWithFallback(bool valid0, bool valid1, uint8_t preferred, Load load) {
  if (preferred > 1) return false;
  if (preferred == 0) {
    if (valid0 && load(0)) return true;
    return valid1 && load(1);
  }
  if (valid1 && load(1)) return true;
  return valid0 && load(0);
}

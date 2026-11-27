#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "helpers/RoomSequence.h"

struct __attribute__((packed)) RoomJournalHeaderCore {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t generation;
  uint16_t base_count;
  uint32_t base_checksum;
  uint32_t checksum;
};

template <size_t PubKeySize, size_t MaxText>
struct __attribute__((packed)) RoomJournalRecordCore {
  uint32_t sequence;
  uint32_t timestamp;
  uint8_t author[PubKeySize];
  uint16_t text_len;
  char text[MaxText];
  uint32_t checksum;
};

static inline uint32_t roomJournalHashUpdate(uint32_t hash, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

static inline uint32_t roomJournalHash(const uint8_t* data, size_t len) {
  return roomJournalHashUpdate(2166136261UL, data, len);
}

template <typename Record>
static inline bool roomJournalRecordValid(const Record& record) {
  return record.text_len > 0 && record.text_len <= sizeof(record.text) &&
    record.checksum == roomJournalHash(reinterpret_cast<const uint8_t*>(&record),
                                       offsetof(Record, checksum));
}

struct RoomJournalScanResult {
  bool valid;
  bool needs_compact;
  bool has_sequence;
  uint32_t last_sequence;
  uint16_t valid_records;
};

// read_record must read exactly one record from the current sequential position.
// The scanner is shared by firmware and host tests; the caller owns the file.
template <typename Header, typename Record, typename ReadRecord, typename OnRecord>
RoomJournalScanResult roomJournalScan(const Header& header, uint32_t file_size,
                                      uint32_t expected_magic, uint16_t expected_version,
                                      uint16_t max_base_count, ReadRecord read_record,
                                      OnRecord on_record) {
  RoomJournalScanResult result = {false, false, false, 0, 0};
  if (header.magic != expected_magic || header.version != expected_version ||
      header.record_size != sizeof(Record) || header.base_count > max_base_count ||
      header.checksum != roomJournalHash(reinterpret_cast<const uint8_t*>(&header),
                                         offsetof(Header, checksum))) return result;

  const uint32_t base_bytes = (uint32_t)header.base_count * sizeof(Record);
  if (file_size < sizeof(Header) || file_size - sizeof(Header) < base_bytes) return result;

  uint32_t base_hash = 2166136261UL;
  RoomSequenceTracker sequences;
  for (uint16_t i = 0; i < header.base_count; i++) {
    Record record;
    if (!read_record(record) || !roomJournalRecordValid(record)) return result;
    base_hash = roomJournalHashUpdate(base_hash, reinterpret_cast<const uint8_t*>(&record), sizeof(record));
    sequences.observe(record.sequence);
    on_record(record);
    result.valid_records++;
  }
  if (base_hash != header.base_checksum) return result;

  uint32_t remaining = file_size - sizeof(Header) - base_bytes;
  while (remaining > 0) {
    if (remaining < sizeof(Record)) {
      result.needs_compact = true;
      break;
    }
    Record record;
    if (!read_record(record) || !roomJournalRecordValid(record)) {
      result.needs_compact = true;
      break;
    }
    sequences.observe(record.sequence);
    on_record(record);
    result.valid_records++;
    remaining -= sizeof(Record);
  }

  result.valid = true;
  result.has_sequence = sequences.hasValue();
  result.last_sequence = sequences.value();
  return result;
}


// Storage adapter contract: openRead/openAppend, read, write, seek, size,
// flush, close, exists and remove. Firmware and host fake filesystems use these
// exact transaction functions, so tests exercise production operation order.
template <typename Storage, typename Header, typename Record, typename OnRecord>
RoomJournalScanResult roomJournalVerifySlot(Storage& storage, const char* path,
    uint32_t expected_magic, uint16_t expected_version, uint16_t max_base_count,
    OnRecord on_record, Header* verified_header = nullptr, uint32_t* verified_size = nullptr) {
  RoomJournalScanResult invalid = {false, false, false, 0, 0};
  if (!storage.openRead(path)) return invalid;
  Header header = {};
  if (storage.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
    storage.close();
    return invalid;
  }
  const uint32_t file_size = storage.size();
  RoomJournalScanResult result = roomJournalScan<Header, Record>(
      header, file_size, expected_magic, expected_version, max_base_count,
      [&storage](Record& record) {
        return storage.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record);
      }, on_record);
  storage.close();
  if (result.valid) {
    if (verified_header) *verified_header = header;
    if (verified_size) *verified_size = file_size;
  }
  return result;
}

template <typename Storage, typename Record>
bool roomJournalAppendVerified(Storage& storage, const char* path, const Record& record) {
  if (!storage.openAppend(path)) return false;
  bool written = storage.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  bool flushed = storage.flush();
  bool closed = storage.close();
  if (!written || !flushed || !closed) return false;
  if (!storage.openRead(path)) return false;
  const uint32_t file_size = storage.size();
  Record stored = {};
  bool verified = file_size >= sizeof(Record) &&
      storage.seek(file_size - sizeof(Record)) &&
      storage.read(reinterpret_cast<uint8_t*>(&stored), sizeof(stored)) == sizeof(stored) &&
      memcmp(&stored, &record, sizeof(record)) == 0 &&
      roomJournalRecordValid(stored);
  bool verify_closed = storage.close();
  return verified && verify_closed;
}

template <typename Storage, typename Header, typename Record, typename GetRecord>
bool roomJournalCompactVerified(Storage& storage, const char* path,
    uint32_t magic, uint16_t version, uint32_t generation, uint16_t base_count,
    uint16_t max_base_count, GetRecord get_record, uint32_t* resulting_size = nullptr) {
  if (base_count > max_base_count) return false;
  if (storage.exists(path) && !storage.remove(path)) return false;
  Header header = {};
  header.magic = magic;
  header.version = version;
  header.record_size = sizeof(Record);
  header.generation = generation;
  header.base_count = base_count;
  header.base_checksum = 2166136261UL;
  if (!storage.openAppend(path)) return false;
  bool success = storage.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  for (uint16_t i = 0; success && i < base_count; i++) {
    Record record = {};
    if (!get_record(i, record) || !roomJournalRecordValid(record)) {
      success = false;
      break;
    }
    header.base_checksum = roomJournalHashUpdate(header.base_checksum,
        reinterpret_cast<const uint8_t*>(&record), sizeof(record));
    success = storage.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  }
  if (success) {
    header.checksum = roomJournalHash(reinterpret_cast<const uint8_t*>(&header), offsetof(Header, checksum));
    success = storage.seek(0) &&
        storage.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  }
  bool flushed = storage.flush();
  bool closed = storage.close();
  if (!success || !flushed || !closed) return false;
  Header verified = {};
  uint32_t verified_size = 0;
  RoomJournalScanResult scan = roomJournalVerifySlot<Storage, Header, Record>(
      storage, path, magic, version, max_base_count, [](const Record&) {},
      &verified, &verified_size);
  if (!scan.valid || scan.needs_compact || scan.valid_records != base_count ||
      verified.generation != generation) return false;
  if (resulting_size) *resulting_size = verified_size;
  return true;
}

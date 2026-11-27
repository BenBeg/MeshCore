#pragma once
#include <stddef.h>
#include <stdint.h>
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

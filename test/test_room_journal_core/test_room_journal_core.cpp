#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "helpers/RoomJournalCore.h"

static const uint32_t MAGIC = 0x31505352UL;
static const uint16_t VERSION = 1;
using Record = RoomJournalRecordCore<32, 151>;
using Header = RoomJournalHeaderCore;

class FakeJournalFile {
public:
  std::vector<uint8_t> bytes;
  size_t cursor = sizeof(Header);
  size_t write_limit = (size_t)-1;
  bool fail_read = false;
  size_t reads = 0;

  size_t write(const uint8_t* data, size_t len) {
    size_t n = std::min(len, write_limit);
    bytes.insert(bytes.end(), data, data + n);
    return n;
  }
  bool readRecord(Record& record) {
    reads++;
    if (fail_read || cursor + sizeof(Record) > bytes.size()) return false;
    memcpy(&record, bytes.data() + cursor, sizeof(Record));
    cursor += sizeof(Record);
    return true;
  }
  Header header() const {
    Header h = {};
    if (bytes.size() >= sizeof(Header)) memcpy(&h, bytes.data(), sizeof(Header));
    return h;
  }
  void writeHeader(const Header& h) {
    memcpy(bytes.data(), &h, sizeof(Header));
  }
};

static Record makeRecord(uint32_t sequence, const char* text) {
  Record r = {};
  r.sequence = sequence;
  r.timestamp = sequence + 100;
  for (size_t i = 0; i < sizeof(r.author); i++) r.author[i] = (uint8_t)i;
  r.text_len = (uint16_t)strlen(text);
  memcpy(r.text, text, r.text_len);
  r.checksum = roomJournalHash(reinterpret_cast<const uint8_t*>(&r), offsetof(Record, checksum));
  return r;
}

static FakeJournalFile makeJournal(const std::vector<Record>& base, const std::vector<Record>& tail = {}) {
  FakeJournalFile file;
  Header h = {};
  h.magic = MAGIC;
  h.version = VERSION;
  h.record_size = sizeof(Record);
  h.generation = 7;
  h.base_count = (uint16_t)base.size();
  h.base_checksum = 2166136261UL;
  for (const auto& r : base) {
    h.base_checksum = roomJournalHashUpdate(h.base_checksum, reinterpret_cast<const uint8_t*>(&r), sizeof(r));
  }
  h.checksum = roomJournalHash(reinterpret_cast<const uint8_t*>(&h), offsetof(Header, checksum));
  file.bytes.resize(sizeof(Header));
  file.writeHeader(h);
  for (const auto& r : base) file.write(reinterpret_cast<const uint8_t*>(&r), sizeof(r));
  for (const auto& r : tail) file.write(reinterpret_cast<const uint8_t*>(&r), sizeof(r));
  file.cursor = sizeof(Header);
  return file;
}

static RoomJournalScanResult scan(FakeJournalFile& file, std::vector<uint32_t>& sequences) {
  Header h = file.header();
  return roomJournalScan<Header, Record>(h, (uint32_t)file.bytes.size(), MAGIC, VERSION, 32,
    [&file](Record& r) { return file.readRecord(r); },
    [&sequences](const Record& r) { sequences.push_back(r.sequence); });
}

TEST(RoomJournalCore, ReadsBaseAndAppendedRecords) {
  auto file = makeJournal({makeRecord(10, "base")}, {makeRecord(11, "append")});
  std::vector<uint32_t> seq;
  auto result = scan(file, seq);
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.needs_compact);
  EXPECT_EQ(2U, result.valid_records);
  ASSERT_EQ(2U, seq.size());
  EXPECT_EQ(11U, result.last_sequence);
}

TEST(RoomJournalCore, RecoversValidPrefixForEveryTruncatedTailBoundary) {
  const Record base = makeRecord(40, "committed");
  const Record tail = makeRecord(41, "new post");
  for (size_t cut = 0; cut < sizeof(Record); cut++) {
    auto file = makeJournal({base});
    file.write(reinterpret_cast<const uint8_t*>(&tail), cut);
    file.cursor = sizeof(Header);
    std::vector<uint32_t> seq;
    auto result = scan(file, seq);
    EXPECT_TRUE(result.valid) << "cut=" << cut;
    EXPECT_EQ(cut != 0, result.needs_compact) << "cut=" << cut;
    EXPECT_EQ(1U, result.valid_records) << "cut=" << cut;
    ASSERT_EQ(1U, seq.size()) << "cut=" << cut;
    EXPECT_EQ(40U, result.last_sequence) << "cut=" << cut;
  }
}

TEST(RoomJournalCore, RejectsCorruptBaseAndPreservesNoFalseSuccess) {
  auto file = makeJournal({makeRecord(50, "base")});
  file.bytes[sizeof(Header) + offsetof(Record, checksum)] ^= 0x80;
  file.cursor = sizeof(Header);
  std::vector<uint32_t> seq;
  auto result = scan(file, seq);
  EXPECT_FALSE(result.valid);
}

TEST(RoomJournalCore, CorruptTailKeepsBaseAndRequestsCompaction) {
  auto file = makeJournal({makeRecord(60, "base")}, {makeRecord(61, "tail")});
  file.bytes.back() ^= 0x01;
  file.cursor = sizeof(Header);
  std::vector<uint32_t> seq;
  auto result = scan(file, seq);
  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.needs_compact);
  EXPECT_EQ(1U, result.valid_records);
  EXPECT_EQ(60U, result.last_sequence);
}

TEST(RoomJournalCore, ReadFailureInBaseRejectsSlot) {
  auto file = makeJournal({makeRecord(70, "base")});
  file.fail_read = true;
  std::vector<uint32_t> seq;
  auto result = scan(file, seq);
  EXPECT_FALSE(result.valid);
}

TEST(RoomJournalCore, ShortWriteNeverCreatesAValidCompleteTailRecord) {
  const Record base = makeRecord(80, "base");
  const Record append = makeRecord(81, "append");
  for (size_t written = 0; written < sizeof(Record); written++) {
    auto file = makeJournal({base});
    file.write_limit = written;
    EXPECT_LT(file.write(reinterpret_cast<const uint8_t*>(&append), sizeof(append)), sizeof(append));
    file.cursor = sizeof(Header);
    std::vector<uint32_t> seq;
    auto result = scan(file, seq);
    EXPECT_TRUE(result.valid) << "written=" << written;
    EXPECT_TRUE(result.needs_compact) << "written=" << written;
    EXPECT_EQ(1U, result.valid_records) << "written=" << written;
  }
}

TEST(RoomJournalCore, SequenceRolloverIsSharedWithProductionScanner) {
  auto file = makeJournal({makeRecord(0xFFFFFFFEUL, "before")}, {
    makeRecord(0xFFFFFFFFUL, "last"), makeRecord(0, "wrapped")
  });
  std::vector<uint32_t> seq;
  auto result = scan(file, seq);
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(0U, result.last_sequence);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

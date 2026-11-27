#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <map>
#include <string>
#include "helpers/RoomJournalCore.h"
#include "helpers/AtomicSlotCore.h"

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

TEST(RoomJournalCore, RejectsBaseAggregateChecksumMismatch) {
  auto file = makeJournal({makeRecord(55, "base")});
  Header h = file.header();
  h.base_checksum ^= 0x100;
  h.checksum = roomJournalHash(reinterpret_cast<const uint8_t*>(&h), offsetof(Header, checksum));
  file.writeHeader(h);
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
    EXPECT_EQ(written != 0, result.needs_compact) << "written=" << written;
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


class FakeJournalStorage {
public:
  std::map<std::string, std::vector<uint8_t>> files;
  std::string active_path;
  bool opened=false, append_mode=false;
  size_t cursor=0;
  bool fail_open_read=false, fail_open_append=false, fail_open_write=false, fail_read=false;
  bool fail_seek=false, fail_flush=false, fail_close=false, fail_remove=false;
  int write_call=0, short_write_call=-1;
  size_t short_write_bytes=0;
  bool openRead(const char* path){if(fail_open_read||files.find(path)==files.end())return false;active_path=path;opened=true;append_mode=false;cursor=0;return true;}
  bool openAppend(const char* path){if(fail_open_append)return false;active_path=path;opened=true;append_mode=true;cursor=files[path].size();return true;}
  bool openWrite(const char* path){if(fail_open_write)return false;active_path=path;opened=true;append_mode=false;cursor=0;files[path].clear();return true;}
  size_t read(uint8_t* data,size_t len){if(!opened||fail_read)return 0;auto& b=files[active_path];size_t n=std::min(len,b.size()>cursor?b.size()-cursor:0);if(n)memcpy(data,b.data()+cursor,n);cursor+=n;return n;}
  size_t write(const uint8_t* data,size_t len){if(!opened)return 0;write_call++;size_t n=len;if(write_call==short_write_call)n=std::min(n,short_write_bytes);auto& b=files[active_path];if(cursor+n>b.size())b.resize(cursor+n);if(n)memcpy(b.data()+cursor,data,n);cursor+=n;return n;}
  bool seek(uint32_t offset){if(!opened||fail_seek)return false;cursor=offset;return true;}
  uint32_t size(){return opened?(uint32_t)files[active_path].size():0;}
  bool flush(){return opened&&!fail_flush;}
  bool close(){opened=false;return !fail_close;}
  bool exists(const char* path){return files.find(path)!=files.end();}
  bool remove(const char* path){if(fail_remove)return false;files.erase(path);return true;}
};

static bool compactFake(FakeJournalStorage& storage,const char* path,const std::vector<Record>& records,uint32_t generation=8,uint32_t* size=nullptr){
  return roomJournalCompactVerified<FakeJournalStorage,Header,Record>(storage,path,MAGIC,VERSION,generation,(uint16_t)records.size(),32,
    [&records](uint16_t i,Record& out){if(i>=records.size())return false;out=records[i];return true;},size);
}
static RoomJournalScanResult verifyFake(FakeJournalStorage& storage,const char* path,std::vector<uint32_t>& seq){
  return roomJournalVerifySlot<FakeJournalStorage,Header,Record>(storage,path,MAGIC,VERSION,32,[&seq](const Record& r){seq.push_back(r.sequence);});
}

TEST(RoomJournalTransactions, ProductionCompactionHelperWritesAndVerifiesSlot){
  FakeJournalStorage s;s.files["active"]=makeJournal({makeRecord(100,"old")}).bytes;s.files["target"]=makeJournal({makeRecord(90,"stale")}).bytes;auto old=s.files["active"];
  std::vector<Record> records={makeRecord(101,"first"),makeRecord(102,"second")};uint32_t size=0;
  EXPECT_TRUE(compactFake(s,"target",records,8,&size));EXPECT_EQ(old,s.files["active"]);EXPECT_EQ(sizeof(Header)+2*sizeof(Record),size);
  std::vector<uint32_t> seq;auto scan=verifyFake(s,"target",seq);EXPECT_TRUE(scan.valid);EXPECT_FALSE(scan.needs_compact);ASSERT_EQ(2U,seq.size());EXPECT_EQ(102U,seq.back());
}

TEST(RoomJournalTransactions, CompactionFailuresPreservePreviousActiveSlot){
  const std::vector<Record> records={makeRecord(111,"one"),makeRecord(112,"two")};
  for(int failure=0;failure<9;failure++){
    FakeJournalStorage s;s.files["active"]=makeJournal({makeRecord(110,"committed")}).bytes;s.files["target"]=makeJournal({makeRecord(90,"stale")}).bytes;auto old=s.files["active"];
    if(failure==0)s.fail_remove=true;if(failure==1)s.fail_open_append=true;if(failure==2)s.short_write_call=1,s.short_write_bytes=0;
    if(failure==3)s.short_write_call=2,s.short_write_bytes=sizeof(Record)-1;if(failure==4)s.fail_seek=true;
    if(failure==5)s.fail_flush=true;if(failure==6)s.fail_close=true;if(failure==7)s.fail_open_read=true;if(failure==8)s.fail_read=true;
    EXPECT_FALSE(compactFake(s,"target",records))<<"failure="<<failure;EXPECT_EQ(old,s.files["active"])<<"failure="<<failure;
  }
}

TEST(RoomJournalTransactions, CompactionShortWritesAtEveryHeaderAndRecordBoundary){
  const std::vector<Record> records={makeRecord(121,"one")};const size_t lengths[]={sizeof(Header),sizeof(Record),sizeof(Header)};
  for(int call=1;call<=3;call++)for(size_t cut=0;cut<lengths[call-1];cut++){
    FakeJournalStorage s;s.files["active"]=makeJournal({makeRecord(120,"safe")}).bytes;auto old=s.files["active"];
    s.short_write_call=call;s.short_write_bytes=cut;EXPECT_FALSE(compactFake(s,"target",records))<<"call="<<call<<" cut="<<cut;EXPECT_EQ(old,s.files["active"]);
  }
}

TEST(RoomJournalTransactions, ProductionAppendHelperReadBackVerifiesExactRecord){
  FakeJournalStorage s;s.files["journal"]=makeJournal({makeRecord(130,"base")}).bytes;const Record appended=makeRecord(131,"append");
  EXPECT_TRUE((roomJournalAppendVerified<FakeJournalStorage,Record>(s,"journal",appended)));std::vector<uint32_t> seq;auto scan=verifyFake(s,"journal",seq);
  EXPECT_TRUE(scan.valid);ASSERT_EQ(2U,seq.size());EXPECT_EQ(131U,seq.back());
}

TEST(RoomJournalTransactions, AppendFailuresAreNotAcknowledged){
  const Record appended=makeRecord(141,"append");
  for(int failure=0;failure<7;failure++){
    FakeJournalStorage s;s.files["journal"]=makeJournal({makeRecord(140,"base")}).bytes;
    if(failure==0)s.fail_open_append=true;if(failure==1)s.short_write_call=1,s.short_write_bytes=sizeof(Record)-1;
    if(failure==2)s.fail_flush=true;if(failure==3)s.fail_close=true;if(failure==4)s.fail_open_read=true;if(failure==5)s.fail_seek=true;if(failure==6)s.fail_read=true;
    EXPECT_FALSE((roomJournalAppendVerified<FakeJournalStorage,Record>(s,"journal",appended)))<<"failure="<<failure;
  }
}


struct TestACLHeader { uint32_t generation; uint16_t count; uint32_t checksum; };
struct TestACLRecord { uint32_t id; uint32_t permissions; };
static uint32_t testACLChecksum(const TestACLHeader& h) {
  return roomJournalHash(reinterpret_cast<const uint8_t*>(&h), offsetof(TestACLHeader, checksum));
}
static bool verifyTestACL(FakeJournalStorage& s, const char* path,
                          const TestACLHeader& expected, uint16_t count) {
  if (!s.openRead(path)) return false;
  TestACLHeader h={};
  bool ok=s.read(reinterpret_cast<uint8_t*>(&h),sizeof(h))==sizeof(h) &&
          h.generation==expected.generation && h.count==count &&
          h.checksum==testACLChecksum(h);
  for(uint16_t i=0;ok&&i<count;i++){
    TestACLRecord r={};
    ok=s.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))==sizeof(r);
  }
  ok=ok && s.size()==sizeof(h)+count*sizeof(TestACLRecord);
  bool closed=s.close();
  return ok&&closed;
}
TEST(AtomicSlotTransactions, WritesAndVerifiesInactiveACLSlotBeforeCallerCanSwitch) {
  FakeJournalStorage s;
  s.files["acl.active"]={1,2,3};
  TestACLHeader h={9,2,0};h.checksum=testACLChecksum(h);
  TestACLRecord records[2]={{10,2},{11,3}};
  EXPECT_TRUE((atomicSlotWriteVerified<FakeJournalStorage,TestACLHeader,TestACLRecord>(
    s,"acl.next",h,2,[&](uint16_t i,TestACLRecord& out){if(i>=2)return false;out=records[i];return true;},
    [&](const char* p,const TestACLHeader& e,uint16_t n){return verifyTestACL(s,p,e,n);})));
  EXPECT_EQ((std::vector<uint8_t>{1,2,3}),s.files["acl.active"]);
  EXPECT_EQ(sizeof(h)+2*sizeof(TestACLRecord),s.files["acl.next"].size());
}
TEST(AtomicSlotTransactions, AnyWriteOrVerificationFailureKeepsActiveACLSlotUntouched) {
  TestACLHeader h={10,1,0};h.checksum=testACLChecksum(h);
  TestACLRecord rec={12,3};
  for(int failure=0;failure<8;failure++){
    FakeJournalStorage s;s.files["acl.active"]={4,5,6};s.files["acl.next"]={0xAA};auto old=s.files["acl.active"];
    if(failure==0)s.fail_remove=true;
    if(failure==1)s.fail_open_write=true;
    if(failure==2)s.short_write_call=1,s.short_write_bytes=sizeof(h)-1;
    if(failure==3)s.short_write_call=2,s.short_write_bytes=sizeof(rec)-1;
    if(failure==4)s.fail_flush=true;
    if(failure==5)s.fail_close=true;
    if(failure==6)s.fail_open_read=true;
    if(failure==7)s.fail_read=true;
    bool ok=atomicSlotWriteVerified<FakeJournalStorage,TestACLHeader,TestACLRecord>(
      s,"acl.next",h,1,[&](uint16_t,TestACLRecord& out){out=rec;return true;},
      [&](const char* p,const TestACLHeader& e,uint16_t n){return verifyTestACL(s,p,e,n);});
    EXPECT_FALSE(ok)<<"failure="<<failure;
    EXPECT_EQ(old,s.files["acl.active"])<<"failure="<<failure;
  }
}
TEST(AtomicSlotTransactions, ShortWritesAtEveryACLHeaderAndRecordBoundaryAreRejected) {
  TestACLHeader h={11,1,0};h.checksum=testACLChecksum(h);
  TestACLRecord rec={13,2};
  for(int call=1;call<=2;call++){
    size_t length=call==1?sizeof(h):sizeof(rec);
    for(size_t cut=0;cut<length;cut++){
      FakeJournalStorage s;s.files["acl.active"]={7,8,9};s.files["acl.next"]={0xBB};auto old=s.files["acl.active"];
      s.short_write_call=call;s.short_write_bytes=cut;
      bool ok=atomicSlotWriteVerified<FakeJournalStorage,TestACLHeader,TestACLRecord>(
        s,"acl.next",h,1,[&](uint16_t,TestACLRecord& out){out=rec;return true;},
        [&](const char* p,const TestACLHeader& e,uint16_t n){return verifyTestACL(s,p,e,n);});
      EXPECT_FALSE(ok)<<"call="<<call<<" cut="<<cut;
      EXPECT_EQ(old,s.files["acl.active"]);
    }
  }
}

TEST(AtomicSlotTransactions, ACLClearCommitsEmptyGenerationBeforeRetiringOldFiles) {
  FakeJournalStorage s;
  s.files["acl.0"]={1,2,3};s.files["legacy"]={4,5,6};
  bool empty_committed=false, memory_cleared=false;
  EXPECT_TRUE(atomicSlotClearVerified(s,"acl.0","legacy",
    [&](){s.files["acl.1"]={0,0,0};empty_committed=true;return true;},
    [&](){memory_cleared=true;}));
  EXPECT_TRUE(empty_committed);EXPECT_TRUE(memory_cleared);
  EXPECT_TRUE(s.exists("acl.1"));EXPECT_FALSE(s.exists("acl.0"));EXPECT_FALSE(s.exists("legacy"));
}
TEST(AtomicSlotTransactions, ACLClearDoesNotRetireOldFilesIfEmptyGenerationCannotCommit) {
  FakeJournalStorage s;s.files["acl.0"]={1};s.files["legacy"]={2};
  bool memory_cleared=false;
  EXPECT_FALSE(atomicSlotClearVerified(s,"acl.0","legacy",
    [](){return false;},[&](){memory_cleared=true;}));
  EXPECT_FALSE(memory_cleared);EXPECT_TRUE(s.exists("acl.0"));EXPECT_TRUE(s.exists("legacy"));
}
TEST(AtomicSlotTransactions, ACLClearCleanupFailureLeavesCommittedEmptyGeneration) {
  FakeJournalStorage s;s.files["acl.0"]={1};s.files["legacy"]={2};s.fail_remove=true;
  bool memory_cleared=false;
  EXPECT_FALSE(atomicSlotClearVerified(s,"acl.0","legacy",
    [&](){s.files["acl.1"]={0};return true;},[&](){memory_cleared=true;}));
  EXPECT_TRUE(memory_cleared);EXPECT_TRUE(s.exists("acl.1"));
  EXPECT_TRUE(s.exists("acl.0"));EXPECT_TRUE(s.exists("legacy"));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

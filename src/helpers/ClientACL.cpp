#include "ClientACL.h"
#include "AtomicSlotCore.h"
#include <new>
#include <stddef.h>

namespace {
static const uint32_t ACL_SLOT_MAGIC = 0x41434C32UL;
static const uint16_t ACL_SLOT_VERSION = 1;
static const size_t ACL_RECORD_SIZE = 136;

struct ACLSlotHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t generation;
  uint32_t payload_hash;
  uint32_t header_hash;
};

static uint32_t aclHashUpdate(uint32_t hash, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

static void encodeACLRecord(uint8_t out[ACL_RECORD_SIZE], const ClientInfo& c) {
  size_t p = 0;
  memcpy(out + p, c.id.pub_key, 32); p += 32;
  out[p++] = c.permissions;
  memcpy(out + p, &c.extra.room.sync_since, 4); p += 4;
  memset(out + p, 0, 2); p += 2;
  out[p++] = c.out_path_len;
  memcpy(out + p, c.out_path, 64); p += 64;
  memcpy(out + p, c.shared_secret, PUB_KEY_SIZE);
}

static bool readACLSlot(FILESYSTEM* fs, const char* path, ACLSlotHeader* header) {
#if defined(RP2040_PLATFORM)
  File file = fs->open(path, "r");
#else
  File file = fs->open(path);
#endif
  if (!file) return false;
  ACLSlotHeader h;
  bool ok = file.read((uint8_t*)&h, sizeof(h)) == sizeof(h);
  if (ok) {
    ok = h.magic == ACL_SLOT_MAGIC && h.version == ACL_SLOT_VERSION &&
         h.count <= MAX_CLIENTS &&
         h.header_hash == aclHashUpdate(2166136261UL, (const uint8_t*)&h, offsetof(ACLSlotHeader, header_hash));
  }
  uint32_t hash = 2166136261UL;
  uint8_t record[ACL_RECORD_SIZE];
  for (uint16_t i = 0; ok && i < h.count; i++) {
    ok = file.read(record, sizeof(record)) == sizeof(record);
    if (ok) {
      // Reject persisted path lengths that could make later path copies unsafe.
      uint8_t path_len = record[39];
      if (path_len != OUT_PATH_UNKNOWN && !mesh::Packet::isValidPathLen(path_len)) ok = false;
      if (ok) hash = aclHashUpdate(hash, record, sizeof(record));
    }
  }
  if (ok) ok = hash == h.payload_hash && file.available() == 0;
  file.close();
  if (ok && header) *header = h;
  return ok;
}
}

static File openWrite(FILESYSTEM* _fs, const char* filename) {
  #if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    _fs->remove(filename);
    return _fs->open(filename, FILE_O_WRITE);
  #elif defined(RP2040_PLATFORM)
    return _fs->open(filename, "w");
  #else
    return _fs->open(filename, "w", true);
  #endif
}


class ACLFileStorage {
  union Holder { File file; Holder() {} ~Holder() {} } holder;
  FILESYSTEM* fs;
  bool opened;
  File* file() { return &holder.file; }
public:
  explicit ACLFileStorage(FILESYSTEM* filesystem) : fs(filesystem), opened(false) {}
  ~ACLFileStorage() { close(); }
  bool openWrite(const char* path) {
    close();
    new (&holder.file) File(::openWrite(fs, path));
    opened = true;
    if (!*file()) { close(); return false; }
    return true;
  }
  size_t write(const uint8_t* data, size_t len) { return opened ? file()->write(data,len) : 0; }
  bool flush() { if(!opened)return false; file()->flush(); return true; }
  bool close() {
    if(!opened)return true;
    file()->close();
    file()->~File();
    opened=false;
    return true; // File API has no close status; verified reopen follows.
  }
  bool exists(const char* path) { return fs->exists(path); }
  bool remove(const char* path) { return fs->remove(path); }
};

void ClientACL::load(FILESYSTEM* fs, const mesh::LocalIdentity& self_id) {
  _fs = fs;
  num_clients = 0;
  if (_fs->exists(_storage_path)) {
  #if defined(RP2040_PLATFORM)
    File file = _fs->open(_storage_path, "r");
  #else
    File file = _fs->open(_storage_path);
  #endif
    if (file) {
      bool full = false;
      while (!full) {
        ClientInfo c;
        uint8_t pub_key[32];
        uint8_t unused[2];

        memset(&c, 0, sizeof(c));

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *) &c.permissions, 1) == 1);
        success = success && (file.read((uint8_t *) &c.extra.room.sync_since, 4) == 4);
        success = success && (file.read(unused, 2) == 2);
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read(c.shared_secret, PUB_KEY_SIZE) == PUB_KEY_SIZE); // will be recalculated below

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        // The persisted path length is an encoded hash-size/count byte, not a
        // byte count. If it is malformed, keep the contact but rediscover its route.
        if (c.out_path_len != OUT_PATH_UNKNOWN && !mesh::Packet::isValidPathLen(c.out_path_len)) {
          c.out_path_len = OUT_PATH_UNKNOWN;
          memset(c.out_path, 0, sizeof(c.out_path));
        }
        self_id.calcSharedSecret(c.shared_secret, pub_key);  // recalculate shared secrets in case our private key changed
        if (num_clients < MAX_CLIENTS) {
          clients[num_clients++] = c;
        } else {
          full = true;
        }
      }
      file.close();
    }
  }
}

bool ClientACL::save(FILESYSTEM* fs, bool (*filter)(ClientInfo*)) {
  _fs = fs;
  File file = openWrite(_fs, _storage_path);
  if (!file) return false;

  uint8_t unused[2];
  memset(unused, 0, sizeof(unused));
  bool success = true;
  for (int i = 0; i < num_clients; i++) {
    auto c = &clients[i];
    if (c->permissions == 0 || (filter && !filter(c))) continue;    // skip deleted entries, or by filter function

    success = (file.write(c->id.pub_key, 32) == 32);
    success = success && (file.write((uint8_t *) &c->permissions, 1) == 1);
    success = success && (file.write((uint8_t *) &c->extra.room.sync_since, 4) == 4);
    success = success && (file.write(unused, 2) == 2);
    success = success && (file.write((uint8_t *)&c->out_path_len, 1) == 1);
    success = success && (file.write(c->out_path, 64) == 64);
    success = success && (file.write(c->shared_secret, PUB_KEY_SIZE) == PUB_KEY_SIZE);
    if (!success) break;
  }
  file.flush();
  file.close();
  return success;
}


void ClientACL::loadAtomic(FILESYSTEM* fs, const mesh::LocalIdentity& self_id) {
  _fs = fs;
  _atomic_mode = true;
  char paths[2][48];
  snprintf(paths[0], sizeof(paths[0]), "%s.0", _storage_path);
  snprintf(paths[1], sizeof(paths[1]), "%s.1", _storage_path);
  ACLSlotHeader headers[2];
  bool valid[2] = { readACLSlot(fs, paths[0], &headers[0]),
                    readACLSlot(fs, paths[1], &headers[1]) };
  if (valid[0] || valid[1]) {
    uint8_t selected = atomicSlotNewest(valid[0], headers[0].generation,
                                           valid[1], headers[1].generation);
    // Use the existing ACL array as staging storage rather than allocating
    // MAX_CLIENTS ClientInfo records on the embedded stack. num_clients is
    // cleared before each attempt and only becomes visible after a full read.
    uint8_t loaded_slot = 0xFF;
    num_clients = 0;
    memset(clients, 0, sizeof(clients));
    bool loaded = atomicSlotLoadWithFallback(valid[0], valid[1], selected,
      [&](uint8_t slot) {
        num_clients = 0;
        memset(clients, 0, sizeof(clients));
#if defined(RP2040_PLATFORM)
        File file = fs->open(paths[slot], "r");
#else
        File file = fs->open(paths[slot]);
#endif
        if (!file) return false;
        bool ok = file.seek(sizeof(ACLSlotHeader));
        uint8_t record[ACL_RECORD_SIZE];
        uint32_t payload_hash = 2166136261UL;
        for (uint16_t i = 0; ok && i < headers[slot].count; i++) {
          if (file.read(record, sizeof(record)) != sizeof(record)) { ok = false; break; }
          uint8_t path_len = record[39];
          if (path_len != OUT_PATH_UNKNOWN && !mesh::Packet::isValidPathLen(path_len)) {
            ok = false;
            break;
          }
          payload_hash = aclHashUpdate(payload_hash, record, sizeof(record));
          ClientInfo& client = clients[num_clients];
          memset(&client, 0, sizeof(client));
          size_t p = 0;
          uint8_t pub_key[32];
          memcpy(pub_key, record + p, 32); p += 32;
          client.permissions = record[p++];
          memcpy(&client.extra.room.sync_since, record + p, 4); p += 4;
          p += 2;
          client.out_path_len = record[p++];
          memcpy(client.out_path, record + p, 64); p += 64;
          memcpy(client.shared_secret, record + p, PUB_KEY_SIZE);
          client.id = mesh::Identity(pub_key);
          self_id.calcSharedSecret(client.shared_secret, pub_key);
          num_clients++;
        }
        if (ok) {
          // The validation pass and this loading pass are separate reads.
          // Recheck the payload so changed/corrupt bytes cannot be published.
          ok = payload_hash == headers[slot].payload_hash && file.available() == 0;
        }
        file.close();
        if (ok) loaded_slot = slot;
        else {
          num_clients = 0;
          memset(clients, 0, sizeof(clients));
        }
        return ok;
      });
    if (loaded) {
      _atomic_slot = loaded_slot;
      _atomic_generation = headers[loaded_slot].generation;
    } else {
      num_clients = 0;
      memset(clients, 0, sizeof(clients));
      _atomic_slot = 1;
      _atomic_generation = 0;
    }
    return;
  }

  // If either atomic slot exists but neither validates, do not fall back to
  // the legacy file: it may be stale and could resurrect revoked contacts.
  bool has_atomic_files = fs->exists(paths[0]) || fs->exists(paths[1]);
  if (has_atomic_files) {
    num_clients = 0;
    return;
  }

  // One-time migration from the legacy single-file format. Keep the old file
  // untouched until a complete, verified slot has been written.
  load(fs, self_id);
  _atomic_generation = 0;
  _atomic_slot = 1;
  if (num_clients > 0 && saveAtomic(fs)) {
    // The verified slot is now authoritative. A failed legacy-file removal is
    // harmless because future boots never fall back when an atomic slot exists.
    if (fs->exists(_storage_path) && !fs->remove(_storage_path)) {
      MESH_DEBUG_PRINTLN("WARNING: could not remove legacy Room ACL after migration");
    }
  }
}

bool ClientACL::saveAtomic(FILESYSTEM* fs, bool (*filter)(ClientInfo*)) {
  _fs = fs;
  _atomic_mode = true;
  char paths[2][48];
  snprintf(paths[0], sizeof(paths[0]), "%s.0", _storage_path);
  snprintf(paths[1], sizeof(paths[1]), "%s.1", _storage_path);
  uint8_t target = 1 - _atomic_slot;
  uint16_t count = 0;
  uint32_t payload_hash = 2166136261UL;
  uint8_t record[ACL_RECORD_SIZE];
  for (int i = 0; i < num_clients; i++) {
    ClientInfo* client = &clients[i];
    if (client->permissions == 0 || (filter && !filter(client))) continue;
    encodeACLRecord(record, *client);
    payload_hash = aclHashUpdate(payload_hash, record, sizeof(record));
    count++;
  }

  ACLSlotHeader header;
  memset(&header, 0, sizeof(header));
  header.magic = ACL_SLOT_MAGIC;
  header.version = ACL_SLOT_VERSION;
  header.count = count;
  header.generation = _atomic_generation + 1;
  header.payload_hash = payload_hash;
  header.header_hash = aclHashUpdate(2166136261UL, (const uint8_t*)&header, offsetof(ACLSlotHeader, header_hash));

  ACLFileStorage storage(fs);
  int client_index = 0;
  bool success = atomicSlotWriteVerified<ACLFileStorage, ACLSlotHeader, uint8_t[ACL_RECORD_SIZE]>(
    storage, paths[target], header, count,
    [&](uint16_t, uint8_t (&out)[ACL_RECORD_SIZE]) {
      while (client_index < num_clients) {
        ClientInfo* client = &clients[client_index++];
        if (client->permissions == 0 || (filter && !filter(client))) continue;
        encodeACLRecord(out, *client);
        return true;
      }
      return false;
    },
    [&](const char* path, const ACLSlotHeader& expected, uint16_t expected_count) {
      ACLSlotHeader verified;
      return readACLSlot(fs, path, &verified) &&
        verified.generation == expected.generation && verified.count == expected_count;
    });
  if (!success) return false;
  _atomic_slot = target;
  _atomic_generation = header.generation;
  return true;
}

bool ClientACL::clear() {
  if (!_fs) return false; // no filesystem, nothing to clear

  if (_atomic_mode) {
    // Commit an empty, checksummed generation before removing the previous
    // slot. If power is lost during the operation, loadAtomic selects either
    // the old complete ACL or this newer empty ACL; it never revives an older
    // contact snapshot after a completed clear.
    int previous_count = num_clients;
    char stale_path[48] = {0};
    ACLFileStorage storage(_fs);
    // The empty generation must verify before RAM state or old snapshots are retired.
    return atomicSlotClearVerified(storage, stale_path, _storage_path,
      [&]() {
        num_clients = 0;
        if (!saveAtomic(_fs)) {
          num_clients = previous_count;
          return false;
        }
        snprintf(stale_path, sizeof(stale_path), "%s.%d", _storage_path, 1 - _atomic_slot);
        return true;
      },
      [&]() { memset(clients, 0, sizeof(clients)); });
  }

  // Preserve the original single-file Repeater ACL behavior.
  bool success = true;
  char slot_path[48];
  for (int i = 0; i < 2; i++) {
    snprintf(slot_path, sizeof(slot_path), "%s.%d", _storage_path, i);
    if (_fs->exists(slot_path) && !_fs->remove(slot_path)) success = false;
  }
  if (_fs->exists(_storage_path) && !_fs->remove(_storage_path)) success = false;
  if (!success) return false;
  memset(clients, 0, sizeof(clients));
  num_clients = 0;
  _atomic_generation = 0;
  _atomic_slot = 1;
  return true;
}

ClientInfo* ClientACL::getClient(const uint8_t* pubkey, int key_len) {
  for (int i = 0; i < num_clients; i++) {
    if (memcmp(pubkey, clients[i].id.pub_key, key_len) == 0) return &clients[i];  // already known
  }
  return NULL;  // not found
}

ClientInfo* ClientACL::putClient(const mesh::Identity& id, uint8_t init_perms) {
  uint32_t min_time = 0xFFFFFFFF;
  ClientInfo* oldest = &clients[MAX_CLIENTS - 1];
  for (int i = 0; i < num_clients; i++) {
    if (id.matches(clients[i].id)) return &clients[i];  // already known
    if (!clients[i].isAdmin() && clients[i].last_activity < min_time) {
      oldest = &clients[i];
      min_time = oldest->last_activity;
    }
  }

  ClientInfo* c;
  if (num_clients < MAX_CLIENTS) {
    c = &clients[num_clients++];
  } else {
    c = oldest;  // evict least active contact
  }
  memset(c, 0, sizeof(*c));
  c->permissions = init_perms;
  c->id = id;
  c->out_path_len = OUT_PATH_UNKNOWN;
  return c;
}

bool ClientACL::applyPermissions(const mesh::LocalIdentity& self_id, const uint8_t* pubkey, int key_len, uint8_t perms) {
  ClientInfo* c;
  if ((perms & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {  // guest role is not persisted in contacts
    c = getClient(pubkey, key_len);
    if (c == NULL) return false;   // partial pubkey not found

    num_clients--;   // delete from contacts[]
    int i = c - clients;
    while (i < num_clients) {
      clients[i] = clients[i + 1];
      i++;
    }
  } else {
    if (key_len < PUB_KEY_SIZE) return false;   // need complete pubkey when adding/modifying

    mesh::Identity id(pubkey);
    c = putClient(id, 0);

    c->permissions = perms;  // update their permissions
    self_id.calcSharedSecret(c->shared_secret, pubkey);
  }
  return true;
}

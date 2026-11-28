#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#include "MyMesh.h"
#if defined(ROOM_EXTRA_FS)
  #include <CustomLFS.h>

  // CustomLFS 0.2.3 formats (erases) its entire region whenever mount fails.
  // That is unsafe for persistent Room data: a transient/corrupt mount must
  // not silently destroy existing posts and ACLs. Only format a demonstrably
  // erased (all 0xFF) region on first provisioning; otherwise fail closed and
  // let the Repeater start without the Room service.
  class SafeRoomExtraFS : public CustomLFS {
  public:
    using CustomLFS::CustomLFS;

    bool begin() override {
      if (Adafruit_LittleFS::begin()) return true;

      uint8_t buffer[128];
      const uint32_t start = getFlashAddr();
      const uint32_t size = getFlashSize();
      for (uint32_t offset = 0; offset < size; offset += sizeof(buffer)) {
        uint32_t chunk = size - offset;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (flash_nrf5x_read(buffer, start + offset, chunk) <= 0) return false;
        for (uint32_t i = 0; i < chunk; i++) {
          if (buffer[i] != 0xFF) return false;  // existing/corrupt data: preserve it
        }
      }

      if (!formatRegion()) return false;  // blank first-use region only
      return Adafruit_LittleFS::begin();
    }
  };

  SafeRoomExtraFS RoomExtraFS(0xD4000, 0x19000, 128);
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(board, display);
#endif

#ifdef ETHERNET_ENABLED
  #define ETHERNET_CLI_BANNER "MeshCore Repeater CLI"
  #include <helpers/nrf52/EthernetCLI.h>
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];
#ifdef ETHERNET_ENABLED
static char ethernet_command[160];
#endif

// For power saving
unsigned long POWERSAVING_FIRSTSLEEP_SECS = 120; // The first sleep (if enabled) from boot

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_)
static unsigned long userBtnDownAt = 0;
#define USER_BTN_HOLD_OFF_MILLIS 1500
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

#if defined(RESOURCE_AUDIT) && defined(ROOM_EXTRA_FS)
  Serial.println("=== SENSECAP RESOURCE AUDIT: COMPILE-TIME FOOTPRINT ===");
  Serial.print("sizeof(MyMesh)="); Serial.println(sizeof(MyMesh));
  Serial.print("sizeof(ClientInfo)="); Serial.println(sizeof(ClientInfo));
  Serial.print("sizeof(ClientACL)="); Serial.println(sizeof(ClientACL));
  Serial.print("sizeof(RoomPostInfo)="); Serial.println(sizeof(RoomPostInfo));
  Serial.print("room_post_cache_bytes=");
  Serial.println(sizeof(RoomPostInfo) * ROOM_MAX_UNSYNCED_POSTS);
  Serial.print("one_acl_client_array_bytes=");
  Serial.println(sizeof(ClientInfo) * MAX_CLIENTS);
  Serial.print("sizeof(mesh::Packet)="); Serial.println(sizeof(mesh::Packet));
  Serial.print("packet_pool_objects=36");
  Serial.println();
  Serial.print("packet_pool_object_bytes=");
  Serial.println(sizeof(mesh::Packet) * 36);
  Serial.print("sizeof(StaticPoolPacketManager)=");
  Serial.println(sizeof(StaticPoolPacketManager));
  Serial.print("packet_queue_backing_array_bytes=");
  Serial.println(3 * 36 * (sizeof(mesh::Packet*) + sizeof(uint8_t) + sizeof(uint32_t)));
  Serial.println("NOTE: excludes allocator metadata, runtime heap fragmentation and stack usage.");
  Serial.println("=== END RESOURCE AUDIT ===");
#endif

  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#if defined(MESH_DEBUG) && defined(NRF52_PLATFORM)
  // give some extra time for serial to settle so
  // boot debug messages can be seen on terminal
  delay(5000);
#endif

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    MESH_DEBUG_PRINTLN("Radio init failed!");
    halt();
  }

  fast_rng.begin(radio_driver.getRngSeed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    // Never replace an existing but unreadable Repeater identity: doing so
    // would silently orphan the deployed node from its existing contacts.
    if (store.exists("_main")) {
      Serial.println("FATAL: existing Repeater identity is invalid; refusing to replace it");
      halt();
    }

    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {
      the_mesh.self_id = radio_new_identity(); count++;
    }
    if (!store.save("_main", the_mesh.self_id)) {
      Serial.println("FATAL: unable to save Repeater identity");
      halt();
    }
    mesh::LocalIdentity verify_main_id;
    if (!store.load("_main", verify_main_id) || !verify_main_id.matches(the_mesh.self_id)) {
      Serial.println("FATAL: Repeater identity read-back verification failed");
      halt();
    }
    the_mesh.self_id = verify_main_id;
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

#if defined(ROOM_EXTRA_FS)
  bool room_identity_ready = false;
  mesh::LocalIdentity room_id;
  if (store.load("_room", room_id)) {
    room_identity_ready = true;
  } else if (store.exists("_room")) {
    // Preserve a damaged Room key file and its associated Room data for
    // recovery. Do not silently mint a new Room identity over it.
    Serial.println("WARNING: existing Room identity is invalid; preserving it and starting Repeater only");
  } else {
    room_id = radio_new_identity();
    int count = 0;
    while (count < 10 && (room_id.pub_key[0] == 0x00 || room_id.pub_key[0] == 0xFF)) {
      room_id = radio_new_identity();
      count++;
    }
    if (store.save("_room", room_id)) {
      mesh::LocalIdentity verify_room_id;
      if (store.load("_room", verify_room_id) && verify_room_id.matches(room_id)) {
        room_id = verify_room_id;
        room_identity_ready = true;
      } else {
        Serial.println("WARNING: Room identity read-back verification failed; starting Repeater only");
      }
    } else {
      Serial.println("WARNING: unable to save Room identity; starting Repeater only");
    }
  }
  if (room_identity_ready) {
    the_mesh.setRoomIdentity(room_id);
    Serial.print("Room ID: ");
    mesh::Utils::printHex(Serial, the_mesh.getRoomIdentity().pub_key, PUB_KEY_SIZE); Serial.println();
  }
#endif

  command[0] = 0;
#ifdef ETHERNET_ENABLED
  ethernet_command[0] = 0;
#endif

  sensors.begin();

#if defined(ROOM_EXTRA_FS)
  FILESYSTEM* room_fs = NULL;
  if (room_identity_ready && RoomExtraFS.begin()) {
    room_fs = &RoomExtraFS;
  } else if (!room_identity_ready) {
    Serial.println("WARNING: Room identity unavailable; Repeater will run without Room service");
  } else {
    Serial.println("WARNING: Room ExtraFS unavailable; Repeater will run without Room service");
  }
  the_mesh.begin(fs, room_fs);
#else
  the_mesh.begin(fs);
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifdef ETHERNET_ENABLED
  ethernet_start_task();
#endif

  // send out initial zero hop Advertisement to the mesh
#if ENABLE_ADVERT_ON_BOOT == 1
  the_mesh.sendSelfAdvertisement(16000, false);
#if defined(ROOM_EXTRA_FS)
  the_mesh.sendRoomSelfAdvertisement(16500, false);
#endif
#endif

  board.onBootComplete();
}

void loop() {
#if defined(RESOURCE_AUDIT)
  static unsigned long next_resource_report = 10000;
#endif
  // Handle Serial CLI
  int len = strlen(command);
  // `command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(command) and the loop below would then
  // index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(command)) {
    command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
    command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
    command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    reply[0] = 0;
#ifdef ETHERNET_ENABLED
    if (!ethernet_handle_command(command, reply)) {
      the_mesh.handleCommand(0, command, reply);
    }
#else
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
#endif
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  board.loop();   // let the board feed its watchdog, run periodic housekeeping

#ifdef ETHERNET_ENABLED
  ethernet_loop_maintain();
  if (ethernet_read_line(ethernet_command, sizeof(ethernet_command))) {
    char reply[160];
    reply[0] = 0;
    if (!ethernet_handle_command(ethernet_command, reply)) {
      the_mesh.handleCommand(0, ethernet_command, reply);
    }
    ethernet_send_reply(reply);
    ethernet_command[0] = 0;
  }
#endif

#if defined(PIN_USER_BTN) && defined(_SEEED_SENSECAP_SOLAR_H_) && !defined(DISPLAY_CLASS)
  // Hold the user button to power off the SenseCAP Solar repeater.
  int btnState = digitalRead(PIN_USER_BTN);
  if (btnState == LOW) {
    if (userBtnDownAt == 0) {
      userBtnDownAt = millis();
    } else if ((unsigned long)(millis() - userBtnDownAt) >= USER_BTN_HOLD_OFF_MILLIS) {
      Serial.println("Powering off...");
      board.powerOff();  // does not return
    }
  } else {
    userBtnDownAt = 0;
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#if defined(RESOURCE_AUDIT)
  if ((int32_t)(millis() - next_resource_report) >= 0) {
    the_mesh.printResourceRuntimeStats();
    next_resource_report = millis() + 60000;
  }
#endif

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif
  if (the_mesh.getNodePrefs()->powersaving_enabled && !the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#else
    if (the_mesh.millisHasNowPassed(POWERSAVING_FIRSTSLEEP_SECS * 1000)) { // To check if it is time to sleep
      board.sleep(30); // Sleep. Wake up after a while or when receiving a LoRa packet
    }
#endif
  }
}

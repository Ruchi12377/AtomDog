#include "motion_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <ESP.h>

// ArduinoJson heap on PSRAM to avoid internal-RAM NoMemory during upload
struct SpiRamAllocator {
  void* allocate(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
  }
  void deallocate(void* pointer) {
    if (pointer) heap_caps_free(pointer);
  }
  void* reallocate(void* ptr, size_t new_size) {
    void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
    return p;
  }
};

using MotionJsonDocument = BasicJsonDocument<SpiRamAllocator>;

static size_t jsonDocCapacity(size_t jsonLen, uint16_t frameCount = 0) {
  size_t cap = jsonLen * 3 + 4096;
  if (frameCount > 0) {
    const size_t byFrames = (size_t)frameCount * 280 + 2048;
    if (byFrames > cap) cap = byFrames;
  }
  if (cap < 8192) cap = 8192;
  if (cap > MOTION_JSON_MAX) cap = MOTION_JSON_MAX;
  return cap;
}

struct MotionSlot {
  char name[MOTION_NAME_MAX];
  MotionFrame* frames;
  uint16_t frameCount;
  uint16_t loopStartFrame;
};

static MotionSlot g_slots[MOTION_STORE_MAX];
static uint8_t g_slotCount = 0;
static int8_t g_activeSlot = -1;
static bool g_fsReady = false;

static float smoothstep(float t) {
  t = constrain(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static uint8_t lerpAngle(uint8_t from, uint8_t to, float t) {
  return (uint8_t)lroundf((float)from + ((float)to - (float)from) * t);
}

static void* motionAlloc(size_t bytes) {
  if (motionStoreHasPsram()) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return malloc(bytes);
}

static void motionFree(void* ptr) {
  if (ptr) heap_caps_free(ptr);
}

static void freeSlot(MotionSlot& slot) {
  motionFree(slot.frames);
  slot.frames = nullptr;
  slot.frameCount = 0;
  slot.loopStartFrame = 0;
  slot.name[0] = '\0';
}

static int findSlot(const char* name) {
  for (uint8_t i = 0; i < g_slotCount; i++) {
    if (strcmp(g_slots[i].name, name) == 0) return i;
  }
  return -1;
}

static String motionFsPath(const char* name) {
  return String("/m/") + name + ".json";
}

static bool normalizeName(const char* in, char* out) {
  if (!in || in[0] == '\0') return false;
  size_t len = strnlen(in, MOTION_NAME_MAX - 1);
  for (size_t i = 0; i < len; i++) {
    const char c = in[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
    out[i] = c;
  }
  out[len] = '\0';
  return true;
}

bool motionStoreHasPsram() {
  return psramFound();
}

static bool writeJsonToFs(const char* name, const String& json) {
  if (!g_fsReady) return false;
  const String path = motionFsPath(name);
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.print(json);
  f.close();
  return true;
}

static bool loadJsonFromFs(const char* name, String& json) {
  if (!g_fsReady) return false;
  const String path = motionFsPath(name);
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  json = f.readString();
  f.close();
  return json.length() > 0;
}

static bool installSlotFromFrames(int slotIndex, const char* name, MotionFrame* buffer,
                                  uint16_t count, uint16_t loopStart) {
  if (!buffer || count == 0) return false;
  freeSlot(g_slots[slotIndex]);
  strncpy(g_slots[slotIndex].name, name, MOTION_NAME_MAX - 1);
  g_slots[slotIndex].name[MOTION_NAME_MAX - 1] = '\0';
  g_slots[slotIndex].frames = buffer;
  g_slots[slotIndex].frameCount = count;
  g_slots[slotIndex].loopStartFrame = loopStart;
  return true;
}

static bool parseAndStoreSlot(int slotIndex, const char* name, const String& jsonBody, String& error) {
  const size_t docSize = jsonDocCapacity(jsonBody.length());
  MotionJsonDocument doc(docSize);
  const DeserializationError jsonError = deserializeJson(doc, jsonBody);
  if (jsonError) {
    error = "invalid JSON: " + String(jsonError.c_str()) +
            " (need " + String(docSize) + " cap, json " + String(jsonBody.length()) + "B)";
    Serial.printf("[Motion] parse fail: %s heap=%u psram=%u\n", jsonError.c_str(),
                  ESP.getFreeHeap(), ESP.getFreePsram());
    return false;
  }

  JsonArray frames = doc["frames"].as<JsonArray>();
  if (frames.isNull()) {
    error = "frames array is required";
    return false;
  }

  const size_t count = frames.size();
  if (count == 0 || count > MOTION_FRAMES_MAX) {
    error = "frames must be 1.." + String(MOTION_FRAMES_MAX);
    return false;
  }

  MotionFrame* buffer = (MotionFrame*)motionAlloc(count * sizeof(MotionFrame));
  if (!buffer) {
    error = "PSRAM alloc failed";
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    JsonObject frame = frames[i].as<JsonObject>();
    JsonArray angles = frame["angles"].as<JsonArray>();
    if (angles.isNull() || angles.size() != MOTION_CHANNELS) {
      motionFree(buffer);
      error = "frame " + String(i) + " must have 8 angles";
      return false;
    }
    buffer[i].ms = constrain(frame["ms"] | 220, 20, MOTION_FRAME_MS_MAX);
    for (uint8_t ch = 0; ch < MOTION_CHANNELS; ch++) {
      buffer[i].angle[ch] = constrain(angles[ch] | 90, 0, 180);
    }
  }

  uint16_t loopStart = constrain(doc["loopStartFrame"] | 0, 0, (uint16_t)(count - 1));
  return installSlotFromFrames(slotIndex, name, buffer, (uint16_t)count, loopStart);
}

bool motionStoreUpload(const char* nameIn, const String& jsonBody, String& error) {
  char name[MOTION_NAME_MAX];
  if (!normalizeName(nameIn, name)) {
    error = "invalid motion name";
    return false;
  }
  if (jsonBody.length() == 0 || jsonBody.length() > MOTION_JSON_MAX) {
    error = "json body empty or too large";
    return false;
  }

  int slot = findSlot(name);
  if (slot < 0) {
    if (g_slotCount >= MOTION_STORE_MAX) {
      error = "motion store full";
      return false;
    }
    slot = g_slotCount++;
  }

  if (!parseAndStoreSlot(slot, name, jsonBody, error)) return false;
  if (!writeJsonToFs(name, jsonBody)) {
    error = "stored in RAM but flash write failed";
    return false;
  }

  Serial.printf("[Motion] uploaded '%s' %u frames loop@%u (%u bytes json)\n",
                name, g_slots[slot].frameCount, g_slots[slot].loopStartFrame, jsonBody.length());
  return true;
}

bool motionStoreDelete(const char* nameIn) {
  char name[MOTION_NAME_MAX];
  if (!normalizeName(nameIn, name)) return false;
  const int slot = findSlot(name);
  if (slot < 0) return false;

  if (g_fsReady) LittleFS.remove(motionFsPath(name));
  freeSlot(g_slots[slot]);
  for (int i = slot; i < (int)g_slotCount - 1; i++) {
    g_slots[i] = g_slots[i + 1];
  }
  g_slotCount--;
  if (g_activeSlot == slot) g_activeSlot = -1;
  else if (g_activeSlot > slot) g_activeSlot--;
  return true;
}

uint8_t motionStoreCount() {
  return g_slotCount;
}

bool motionStoreSetActive(const char* nameIn) {
  char name[MOTION_NAME_MAX];
  if (!normalizeName(nameIn, name)) return false;
  const int slot = findSlot(name);
  if (slot < 0) return false;
  g_activeSlot = slot;
  return true;
}

MotionPlayback motionStoreGetActive() {
  MotionPlayback pb = {nullptr, 0, 0, false};
  if (g_activeSlot < 0 || g_activeSlot >= (int)g_slotCount) return pb;
  const MotionSlot& s = g_slots[g_activeSlot];
  pb.frames = s.frames;
  pb.frameCount = s.frameCount;
  pb.loopStartFrame = s.loopStartFrame;
  pb.valid = s.frames && s.frameCount > 0;
  return pb;
}

bool motionStoreGetByName(const char* nameIn, MotionPlayback& out) {
  char name[MOTION_NAME_MAX];
  if (!normalizeName(nameIn, name)) return false;
  const int slot = findSlot(name);
  if (slot < 0) return false;
  out.frames = g_slots[slot].frames;
  out.frameCount = g_slots[slot].frameCount;
  out.loopStartFrame = g_slots[slot].loopStartFrame;
  out.valid = out.frames && out.frameCount > 0;
  return true;
}

static void appendFramesToJson(JsonArray framesArr, const MotionFrame* frames, uint16_t count) {
  for (uint16_t i = 0; i < count; i++) {
    JsonObject frame = framesArr.createNestedObject();
    frame["ms"] = frames[i].ms;
    JsonArray angles = frame.createNestedArray("angles");
    for (uint8_t ch = 0; ch < MOTION_CHANNELS; ch++) {
      angles.add(frames[i].angle[ch]);
    }
  }
}

String motionStoreBuildJson(const char* nameIn) {
  char name[MOTION_NAME_MAX];
  if (!normalizeName(nameIn, name)) return "{}";
  const int slot = findSlot(name);
  if (slot < 0) return "{}";

  const uint16_t fc = g_slots[slot].frameCount;
  MotionJsonDocument doc(jsonDocCapacity(0, fc));
  doc["name"] = g_slots[slot].name;
  doc["loopStartFrame"] = g_slots[slot].loopStartFrame;
  doc["frameCount"] = fc;
  JsonArray frames = doc.createNestedArray("frames");
  appendFramesToJson(frames, g_slots[slot].frames, fc);
  String body;
  serializeJson(doc, body);
  return body;
}

String motionStoreBuildCatalogJson() {
  MotionJsonDocument doc(2048);
  doc["count"] = g_slotCount;
  doc["psram"] = motionStoreHasPsram();
  JsonArray motions = doc.createNestedArray("motions");
  for (uint8_t i = 0; i < g_slotCount; i++) {
    JsonObject m = motions.createNestedObject();
    m["name"] = g_slots[i].name;
    m["frames"] = g_slots[i].frameCount;
    m["loopStartFrame"] = g_slots[i].loopStartFrame;
    m["active"] = (g_activeSlot == (int)i);
  }
  String body;
  serializeJson(doc, body);
  return body;
}

String motionStoreBuildActiveJson() {
  if (g_activeSlot < 0) return "{\"ok\":false,\"error\":\"no active motion\"}";
  return motionStoreBuildJson(g_slots[g_activeSlot].name);
}

const char* motionStoreGetActiveName() {
  if (g_activeSlot < 0 || g_activeSlot >= (int)g_slotCount) return "";
  return g_slots[g_activeSlot].name;
}

bool motionStoreInit() {
  for (uint8_t i = 0; i < MOTION_STORE_MAX; i++) {
    freeSlot(g_slots[i]);
  }
  g_slotCount = 0;
  g_activeSlot = -1;

  g_fsReady = LittleFS.begin(true);
  if (!g_fsReady) {
    Serial.println("[Motion] LittleFS mount failed");
    return false;
  }

  if (!LittleFS.exists("/m")) {
    LittleFS.mkdir("/m");
  }

  Serial.printf("[Motion] store ready PSRAM=%s\n", motionStoreHasPsram() ? "yes" : "no");
  return true;
}

bool motionStoreLoadAllFromFs() {
  File root = LittleFS.open("/m");
  if (!root || !root.isDirectory()) return false;

  File entry = root.openNextFile();
  while (entry) {
    String path = entry.name();
    entry.close();
    if (!path.endsWith(".json")) {
      entry = root.openNextFile();
      continue;
    }

    int slash = path.lastIndexOf('/');
    String fileName = (slash >= 0) ? path.substring(slash + 1) : path;
    const int dot = fileName.lastIndexOf('.');
    if (dot <= 0) {
      entry = root.openNextFile();
      continue;
    }
    String motionName = fileName.substring(0, dot);

    String json;
    if (loadJsonFromFs(motionName.c_str(), json)) {
      String err;
      motionStoreUpload(motionName.c_str(), json, err);
    }
    entry = root.openNextFile();
  }
  root.close();
  return true;
}

// --- default motion builders (seed) ---

static bool appendSegment(MotionFrame* out, uint16_t cap, uint16_t& idx,
                          const uint8_t from[MOTION_CHANNELS],
                          const uint8_t to[MOTION_CHANNELS],
                          uint8_t steps, uint16_t ms) {
  for (uint8_t step = 1; step <= steps; step++) {
    if (idx >= cap) return false;
    const float t = smoothstep((float)step / (float)steps);
    out[idx].ms = ms;
    for (uint8_t ch = 0; ch < MOTION_CHANNELS; ch++) {
      out[idx].angle[ch] = lerpAngle(from[ch], to[ch], t);
    }
    idx++;
  }
  return true;
}

static bool appendHold(MotionFrame* out, uint16_t cap, uint16_t& idx,
                       const uint8_t pose[MOTION_CHANNELS], uint8_t frames, uint16_t ms) {
  for (uint8_t i = 0; i < frames; i++) {
    if (idx >= cap) return false;
    out[idx].ms = ms;
    for (uint8_t ch = 0; ch < MOTION_CHANNELS; ch++) out[idx].angle[ch] = pose[ch];
    idx++;
  }
  return true;
}

static bool appendPose(MotionFrame* out, uint16_t cap, uint16_t& idx,
                       const uint8_t pose[MOTION_CHANNELS], uint16_t ms) {
  if (idx >= cap) return false;
  out[idx].ms = ms;
  for (uint8_t ch = 0; ch < MOTION_CHANNELS; ch++) out[idx].angle[ch] = pose[ch];
  idx++;
  return true;
}

static bool buildDefaultWalk(MotionFrame* out, uint16_t cap, uint16_t& count, uint16_t& loopStart) {
  // 安定寄りの対角歩行。片対角を曲げて前に出し、その間に接地対角で軽く押す。
  // frame 0 は停止状態からの入り。ループは frame 1 から回して、最後から戻る時の跳びを小さくする。
  constexpr uint16_t ENTRY_MS = 200;
  constexpr uint16_t LOAD_MS = 130;
  constexpr uint16_t SWING_MS = 140;
  constexpr uint16_t LAND_MS = 120;
  const uint8_t entry[8] = {68, 112, 112, 68, 68, 112, 112, 68};
  const uint8_t aLoad[8] = {52, 120, 112, 68, 68, 112, 128, 60};
  const uint8_t aSwing[8] = {52, 128, 112, 76, 68, 104, 128, 52};
  const uint8_t aLand[8] = {68, 128, 112, 84, 68, 96, 112, 52};
  const uint8_t aPushBPreload[8] = {68, 120, 112, 84, 68, 96, 112, 60};
  const uint8_t bLoad[8] = {68, 112, 128, 60, 52, 120, 112, 68};
  const uint8_t bSwing[8] = {68, 104, 128, 52, 52, 128, 112, 76};
  const uint8_t bLand[8] = {68, 96, 112, 52, 68, 128, 112, 84};
  const uint8_t bPushAPreload[8] = {68, 96, 112, 60, 68, 120, 112, 84};
  uint16_t idx = 0;
  loopStart = 1;
  bool ok = appendPose(out, cap, idx, entry, ENTRY_MS);
  ok = ok && appendPose(out, cap, idx, aLoad, LOAD_MS);
  ok = ok && appendPose(out, cap, idx, aSwing, SWING_MS);
  ok = ok && appendPose(out, cap, idx, aLand, LAND_MS);
  ok = ok && appendPose(out, cap, idx, aPushBPreload, LOAD_MS);
  ok = ok && appendPose(out, cap, idx, bLoad, LOAD_MS);
  ok = ok && appendPose(out, cap, idx, bSwing, SWING_MS);
  ok = ok && appendPose(out, cap, idx, bLand, LAND_MS);
  ok = ok && appendPose(out, cap, idx, bPushAPreload, LOAD_MS);
  count = idx;
  return ok;
}

static bool uploadBuiltMotion(const char* name,
                              bool (*builder)(MotionFrame*, uint16_t, uint16_t&, uint16_t&)) {
  MotionFrame temp[MOTION_FRAMES_MAX];
  uint16_t count = 0;
  uint16_t loopStart = 0;
  if (!builder(temp, MOTION_FRAMES_MAX, count, loopStart)) return false;

  int slot = findSlot(name);
  if (slot < 0) {
    if (g_slotCount >= MOTION_STORE_MAX) return false;
    slot = g_slotCount++;
  }

  MotionFrame* buffer = (MotionFrame*)motionAlloc((size_t)count * sizeof(MotionFrame));
  if (!buffer) return false;
  for (uint16_t i = 0; i < count; i++) buffer[i] = temp[i];
  if (!installSlotFromFrames(slot, name, buffer, count, loopStart)) {
    motionFree(buffer);
    return false;
  }

  MotionJsonDocument doc(jsonDocCapacity(0, count));
  doc["name"] = name;
  doc["loopStartFrame"] = loopStart;
  JsonArray frames = doc.createNestedArray("frames");
  appendFramesToJson(frames, temp, count);
  String json;
  serializeJson(doc, json);
  String err;
  if (!writeJsonToFs(name, json)) {
    Serial.println("[Motion] seed flash write failed");
  }
  return true;
}

void motionStoreSeedDefaultsIfEmpty() {
  motionStoreLoadAllFromFs();
  if (g_slotCount > 0) {
    if (g_activeSlot < 0) motionStoreSetActive(g_slots[0].name);
    Serial.printf("[Motion] loaded %u motions from flash\n", g_slotCount);
    return;
  }

  Serial.println("[Motion] seeding default walk");
  uploadBuiltMotion("walk", buildDefaultWalk);
  motionStoreSetActive("walk");
}

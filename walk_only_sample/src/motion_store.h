#pragma once

#include <Arduino.h>

static constexpr uint8_t MOTION_CHANNELS = 8;
static constexpr uint8_t MOTION_NAME_MAX = 24;
static constexpr uint8_t MOTION_STORE_MAX = 16;
static constexpr uint16_t MOTION_FRAMES_MAX = 128;
static constexpr uint16_t MOTION_FRAME_MS_MAX = 8000;
static constexpr size_t MOTION_JSON_MAX = 65535;

// く字の軽いしゃがみ (前: 膝-, 股+ / 後: 膝+, 股-). 90±CROUCH_DELTA
static constexpr uint8_t MOTION_CROUCH_DELTA = 22;
static constexpr uint8_t MOTION_DEFAULT_STANCE[MOTION_CHANNELS] = {
    90 - MOTION_CROUCH_DELTA, 90 + MOTION_CROUCH_DELTA,  // RF knee, hip
    90 + MOTION_CROUCH_DELTA, 90 - MOTION_CROUCH_DELTA,  // RR knee, hip
    90 - MOTION_CROUCH_DELTA, 90 + MOTION_CROUCH_DELTA,  // LF knee, hip
    90 + MOTION_CROUCH_DELTA, 90 - MOTION_CROUCH_DELTA,  // LR knee, hip
};

struct MotionFrame {
  uint16_t ms;
  uint8_t angle[MOTION_CHANNELS];
};

struct MotionPlayback {
  const MotionFrame* frames;
  uint16_t frameCount;
  uint16_t loopStartFrame;
  bool valid;
};

bool motionStoreInit();
bool motionStoreHasPsram();

bool motionStoreUpload(const char* name, const String& jsonBody, String& error);
bool motionStoreDelete(const char* name);
uint8_t motionStoreCount();

bool motionStoreSetActive(const char* name);
MotionPlayback motionStoreGetActive();
bool motionStoreGetByName(const char* name, MotionPlayback& out);

String motionStoreBuildJson(const char* name);
String motionStoreBuildCatalogJson();
String motionStoreBuildActiveJson();

const char* motionStoreGetActiveName();

void motionStoreSeedDefaultsIfEmpty();

#include <Arduino.h>
#include <M5Unified.h>
#include <Wire.h>
#include <PWMServoDriver.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>
#include "motion_store.h"

#if __has_include("local_config.h")
#include "local_config.h"
#endif

#ifndef WALK_WIFI_SSID
#define WALK_WIFI_SSID ""
#endif

#ifndef WALK_WIFI_PASSWORD
#define WALK_WIFI_PASSWORD ""
#endif

// AtomS3R Grove/HY2.0-4P port confirmed working with PCA9685.
static constexpr int I2C_SDA = 1;
static constexpr int I2C_SCL = 2;
static constexpr uint32_t I2C_FREQ = 100000;

static constexpr uint8_t PCA9685_ADDR = 0x40;
static constexpr uint16_t SERVO_FREQ_HZ = 50;
// SG90 practical range varies by unit. Keep 90deg at 1500us and avoid overdriving the ends.
static constexpr uint16_t DEFAULT_SERVO_MIN_US = 700;
static constexpr uint16_t DEFAULT_SERVO_MAX_US = 2300;
static constexpr uint8_t SEQUENCE_CHANNELS = MOTION_CHANNELS;
static constexpr uint16_t WALK_MAX_HOLD_MS = MOTION_FRAME_MS_MAX;
static constexpr const char* WIFI_SSID = WALK_WIFI_SSID;
static constexpr const char* WIFI_PASSWORD = WALK_WIFI_PASSWORD;
static constexpr const char* DEFAULT_WALK_NAME = "walk";
static constexpr const char* DEFAULT_SIT_NAME = "sit";
static constexpr const char* DEFAULT_STAND_NAME = "stand";

static WebServer server(80);
static PWMServoDriver pwm(PCA9685_ADDR);
static uint8_t servoAngles[8] = {
    MOTION_DEFAULT_STANCE[0], MOTION_DEFAULT_STANCE[1], MOTION_DEFAULT_STANCE[2],
    MOTION_DEFAULT_STANCE[3], MOTION_DEFAULT_STANCE[4], MOTION_DEFAULT_STANCE[5],
    MOTION_DEFAULT_STANCE[6], MOTION_DEFAULT_STANCE[7],
};
static uint16_t servoMinUs = DEFAULT_SERVO_MIN_US;
static uint16_t servoMaxUs = DEFAULT_SERVO_MAX_US;
static uint16_t servoPulseUs[8] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};
// 対角歩行 2フレーム (CH0-1 右前 / CH2-3 右後 / CH4-5 左前 / CH6-7 左後).
// ロボ前進方向の符号 (中立 90deg 基準):
//   CH0,1,4,5 (前脚)  "+" -> angle > 90
//   CH2,3,6,7 (後脚)  "-" -> angle < 90  (CH6,7 = 左後; CH8 は未使用)
// 各ステップで動かさない対角は 90 のまま固定。
static constexpr uint8_t CH_RF_KNEE = 0;
static constexpr uint8_t CH_RF_HIP  = 1;
static constexpr uint8_t CH_RR_KNEE = 2;
static constexpr uint8_t CH_RR_HIP  = 3;
static constexpr uint8_t CH_LF_KNEE = 4;
static constexpr uint8_t CH_LF_HIP  = 8;
static constexpr uint8_t CH_LR_KNEE = 6;
static constexpr uint8_t CH_LR_HIP  = 7;

static const uint8_t SEQUENCE_TO_PCA_CHANNEL[8] = {
  CH_RF_KNEE,
  CH_RF_HIP,
  CH_RR_KNEE,
  CH_RR_HIP,
  CH_LF_KNEE,
  CH_LF_HIP,
  CH_LR_KNEE,
  CH_LR_HIP
};

static constexpr uint8_t NEUTRAL_ANGLE = 90;
static constexpr uint8_t BEND_DELTA = 38;
static constexpr uint8_t FRONT_PLUS_KNEE = NEUTRAL_ANGLE + BEND_DELTA;
static constexpr uint8_t FRONT_PLUS_HIP = NEUTRAL_ANGLE + BEND_DELTA;
static constexpr uint8_t FRONT_KNEE_BACK = NEUTRAL_ANGLE - BEND_DELTA;
static constexpr uint8_t REAR_MINUS_KNEE = NEUTRAL_ANGLE - BEND_DELTA;
static constexpr uint8_t REAR_MINUS_HIP = NEUTRAL_ANGLE - BEND_DELTA;
static constexpr uint8_t REAR_KNEE_BACK = NEUTRAL_ANGLE + BEND_DELTA;

// Sit pose (separate from walk): front hip +30deg, front knee -30deg,
// rear hip max forward (-), rear knee max backward (+).
static constexpr uint8_t SIT_DELTA_FRONT = 30;
static constexpr uint8_t SIT_DELTA_REAR = 30;
static constexpr uint8_t SIT_FRONT_KNEE = NEUTRAL_ANGLE - SIT_DELTA_FRONT;
static constexpr uint8_t SIT_FRONT_HIP = NEUTRAL_ANGLE + SIT_DELTA_FRONT;
static constexpr uint8_t SIT_REAR_KNEE = NEUTRAL_ANGLE + SIT_DELTA_REAR;
static constexpr uint8_t SIT_REAR_HIP = NEUTRAL_ANGLE - SIT_DELTA_REAR;
static constexpr uint8_t SIT_SUBSTEPS = 10;
static constexpr uint8_t SIT_HOLD_FRAMES = 2;
static constexpr uint16_t SIT_SUBFRAME_MS = 80;

static bool isSitting = false;
static constexpr uint32_t SERVO_INTERP_MIN_INTERVAL_MS = 18;

static MotionPlayback loopMotion = {};
static bool walkLoopActive = false;
static uint16_t walkLoopIndex = 0;
static bool walkAdvancePending = false;
static bool servoInterpActive = false;
static uint32_t servoInterpLastTickMs = 0;
static uint32_t servoInterpStartMs = 0;
static uint16_t servoInterpDurationMs = 0;
static uint8_t servoInterpFrom[SEQUENCE_CHANNELS];
static uint8_t servoInterpTo[SEQUENCE_CHANNELS];
static uint32_t pcaWatchdogLastMs = 0;
static bool pcaReady = false;
static char localIpText[32] = "";
static constexpr uint8_t DISPLAY_ROTATION = 2;

static void stopWalkLoop();
static void startServoTransition(const uint8_t target[SEQUENCE_CHANNELS], uint16_t durationMs);
static void tickServoInterpolation();
static void updateWalkLoop();
static void tickPcaWatchdog();

static float easeInOutCubic(float t) {
  t = constrain(t, 0.0f, 1.0f);
  if (t < 0.5f) return 4.0f * t * t * t;
  const float f = -2.0f * t + 2.0f;
  return 1.0f - (f * f * f) * 0.5f;
}

static uint8_t lerpAngle(uint8_t from, uint8_t to, float t) {
  return (uint8_t)lroundf((float)from + ((float)to - (float)from) * t);
}

static MotionPlayback resolveMotionByArg(const char* nameArg) {
  if (nameArg && nameArg[0] != '\0') {
    MotionPlayback pb = {};
    if (motionStoreGetByName(nameArg, pb)) return pb;
  }
  return motionStoreGetActive();
}

static const char* channelNames[SEQUENCE_CHANNELS] = {
    "right front knee",
    "right front hip",
    "right rear knee",
    "right rear hip",
    "left front knee",
    "left front hip",
    "left rear knee",
    "left rear hip",
};

static bool pcaBegin() {
  Wire.end();
  delay(20);
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
  delay(20);

  Wire.beginTransmission(PCA9685_ADDR);
  if (Wire.endTransmission() != 0) return false;

  if (!pwm.begin(false)) return false;
  pwm.setPWMFreq(SERVO_FREQ_HZ);
  for (uint8_t ch = 0; ch < 16; ch++) {
    pwm.setServoUs(ch, servoMinUs, servoMaxUs);
  }
  delay(10);
  return true;
}

static bool pcaPing() {
  Wire.beginTransmission(PCA9685_ADDR);
  return Wire.endTransmission() == 0;
}

static uint16_t angleToMicros(int angle) {
  angle = constrain(angle, 0, 180);
  return (uint16_t)map(angle, 0, 180, servoMinUs, servoMaxUs);
}

static bool writeServoMicros(uint8_t channel, uint16_t micros) {
  if (!pcaReady) return false;
  uint8_t physicalChannel = channel;
  if (channel < 8) {
    physicalChannel = SEQUENCE_TO_PCA_CHANNEL[channel];
  }
  if (physicalChannel >= 16) return false;
  pwm.setServoPulse(physicalChannel, micros);
  return true;
}

static void resendCurrentServoAngles() {
  if (!pcaReady) return;
  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    const uint16_t micros = angleToMicros(servoAngles[ch]);
    writeServoMicros(ch, micros);
    servoPulseUs[ch] = micros;
  }
}

static void writeAllServosAngle(int angle) {
  const uint16_t micros = angleToMicros(angle);
  for (uint8_t ch = 0; ch < 16; ch++) {
    writeServoMicros(ch, micros);
  }
  for (uint8_t ch = 0; ch < 8; ch++) {
    servoAngles[ch] = constrain(angle, 0, 180);
    servoPulseUs[ch] = micros;
  }
  Serial.printf("[Servo] all channels -> %d deg (%u us)\n", angle, micros);
}

static M5Canvas faceCanvas(&M5.Display);
static bool faceCanvasCreated = false;
static String faceStatusText = "";
static uint32_t faceLastDrawMs = 0;

static void drawFaceLoop();

static void drawStatus(const char* line1, const char* line2 = "") {
  String text = String(line1);
  if (line2 && line2[0]) {
    text += " ";
    text += line2;
  }
  faceStatusText = text;
  Serial.printf("[Status] %s\n", text.c_str());
  
  if (!faceCanvasCreated) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(1.5);
    M5.Display.setCursor(8, 28);
    M5.Display.println(line1);
    M5.Display.setCursor(8, 64);
    M5.Display.println(line2);
  }
}

static void drawHome(const String& status = "") {
  faceStatusText = status;
}

static void drawFaceLoop() {
  uint32_t now = millis();
  if (now - faceLastDrawMs < 33) return;
  faceLastDrawMs = now;

  if (!faceCanvasCreated) {
    faceCanvas.setColorDepth(16);
    faceCanvas.createSprite(128, 128);
    faceCanvasCreated = true;
  }

  faceCanvas.fillScreen(TFT_BLACK);

  // Breath calculation (sin wave, period = 1000ms)
  float breathOffset = 1.5f * sinf(now / 500.0f);

  // Blink calculation
  static uint32_t nextBlinkMs = 0;
  static uint32_t blinkEndMs = 0;
  if (now > nextBlinkMs) {
    nextBlinkMs = now + random(2500, 6000);
    blinkEndMs = now + 150;
  }
  bool isBlinking = (now < blinkEndMs);

  // Eyes coordinates
  int32_t lx = 38;
  int32_t ly = 52 + (int32_t)breathOffset;
  int32_t rx = 90;
  int32_t ry = 52 + (int32_t)breathOffset;
  int32_t r = 8;

  if (isBlinking) {
    // Draw closed eyes (horizontal line)
    faceCanvas.fillRect(lx - r, ly - 1, r * 2, 3, TFT_WHITE);
    faceCanvas.fillRect(rx - r, ry - 1, r * 2, 3, TFT_WHITE);
  } else {
    // Draw open eyes (circles)
    faceCanvas.fillCircle(lx, ly, r, TFT_WHITE);
    faceCanvas.fillCircle(rx, ry, r, TFT_WHITE);
  }

  // Mouth coordinates
  int32_t mx = 64;
  int32_t my = 82 + (int32_t)breathOffset;
  
  // Animate mouth slightly if walkLoopActive or servoInterpActive
  bool isMoving = walkLoopActive || servoInterpActive;
  int32_t h = 3;
  if (isMoving) {
    h = 3 + (int32_t)(4.0f * (0.5f + 0.5f * sinf(now / 150.0f)));
  }
  int32_t w = 34;

  if (h <= 3) {
    faceCanvas.fillRect(mx - w / 2, my - 1, w, 3, TFT_WHITE);
  } else {
    faceCanvas.fillRoundRect(mx - w / 2, my - h / 2, w, h, 3, TFT_WHITE);
  }

  // Draw IP and Status text at the bottom
  faceCanvas.setFont(&fonts::lgfxJapanGothic_12);
  faceCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  faceCanvas.setTextSize(1.0f);
  faceCanvas.setTextDatum(lgfx::datum_t::bottom_center);

  String displayMsg = "";
  if (faceStatusText.length() > 0) {
    displayMsg = faceStatusText;
  } else {
    displayMsg = (localIpText[0] ? localIpText : "No IP");
  }
  faceCanvas.drawString(displayMsg.c_str(), 64, 122);

  // Push to screen
  M5.Display.startWrite();
  faceCanvas.pushSprite(&M5.Display, 0, 0);
  M5.Display.endWrite();
}

static bool applyServoAnglesImmediate(const uint8_t angles[SEQUENCE_CHANNELS]);

static void applyDefaultStance() {
  applyServoAnglesImmediate(MOTION_DEFAULT_STANCE);
}

static bool applyServoAnglesImmediate(const uint8_t angles[SEQUENCE_CHANNELS]) {
  if (!pcaReady) return false;
  stopWalkLoop();
  servoInterpActive = false;
  bool ok = true;
  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    const uint8_t angle = constrain(angles[ch], 0, 180);
    const uint16_t micros = angleToMicros(angle);
    ok = writeServoMicros(ch, micros) && ok;
    servoAngles[ch] = angle;
    servoPulseUs[ch] = micros;
  }
  return ok;
}

static bool applyMotionFrame(const MotionPlayback& pb, uint16_t frameIndex) {
  if (!pb.valid || frameIndex >= pb.frameCount) return false;

  uint8_t angles[SEQUENCE_CHANNELS];
  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    angles[ch] = constrain(pb.frames[frameIndex].angle[ch], 0, 180);
  }
  const bool ok = applyServoAnglesImmediate(angles);
  Serial.printf("[Motion] frame %u/%u ms=%u %s\n",
                frameIndex + 1, pb.frameCount, pb.frames[frameIndex].ms, ok ? "OK" : "NG");
  return ok;
}

static void startServoTransition(const uint8_t target[SEQUENCE_CHANNELS], uint16_t durationMs) {
  durationMs = constrain(durationMs, 20, WALK_MAX_HOLD_MS);
  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    servoInterpFrom[ch] = servoAngles[ch];
    servoInterpTo[ch] = constrain(target[ch], 0, 180);
  }
  servoInterpDurationMs = durationMs;
  servoInterpStartMs = millis();
  servoInterpLastTickMs = 0;
  servoInterpActive = true;
}

static void startServoTransitionToFrame(const MotionPlayback& pb, uint16_t frameIndex) {
  if (!pb.valid || frameIndex >= pb.frameCount) return;
  startServoTransition(pb.frames[frameIndex].angle,
                       constrain(pb.frames[frameIndex].ms, 20, WALK_MAX_HOLD_MS));
}

static void tickServoInterpolation() {
  if (!servoInterpActive || !pcaReady) return;

  const uint32_t now = millis();
  if (servoInterpLastTickMs != 0 &&
      (uint32_t)(now - servoInterpLastTickMs) < SERVO_INTERP_MIN_INTERVAL_MS) {
    return;
  }
  servoInterpLastTickMs = now;

  float t = 1.0f;
  if (servoInterpDurationMs > 0) {
    t = (float)(now - servoInterpStartMs) / (float)servoInterpDurationMs;
  }
  if (t >= 1.0f) {
    t = 1.0f;
    servoInterpActive = false;
    if (walkLoopActive) walkAdvancePending = true;
  }
  const float eased = easeInOutCubic(t);

  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    const uint8_t angle = lerpAngle(servoInterpFrom[ch], servoInterpTo[ch], eased);
    const uint16_t micros = angleToMicros(angle);
    writeServoMicros(ch, micros);
    servoAngles[ch] = angle;
    servoPulseUs[ch] = micros;
  }
}

static void tickPcaWatchdog() {
  const uint32_t now = millis();
  const uint32_t intervalMs = walkLoopActive || servoInterpActive ? 500 : 2000;
  if ((uint32_t)(now - pcaWatchdogLastMs) < intervalMs) return;
  pcaWatchdogLastMs = now;

  if (pcaReady && pcaPing()) return;

  if (pcaReady) {
    pcaReady = false;
    Serial.println("[PCA9685] lost, will retry");
    drawHome("PCA retry...");
    return;
  }

  if (!pcaBegin()) return;

  pcaReady = true;
  resendCurrentServoAngles();
  Serial.println("[PCA9685] recovered");
  drawHome(walkLoopActive ? "PCA recovered" : "PCA OK");
}

static bool playMotionPlayback(const MotionPlayback& pb, const char* label) {
  if (!pcaReady || !pb.valid || pb.frameCount == 0) return false;
  bool ok = true;
  for (uint16_t i = 0; i < pb.frameCount; i++) {
    startServoTransition(pb.frames[i].angle, constrain(pb.frames[i].ms, 20, WALK_MAX_HOLD_MS));
    while (servoInterpActive) {
      server.handleClient();
      M5.update();
      tickPcaWatchdog();
      tickServoInterpolation();
      if (walkLoopActive) updateWalkLoop();
      delay(5);
    }
    ok = ok && pcaReady;
  }
  Serial.printf("[Motion] %s done (%u frames)\n", label, pb.frameCount);
  return ok;
}

static bool playNamedMotion(const char* name) {
  MotionPlayback pb = {};
  if (!motionStoreGetByName(name, pb)) return false;
  return playMotionPlayback(pb, name);
}

static bool playSitSequence() {
  stopWalkLoop();
  drawHome("Sit...");
  const bool ok = playNamedMotion(DEFAULT_SIT_NAME);
  isSitting = ok;
  drawHome(ok ? "Sitting" : "Sit NG");
  return ok;
}

static bool playStandSequence() {
  stopWalkLoop();
  drawHome("Stand...");
  const bool ok = playNamedMotion(DEFAULT_STAND_NAME);
  isSitting = false;
  drawHome(ok ? "Standing" : "Stand NG");
  return ok;
}

static void stopWalkLoop() {
  walkLoopActive = false;
  walkLoopIndex = 0;
  walkAdvancePending = false;
  servoInterpActive = false;
}

static void startWalkLoopFor(const MotionPlayback& pb) {
  if (!pcaReady || !pb.valid || pb.frameCount == 0) return;
  stopWalkLoop();
  loopMotion = pb;
  walkLoopActive = true;
  walkLoopIndex = 0;
  walkAdvancePending = false;
  startServoTransitionToFrame(loopMotion, 0);
  drawHome("Walk loop");
  Serial.printf("[Walk] loop %u frames (cycle@%u)\n", loopMotion.frameCount, loopMotion.loopStartFrame + 1);
}

static void startWalkLoop() {
  startWalkLoopFor(motionStoreGetActive());
}

static void updateWalkLoop() {
  if (!walkLoopActive || !pcaReady || !loopMotion.valid || servoInterpActive || !walkAdvancePending) {
    return;
  }

  walkAdvancePending = false;
  if (++walkLoopIndex >= loopMotion.frameCount) {
    walkLoopIndex = loopMotion.loopStartFrame;
  }
  startServoTransitionToFrame(loopMotion, walkLoopIndex);
}

static bool playMotionOnce(const MotionPlayback& pb, const char* label) {
  if (!pb.valid || pb.frameCount == 0) return false;

  const bool wasLooping = walkLoopActive;
  stopWalkLoop();
  drawHome(label);
  const bool ok = playMotionPlayback(pb, label);
  drawHome(ok ? "Motion done" : "Motion I2C NG");
  if (wasLooping) startWalkLoop();
  return ok;
}

static String motionNameFromRequest(const char* fallback) {
  if (server.hasArg("name")) {
    const String n = server.arg("name");
    if (n.length() > 0) return n;
  }
  return String(fallback);
}

static void sendNoCache(int code, const char* type, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Connection", "close");
  if (strcmp(type, "text/html") == 0) {
    server.send(code, "text/html; charset=utf-8", body);
  } else {
    server.send(code, type, body);
  }
}

static String buildIndexHtml() {
  String html;
  html.reserve(12000);
  html += F("<!doctype html><html><head><meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>AtomS3R Walk</title><style>");
  html += F(":root{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#1d1d1f;background:#f5f5f7}");
  html += F("body{margin:0;padding:20px}.wrap{max-width:760px;margin:auto}h1{font-size:24px;margin:0 0 16px}");
  html += F(".card{background:#fff;border:1px solid #ddd;border-radius:8px;padding:14px;margin:10px 0;box-shadow:0 1px 2px #0001}");
  html += F(".actions{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0}button{border:1px solid #bbb;background:#fff;border-radius:6px;padding:10px 12px;font-size:15px}");
  html += F("textarea{width:100%;min-height:180px;box-sizing:border-box;border:1px solid #bbb;border-radius:6px;padding:10px;font:12px ui-monospace,SFMono-Regular,Menlo,monospace}");
  html += F(".tablewrap{overflow:auto}table{border-collapse:collapse;min-width:900px;width:100%;font-size:13px}th,td{border-bottom:1px solid #e5e5e5;padding:6px;text-align:left}th{font-size:12px;color:#555}td input{width:58px;padding:6px;border:1px solid #bbb;border-radius:5px}");
  html += F("#status{min-height:20px;margin:10px 0;font-size:13px;color:#555}");
  html += F(".ok{color:#0a7f33}.ng{color:#b00020}.meta{font-size:13px;color:#555}</style></head><body><div class=\"wrap\">");
  html += F("<h1>AtomS3R 歩行</h1>");
  html += pcaReady ? F("<div class=\"meta ok\">PCA9685 OK (SDA=G1 SCL=G2)</div>") : F("<div class=\"meta ng\">PCA9685 not connected</div>");
  html += F("<div class=\"card\"><div class=\"actions\">Pulse us ");
  html += F("<input id=\"minUs\" type=\"number\" min=\"400\" max=\"1500\" value=\"");
  html += servoMinUs;
  html += F("\">");
  html += F("<input id=\"maxUs\" type=\"number\" min=\"1500\" max=\"2600\" value=\"");
  html += servoMaxUs;
  html += F("\"><button onclick=\"setPulseRange()\">Apply</button></div></div>");
  html += F("<div id=\"status\">Ready</div>");
  html += F("<div class=\"card\"><h2 style=\"font-size:18px;margin:0 0 8px\">Motions / モーション</h2>");
  html += F("<div class=\"meta\">POST /upload?name=xxx で JSON を PSRAM + LittleFS に保存。loopStartFrame でループ開始コマ指定。</div>");
  html += F("<div class=\"actions\"><input id=\"motionName\" value=\"walk\" placeholder=\"name\">");
  html += F("<button onclick=\"loadMotions()\">Refresh list</button><button onclick=\"loadMotionJson()\">Load JSON</button>");
  html += F("<button onclick=\"uploadMotion()\">Upload</button><button onclick=\"playMotion()\">Play once</button>");
  html += F("<button onclick=\"startMotionLoop()\">Loop</button><button onclick=\"stopWalkLoop()\">Stop</button></div>");
  html += F("<ul id=\"motionList\" class=\"meta\"></ul><textarea id=\"motionJson\" spellcheck=\"false\"></textarea></div>");
  html += F("<div class=\"card\"><h2 style=\"font-size:18px;margin:0 0 8px\">Sit / おすわり</h2>");
  html += F("<div class=\"meta\">sit / stand もモーション名で保存済み。</div>");
  html += F("<div class=\"actions\"><button onclick=\"playSit()\">Sit</button><button onclick=\"playStand()\">Stand</button>");
  html += F("<button onclick=\"loadSitJson()\">Load sit JSON</button></div></div>");
  html += F("<script>");
  html += F("const statusEl=document.getElementById('status');");
  html += F("function showStatus(t,ok=true){statusEl.textContent=t;statusEl.className=ok?'ok':'ng'}");
  html += F("function motionName(){return document.getElementById('motionName').value.trim()||'walk'}");
  html += F("async function getJson(path){const url=path+(path.includes('?')?'&':'?')+'_='+Date.now();const r=await fetch(url,{cache:'no-store'});let j={};try{j=await r.json()}catch(e){};if(!r.ok||j.ok===false)throw new Error(j.error||('HTTP '+r.status));return j}");
  html += F("async function loadMotions(){const c=await getJson('/motions');const ul=document.getElementById('motionList');ul.innerHTML='';(c.motions||[]).forEach(m=>{const li=document.createElement('li');li.textContent=m.name+' ('+m.frames+'f loop@'+m.loopStartFrame+')'+(m.active?' *active*':'');li.style.cursor='pointer';li.onclick=()=>{document.getElementById('motionName').value=m.name;loadMotionJson()};ul.appendChild(li)});showStatus('Motions: '+c.count+(c.psram?' PSRAM':''))}");
  html += F("async function loadMotionJson(){const n=motionName();const r=await fetch('/motion?name='+encodeURIComponent(n)+'&_='+Date.now(),{cache:'no-store'});const j=await r.json();document.getElementById('motionJson').value=JSON.stringify(j,null,2);showStatus('Loaded '+n)}");
  html += F("async function uploadMotion(){const n=motionName();const body=document.getElementById('motionJson').value;const r=await fetch('/upload?name='+encodeURIComponent(n),{method:'POST',headers:{'Content-Type':'application/json'},body});let j={};try{j=await r.json()}catch(e){};if(!r.ok||j.ok===false)throw new Error(j.error||('HTTP '+r.status));showStatus('Uploaded '+n+' '+j.frames+'f');await loadMotions()}");
  html += F("async function playMotion(){const n=motionName();const j=await getJson('/play?name='+encodeURIComponent(n));showStatus('Play '+n+' '+(j.pca?'OK':'NG'),j.pca)}");
  html += F("async function startMotionLoop(){const n=motionName();const j=await getJson('/loop/start?name='+encodeURIComponent(n));showStatus('Loop '+n+' '+(j.pca?'ON':'NG'),j.pca)}");
  html += F("async function stopWalkLoop(){try{await getJson('/walk/stop');showStatus('Loop stopped')}catch(e){showStatus(e.message,false)}}");
  html += F("async function setPulseRange(){const min=document.getElementById('minUs').value;const max=document.getElementById('maxUs').value;try{const j=await getJson('/range?min='+min+'&max='+max);showStatus('Range '+j.min+'-'+j.max+'us')}catch(e){showStatus(e.message,false)}}");
  html += F("async function playSit(){const j=await getJson('/sit/play');showStatus('Sit '+(j.pca?'OK':'NG'),j.pca)}");
  html += F("async function playStand(){const j=await getJson('/stand');showStatus('Stand '+(j.pca?'OK':'NG'),j.pca)}");
  html += F("async function loadSitJson(){document.getElementById('motionName').value='sit';await loadMotionJson()}");
  html += F("async function pollStatus(){try{const s=await getJson('/status');if(s.walking)showStatus('Loop '+s.activeName+' frame '+(s.loopIndex+1)+' '+(s.pca?'PCA OK':'PCA NG'),s.pca)}catch(e){}}");
  html += F("window.addEventListener('load',async()=>{try{await loadMotions();await loadMotionJson();setInterval(pollStatus,1000)}catch(e){showStatus(e.message,false)}});");
  html += F("</script></div></body></html>");
  return html;
}

static void handleRoot() {
  sendNoCache(200, "text/html", buildIndexHtml());
}

static bool applyServoPulseRange(uint16_t minUs, uint16_t maxUs) {
  if (minUs >= maxUs) return false;
  servoMinUs = minUs;
  servoMaxUs = maxUs;
  if (!pcaReady) return true;

  for (uint8_t ch = 0; ch < 16; ch++) {
    pwm.setServoUs(ch, servoMinUs, servoMaxUs);
  }
  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    const uint16_t micros = angleToMicros(servoAngles[ch]);
    writeServoMicros(ch, micros);
    servoPulseUs[ch] = micros;
  }
  return true;
}

static void saveServoRangeToFs() {
  if (!LittleFS.exists("/cfg")) LittleFS.mkdir("/cfg");
  File f = LittleFS.open("/cfg/range.json", "w");
  if (!f) return;
  f.printf("{\"min\":%u,\"max\":%u}", servoMinUs, servoMaxUs);
  f.close();
}

static void loadServoRangeFromFs() {
  if (!LittleFS.exists("/cfg/range.json")) return;
  File f = LittleFS.open("/cfg/range.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(128);
  if (deserializeJson(doc, f.readString()) == DeserializationError::Ok) {
    const uint16_t minUs = constrain(doc["min"] | DEFAULT_SERVO_MIN_US, 400, 1500);
    const uint16_t maxUs = constrain(doc["max"] | DEFAULT_SERVO_MAX_US, 1500, 2600);
    if (minUs < maxUs) applyServoPulseRange(minUs, maxUs);
  }
  f.close();
  Serial.printf("[Servo] range loaded %u-%u us\n", servoMinUs, servoMaxUs);
}

static void handleRange() {
  if (!server.hasArg("min") || !server.hasArg("max")) {
    String body = "{\"ok\":true,\"min\":" + String(servoMinUs) + ",\"max\":" + String(servoMaxUs) + "}";
    sendNoCache(200, "application/json", body);
    return;
  }

  const uint16_t nextMin = constrain(server.arg("min").toInt(), 400, 1500);
  const uint16_t nextMax = constrain(server.arg("max").toInt(), 1500, 2600);
  if (nextMin >= nextMax) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"min must be smaller than max\"}");
    return;
  }
  if (!applyServoPulseRange(nextMin, nextMax)) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"invalid range\"}");
    return;
  }
  saveServoRangeToFs();
  String body = "{\"ok\":true,\"min\":" + String(servoMinUs) + ",\"max\":" + String(servoMaxUs) + "}";
  drawHome("Range " + String(servoMinUs) + "-" + String(servoMaxUs) + " us");
  Serial.printf("[Servo] pulse range -> %u-%u us (saved)\n", servoMinUs, servoMaxUs);
  sendNoCache(200, "application/json", body);
}

static void handleSequenceGet() {
  const String name = motionNameFromRequest(DEFAULT_WALK_NAME);
  sendNoCache(200, "application/json", motionStoreBuildJson(name.c_str()));
}

static void handleSequencePost() {
  const String body = server.arg("plain");
  if (body.length() == 0) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }
  const String name = motionNameFromRequest(DEFAULT_WALK_NAME);
  String error;
  if (!motionStoreUpload(name.c_str(), body, error)) {
    error.replace("\\", "\\\\");
    error.replace("\"", "\\\"");
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"" + error + "\"}");
    return;
  }
  motionStoreSetActive(name.c_str());
  MotionPlayback pb = {};
  motionStoreGetByName(name.c_str(), pb);
  sendNoCache(200, "application/json",
                "{\"ok\":true,\"name\":\"" + name + "\",\"frames\":" + String(pb.frameCount) + "}");
}

static void handleUpload() {
  const String body = server.arg("plain");
  if (body.length() == 0) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }
  if (!server.hasArg("name")) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"missing name query\"}");
    return;
  }
  const String name = server.arg("name");
  Serial.printf("[Motion] upload '%s' %u bytes\n", name.c_str(), body.length());
  String error;
  if (!motionStoreUpload(name.c_str(), body, error)) {
    error.replace("\\", "\\\\");
    error.replace("\"", "\\\"");
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"" + error + "\"}");
    return;
  }
  motionStoreSetActive(name.c_str());
  MotionPlayback pb = {};
  motionStoreGetByName(name.c_str(), pb);
  drawHome("Uploaded " + name);
  sendNoCache(200, "application/json",
                "{\"ok\":true,\"name\":\"" + name + "\",\"frames\":" + String(pb.frameCount) +
                    ",\"loopStart\":" + String(pb.loopStartFrame) + "}");
}

static void handleMotions() {
  sendNoCache(200, "application/json", motionStoreBuildCatalogJson());
}

static void handleMotionGet() {
  if (!server.hasArg("name")) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
    return;
  }
  sendNoCache(200, "application/json", motionStoreBuildJson(server.arg("name").c_str()));
}

static void handlePlaySequence() {
  String nameArg;
  const char* namePtr = nullptr;
  if (server.hasArg("name")) {
    nameArg = server.arg("name");
    namePtr = nameArg.c_str();
  }
  const MotionPlayback pb = resolveMotionByArg(namePtr);
  const bool ok = playMotionOnce(pb, "play");
  sendNoCache(200, "application/json", ok ? "{\"ok\":true,\"pca\":true}" : "{\"ok\":true,\"pca\":false}");
}

static void handleWalkStart() {
  String nameArg;
  const char* namePtr = nullptr;
  if (server.hasArg("name")) {
    nameArg = server.arg("name");
    namePtr = nameArg.c_str();
  }
  const MotionPlayback pb = resolveMotionByArg(namePtr);
  if (pb.valid && namePtr) motionStoreSetActive(namePtr);
  startWalkLoopFor(pb);
  sendNoCache(200, "application/json", pcaReady && pb.valid ? "{\"ok\":true,\"pca\":true,\"loop\":true}"
                                                               : "{\"ok\":true,\"pca\":false}");
}

static void handleWalkStop() {
  stopWalkLoop();
  if (pcaReady) {
    servoInterpActive = false;
    applyDefaultStance();
    isSitting = false;
  }
  drawHome("Walk loop OFF");
  Serial.println("[Walk] loop stopped");
  sendNoCache(200, "application/json", "{\"ok\":true}");
}

static void handleSitSequenceGet() {
  sendNoCache(200, "application/json", motionStoreBuildJson(DEFAULT_SIT_NAME));
}

static void handleSitPlay() {
  const bool ok = playSitSequence();
  sendNoCache(200, "application/json", ok ? "{\"ok\":true,\"pca\":true,\"sitting\":true}" : "{\"ok\":true,\"pca\":false}");
}

static void handleStand() {
  const bool ok = playStandSequence();
  sendNoCache(200, "application/json", ok ? "{\"ok\":true,\"pca\":true,\"sitting\":false}" : "{\"ok\":true,\"pca\":false}");
}

static void handleAnglesPost() {
  const String body = server.arg("plain");
  if (body.length() == 0) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }

  DynamicJsonDocument doc(512);
  const DeserializationError jsonError = deserializeJson(doc, body);
  if (jsonError) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"invalid JSON\"}");
    return;
  }

  JsonArray anglesArr = doc["angles"].as<JsonArray>();
  if (anglesArr.isNull() || anglesArr.size() != SEQUENCE_CHANNELS) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"angles must be 8 values\"}");
    return;
  }

  uint8_t angles[SEQUENCE_CHANNELS];
  for (uint8_t ch = 0; ch < SEQUENCE_CHANNELS; ch++) {
    angles[ch] = constrain(anglesArr[ch] | 90, 0, 180);
  }

  stopWalkLoop();
  startServoTransition(angles, 100);
  sendNoCache(200, "application/json", pcaReady ? "{\"ok\":true,\"pca\":true}" : "{\"ok\":true,\"pca\":false}");
}

static void handleStatus() {
  String body = "{\"walking\":";
  body += walkLoopActive ? "true" : "false";
  body += ",\"pca\":";
  body += pcaReady ? "true" : "false";
  body += ",\"loopIndex\":";
  body += walkLoopIndex;
  body += ",\"activeName\":\"";
  body += motionStoreGetActiveName();
  body += "\",\"frameCount\":";
  body += loopMotion.valid ? loopMotion.frameCount : 0;
  body += ",\"angles\":[";
  for (uint8_t i = 0; i < 8; i++) {
    body += servoAngles[i];
    if (i < 7) body += ',';
  }
  body += "]}";
  sendNoCache(200, "application/json", body);
}

static void handleFrame() {
  if (!server.hasArg("idx")) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"missing idx\"}");
    return;
  }
  String nameArg;
  const char* namePtr = nullptr;
  if (server.hasArg("name")) {
    nameArg = server.arg("name");
    namePtr = nameArg.c_str();
  }
  const MotionPlayback pb = resolveMotionByArg(namePtr);
  const int idx = server.arg("idx").toInt();
  if (idx < 0 || !pb.valid || idx >= (int)pb.frameCount) {
    sendNoCache(400, "application/json", "{\"ok\":false,\"error\":\"idx out of range\"}");
    return;
  }
  const bool ok = applyMotionFrame(pb, (uint16_t)idx);
  drawHome("Frame " + String(idx + 1));
  sendNoCache(200, "application/json", ok ? "{\"ok\":true,\"pca\":true}" : "{\"ok\":true,\"pca\":false}");
}

static void connectWiFi() {
  if (WIFI_SSID[0] == '\0') {
    drawStatus("WiFi not configured", "create local_config.h");
    Serial.println("[WiFi] skipped: create walk_only_sample/src/local_config.h");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  drawStatus("WiFi connecting", WIFI_SSID);
  Serial.printf("[WiFi] connecting to %s\n", WIFI_SSID);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    drawStatus("WiFi failed", "check SSID/PW");
    Serial.println("[WiFi] failed");
    return;
  }

  const IPAddress ip = WiFi.localIP();
  snprintf(localIpText, sizeof(localIpText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  drawHome();
  Serial.printf("[WiFi] connected: %s\n", localIpText);
}

static void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/range", HTTP_GET, handleRange);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/angles", HTTP_POST, handleAnglesPost);
  server.on("/upload", HTTP_POST, handleUpload);
  server.on("/motions", HTTP_GET, handleMotions);
  server.on("/motion", HTTP_GET, handleMotionGet);
  server.on("/sequence", HTTP_GET, handleSequenceGet);
  server.on("/sequence", HTTP_POST, handleSequencePost);
  server.on("/play", HTTP_GET, handlePlaySequence);
  server.on("/walk/start", HTTP_GET, handleWalkStart);
  server.on("/walk/stop", HTTP_GET, handleWalkStop);
  server.on("/loop/start", HTTP_GET, handleWalkStart);
  server.on("/loop/stop", HTTP_GET, handleWalkStop);
  server.on("/sit/sequence", HTTP_GET, handleSitSequenceGet);
  server.on("/sit/play", HTTP_GET, handleSitPlay);
  server.on("/stand", HTTP_GET, handleStand);
  server.on("/frame", HTTP_GET, handleFrame);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
      server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
      server.send(204);
    } else {
      sendNoCache(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    }
  });
  server.begin();
  Serial.println("[HTTP] server started");
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  cfg.external_speaker.atomic_echo = true;

  Serial.begin(115200);
  M5.begin(cfg);
  M5.Display.setRotation(DISPLAY_ROTATION);
  M5.Display.setTextDatum(top_left);

  drawStatus("PCA9685 init", "SDA=G1 SCL=G2");
  Serial.println("[Boot] PCA9685 servo sweep test");
  Serial.printf("[I2C] SDA=GPIO%d SCL=GPIO%d addr=0x%02X\n", I2C_SDA, I2C_SCL, PCA9685_ADDR);

  pcaReady = pcaBegin();
  if (!pcaReady) {
    drawStatus("PCA9685 NG", "Check wiring/power");
    Serial.println("[PCA9685] init failed");
    return;
  }

  Serial.println("[PCA9685] init OK");
  applyDefaultStance();
  motionStoreInit();
  loadServoRangeFromFs();
  motionStoreSeedDefaultsIfEmpty();
  connectWiFi();
  setupWebServer();
}

void loop() {
  server.handleClient();
  M5.update();
  tickPcaWatchdog();
  tickServoInterpolation();
  updateWalkLoop();

  drawFaceLoop();

  if (M5.BtnA.wasPressed()) {
    if (walkLoopActive) {
      stopWalkLoop();
      if (pcaReady) {
        servoInterpActive = false;
        applyDefaultStance();
        isSitting = false;
      }
      drawHome("Walk loop OFF");
      Serial.println("[Walk] BtnA: loop stopped");
    } else {
      startWalkLoop();
    }
  }

  if (M5.BtnB.wasPressed()) {
    if (isSitting) {
      playStandSequence();
      Serial.println("[Sit] BtnB: stand");
    } else {
      playSitSequence();
      Serial.println("[Sit] BtnB: sit");
    }
  }

  delay(10);
}

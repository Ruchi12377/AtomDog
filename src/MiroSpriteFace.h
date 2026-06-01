#pragma once

#include <M5Unified.h>
#include <Avatar.h>
#include <freertos/semphr.h>

class MiroSpriteFace final : public m5avatar::Face {
 public:
  MiroSpriteFace() : m5avatar::Face(), _canvas(&M5.Display) {}

  static void setDisplayMutex(SemaphoreHandle_t mutex) { s_display_mutex = mutex; }

  void forceRedraw() { _redrawPending = true; }

  void draw(m5avatar::DrawContext* ctx) override {
    _eyeOpenRatio = ctx->getEyeOpenRatio();
    _mouthOpenRatio = ctx->getMouthOpenRatio();
    _expression = ctx->getExpression();
    _breath = ctx->getBreath();
    _gazeH = ctx->getGaze().getHorizontal();
    _gazeV = ctx->getGaze().getVertical();
    _speechText = ctx->getspeechText();

    _redrawPending = true;
  }

  void flushToDisplay() {
    if (!_redrawPending) return;
    if (!s_display_mutex) return;
    if (xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    _redrawPending = false;

    // Use double buffering with a local canvas to avoid flickering
    if (!_canvasCreated) {
      _canvas.setColorDepth(16); // 16-bit color is fast and fits in heap easily
      _canvas.createSprite(128, 128);
      _canvasCreated = true;
    }

    _canvas.fillScreen(TFT_BLACK);

    float breathOffset = _breath * 1.5f;
    float gazeX = _gazeH * 2.0f;
    float gazeY = _gazeV * 2.0f;

    // Left Eye
    int32_t lx = 38 + (int32_t)gazeX;
    int32_t ly = 52 + (int32_t)(gazeY + breathOffset);
    int32_t r = 8;
    drawEye(&_canvas, lx, ly, r, true, _eyeOpenRatio, _expression);

    // Right Eye
    int32_t rx = 90 + (int32_t)gazeX;
    int32_t ry = 52 + (int32_t)(gazeY + breathOffset);
    drawEye(&_canvas, rx, ry, r, false, _eyeOpenRatio, _expression);

    // Mouth
    int32_t mx = 64;
    int32_t my = 82 + (int32_t)breathOffset;
    drawMouth(&_canvas, mx, my, _mouthOpenRatio, _expression);

    // Speech Text
    if (!_speechText.isEmpty()) {
      _canvas.setFont(&fonts::lgfxJapanGothic_12);
      _canvas.setTextColor(TFT_WHITE, TFT_BLACK);
      _canvas.setTextSize(1.0f);
      _canvas.setTextDatum(lgfx::datum_t::bottom_center);
      _canvas.drawString(_speechText.c_str(), 64, 120);
    }

    // Push the complete frame to screen at once (flicker-free!)
    M5.Display.startWrite();
    _canvas.pushSprite(&M5.Display, 0, 0);
    M5.Display.endWrite();

    xSemaphoreGive(s_display_mutex);
  }

 private:
  static SemaphoreHandle_t s_display_mutex;

  M5Canvas _canvas;
  bool _canvasCreated = false;

  volatile bool _redrawPending = false;
  float _eyeOpenRatio = 1.0f;
  float _mouthOpenRatio = 0.0f;
  m5avatar::Expression _expression = m5avatar::Expression::Neutral;
  float _breath = 0.0f;
  float _gazeH = 0.0f;
  float _gazeV = 0.0f;
  String _speechText;

  void drawEye(M5Canvas* canvas, int32_t x, int32_t y, int32_t r, bool isLeft, float openRatio, m5avatar::Expression exp) {
    if (openRatio <= 0.0f || exp == m5avatar::Expression::Sleepy) {
      int32_t h = (exp == m5avatar::Expression::Sleepy) ? 2 : 3;
      canvas->fillRect(x - r, y - h/2, r * 2, h, TFT_WHITE);
      return;
    }

    canvas->fillCircle(x, y, r, TFT_WHITE);

    if (exp == m5avatar::Expression::Happy) {
      canvas->fillCircle(x, y, (int32_t)(r / 1.5f), TFT_BLACK);
      canvas->fillRect(x - r - 2, y, r * 2 + 4, r + 2, TFT_BLACK);
    }
    else if (exp == m5avatar::Expression::Angry) {
      if (isLeft) {
        canvas->fillTriangle(x - r, y - r, x + r, y - r, x + r, y, TFT_BLACK);
      } else {
        canvas->fillTriangle(x - r, y - r, x + r, y - r, x - r, y, TFT_BLACK);
      }
    }
    else if (exp == m5avatar::Expression::Sad) {
      if (isLeft) {
        canvas->fillTriangle(x - r, y - r, x + r, y - r, x - r, y, TFT_BLACK);
      } else {
        canvas->fillTriangle(x - r, y - r, x + r, y - r, x + r, y, TFT_BLACK);
      }
    }
  }

  void drawMouth(M5Canvas* canvas, int32_t x, int32_t y, float openRatio, m5avatar::Expression exp) {
    int32_t minWidth = 34;
    int32_t maxWidth = 48;
    int32_t minHeight = 3;
    int32_t maxHeight = 28;

    if (exp == m5avatar::Expression::Happy) {
      int32_t w = 34;
      int32_t h = 12;
      canvas->fillEllipse(x, y, w/2, h, TFT_WHITE);
      canvas->fillRect(x - w/2 - 1, y - h - 1, w + 2, h + 1, TFT_BLACK);
      return;
    }

    int32_t h = minHeight + (int32_t)((maxHeight - minHeight) * openRatio);
    int32_t w = minWidth + (int32_t)((maxWidth - minWidth) * (1.0f - openRatio));

    if (h <= 3) {
      canvas->fillRect(x - w / 2, y - 1, w, 3, TFT_WHITE);
    } else {
      canvas->fillRoundRect(x - w / 2, y - h / 2, w, h, 3, TFT_WHITE);
    }
  }
};

SemaphoreHandle_t MiroSpriteFace::s_display_mutex = nullptr;

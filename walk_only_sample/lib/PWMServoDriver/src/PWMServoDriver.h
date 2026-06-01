#pragma once

#include <Arduino.h>
#include <Wire.h>

#define DEFAULT_SERVO_MICROSEC_MIN 439
#define DEFAULT_SERVO_MICROSEC_MAX 2661

#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_LED0_ON_L 0x06
#define PCA9685_PRESCALE 0xFE

#define MODE1_SLEEP 0x10
#define MODE1_AI 0x20
#define MODE1_RESTART 0x80
#define MODE2_OUTDRV 0x04

class PWMServoDriver {
 public:
  explicit PWMServoDriver(uint8_t addr = 0x40);
  bool begin(bool initWire = false);
  void reset();
  void sleep();
  void wakeup();
  void setPWMFreq(float freq);
  double usPerBit() const;
  uint8_t setPWM(uint8_t num, uint16_t on, uint16_t off);
  double servoMicroSec(uint8_t n, double us);
  double servoAngle(uint8_t n, double ang);
  void setServoUs(uint8_t n, double min, double max);
  void setServoPWM(uint8_t n, double pulse);
  void setServoPulse(uint8_t n, double us);
  void setServoAngle(uint8_t n, double angle);

 private:
  uint8_t _i2caddr;
  float _freq = 50.0f;
  double _servoUsMin[16];
  double _servoUsMax[16];

  uint8_t read8(uint8_t addr);
  void write8(uint8_t addr, uint8_t d);
};

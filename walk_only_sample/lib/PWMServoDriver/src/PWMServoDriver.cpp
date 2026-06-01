#include "PWMServoDriver.h"

PWMServoDriver::PWMServoDriver(uint8_t addr) : _i2caddr(addr) {
  for (uint8_t i = 0; i < 16; i++) {
    _servoUsMin[i] = DEFAULT_SERVO_MICROSEC_MIN;
    _servoUsMax[i] = DEFAULT_SERVO_MICROSEC_MAX;
  }
}

bool PWMServoDriver::begin(bool initWire) {
  if (initWire) {
    Wire.begin();
  }

  Wire.beginTransmission(_i2caddr);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  reset();
  write8(PCA9685_MODE2, MODE2_OUTDRV);
  return true;
}

void PWMServoDriver::reset() {
  write8(PCA9685_MODE1, MODE1_RESTART);
  delay(10);
}

void PWMServoDriver::sleep() {
  uint8_t awake = read8(PCA9685_MODE1);
  uint8_t sleep = awake | MODE1_SLEEP;
  write8(PCA9685_MODE1, sleep);
  delay(5);
}

void PWMServoDriver::wakeup() {
  uint8_t sleep = read8(PCA9685_MODE1);
  uint8_t wakeup = sleep & ~MODE1_SLEEP;
  write8(PCA9685_MODE1, wakeup);
}

void PWMServoDriver::setPWMFreq(float freq) {
  _freq = freq;
  float prescaleval = 25000000.0f;
  prescaleval /= 4096.0f;
  prescaleval /= freq;
  prescaleval -= 1.0f;
  uint8_t prescale = floorf(prescaleval + 0.5f);

  uint8_t oldmode = read8(PCA9685_MODE1);
  uint8_t newmode = (oldmode & ~MODE1_RESTART) | MODE1_SLEEP;
  write8(PCA9685_MODE1, newmode);
  write8(PCA9685_PRESCALE, prescale);
  write8(PCA9685_MODE1, oldmode);
  delay(5);
  write8(PCA9685_MODE1, oldmode | MODE1_RESTART | MODE1_AI);
}

double PWMServoDriver::usPerBit() const {
  return 1000000.0 / _freq / 4096.0;
}

uint8_t PWMServoDriver::setPWM(uint8_t num, uint16_t on, uint16_t off) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(PCA9685_LED0_ON_L + 4 * num);
  Wire.write(on);
  Wire.write(on >> 8);
  Wire.write(off);
  Wire.write(off >> 8);
  return Wire.endTransmission();
}

double PWMServoDriver::servoMicroSec(uint8_t n, double us) {
  double pulse = us / usPerBit();
  if (pulse < 0) pulse = 0;
  if (pulse > 4095) pulse = 4095;
  setPWM(n, 0, (uint16_t)(pulse + 0.5));
  return pulse;
}

double PWMServoDriver::servoAngle(uint8_t n, double ang) {
  if (n >= 16) return 0;
  if (ang < 0) ang = 0;
  if (ang > 180) ang = 180;
  double us = _servoUsMin[n] + (_servoUsMax[n] - _servoUsMin[n]) * (ang / 180.0);
  return servoMicroSec(n, us);
}

void PWMServoDriver::setServoUs(uint8_t n, double min, double max) {
  if (n >= 16) return;
  _servoUsMin[n] = min;
  _servoUsMax[n] = max;
}

void PWMServoDriver::setServoPWM(uint8_t n, double pulse) {
  setPWM(n, 0, (uint16_t)pulse);
}

void PWMServoDriver::setServoPulse(uint8_t n, double us) {
  servoMicroSec(n, us);
}

void PWMServoDriver::setServoAngle(uint8_t n, double angle) {
  servoAngle(n, angle);
}

uint8_t PWMServoDriver::read8(uint8_t addr) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(addr);
  Wire.endTransmission();

  Wire.requestFrom((uint8_t)_i2caddr, (uint8_t)1);
  return Wire.read();
}

void PWMServoDriver::write8(uint8_t addr, uint8_t d) {
  Wire.beginTransmission(_i2caddr);
  Wire.write(addr);
  Wire.write(d);
  Wire.endTransmission();
}

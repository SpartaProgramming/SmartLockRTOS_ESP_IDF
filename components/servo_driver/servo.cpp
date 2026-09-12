#include "servo.hpp"

#include <esp_log.h>

static const char *TAG = "ServoMotor";

namespace {
constexpr ledc_timer_t kTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_0;
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_bit_t kDutyResolution = LEDC_TIMER_14_BIT;
constexpr uint32_t kFrequencyHz = 50; // standardowe 50Hz dla serw hobbystycznych
constexpr uint32_t kMaxDuty = (1u << 14) - 1; // zgodnie z kDutyResolution
constexpr int kPeriodUs = 1000000 / static_cast<int>(kFrequencyHz); // 20000us

constexpr int kDefaultOpenAngle = 160;  // jak w starym DoorLock::openLatch()
constexpr int kDefaultCloseAngle = 70;  // jak w starym DoorLock::closeLatch()
} // namespace

ServoMotor::ServoMotor(gpio_num_t pin, int minPulseUs, int maxPulseUs)
    : _pin(pin), _minPulseUs(minPulseUs), _maxPulseUs(maxPulseUs),
      _openAngle(kDefaultOpenAngle), _closeAngle(kDefaultCloseAngle),
      _currentAngle(-1), _attached(false) {}

ServoMotor::~ServoMotor() { end(); }

esp_err_t ServoMotor::begin() {
  ledc_timer_config_t timerCfg = {};
  timerCfg.speed_mode = kSpeedMode;
  timerCfg.timer_num = kTimer;
  timerCfg.duty_resolution = kDutyResolution;
  timerCfg.freq_hz = kFrequencyHz;
  timerCfg.clk_cfg = LEDC_AUTO_CLK;

  esp_err_t err = ledc_timer_config(&timerCfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad konfiguracji timera LEDC: %d", err);
    return err;
  }

  ledc_channel_config_t chCfg = {};
  chCfg.gpio_num = _pin;
  chCfg.speed_mode = kSpeedMode;
  chCfg.channel = kChannel;
  chCfg.timer_sel = kTimer;
  chCfg.duty = 0;
  chCfg.hpoint = 0;

  err = ledc_channel_config(&chCfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad konfiguracji kanalu LEDC: %d", err);
    return err;
  }

  _attached = true;
  close(); // bezpieczny stan poczatkowy - zamkniete

  ESP_LOGI(TAG, "ServoMotor (GPIO%d) zainicjalizowany", static_cast<int>(_pin));
  return ESP_OK;
}

void ServoMotor::end() {
  if (!_attached) {
    return;
  }
  ledc_stop(kSpeedMode, kChannel, 0);
  _attached = false;
}

void ServoMotor::writePulseUs(int pulseUs) {
  if (!_attached) {
    return;
  }
  uint32_t duty = static_cast<uint32_t>(
      (static_cast<int64_t>(pulseUs) * kMaxDuty) / kPeriodUs);
  ledc_set_duty(kSpeedMode, kChannel, duty);
  ledc_update_duty(kSpeedMode, kChannel);
}

void ServoMotor::writeAngle(int angle) {
  if (angle < 0) {
    angle = 0;
  }
  if (angle > 180) {
    angle = 180;
  }

  int pulseUs = _minPulseUs + ((_maxPulseUs - _minPulseUs) * angle) / 180;
  writePulseUs(pulseUs);
  _currentAngle = angle;
}

void ServoMotor::open() { writeAngle(_openAngle); }

void ServoMotor::close() { writeAngle(_closeAngle); }

void ServoMotor::setOpenAngle(int angle) { _openAngle = angle; }

void ServoMotor::setCloseAngle(int angle) { _closeAngle = angle; }

int ServoMotor::currentAngle() const { return _currentAngle; }

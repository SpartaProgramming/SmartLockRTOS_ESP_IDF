#include "pressure_sensor.hpp"

#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

static const char *TAG = "PressureSensor";

namespace {
constexpr int kPollIntervalMs = 100;
constexpr int kOversampleCount = 8;
// TODO(kalibracja-czujnika): to tylko punkt startowy - zaleznie od
// konkretnego czujnika/przetwornika nacisku (rozny zakres napiecia
// wyjsciowego), sprawdz realne odczyty przez readMillivolts() i dostosuj
// przez setThresholdMv().
constexpr int kDefaultThresholdMv = 1650;
} // namespace

PressureSensor::PressureSensor(gpio_num_t pin)
    : _pin(pin), _unit(ADC_UNIT_1), _channel(ADC_CHANNEL_0),
      _adcHandle(nullptr), _caliHandle(nullptr), _caliEnabled(false),
      _taskHandle(nullptr), _notifyTask(nullptr), _stateMutex(nullptr),
      _lastRaw(0), _lastMv(0), _isPressed(false),
      _thresholdMv(kDefaultThresholdMv), _pressedBelowThreshold(true) {
  _stateMutex = xSemaphoreCreateMutex();
}

PressureSensor::~PressureSensor() {
  end();
  if (_stateMutex) {
    vSemaphoreDelete(_stateMutex);
  }
}

void PressureSensor::initCalibration() {
  adc_cali_handle_t handle = nullptr;
  esp_err_t err = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  adc_cali_curve_fitting_config_t curveCfg = {};
  curveCfg.unit_id = _unit;
  curveCfg.chan = _channel;
  curveCfg.atten = ADC_ATTEN_DB_12;
  curveCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  err = adc_cali_create_scheme_curve_fitting(&curveCfg, &handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  adc_cali_line_fitting_config_t lineCfg = {};
  lineCfg.unit_id = _unit;
  lineCfg.atten = ADC_ATTEN_DB_12;
  lineCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  err = adc_cali_create_scheme_line_fitting(&lineCfg, &handle);
#endif

  if (err == ESP_OK) {
    _caliHandle = handle;
    _caliEnabled = true;
  } else {
    ESP_LOGW(TAG, "Kalibracja ADC niedostepna na tym egzemplarzu ESP32 (%d) "
                  "- uzywam przyblizonego przeliczenia raw->mV",
             err);
    _caliEnabled = false;
  }
}

int PressureSensor::rawToMv(int raw) const {
  if (_caliEnabled) {
    int mv = 0;
    if (adc_cali_raw_to_voltage(_caliHandle, raw, &mv) == ESP_OK) {
      return mv;
    }
  }
  // Przyblizenie bez kalibracji - ADC ESP32 przy ADC_ATTEN_DB_12 ma zakres
  // pelnej skali w okolicy ~3300mV (nieliniowo, stad tylko przyblizenie).
  return (raw * 3300) / 4095;
}

esp_err_t PressureSensor::begin() {
  esp_err_t err = adc_oneshot_io_to_channel(_pin, &_unit, &_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO%d nie jest prawidlowym wejsciem ADC: %d",
             static_cast<int>(_pin), err);
    return err;
  }

  adc_oneshot_unit_init_cfg_t unitCfg = {};
  unitCfg.unit_id = _unit;
  err = adc_oneshot_new_unit(&unitCfg, &_adcHandle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia jednostki ADC: %d", err);
    return err;
  }

  adc_oneshot_chan_cfg_t chanCfg = {};
  chanCfg.atten = ADC_ATTEN_DB_12;
  chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  err = adc_oneshot_config_channel(_adcHandle, _channel, &chanCfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad konfiguracji kanalu ADC: %d", err);
    adc_oneshot_del_unit(_adcHandle);
    _adcHandle = nullptr;
    return err;
  }

  initCalibration();

  _lastRaw = sampleAveragedRaw();
  _lastMv = rawToMv(_lastRaw);
  _isPressed = _pressedBelowThreshold ? (_lastMv <= _thresholdMv)
                                      : (_lastMv >= _thresholdMv);

  BaseType_t ok = xTaskCreate(taskThunk, "pressure_sensor_task", 3072, this, 5,
                              &_taskHandle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udalo sie utworzyc taska PressureSensor");
    if (_caliHandle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
      adc_cali_delete_scheme_curve_fitting(_caliHandle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
      adc_cali_delete_scheme_line_fitting(_caliHandle);
#endif
      _caliHandle = nullptr;
    }
    adc_oneshot_del_unit(_adcHandle);
    _adcHandle = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG,
           "PressureSensor (GPIO%d, ADC%d/CH%d) zainicjalizowany, odczyt "
           "poczatkowy: %d mV (raw=%d)",
           static_cast<int>(_pin), static_cast<int>(_unit) + 1,
           static_cast<int>(_channel), _lastMv, _lastRaw);
  return ESP_OK;
}

void PressureSensor::end() {
  if (_taskHandle) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }
  if (_caliHandle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_delete_scheme_curve_fitting(_caliHandle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_delete_scheme_line_fitting(_caliHandle);
#endif
    _caliHandle = nullptr;
  }
  if (_adcHandle) {
    adc_oneshot_del_unit(_adcHandle);
    _adcHandle = nullptr;
  }
}

void PressureSensor::attachNotifyTask(TaskHandle_t task) {
  _notifyTask = task;
}

int PressureSensor::readMillivolts() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  int mv = _lastMv;
  xSemaphoreGive(_stateMutex);
  return mv;
}

int PressureSensor::readRaw() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  int raw = _lastRaw;
  xSemaphoreGive(_stateMutex);
  return raw;
}

bool PressureSensor::isPressed() const {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  bool pressed = _isPressed;
  xSemaphoreGive(_stateMutex);
  return pressed;
}

void PressureSensor::setThresholdMv(int thresholdMv) {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  _thresholdMv = thresholdMv;
  xSemaphoreGive(_stateMutex);
}

void PressureSensor::setPressedBelowThreshold(bool pressedBelowThreshold) {
  xSemaphoreTake(_stateMutex, portMAX_DELAY);
  _pressedBelowThreshold = pressedBelowThreshold;
  xSemaphoreGive(_stateMutex);
}

int PressureSensor::sampleAveragedRaw() {
  int sum = 0;
  for (int i = 0; i < kOversampleCount; i++) {
    int raw = 0;
    if (adc_oneshot_read(_adcHandle, _channel, &raw) == ESP_OK) {
      sum += raw;
    }
  }
  return sum / kOversampleCount;
}

void PressureSensor::taskThunk(void *pParam) {
  static_cast<PressureSensor *>(pParam)->taskLoop();
}

void PressureSensor::taskLoop() {
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));

    int raw = sampleAveragedRaw();
    int mv = rawToMv(raw);

    bool changed = false;
    bool newPressed;
    xSemaphoreTake(_stateMutex, portMAX_DELAY);
    _lastRaw = raw;
    _lastMv = mv;
    newPressed = _pressedBelowThreshold ? (mv <= _thresholdMv)
                                        : (mv >= _thresholdMv);
    if (newPressed != _isPressed) {
      _isPressed = newPressed;
      changed = true;
    }
    xSemaphoreGive(_stateMutex);

    if (!changed) {
      continue;
    }

    ESP_LOGI(TAG, "Nacisk: %s (%d mV)", newPressed ? "wykryto" : "zwolniono",
             mv);
    if (_notifyTask) {
      xTaskNotify(_notifyTask,
                  static_cast<uint32_t>(newPressed ? PressureEvent::PRESSED
                                                   : PressureEvent::RELEASED),
                  eSetBits);
    }
  }
}

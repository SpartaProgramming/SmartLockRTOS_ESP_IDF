#include "display.hpp"

#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>

static const char *TAG = "Display";

Display::Display(spi_host_device_t host, gpio_num_t cs, gpio_num_t dc,
                 gpio_num_t rst, int width, int height)
    : _host(host), _cs(cs), _dc(dc), _rst(rst), _width(width), _height(height),
      _isOn(false), _ioHandle(nullptr), _panel(nullptr), _lvDisplay(nullptr),
      _titleLabel(nullptr), _headerLine(nullptr), _line1Label(nullptr),
      _line2Label(nullptr), _pinStarsLabel(nullptr), _popup(nullptr),
      _popupLabel(nullptr), _faceIndicatorLabel(nullptr) {}

Display::~Display() { end(); }

esp_err_t Display::begin() {
  esp_lcd_panel_io_spi_config_t ioConfig = {};
  ioConfig.dc_gpio_num = _dc;
  ioConfig.cs_gpio_num = _cs;
  ioConfig.pclk_hz = 20 * 1000 * 1000;
  ioConfig.lcd_cmd_bits = 8;
  ioConfig.lcd_param_bits = 8;
  ioConfig.spi_mode = 0;
  ioConfig.trans_queue_depth = 10;

  esp_err_t err = esp_lcd_new_panel_io_spi(_host, &ioConfig, &_ioHandle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia panel IO SPI: %d", err);
    return err;
  }

  esp_lcd_panel_dev_config_t panelConfig = {};
  panelConfig.reset_gpio_num = _rst;
  panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panelConfig.bits_per_pixel = 16;

  err = esp_lcd_new_panel_st7789(_ioHandle, &panelConfig, &_panel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad tworzenia panelu (sterownik st7789): %d", err);
    esp_lcd_panel_io_del(_ioHandle);
    _ioHandle = nullptr;
    return err;
  }

  esp_lcd_panel_reset(_panel);
  esp_lcd_panel_init(_panel);
  esp_lcd_panel_invert_color(_panel, false);

  esp_lcd_panel_set_gap(_panel, 2, 1);

  esp_lcd_panel_disp_on_off(_panel, true);

  lvgl_port_cfg_t lvglCfg = ESP_LVGL_PORT_INIT_CONFIG();
  lvglCfg.task_affinity =
      1; // rdzen 1 (APP_CPU) - rdzen 0 zarezerwowany dla WiFi
  err = lvgl_port_init(&lvglCfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Blad inicjalizacji lvgl_port: %d", err);
    esp_lcd_panel_del(_panel);
    _panel = nullptr;
    esp_lcd_panel_io_del(_ioHandle);
    _ioHandle = nullptr;
    return err;
  }

  lvgl_port_display_cfg_t dispCfg = {};
  dispCfg.io_handle = _ioHandle;
  dispCfg.panel_handle = _panel;
  dispCfg.buffer_size = static_cast<uint32_t>(_width) * 40;
  dispCfg.double_buffer = false;
  dispCfg.hres = static_cast<uint32_t>(_width);
  dispCfg.vres = static_cast<uint32_t>(_height);
  dispCfg.monochrome = false;
  dispCfg.flags.buff_dma = false;
  dispCfg.flags.sw_rotate = true;

  _lvDisplay = lvgl_port_add_disp(&dispCfg);
  if (!_lvDisplay) {
    ESP_LOGE(TAG, "Blad dodania wyswietlacza do lvgl_port");
    return ESP_FAIL;
  }

  lv_disp_set_rotation(_lvDisplay, LV_DISP_ROT_90);
  {
    int landscapeWidth = _height;
    int landscapeHeight = _width;
    _width = landscapeWidth;
    _height = landscapeHeight;
  }

  buildWidgets();

  _isOn = true;
  ESP_LOGI(TAG, "Display (ST7735S oraz LVGL) zainicjalizowany");
  return ESP_OK;
}

void Display::end() {
  if (_lvDisplay) {
    lvgl_port_remove_disp(_lvDisplay);
    _lvDisplay = nullptr;
  }
  if (_panel) {
    esp_lcd_panel_del(_panel);
    _panel = nullptr;
  }
  if (_ioHandle) {
    esp_lcd_panel_io_del(_ioHandle);
    _ioHandle = nullptr;
  }
}

void Display::wakeUp() {
  if (_isOn) {
    return;
  }
  esp_lcd_panel_disp_on_off(_panel, true);
  _isOn = true;
}

void Display::sleep() {
  if (!_isOn) {
    return;
  }
  esp_lcd_panel_disp_on_off(_panel, false);
  _isOn = false;
}

void Display::buildWidgets() {
  lvgl_port_lock(0);

  // kolejność tworzenia widgetów ma znaczenie - najpierw tło, potem napisy,
  // potem pop-up, potem widget

  lv_obj_t *scr = lv_disp_get_scr_act(_lvDisplay); // wskaźnik na aktywn ekran
  lv_obj_set_style_bg_color(
      scr, lv_color_black(),
      LV_PART_MAIN); // LV_PART_MAIN - główna część gdy widget złożony
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

  _titleLabel = lv_label_create(scr);
  lv_obj_set_pos(_titleLabel, 10, 5);
  lv_obj_set_style_text_color(_titleLabel, lv_color_hex(0x00FF00),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(_titleLabel, &lv_font_montserrat_20, LV_PART_MAIN);

  _headerLine = lv_obj_create(scr);
  lv_obj_remove_style_all(_headerLine);
  lv_obj_set_size(_headerLine, _width, 2);
  lv_obj_set_pos(_headerLine, 0, 34);
  lv_obj_set_style_bg_color(_headerLine, lv_color_hex(0x00FF00), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(_headerLine, LV_OPA_COVER, LV_PART_MAIN);

  _line1Label = lv_label_create(scr);
  lv_obj_set_pos(_line1Label, 10, 45);
  lv_obj_set_style_text_color(_line1Label, lv_color_hex(0x00FF00),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(_line1Label, &lv_font_montserrat_16, LV_PART_MAIN);

  _line2Label = lv_label_create(scr);
  lv_obj_set_pos(_line2Label, 10, 68);
  lv_obj_set_style_text_color(_line2Label, lv_color_hex(0x00FF00),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(_line2Label, &lv_font_montserrat_16, LV_PART_MAIN);

  // Szablon ekranu wpisywania PIN-u - duza czcionka na gwiazdki,
  _pinStarsLabel = lv_label_create(scr);
  lv_obj_set_pos(_pinStarsLabel, 10, 55);
  lv_obj_set_style_text_color(_pinStarsLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(_pinStarsLabel, &lv_font_montserrat_28,
                             LV_PART_MAIN);
  lv_obj_add_flag(_pinStarsLabel, LV_OBJ_FLAG_HIDDEN);

  // Pop-up - tworzony po szablonach, zeby rysowal sie nad nimi.
  _popup = lv_obj_create(scr);              // nowy obiekt
  lv_obj_remove_style_all(_popup);          // usuwa domyślne style wbudowane
  lv_obj_set_size(_popup, _width - 16, 64); // trochę mniejsze niż ekran
  lv_obj_align(_popup, LV_ALIGN_CENTER, 0, 8);
  lv_obj_set_style_bg_color(_popup, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(_popup, LV_OPA_COVER,
                          LV_PART_MAIN); // nieprzeźroczyste
  lv_obj_set_style_border_color(_popup, lv_color_hex(0xFF0000), LV_PART_MAIN);
  lv_obj_set_style_border_width(_popup, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(_popup, 6, LV_PART_MAIN);
  lv_obj_clear_flag(_popup, LV_OBJ_FLAG_SCROLLABLE);

  _popupLabel = lv_label_create(_popup);
  lv_obj_set_style_text_color(_popupLabel, lv_color_hex(0xFF0000),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(_popupLabel, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_set_style_text_align(_popupLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_center(_popupLabel);
  lv_obj_add_flag(_popup, LV_OBJ_FLAG_HIDDEN);

  // "F" tworzone na samym koncu - zawsze na wierzchu, takze nad pop-upem.
  _faceIndicatorLabel = lv_label_create(scr);
  lv_label_set_text(_faceIndicatorLabel, "F");
  lv_obj_set_style_text_color(_faceIndicatorLabel, lv_color_hex(0xFFFF00),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(_faceIndicatorLabel, &lv_font_montserrat_20,
                             LV_PART_MAIN);
  lv_obj_align(_faceIndicatorLabel, LV_ALIGN_TOP_RIGHT, -8, 5);
  lv_obj_add_flag(_faceIndicatorLabel, LV_OBJ_FLAG_HIDDEN);

  lvgl_port_unlock();
}

/**
 * @brief Odkrywa standardowy widok ekranu, ukrywa wpisywanie PINu.
 */
void Display::applyView(const char *title, const char *line1, const char *line2,
                        lv_color_t color) {
  lvgl_port_lock(0);

  lv_label_set_text(_titleLabel, title);
  lv_label_set_text(_line1Label, line1 ? line1 : "");
  lv_label_set_text(_line2Label, line2 ? line2 : "");
  lv_obj_set_style_text_color(_line1Label, color, LV_PART_MAIN);
  lv_obj_set_style_text_color(_line2Label, color, LV_PART_MAIN);

  lv_obj_clear_flag(_headerLine, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(_line1Label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(_line2Label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(_pinStarsLabel, LV_OBJ_FLAG_HIDDEN);

  lvgl_port_unlock();
}

/**
 * @brief Rysuje naglowek ekranu.
 * @param title Tytul naglowka.
 */
void Display::drawHeader(const char *title) {
  applyView(title, "", "", lv_color_hex(0x00FF00));
}

/**
 * @brief Pokazuje ekran spoczynkowy (idle) - LOCKED lub OPEN.
 * @param locked true = LOCKED, false = OPEN.
 */
void Display::showIdle(bool locked) {
  if (locked) {
    applyView("LOCKED", "Please look", "at the camera!",
              lv_color_hex(0x00FF00));
  } else {
    applyView("UNLOCKED", "Please close", "the door!", lv_color_hex(0x00FF00));
  }
}

void Display::showUserWelcome(const char *firstName, const char *lastName) {
  applyView("Welcome!", firstName, lastName, lv_color_hex(0x00FF00));
}

void Display::showPasswordInput(uint8_t length) {
  constexpr uint8_t kMaxStars = 8;
  char stars[kMaxStars + 1];
  uint8_t n = (length < kMaxStars) ? length : kMaxStars;
  for (uint8_t i = 0; i < n; i++) {
    stars[i] = '*';
  }
  stars[n] = '\0';

  lvgl_port_lock(0);

  lv_label_set_text(_titleLabel, "PIN:");
  lv_obj_add_flag(_headerLine, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(_line1Label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(_line2Label, LV_OBJ_FLAG_HIDDEN);

  lv_obj_clear_flag(_pinStarsLabel, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(_pinStarsLabel, stars);
  lv_obj_align(_pinStarsLabel, LV_ALIGN_CENTER, 0, 10);

  lvgl_port_unlock();
}

void Display::showAccessGranted() {
  applyView("Access!", "Welcome to", "your home!", lv_color_hex(0x00FF00));
}

void Display::showAccessDenied() {
  applyView("No Access!", "Try once", "again!", lv_color_hex(0xFF0000));
}

void Display::showFaceWindow() {
  applyView("Face OK", "MFA: Enter PIN", "or use card", lv_color_hex(0xFFFF00));
}

void Display::setFaceIndicator(bool visible) {
  lvgl_port_lock(0);
  if (visible) {
    lv_obj_clear_flag(_faceIndicatorLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(_faceIndicatorLabel, LV_OBJ_FLAG_HIDDEN);
  }
  lvgl_port_unlock();
}

void Display::showPopup(const char *text) {
  lvgl_port_lock(0);
  lv_label_set_text(_popupLabel, text);
  lv_obj_center(_popupLabel);
  lv_obj_clear_flag(_popup, LV_OBJ_FLAG_HIDDEN);
  lvgl_port_unlock();
}

void Display::hidePopup() {
  lvgl_port_lock(0);
  lv_obj_add_flag(_popup, LV_OBJ_FLAG_HIDDEN);
  lvgl_port_unlock();
}

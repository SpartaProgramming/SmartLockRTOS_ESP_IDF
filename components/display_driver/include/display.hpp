#pragma once

#include <cstdint>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <lvgl.h>

class Display {
public:
  explicit Display(spi_host_device_t host, gpio_num_t cs, gpio_num_t dc,
                   gpio_num_t rst, int width, int height);
  ~Display();

  Display(const Display &) = delete;
  Display &operator=(const Display &) = delete;
  Display(Display &&) = delete;
  Display &operator=(Display &&) = delete;

  esp_err_t begin();

  void end();

  void wakeUp();

  void sleep();

  void drawHeader(const char *title);
  void showIdle();
  void showUserWelcome(const char *firstName, const char *lastName);
  void showPasswordInput(uint8_t length);
  void showAccessGranted();
  void showAccessDenied();

private:
  void buildWidgets();
  void applyView(const char *title, const char *line1, const char *line2,
                 lv_color_t color);

  spi_host_device_t _host;
  gpio_num_t _cs, _dc, _rst;
  int _width, _height;
  bool _isOn;

  esp_lcd_panel_io_handle_t _ioHandle;
  esp_lcd_panel_handle_t _panel;

  lv_disp_t *_lvDisplay;
  lv_obj_t *_titleLabel;
  lv_obj_t *_headerLine;
  lv_obj_t *_line1Label;
  lv_obj_t *_line2Label;
};

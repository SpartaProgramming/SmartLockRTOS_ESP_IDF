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

  /// Ekran spoczynkowy - locked=true pokazuje stan LOCKED z promptem
  /// wpisania PINu/przylozenia karty; locked=false pokazuje stan OPEN.
  void showIdle(bool locked);

  void showUserWelcome(const char *firstName, const char *lastName);

  /// Osobny szablon ekranu wpisywania PIN-u (nie applyView()) - gorna
  /// belka pokazuje staly prompt "PIN:", a gwiazdki sa duzym, wlasnym
  /// widgetem (patrz _pinStarsLabel). length=0 czysci gwiazdki, ale
  /// zostaje na tym szablonie (nie wraca do idle).
  void showPasswordInput(uint8_t length);

  void showAccessGranted();
  void showAccessDenied();

  /// Ekran "okna wejscia" po rozpoznaniu twarzy - prompt PIN/karta.
  void showFaceWindow();

  /// Zolte "F" w prawym gornym rogu - niezalezne od szablonow ekranu
  /// (applyView()/showPasswordInput() go nie ruszaja), wiec widoczne na
  /// kazdym ekranie, dopoki trwa okno wejscia.
  void setFaceIndicator(bool visible);

  /// Pop-up nakladany na biezacy ekran (nie podmienia go) - po hidePopup()
  /// widac znow to, co bylo pod spodem. text moze zawierac '\n'.
  void showPopup(const char *text);
  void hidePopup();

private:
  void buildWidgets();
  void applyView(const char *title, const char *line1, const char *line2,
                 lv_color_t color); // zmienia treść i kolorystykę wigetów

  spi_host_device_t _host;
  gpio_num_t _cs, _dc, _rst;
  int _width, _height;
  bool _isOn; // zasilanie podświetlania

  // uchwyty sterownika panelu
  esp_lcd_panel_io_handle_t _ioHandle;
  esp_lcd_panel_handle_t _panel;

  lv_disp_t *_lvDisplay; // wskaźnik do LVGL display driver

  // Szablon ogolny
  lv_obj_t *_titleLabel;
  lv_obj_t *_headerLine;
  lv_obj_t *_line1Label;
  lv_obj_t *_line2Label;

  // Osobny szablon ekranu wpisywania PIN-u
  lv_obj_t *_pinStarsLabel;

  // Nakladki niezalezne od szablonow - patrz showPopup()/setFaceIndicator().
  lv_obj_t *_popup;
  lv_obj_t *_popupLabel;
  lv_obj_t *_faceIndicatorLabel;
};

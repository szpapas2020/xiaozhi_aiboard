/**
 * LovyanGFX 屏幕配置 - ESP32-S3 AI Board
 * 使用 include/pins.h 中的引脚定义
 * 屏幕: ST7789 1.9寸 IPS TFT 170x320
 */

#ifndef LGFX_SETUP_H
#define LGFX_SETUP_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "pins.h"

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;
  lgfx::Light_PWM    _light_instance;

public:
  LGFX(void)
  {
    { // SPI 总线
      auto cfg = _bus_instance.config();
#if defined(ESP32)
      cfg.spi_host = SPI3_HOST;  // 若 SPI2 被占用可试 SPI3
#else
      cfg.spi_host = SPI2_HOST;
#endif
      cfg.spi_mode = 0;
      cfg.freq_write = 8000000;   // 先降速到 8MHz 保证通信
      cfg.freq_read  = 8000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_LCD_MISO;
      cfg.pin_dc   = PIN_LCD_DC;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // 面板 ST7789 1.9寸 170x320（显存多为 320x320，需 X 偏移）
      auto cfg = _panel_instance.config();

      cfg.pin_cs   = PIN_LCD_CS;
      cfg.pin_rst  = PIN_LCD_RST;
      cfg.pin_busy = -1;

      cfg.memory_width  = 170;
      cfg.memory_height = 320;
      cfg.panel_width   = LCD_WIDTH;
      cfg.panel_height  = LCD_HEIGHT;
      cfg.offset_x      = -35;   // 若画面偏左/右可改为 0 或 75 试
      cfg.offset_y      = 0;
      cfg.offset_rotation = 0;
      cfg.readable      = false;
      cfg.invert        = true;
      cfg.rgb_order     = false;  // false=红→青时试 true；true=红→黄时改回 false
      // cfg.dlen_16bit    = true;   // 部分屏需 16bit 对齐/字节序
      cfg.bus_shared    = false;

      _panel_instance.config(cfg);
    }

    { // 背光 PWM
      auto cfg = _light_instance.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq   = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

#endif // LGFX_SETUP_H

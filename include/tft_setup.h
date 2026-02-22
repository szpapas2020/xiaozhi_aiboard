/**
 * TFT_eSPI 配置文件 - 针对 ESP32-S3 AI Board
 * 重新定义默认引脚以匹配硬件
 */

#ifndef TFT_SETUP_H
#define TFT_SETUP_H

// 强制使用 ST7789 驱动
#define ST7789_DRIVER

// 引脚定义 - 覆盖默认配置
#define TFT_MOSI    10  // SDA
#define TFT_SCLK    12  // SCL
#define TFT_CS      13  // Chip select control pin
#define TFT_DC      11  // Data Command control pin
#define TFT_RST     14  // Reset pin

// 背光控制
#define TFT_BL      3
#define TFT_BACKLIGHT_ON HIGH

// 屏幕分辨率
#define TFT_WIDTH   170
#define TFT_HEIGHT  320

// 频率设置
#define SPI_FREQUENCY  40000000  // 40MHz

// 取消注释以启用
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF4 to FF48

#define SMOOTH_FONT

#endif // TFT_SETUP_H

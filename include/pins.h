/**
 * ESP32-S3 AI Board 引脚定义
 * 开发板: ESP32-S3 AI Board
 * 芯片: ESP32-S3-WROOM-1 N16R8
 */

#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

// ============ 按键引脚 ============
#define PIN_BTN_RST 0     // RST 复位按键 (内置)
#define PIN_BTN_BOOT 9    // BOOT 按键 - AI对话打断
#define PIN_BTN_VOL_UP 40 // VOL+ 音量加
#define PIN_BTN_VOL_DN 39 // VOL- 音量减

// ============ WS2812 RGB LED ============
#define PIN_LED_WS2812 48 // WS2812 彩灯控制引脚
#define LED_COUNT 1       // LED 数量

// ============ LCD 屏幕 ============
// ST7789 1.9寸 IPS TFT 170x320
#define PIN_LCD_SCLK 12 // SPI 时钟
#define PIN_LCD_MOSI 10 // SPI MOSI
#define PIN_LCD_MISO -1 // SPI MISO (不使用，设为-1)
#define PIN_LCD_DC 11   // 数据/命令选择
#define PIN_LCD_CS 13   // 片选
#define PIN_LCD_RST 14  // 复位
#define PIN_LCD_BL 3    // 背光

#define LCD_WIDTH 170
#define LCD_HEIGHT 320
#define LCD_ROTATION 1 // 屏幕旋转方向

// ============ 音频 ============
#define PIN_I2S_BCLK 15 // I2S 位时钟
#define PIN_I2S_LRCK 16 // I2S 左右声道时钟
#define PIN_I2S_DIN 7   // I2S 数据输出 (DAC)

// ============ 麦克风 ============
#define PIN_MIC_SCK 5  // 麦克风时钟
#define PIN_MIC_DATA 6 // 麦克风数据
#define PIN_MIC_WS 4   // 麦克风电源控制 (如果有的话)

// ============ 扩展接口 ============
// I2C 接口
#define PIN_I2C_SDA 41 // I2C SDA
#define PIN_I2C_SCL 42 // I2C SCL

// UART 接口 (MX1.25-4P)
// #define PIN_UART_TX 1 // UART TX
// #define PIN_UART_RX 2 // UART RX

// ============ 电源 ============
// #define PIN_BAT_SENSE 4      // 电池检测 ADC (如果有的话)
// #define BAT_VOLTAGE_DIV 2.0f // 电压分压比

#endif // PINS_H

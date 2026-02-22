/**
 * ESP32-S3 AI Board - 屏幕显示 + I2S 音频问候
 * 显示问候语并播放向上音调
 */

#include <Arduino.h>
#include "lgfx_setup.h"
#include "pins.h"
#include <driver/i2s.h>

LGFX display;

// I2S 引脚定义
#define I2S_BCLK_PIN PIN_I2S_BCLK // 15
#define I2S_LRCK_PIN PIN_I2S_LRCK // 16
#define I2S_DOUT_PIN PIN_I2S_DIN  // 7

// I2S 配置
static const i2s_port_t i2s_port = I2S_NUM_0;

// 音频参数
#define SAMPLE_RATE 16000

// 简单的正弦波生成（向上音调）
void playTone(uint16_t frequency, uint32_t duration)
{
  int16_t sample;
  uint32_t samples_count = (SAMPLE_RATE * duration) / 1000;
  float phase = 0.0;
  float phase_increment = (2.0 * PI * frequency) / SAMPLE_RATE;

  for (uint32_t i = 0; i < samples_count; i++)
  {
    sample = (int16_t)(sin(phase) * 32767 * 0.5);
    phase += phase_increment;
    if (phase > 2 * PI)
    {
      phase -= 2 * PI;
    }

    uint32_t sample32 = (uint16_t)sample;
    sample32 |= ((uint32_t)sample << 16);

    size_t bytes_written;
    i2s_write(i2s_port, &sample32, sizeof(sample32), &bytes_written, portMAX_DELAY);
  }
}

// 播放向上滑音（从低频到高频）
void playSlideUp()
{
  uint32_t duration_per_step = 50;
  uint16_t start_freq = 440;
  uint16_t end_freq = 880;
  uint16_t step = 20;

  for (uint16_t freq = start_freq; freq <= end_freq; freq += step)
  {
    playTone(freq, duration_per_step);
  }
}

void setupI2S()
{
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 1024,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_BCLK_PIN,
      .ws_io_num = I2S_LRCK_PIN,
      .data_out_num = I2S_DOUT_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE};

  esp_err_t ret = i2s_driver_install(i2s_port, &i2s_config, 0, NULL);
  if (ret != ESP_OK)
  {
    Serial.printf("I2S driver install failed: %d\n", ret);
    return;
  }

  ret = i2s_set_pin(i2s_port, &pin_config);
  if (ret != ESP_OK)
  {
    Serial.printf("I2S set pin failed: %d\n", ret);
    return;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2500);
  Serial.println("boot");

  // 初始化 I2S
  setupI2S();
  Serial.println("I2S ready");

  // 背光先拉高
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  // 屏幕硬复位：RST 拉低再拉高
  pinMode(PIN_LCD_RST, OUTPUT);
  digitalWrite(PIN_LCD_RST, LOW);
  delay(20);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(150);

  Serial.println("LCD init...");
  display.init();
  Serial.println("LCD init ok");
  display.setRotation(LCD_ROTATION);
  display.setBrightness(255);

  // 先全屏红色，确认有画面
  display.fillScreen(TFT_RED);
  delay(500);
  display.fillScreen(TFT_WHITE);

  display.setTextColor(TFT_BLACK);
  display.setTextSize(2);
  display.setCursor(20, display.height() / 2 - 10);
  display.print("Hello, Screen!");

  Serial.println("LCD ready.");

  // 播放问候音
  delay(500);
  Serial.println("Playing greeting tone...");
  playSlideUp();
}

void loop()
{
  display.setRotation(1);
  display.fillScreen(TFT_RED);
  delay(1000);
  display.fillScreen(TFT_WHITE);

  display.setCursor(20, 20);
  display.print("Rotation 1");
  delay(1000);
}

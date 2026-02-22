/**
 * ESP32-S3 AI Board - 麦克风 FFT 频谱测试 + 网络音乐播放
 * 显示声音 FFT 频谱并播放问候音，支持播放网络音乐
 */

#include <Arduino.h>
#include "lgfx_setup.h"
#include "pins.h"
#include <driver/i2s.h>
#include <WiFi.h>
#include <Audio.h>  // ESP32-audioI2S 库 - MP3 解码

LGFX display;

// ============ WiFi 配置 ============
const char *WIFI_SSID = "9-404-2G&5G";     // 修改为你的 WiFi SSID
const char *WIFI_PASSWORD = "sunkillytsn"; // 修改为你的 WiFi 密码

// ============ 网络音乐流配置 ============
// 一些可用的网络电台 URL 示例 (MP3 流)
const char *RADIO_URLS[] = {
    "http://stream.zeno.fm/f3wvbbqmdg8uv", // 轻音乐
    "http://stream.zeno.fm/0r0xa792kwzuv", // 古典音乐
    "http://stream.zeno.fm/vp0qxe0h7fdvv", // 流行音乐
};
const int NUM_RADIOS = sizeof(RADIO_URLS) / sizeof(RADIO_URLS[0]);
int currentRadioIndex = 0;

// ============ I2S 麦克风配置 ============
#define MIC_I2S_PORT I2S_NUM_1
#define MIC_BCLK_PIN PIN_MIC_SCK  // 5
#define MIC_DATA_PIN PIN_MIC_DATA // 6
#define MIC_WS_PIN PIN_MIC_WS     // 4

// I2S 麦克风配置
static const i2s_port_t mic_i2s_port = I2S_NUM_1;
#define MIC_SAMPLE_RATE 16000
#define SAMPLE_COUNT 256 // FFT 采样点数 (必须是 2 的幂)

// FFT 相关
#define FFT_SIZE SAMPLE_COUNT
double vReal[SAMPLE_COUNT];
double vImag[SAMPLE_COUNT];
int16_t samples[SAMPLE_COUNT];

// ============ I2S 音频输出配置 ============
#define I2S_BCLK_PIN PIN_I2S_BCLK // 15
#define I2S_LRCK_PIN PIN_I2S_LRCK // 16
#define I2S_DOUT_PIN PIN_I2S_DIN  // 7

static const i2s_port_t dac_i2s_port = I2S_NUM_0;
#define DAC_SAMPLE_RATE 16000

// ============ 频谱显示参数 ============
#define SPECTRUM_WIDTH 300  // 频谱宽度（几乎占满屏幕）
#define SPECTRUM_HEIGHT 120 // 频谱高度
#define SPECTRUM_X 10       // X 起始位置
#define SPECTRUM_Y 25       // Y 起始位置
#define NUM_BARS 32         // 显示的频带数量

// ============ 按键检测 ============
bool lastBtnState = HIGH; // 上一次按键状态
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50; // 去抖延迟
unsigned long lastBtnPressTime = 0;

// ============ 音频播放对象 ============
Audio *audio = nullptr;
bool isPlayingMusic = false;
bool wifiConnected = false;

// 函数声明
void stopMusic();

// 音频回调
void audio_info(const char *info);
void audio_id3data(BufferedData *buf);

// 简单的正弦波生成（向上音调）
void playTone(uint16_t frequency, uint32_t duration)
{
  int16_t sample;
  uint32_t samples_count = (DAC_SAMPLE_RATE * duration) / 1000;
  float phase = 0.0;
  float phase_increment = (2.0 * PI * frequency) / DAC_SAMPLE_RATE;

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
    i2s_write(dac_i2s_port, &sample32, sizeof(sample32), &bytes_written, portMAX_DELAY);
  }
}

// 播放向上滑音
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

// 播放 "di" 声 (短促提示音)
void playBeep()
{
  playTone(1000, 100); // 1kHz, 100ms
}

// ============ WiFi 和网络音乐功能 ============

// 连接 WiFi
void connectWiFi()
{
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 30)
  {
    delay(500);
    Serial.print(".");
    timeout++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
  }
  else
  {
    Serial.println("\nWiFi connection failed!");
    wifiConnected = false;
  }
}

// 播放网络电台
void playRadio(int index)
{
  if (index < 0 || index >= NUM_RADIOS)
    return;

  Serial.print("Playing radio: ");
  Serial.println(RADIO_URLS[index]);

  if (audio == nullptr)
  {
    Serial.println("Audio not initialized!");
    return;
  }

  // 停止当前播放
  stopMusic();
  
  // 连接到新的电台
  audio->connecttohost(RADIO_URLS[index]);
  isPlayingMusic = true;
  Serial.println("Connecting to radio stream...");
}

// 停止播放
void stopMusic()
{
  if (isPlayingMusic && audio != nullptr)
  {
    audio->stopSong();
    isPlayingMusic = false;
    Serial.println("Music stopped");
  }
}

// 切换电台
void nextRadio()
{
  currentRadioIndex = (currentRadioIndex + 1) % NUM_RADIOS;
  playRadio(currentRadioIndex);
}

// 音频信息回调
void audio_info(const char *info)
{
  Serial.print("info: ");
  Serial.println(info);
}

// 音频 ID3 数据回调
void audio_id3data(BufferedData *buf)
{
  if (buf->pos)
  {
    Serial.printf("ID3: %s\n", buf->data);
  }
}

void setupDAC()
{
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = DAC_SAMPLE_RATE,
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

  esp_err_t ret = i2s_driver_install(dac_i2s_port, &i2s_config, 0, NULL);
  if (ret != ESP_OK)
  {
    Serial.printf("DAC I2S driver install failed: %d\n", ret);
    return;
  }

  ret = i2s_set_pin(dac_i2s_port, &pin_config);
  if (ret != ESP_OK)
  {
    Serial.printf("DAC I2S set pin failed: %d\n", ret);
    return;
  }
}

void setupMIC()
{
  Serial.println("Setting up MIC...");

  // I2S 麦克风配置 (PDM 或 I2S 模式)
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = MIC_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 1024,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  i2s_pin_config_t pin_config = {
      .bck_io_num = MIC_BCLK_PIN,
      .ws_io_num = MIC_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = MIC_DATA_PIN};

  esp_err_t ret = i2s_driver_install(mic_i2s_port, &i2s_config, 0, NULL);
  if (ret != ESP_OK)
  {
    Serial.printf("MIC I2S driver install failed: %d\n", ret);
    return;
  }

  ret = i2s_set_pin(mic_i2s_port, &pin_config);
  if (ret != ESP_OK)
  {
    Serial.printf("MIC I2S set pin failed: %d\n", ret);
    return;
  }

  Serial.println("MIC setup complete!");
}

// 从麦克风读取样本
bool readMicSamples()
{
  size_t bytes_read;
  esp_err_t ret = i2s_read(mic_i2s_port, samples, sizeof(samples), &bytes_read, portMAX_DELAY);
  if (ret != ESP_OK || bytes_read < sizeof(samples))
  {
    return false;
  }

  // 转换为 double 并归一化
  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    vReal[i] = (double)samples[i] / 32768.0;
    vImag[i] = 0;
  }

  return true;
}

// 复数乘法
void complexMultiply(double &a, double &b, double c, double d)
{
  double real = a * c - b * d;
  double imag = a * d + b * c;
  a = real;
  b = imag;
}

// 位反转
int bitReverse(int n, int bits)
{
  int reversed = 0;
  for (int i = 0; i < bits; i++)
  {
    reversed = (reversed << 1) | (n & 1);
    n >>= 1;
  }
  return reversed;
}

// 执行 FFT 并计算频谱 (使用 Cooley-Tukey 算法)
void computeFFT()
{
  int bits = 8; // log2(256) = 8

  // 位反转重排
  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    int rev = bitReverse(i, bits);
    if (i < rev)
    {
      double temp = vReal[i];
      vReal[i] = vReal[rev];
      vReal[rev] = temp;
      temp = vImag[i];
      vImag[i] = vImag[rev];
      vImag[rev] = temp;
    }
  }

  // Cooley-Tukey FFT
  for (int len = 2; len <= SAMPLE_COUNT; len *= 2)
  {
    double angle = -2 * PI / len;
    double wlenReal = cos(angle);
    double wlenImag = sin(angle);

    for (int i = 0; i < SAMPLE_COUNT; i += len)
    {
      double wReal = 1;
      double wImag = 0;

      for (int j = 0; j < len / 2; j++)
      {
        double uReal = vReal[i + j];
        double uImag = vImag[i + j];
        double vRealTemp = vReal[i + j + len / 2];
        double vImagTemp = vImag[i + j + len / 2];

        double tReal = vRealTemp * wReal - vImagTemp * wImag;
        double tImag = vRealTemp * wImag + vImagTemp * wReal;

        vReal[i + j] = uReal + tReal;
        vImag[i + j] = uImag + tImag;
        vReal[i + j + len / 2] = uReal - tReal;
        vImag[i + j + len / 2] = uImag - tImag;

        complexMultiply(wReal, wImag, wlenReal, wlenImag);
      }
    }
  }

  // 计算幅度
  for (int i = 0; i < SAMPLE_COUNT / 2; i++)
  {
    vReal[i] = sqrt(vReal[i] * vReal[i] + vImag[i] * vImag[i]);
  }
}

// 绘制频谱
void drawSpectrum()
{
  // 计算每个频带的平均值
  uint8_t barHeights[NUM_BARS];
  int binsPerBar = (SAMPLE_COUNT / 2) / NUM_BARS;

  for (int bar = 0; bar < NUM_BARS; bar++)
  {
    double sum = 0;
    int start = bar * binsPerBar;
    int end = start + binsPerBar;
    if (end > SAMPLE_COUNT / 2)
      end = SAMPLE_COUNT / 2;

    for (int i = start; i < end; i++)
    {
      sum += vReal[i];
    }
    double avg = sum / binsPerBar;

    // 线性缩放
    int height = (int)map(avg * 1000, 0, 100, 0, SPECTRUM_HEIGHT);
    if (height > SPECTRUM_HEIGHT)
      height = SPECTRUM_HEIGHT;
    if (height < 0)
      height = 0;

    barHeights[bar] = height;
  }

  // 只清除频谱区域（标题下方到基线）
  display.fillRect(SPECTRUM_X - 5, SPECTRUM_Y,
                   SPECTRUM_WIDTH + 10, SPECTRUM_HEIGHT + 30, TFT_BLACK);

  // 绘制基线
  display.drawLine(SPECTRUM_X - 5, SPECTRUM_Y + SPECTRUM_HEIGHT,
                   SPECTRUM_X + SPECTRUM_WIDTH + 5, SPECTRUM_Y + SPECTRUM_HEIGHT, TFT_WHITE);

  // 绘制频带
  int barWidth = SPECTRUM_WIDTH / NUM_BARS;
  for (int bar = 0; bar < NUM_BARS; bar++)
  {
    int x = SPECTRUM_X + bar * barWidth;
    int y = SPECTRUM_Y + SPECTRUM_HEIGHT - barHeights[bar];
    int h = barHeights[bar];

    // 根据高度选择颜色
    uint16_t color;
    if (h < 50)
    {
      color = TFT_GREEN;
    }
    else if (h < 100)
    {
      color = TFT_YELLOW;
    }
    else
    {
      color = TFT_RED;
    }

    display.fillRect(x + 1, y, barWidth - 2, h, color);
  }

  // 显示 RMS 音量
  double rms = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    rms += samples[i] * samples[i];
  }
  rms = sqrt(rms / SAMPLE_COUNT);
  int volume = map(rms, 0, 10000, 0, 1000); // 放大 10 倍
  if (volume > 100)
    volume = 100;

  display.setCursor(10, 155);
  display.printf("Volume: %d%%     ", volume);
}

void setup()
{
  Serial.begin(115200);
  delay(2500);
  Serial.println("boot");

  // 初始化 I2S DAC
  setupDAC();
  Serial.println("DAC ready");

  // 初始化音频播放 (使用 I2S_NUM_1 避免与麦克风冲突)
  audio = new Audio(true, I2S_NUM_1);
  audio->setPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);
  audio->setVolume(15);  // 音量 0-21
  audio->infoCallback = audio_info;
  audio->id3Callback = audio_id3data;
  Serial.println("Audio player ready");

  // 连接 WiFi
  connectWiFi();

  // 初始化麦克风
  setupMIC();

  // 初始化按键 (GPIO 0)
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

  // 初始化屏幕
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

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

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(20, display.height() / 2 - 10);
  display.print("Hello, Screen!");

  Serial.println("LCD ready.");

  // 播放问候音
  delay(500);
  Serial.println("Playing greeting tone...");
  playSlideUp();
  Serial.println("Greeting done. Starting spectrum display...");

  // 显示 FFT 标题（只显示一次）
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("MIC FFT Spectrum");

  // 显示 WiFi 状态
  display.setCursor(10, 30);
  if (wifiConnected)
  {
    display.print("WiFi: Connected");
  }
  else
  {
    display.print("WiFi: Disconnected");
  }
  display.print("  Press BTN: Music");
}

// 显示播放状态
void drawPlayStatus()
{
  display.fillRect(0, 30, 320, 20, TFT_BLACK);
  display.setCursor(10, 30);
  if (wifiConnected && isPlayingMusic)
  {
    display.printf("Playing: Radio %d  ", currentRadioIndex + 1);
  }
  else if (wifiConnected)
  {
    display.print("WiFi: Ready  Press BTN: Play  ");
  }
  else
  {
    display.print("WiFi: Disconnected  ");
  }
}

void loop()
{
  // 处理音频播放 (MP3 解码)
  if (audio != nullptr)
  {
    audio->loop();
  }

  // 检测按键 (GPIO 0 拉低)
  bool reading = digitalRead(PIN_BTN_BOOT);
  if (reading != lastBtnState)
  {
    lastDebounceTime = millis();
  }
  lastBtnState = reading;

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (reading == LOW) // 按键按下 (拉低)
    {
      Serial.println("Button pressed...");

      if (wifiConnected)
      {
        if (isPlayingMusic)
        {
          stopMusic();
        }
        else
        {
          playRadio(currentRadioIndex);
        }
      }
      else
      {
        playBeep(); // WiFi 未连接时播放提示音
      }

      drawPlayStatus();
      lastBtnPressTime = millis() + 500; // 防止重复触发，延迟 500ms
      lastDebounceTime = lastBtnPressTime;
    }
  }

  // 读取麦克风数据
  if (readMicSamples())
  {
    // 计算 FFT
    computeFFT();
    // 绘制频谱
    drawSpectrum();
  }

  yield();
}

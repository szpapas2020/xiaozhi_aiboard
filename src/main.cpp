/**
 * ESP32-S3 AI Board - 麦克风 FFT 频谱测试 + 网络音乐播放
 * 显示声音 FFT 频谱并播放问候音，支持播放网络音乐
 */

#include <Arduino.h>
#include "lgfx_setup.h"
#include "pins.h"
#include <driver/i2s.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "AudioFileSourceSPIFFS.h"
#include "AudioFileSourceID3.h"
#include "AudioFileSourceICYStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

LGFX display;

// ============ WiFi 配置 ============
const char *WIFI_SSID = "9-404-2G&5G";     // 修改为你的 WiFi SSID
const char *WIFI_PASSWORD = "sunkillytsn"; // 修改为你的 WiFi 密码

// ============ 网络音乐流配置 ============
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

// ============ I2S 音频输出配置 (用于 ESP8266Audio) ============
#define I2S_BCLK_PIN PIN_I2S_BCLK // 15
#define I2S_LRCK_PIN PIN_I2S_LRCK // 16
#define I2S_DOUT_PIN PIN_I2S_DIN  // 7

// ============ 频谱显示参数 ============
#define SPECTRUM_WIDTH 300  // 频谱宽度（几乎占满屏幕）
#define SPECTRUM_HEIGHT 120 // 频谱高度
#define SPECTRUM_X 10       // X 起始位置
#define SPECTRUM_Y 25       // Y 起始位置
#define NUM_BARS 32         // 显示的频带数量

// ============ 按键检测 ============
bool lastBtnState = HIGH;      // 上一次读取状态
bool lastStableState = HIGH;  // 上一次稳定后的状态（用于边沿检测）
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50; // 去抖延迟

// ============ 音频播放状态 ============
AudioFileSourceICYStream *file = nullptr;
AudioFileSourceBuffer *buff = nullptr;
AudioFileSourceSPIFFS *fileSpiffs = nullptr;
AudioFileSourceID3 *id3 = nullptr;
AudioGeneratorMP3 *mp3 = nullptr;
AudioOutputI2S *out = nullptr;
bool isPlayingMusic = false;
bool isPlayingLocal = false;  // true=本地SPIFFS, false=网络流
bool wifiConnected = false;
unsigned long streamingStartTime = 0;
const unsigned long STREAM_TIMEOUT = 10000; // 10 秒超时

//  Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode;
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1) - 1] = 0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2) - 1] = 0;
  Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  Serial.flush();
}

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string) {
  const char *ptr = reinterpret_cast<const char *>(cbData);
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1) - 1] = 0;
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}

// 函数声明
void stopMusic();
void playLocalMP3(const char *path = "/pno-cs.mp3");

// 简单的正弦波生成（使用 I2S 直接输出，与 ESP8266Audio 配置一致）
void playTone(uint16_t frequency, uint32_t duration)
{
  Serial.printf("Playing tone: %dHz for %dms\n", frequency, duration);

  const int SAMPLE_RATE = 44100;
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
    .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
    .bits_per_chan = I2S_BITS_PER_CHAN_DEFAULT
  };

  i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRCK_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (ret != ESP_OK) {
    Serial.printf("I2S tone install failed: %d\n", ret);
    return;
  }

  ret = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (ret != ESP_OK) {
    Serial.printf("I2S set pin failed: %d\n", ret);
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);
  delay(20);  // 等待 I2S 时钟稳定

  uint32_t samples_count = (SAMPLE_RATE * duration) / 1000;
  float phase = 0.0;
  float phase_increment = (2.0 * PI * frequency) / SAMPLE_RATE;

  const int BUF_SIZE = 256;
  uint32_t buf[BUF_SIZE];
  uint32_t pos = 0;

  for (uint32_t i = 0; i < samples_count; i++) {
    int16_t sample = (int16_t)(sin(phase) * 32767 * 0.8);  // 80% 音量
    phase += phase_increment;
    if (phase > 2 * PI) phase -= 2 * PI;

    buf[pos++] = ((uint32_t)sample << 16) | (uint16_t)sample;

    if (pos >= BUF_SIZE) {
      size_t bytes_written = 0;
      ret = i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes_written, portMAX_DELAY);
      if (ret != ESP_OK) {
        Serial.printf("I2S write failed: %d\n", ret);
        break;
      }
      pos = 0;
    }
  }

  if (pos > 0) {
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, buf, pos * sizeof(uint32_t), &bytes_written, portMAX_DELAY);
  }

  // 等待 DMA 排空最后一块缓冲 (约 6ms/256样本@44.1k)
  delay(30);
  i2s_driver_uninstall(I2S_NUM_0);
  Serial.println("Tone done");
}

// 播放向上滑音
void playSlideUp()
{
  uint32_t duration_per_step = 50;
  uint16_t start_freq = 440;
  uint16_t end_freq = 880;
  uint16_t step = 20;

  for (uint16_t freq = start_freq; freq <= end_freq; freq += step) {
    playTone(freq, duration_per_step);
  }
}

// 播放 "di" 声 (短促提示音)
void playBeep()
{
  playTone(1000, 100);  // 1kHz, 100ms
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

  // 清理旧的音频对象
  if (mp3 != nullptr)
  {
    mp3->stop();
    delete mp3;
    mp3 = nullptr;
  }
  if (buff != nullptr)
  {
    delete buff;
    buff = nullptr;
  }
  if (file != nullptr)
  {
    delete file;
    file = nullptr;
  }
  if (out != nullptr)
  {
    delete out;
    out = nullptr;
  }

  audioLogger = &Serial;

  // 使用 ICY Stream 支持网络电台元数据
  file = new AudioFileSourceICYStream(RADIO_URLS[index]);
  file->RegisterMetadataCB(MDCallback, (void*)"ICY");

  // 添加缓冲（增大缓冲减少解码错误导致的静音）
  buff = new AudioFileSourceBuffer(file, 4096);
  buff->RegisterStatusCB(StatusCallback, (void*)"buffer");

  // 创建 I2S 输出 (使用 I2S_NUM_0 避免与麦克风冲突)
  out = new AudioOutputI2S(I2S_NUM_0);
  out->SetPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);
  out->SetGain(1.0);  // 音量增益，无声音时可试 1.5 或 2.0

  // 创建 MP3 解码器
  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");
  mp3->RegisterMetadataCB(MDCallback, (void*)"ICY");

  Serial.println("Initializing MP3 decoder...");
  Serial.println("Calling mp3->begin...");
  bool result = mp3->begin(buff, out);
  Serial.printf("mp3->begin returned: %d\n", result);
  if (result)
  {
    isPlayingMusic = true;
    streamingStartTime = millis();
    Serial.println("Streaming started successfully!");
  }
  else
  {
    Serial.println("Failed to initialize streaming!");
    stopMusic();
  }
}

// 播放本地 SPIFFS 中的 MP3
void playLocalMP3(const char *path)
{
  Serial.printf("Playing local MP3: %s\n", path);

  // 清理旧的音频对象
  stopMusic();

  if (!SPIFFS.exists(path))
  {
    Serial.printf("File not found in SPIFFS: %s\n", path);
    return;
  }

  audioLogger = &Serial;

  fileSpiffs = new AudioFileSourceSPIFFS(path);
  id3 = new AudioFileSourceID3(fileSpiffs);
  id3->RegisterMetadataCB(MDCallback, (void *)"ID3TAG");

  // 添加缓冲层以减少卡顿
  buff = new AudioFileSourceBuffer(id3, 4096);
  buff->RegisterStatusCB(StatusCallback, (void*)"buffer");

  // 复用 I2S 输出对象，避免重复 install/uninstall 导致 "register I2S object to platform failed"
  if (out == nullptr)
  {
    out = new AudioOutputI2S(I2S_NUM_0);
    out->SetPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);
    out->SetGain(1.0);
  }

  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, (void *)"mp3");
  mp3->RegisterMetadataCB(MDCallback, (void *)"ID3TAG");

  if (mp3->begin(buff, out))
  {
    isPlayingMusic = true;
    isPlayingLocal = true;
    Serial.println("Local MP3 playback started!");
  }
  else
  {
    Serial.println("Failed to start local MP3!");
    stopMusic();
  }
}

// 停止播放
void stopMusic()
{
  if (isPlayingMusic)
  {
    isPlayingMusic = false;
    isPlayingLocal = false;
    Serial.println("Music stopped");
  }

  if (mp3 != nullptr)
  {
    mp3->stop();
    delete mp3;
    mp3 = nullptr;
  }
  if (buff != nullptr)
  {
    delete buff;
    buff = nullptr;
  }
  if (file != nullptr)
  {
    delete file;
    file = nullptr;
  }
  if (id3 != nullptr)
  {
    delete id3;
    id3 = nullptr;
  }
  if (fileSpiffs != nullptr)
  {
    delete fileSpiffs;
    fileSpiffs = nullptr;
  }
  // 不删除 out，复用 I2S 驱动，避免 "register I2S object to platform failed"
}

// 切换电台
void nextRadio()
{
  currentRadioIndex = (currentRadioIndex + 1) % NUM_RADIOS;
  playRadio(currentRadioIndex);
}

void setupMIC()
{
  Serial.println("Setting up MIC...");

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

  // 连接 WiFi
  connectWiFi();

  // 初始化麦克风
  setupMIC();

  // 初始化按键 (VOL+ 按键 - GPIO 40)
  pinMode(PIN_BTN_VOL_UP, INPUT_PULLUP);

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

  // 初始化 SPIFFS 并播放本地 MP3 问候音
  // 注：playBeep() 会卸载 I2S，若放在此处会导致 playLocalMP3 无法安装 I2S
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS mount failed!");
  }
  else
  {
    Serial.println("SPIFFS mounted.");
    delay(500);
    Serial.println("Playing local MP3 greeting...");
    playLocalMP3("/pno-cs.mp3");
  }
  Serial.println("Starting spectrum display...");

  // 显示 FFT 标题（只显示一次）
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("MIC FFT Spectrum");

  // 显示状态
  display.setCursor(10, 30);
  display.print("Press BTN: Play Local MP3");
}

// 显示播放状态
void drawPlayStatus()
{
  display.fillRect(0, 30, 320, 20, TFT_BLACK);
  display.setCursor(10, 30);
  if (isPlayingMusic)
  {
    display.print("Playing: Local MP3  ");
  }
  else
  {
    display.print("Press BTN: Play MP3  ");
  }
}

void loop()
{
  // 处理音频播放 (MP3 解码) - 优先处理，多次调用以保证流畅播放
  if (isPlayingMusic && mp3 != nullptr)
  {
    // 网络流超时检查（本地播放不检查）
    if (!isPlayingLocal && (millis() - streamingStartTime > STREAM_TIMEOUT))
    {
      Serial.println("Streaming timeout, stopping...");
      stopMusic();
    }
    else if (!mp3->loop())
    {
      Serial.println("MP3 decoder stopped or stream ended");
      stopMusic();
    }

    // 播放音乐时跳过麦克风采样，避免 CPU 占用导致卡顿
    // 只处理按键检测
  }
  else
  {
    // 不播放音乐时才读取麦克风数据
    if (readMicSamples())
    {
      // 计算 FFT
      computeFFT();
      // 绘制频谱
      drawSpectrum();
    }
  }

  // 检测按键 (VOL+ - GPIO 40 拉低) - 仅在下落沿触发，避免 GPIO 浮空/噪声误触发
  bool reading = digitalRead(PIN_BTN_VOL_UP);
  // Serial.printf("reading: %d, lastBtnState: %d, lastStableState: %d\n", reading, lastBtnState, lastStableState);
  if (reading != lastBtnState)
  {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    // 仅在下落沿 (HIGH→LOW) 触发，不持续触发
    if (reading == LOW && lastStableState == HIGH)
    {
      Serial.println("Button pressed...");

      if (isPlayingMusic)
      {
        Serial.println("Stopping music...");
        stopMusic();
      }
      else
      {
        Serial.println("Playing local MP3...");
        playLocalMP3("/pno-cs.mp3");
      }

      drawPlayStatus();
    }
    lastStableState = reading;  // 更新稳定状态
  }

  lastBtnState = reading;  // 在去抖动检查之后更新

  yield();
}

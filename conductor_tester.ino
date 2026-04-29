/**
 * ====================================================
 *  Smart Electro-World — Conductor & Insulator Tester
 * ====================================================
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"

// 语音文件
#include "audio_conductor.h"
#include "audio_weak.h"
#include "audio_insulator.h"
#include "audio_num_0.h"
#include "audio_num_1.h"
#include "audio_num_2.h"
#include "audio_num_3.h"
#include "audio_num_4.h"
#include "audio_num_5.h"
#include "audio_num_6.h"
#include "audio_num_7.h"
#include "audio_num_8.h"
#include "audio_num_9.h"
#include "audio_num_10.h"
#include "audio_now_ohm.h"
#include "audio_hundred.h"
#include "audio_kilo.h"
#include "audio_wan.h"
#include "audio_ohm.h"
#include "audio_point.h"

// WiFi + API
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// 引脚定义
#define TFT_SCL  21
#define TFT_SDA  47
#define TFT_RES  45
#define TFT_DC   40
#define TFT_BLK  42
#define TFT_CS   -1

#define RGB_R  17
#define RGB_G  18
#define RGB_B  48

#define I2S_BCLK  15
#define I2S_LRC   16
#define I2S_DOUT  7
#define ADC_PIN   4

const float R_REF    = 1000.0f;
const float V_IN     = 3300.0f;
const float THR_GOOD = 100.0f;
const float THR_WEAK = 10000.0f;

const char* ssid     = "WIFI_NAME";
const char* password = "PASSWORD";
const char* apiKey   = "API_KEY";

enum DetectState {
  STATE_NONE,
  STATE_GOOD,
  STATE_WEAK,
  STATE_INSULATOR
};

// ==================== 屏幕刷新缓存（防抖版） ====================
DetectState lastDisplayState = STATE_NONE;
float       lastDisplayRx    = -1;
uint32_t    lastDisplaymV    = 0;
String      lastDisplayAI    = "";

// ==================== 音频系统 ====================
struct AudioClip {
  const uint8_t *data;
  size_t         len;
};

#define AUDIO_QUEUE_SIZE 25
QueueHandle_t     audioClipQueue;
SemaphoreHandle_t audioMutex;

AudioOutputI2S         *audioOut = nullptr;
AudioGeneratorAAC      *aac      = nullptr;
AudioFileSourcePROGMEM *audioSrc = nullptr;

volatile bool audioStopRequest = false;

Adafruit_ST7789       tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RES);
U8G2_FOR_ADAFRUIT_GFX u8g2;

DetectState lastState = STATE_NONE;
String      ai_text   = "";

// ==================== 产品级UI工具函数 ====================
void drawCard(int x, int y, int w, int h, uint16_t color, bool fill = false) {
  if (fill) {
    tft.fillRoundRect(x, y, w, h, 6, color);
  } else {
    tft.drawRoundRect(x, y, w, h, 6, color);
  }
}

void drawStatusLight(int x, int y, int radius, DetectState state) {
  uint16_t lightColor = ST77XX_WHITE;
  if (state == STATE_GOOD) lightColor = ST77XX_GREEN;
  else if (state == STATE_WEAK) lightColor = 0xFFE0;
  else if (state == STATE_INSULATOR) lightColor = ST77XX_RED;
  
  tft.fillCircle(x, y, radius, lightColor);
  tft.drawCircle(x, y, radius+1, ST77XX_WHITE);
}

// ==================== 音频任务 ====================
void audioTask(void *param) {
  bool playing = false;
  for (;;) {
    if (audioStopRequest) {
      audioStopRequest = false;
      xQueueReset(audioClipQueue);
      xSemaphoreTake(audioMutex, portMAX_DELAY);
      if (aac && aac->isRunning()) aac->stop();
      if (audioSrc) { delete audioSrc; audioSrc = nullptr; }
      if (aac)      { delete aac;      aac      = nullptr; }
      playing = false;
      xSemaphoreGive(audioMutex);
    }

    if (playing) {
      xSemaphoreTake(audioMutex, portMAX_DELAY);
      bool ok = false;
      if (aac && aac->isRunning()) ok = aac->loop();
      if (!ok) {
        if (aac) aac->stop();
        playing = false;
      }
      xSemaphoreGive(audioMutex);
    }

    if (!playing) {
      AudioClip next;
      if (xQueueReceive(audioClipQueue, &next, 0) == pdTRUE) {
        xSemaphoreTake(audioMutex, portMAX_DELAY);
        if (audioSrc) { delete audioSrc; audioSrc = nullptr; }
        if (aac)      { delete aac;      aac      = nullptr; }
        audioSrc = new AudioFileSourcePROGMEM(next.data, next.len);
        aac      = new AudioGeneratorAAC();
        aac->begin(audioSrc, audioOut);
        playing = true;
        xSemaphoreGive(audioMutex);
      }
    }
    vTaskDelay(1);
  }
}

void enqueueAudio(const uint8_t *data, size_t len) {
  AudioClip clip = { data, len };
  xQueueSend(audioClipQueue, &clip, pdMS_TO_TICKS(50));
}

void stopAndClearAudio() {
  audioStopRequest = true;
}

#define ENQUEUE(arr) enqueueAudio(arr, sizeof(arr))

void enqueueDigit(int d) {
  switch (d) {
    case 0:  ENQUEUE(audio_num_0);  break;
    case 1:  ENQUEUE(audio_num_1);  break;
    case 2:  ENQUEUE(audio_num_2);  break;
    case 3:  ENQUEUE(audio_num_3);  break;
    case 4:  ENQUEUE(audio_num_4);  break;
    case 5:  ENQUEUE(audio_num_5);  break;
    case 6:  ENQUEUE(audio_num_6);  break;
    case 7:  ENQUEUE(audio_num_7);  break;
    case 8:  ENQUEUE(audio_num_8);  break;
    case 9:  ENQUEUE(audio_num_9);  break;
    case 10: ENQUEUE(audio_num_10); break;
  }
}

void speakInt(int n) {
  if (n <= 0) { enqueueDigit(0); return; }
  bool hadHigh = false;
  if (n >= 10000) { enqueueDigit(n/10000); ENQUEUE(audio_wan);     n %= 10000; hadHigh = true; if(n==0) return; if(n<1000) enqueueDigit(0); }
  if (n >= 1000)  { enqueueDigit(n/1000);  ENQUEUE(audio_kilo);    n %= 1000;  hadHigh = true; if(n==0) return; if(n<100)  enqueueDigit(0); }
  if (n >= 100)   { enqueueDigit(n/100);   ENQUEUE(audio_hundred); n %= 100;   hadHigh = true; if(n==0) return; if(n<10)   enqueueDigit(0); }
  if (n >= 10) {
    int shi = n / 10;
    bool leadingTeen = (shi == 1) && !hadHigh;
    if (!leadingTeen) enqueueDigit(shi);
    ENQUEUE(audio_num_10);
    n %= 10;
    hadHigh = true;
    if (n == 0) return;
  }
  if (n > 0) enqueueDigit(n);
}

void queueResistanceAnnounce(float rx) {
  ENQUEUE(audio_now_ohm);
  int r = (int)roundf(rx);
  if (r < 1)     r = 1;
  if (r > 10000) r = 10000;
  speakInt(r);
  ENQUEUE(audio_ohm);
}

// ==================== UTF-8工具函数 ====================
int utf8CharByteLen(const String &s, int bytePos) {
  uint8_t c = (uint8_t)s[bytePos];
  if (c >= 0xE0) return 3;
  if (c >= 0xC0) return 2;
  return 1;
}

String utf8Truncate(const String &s, int maxChars) {
  int bytePos = 0;
  int charCount = 0;
  while (bytePos < (int)s.length() && charCount < maxChars) {
    bytePos += utf8CharByteLen(s, bytePos);
    charCount++;
  }
  return s.substring(0, bytePos);
}

String utf8Substr(const String &s, int startChar, int count) {
  int bytePos = 0;
  int charIdx = 0;
  while (bytePos < (int)s.length() && charIdx < startChar) {
    bytePos += utf8CharByteLen(s, bytePos);
    charIdx++;
  }
  int byteStart = bytePos;
  int taken = 0;
  while (bytePos < (int)s.length() && taken < count) {
    bytePos += utf8CharByteLen(s, bytePos);
    taken++;
  }
  return s.substring(byteStart, bytePos);
}

int utf8Len(const String &s) {
  int bytePos = 0;
  int count = 0;
  while (bytePos < (int)s.length()) {
    bytePos += utf8CharByteLen(s, bytePos);
    count++;
  }
  return count;
}

// ==================== askAI（只在状态变化时请求） ====================
String askAI(float resistance) {
  if (WiFi.status() != WL_CONNECTED) return "WiFi未连接";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, "https://api.deepseek.com/chat/completions")) return "连接失败";

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + apiKey);

  String payload = "{\"model\":\"deepseek-chat\",\"messages\":[{\"role\":\"user\",\"content\":\"电阻为 "
                   + String((int)roundf(resistance))
                   + " 欧。请用教学风格的中文说明其导电性好坏，并简单说明原因，并对类似大小的阻值的生活中常见的材料做一些拓展介绍，控制在40字以内。（如果电阻小于100，则判定为良导体，导电性相对较高）\"}],\"max_tokens\":120}";

  int httpCode = http.POST(payload);
  if (httpCode != 200) {
    http.getString();
    http.end();
    return "API错误" + String(httpCode);
  }

  String response = http.getString();
  http.end();

  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, response);
  if (err) return "解析失败";

  const char* content = doc["choices"][0]["message"]["content"];
  if (!content) return "无返回内容";

  String aiResp = String(content);
  aiResp.trim();
  aiResp = utf8Truncate(aiResp, 64);
  return aiResp;
}

// ==================== RGB ====================
void setRGB(bool r, bool g, bool b) {
  digitalWrite(RGB_R, r ? HIGH : LOW);
  digitalWrite(RGB_G, g ? HIGH : LOW);
  digitalWrite(RGB_B, b ? HIGH : LOW);
}

// ==================== 产品级UI绘制 ====================
void drawUI(uint32_t mV, float rx, DetectState state) {
  tft.fillScreen(ST77XX_BLACK);
  
  drawCard(0, 0, 160, 60, ST77XX_WHITE);
  drawCard(160, 0, 80, 60, ST77XX_WHITE);
  drawCard(0, 60, 240, 120, ST77XX_CYAN);
  drawCard(0, 180, 240, 60, ST77XX_WHITE);

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  if (state == STATE_GOOD) {
    u8g2.setForegroundColor(ST77XX_GREEN);
    u8g2.setCursor(15, 32);
    u8g2.print("良导体");
  } else if (state == STATE_WEAK) {
    u8g2.setForegroundColor(0xFFE0);
    u8g2.setCursor(15, 32);
    u8g2.print("弱导电");
  } else if (state == STATE_INSULATOR) {
    u8g2.setForegroundColor(ST77XX_RED);
    u8g2.setCursor(15, 32);
    u8g2.print("绝缘体");
  } else {
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(15, 32);
    u8g2.print("等待测试");
  }

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(15, 52);
  if (rx > 0 && state != STATE_NONE) {
    u8g2.print("电阻: ");
    u8g2.print((int)roundf(rx));
    u8g2.print(" Ω");
  } else {
    u8g2.print("无有效信号");
  }

  drawStatusLight(200, 30, 12, state);
  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(170, 55);
  u8g2.print("状态指示");

  u8g2.setFont(u8g2_font_wqy13_t_gb2312);
  u8g2.setForegroundColor(ST77XX_CYAN);
  
  const int CHARS_PER_LINE = 16;
  const int LINE_HEIGHT = 18;
  const int START_X = 10;
  const int START_Y = 85;

  if (ai_text.length() > 0) {
    int totalChars = utf8Len(ai_text);
    int charPos = 0;
    int y = START_Y;

    while (charPos < totalChars && y < 170) {
      int take = min(CHARS_PER_LINE, totalChars - charPos);
      String line = utf8Substr(ai_text, charPos, take);
      u8g2.setCursor(START_X, y);
      u8g2.print(line);
      charPos += take;
      y += LINE_HEIGHT;
    }
  } else {
    u8g2.setCursor(70, 120);
    u8g2.print("AI分析中...");
  }

  u8g2.setFont(u8g2_font_wqy12_t_gb2312);
  
  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(15, 205);
  u8g2.print("检测电压: ");
  u8g2.print(mV);
  u8g2.print(" mV");

  u8g2.setForegroundColor(0xC618);
  u8g2.setCursor(15, 225);
  u8g2.print("基准电阻:1kΩ  检测范围:1-10kΩ");
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TFT_RES, OUTPUT);
  digitalWrite(TFT_RES, HIGH); delay(10);
  digitalWrite(TFT_RES, LOW);  delay(20);
  digitalWrite(TFT_RES, HIGH); delay(50);

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  SPI.begin(TFT_SCL, -1, TFT_SDA, -1);
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.setSPISpeed(10000000);
  u8g2.begin(tft);

  pinMode(RGB_R, OUTPUT); digitalWrite(RGB_R, LOW);
  pinMode(RGB_G, OUTPUT); digitalWrite(RGB_G, LOW);
  pinMode(RGB_B, OUTPUT); digitalWrite(RGB_B, LOW);

  audioClipQueue = xQueueCreate(AUDIO_QUEUE_SIZE, sizeof(AudioClip));
  audioMutex     = xSemaphoreCreateMutex();

  audioOut = new AudioOutputI2S();
  audioOut->SetPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audioOut->SetGain(0.8f);

  xTaskCreatePinnedToCore(audioTask, "audioTask", 8192, nullptr, 2, nullptr, 0);

  WiFi.begin(ssid, password);
  for (int i = 0; i < 25 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
  }

  // 开机第一次刷新
  drawUI(0, -1, STATE_NONE);
}

// ==================== loop（终极防抖 · 大幅变化才刷新） ====================
void loop() {
  uint32_t sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogReadMilliVolts(ADC_PIN);
    delay(3);
  }
  uint32_t mV = sum / 10;

  float rx = -1.0f;
  if (mV > 80 && mV < 3250) {
    rx = R_REF * (V_IN / (float)mV - 1.0f);
  }

  DetectState curState = STATE_NONE;
  if      (mV >= 3250 || mV <= 5) curState = STATE_NONE;
  else if (mV <= 80)               curState = STATE_INSULATOR;
  else if (rx < THR_GOOD)          curState = STATE_GOOD;
  else if (rx < THR_WEAK)          curState = STATE_WEAK;
  else                             curState = STATE_INSULATOR;

  // 状态变化时才播放语音 + 请求AI
  if (curState != lastState) {
    lastState = curState;

    switch (curState) {
      case STATE_GOOD:      setRGB(false, true,  false); break;
      case STATE_WEAK:      setRGB(true,  true,  false); break;
      case STATE_INSULATOR: setRGB(true,  false, false); break;
      default:              setRGB(false, false, false); break;
    }

    stopAndClearAudio();
    delay(15);

    if (curState == STATE_GOOD) {
      ENQUEUE(audio_conductor);
      queueResistanceAnnounce(rx);
    } else if (curState == STATE_WEAK) {
      ENQUEUE(audio_weak);
      queueResistanceAnnounce(rx);
    } else if (curState == STATE_INSULATOR) {
      ENQUEUE(audio_insulator);
    }

    // AI 只在状态变化时请求一次
    if (curState != STATE_NONE && rx > 0) {
      ai_text = askAI(rx);
    } else {
      ai_text = "";
    }
  }

  // ==================== 核心：电阻波动不刷新 · 大变才刷新 ====================
  const float CHANGE_THRESHOLD = 50.0f; // 变化超过50Ω才刷新
  bool needRefresh = false;

  // 1. 状态变了（良/弱/绝缘）→ 刷新
  if (curState != lastDisplayState) needRefresh = true;
  // 2. 电阻突变超阈值 → 刷新
  else if (abs(rx - lastDisplayRx) > CHANGE_THRESHOLD) needRefresh = true;
  // 3. AI内容更新 → 刷新
  else if (ai_text != lastDisplayAI) needRefresh = true;

  if (needRefresh) {
    drawUI(mV, rx, curState);
    
    lastDisplayState = curState;
    lastDisplayRx    = rx;
    lastDisplaymV    = mV;
    lastDisplayAI    = ai_text;
  }

  delay(100);
}

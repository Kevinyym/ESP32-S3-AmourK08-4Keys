#include "lgfx.h"
#include <Adafruit_AHTX0.h>
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Audio.h>
#include <OneButton.h>
#include <Preferences.h>
#include <WiFi.h>
#include <vector>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#define PIN_LED 48
#define PIN_RED_LED 47

#define PIN_I2S_SD 4
#define PIN_I2S_DOUT 5
#define PIN_I2S_BCLK 6
#define PIN_I2S_LRC 7

#define PIN_KEY_ADD 9
#define PIN_KEY_MINUS 21
#define PIN_KEY_MODE 14

#define PIN_I2C_SDA 1
#define PIN_I2C_SCL 2

using namespace std;

#define FONT16 &fonts::efontCN_16
#define FM_URL "http://lhttp.qtfm.cn/live/%lu/64k.mp3"

constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t SMART_CONFIG_TIMEOUT_MS = 120000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t RADIO_RETRY_INTERVAL_MS = 2000;
constexpr uint32_t RADIO_STABLE_WINDOW_MS = 30000;
constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 2000;
constexpr uint32_t AHT_RETRY_INTERVAL_MS = 60000;
constexpr uint8_t MAX_RADIO_RETRIES = 3;

typedef struct {
  u32_t id;
  String name;
} RadioItem;

static const char *WEEK_DAYS[] = {"日", "一", "二", "三", "四", "五", "六"};
uint32_t check1s = 0, check10ms = 0, check60s = 0;
char buf[128] = {0};
LGFX tft;
LGFX_Sprite dateSprite(&tft);
LGFX_Sprite timeSprite(&tft);
LGFX_Sprite radioSprite(&tft);
LGFX_Sprite sensorSprite(&tft);
LGFX_Sprite volumeSprite(&tft);
LGFX_Sprite ipSprite(&tft);
Audio audio;
Preferences preferences;
int curIndex = 0;
int curVolume = 6;
bool ahtAvailable = false;
bool wireReady = false;
bool otaReady = false;
bool wifiWasConnected = false;
bool redLedState = false;
bool settingsDirty = false;
volatile bool radioRetryPending = false;
volatile uint8_t radioRetryCount = 0;
uint32_t lastWifiRetry = 0;
uint32_t lastRadioAttempt = 0;
uint32_t radioConnectedAt = 0;
uint32_t settingsChangedAt = 0;
uint32_t lastAhtAttempt = 0;
Adafruit_NeoPixel pixels(4, PIN_LED, NEO_GRB + NEO_KHZ800);
Adafruit_AHTX0 aht;
OneButton addButton(PIN_KEY_ADD);
OneButton minusButton(PIN_KEY_MINUS);
OneButton modeButton(PIN_KEY_MODE);
OneButton *buttons[] = {&addButton, &minusButton, &modeButton};
std::vector<RadioItem> radios = {
    {4915, "清晨音乐台"},
    {1223, "怀旧好声音"},
    {4866, "浙江音乐调频"},
    {20211686, "成都年代音乐怀旧好声音"},
    {1739, "厦门音乐广播"},
    {1271, "深圳飞扬971"},
    {20240, "山东经典音乐广播"},
    {20500066, "年代音乐1022"},
    {1296, "湖北经典音乐广播"},
    {267, "上海经典947"},
    {20212426, "崂山921"},
    {20003, "天津TIKI FM100.5"},
    {1111, "四川城市之音"},
    {4936, "江苏音乐广播PlayFM897"},
    {4237, "长沙FM101.7城市之声"},
    {1665, "山东音乐广播"},
    {1947, "安徽音乐广播"},
    {332, "北京音乐广播"},
    {4932, "山西音乐广播"},
    {20500149, "两广之声音乐台"},
    {4804, "怀集音乐之声"},
    {1649, "河北音乐广播"},
    {4938, "江苏经典流行音乐"},
    {1260, "广东音乐之声"},
    {273, "上海流行音乐LoveRadio"},
    {274, "上海动感101"},
    {2803, "苏州音乐广播"},
    {839, "哈尔滨音乐广播"},
    {5021381, "959年代音乐怀旧好声音"},
    {15318569, "AsiaFM 亚洲粤语台"},
    {5022308, "500首华语经典"},
    {20500150, "顺德音乐之声"},
    {4875, "FM950广西音乐台"},
    {1283, "江门旅游之声"},
    {1936, "FM954汽车音乐广播"},
    {20847, "FM88.6长沙音乐广播"},
    {1612, "西安音乐广播"},
    {20210755, "星河音乐"},
    {1886, "内蒙古音乐之声"},
    {1208, "河南音乐广播"},
    {4963, "南京音乐广播"},
    {1802, "江西音乐广播"},
    {15318146, "杭州FM90.7"},
    {647, "重庆音乐广播"},
    {15318703, "欧美音乐88.7"},
    {5021523, "惠州音乐广播"},
    {15318341, "AsiaFM HD音乐台"},
    {20769, "南宁经典1049"},
    {1289, "楚天音乐广播"},
    {4873, "陕西音乐广播"},
    {5022474, "武安融媒综合广播"},
    {21209, "东莞音乐广播"},
    {4969, "黑龙江音乐广播"},
    {1136, "嘉兴音乐广播"},
    {21275, "南通音乐广播"},
    {20211619, "怀旧音乐广播895"},
    {4981, "芒果时空音乐台"},
    {1297, "武汉经典音乐广播"},
    {20211638, "定州交通音乐广播"},
    {5022023, "上海KFM981"},
    {20207761, "80后音悦台"},
    {1654, "石家庄音乐广播"},
    {20212227, "经典FM1008"},
    {1149, "1003温州音乐之声"},
    {1671, "济南音乐广播FM88.7"},
    {5021912, "AsiaFM 亚洲经典台"},
    {1084, "大连1067"},
    {1892, "包头汽车音乐广播"},
    {1110, "四川岷江音乐广播"},
    {1831, "吉林音乐广播"},
    {5022405, "AsiaFM 亚洲音乐台"},
    {4581, "亚洲音乐成都FM96.5"},
    {20071, "AsiaFM 亚洲天空台"},
    {20033, "1047 Nice FM"},
    {4930, "FM102.2亲子智慧电台"},
    {4846, "893音乐广播"},
    {20026, "郁南音乐台"},
    {1608, "陕西故事广播·年代878"},
    {4923, "徐州音乐广播FM91.9"},
    {4878, "海南音乐广播"},
    {20211575, "经典983电台"},
    {4594, "潮州交通音乐广播"},
    {20500097, "经典音乐广播FM94.8"},
    {4885, "陕西青少广播·好听1055"},
    {4585, "福建音乐广播"},
    {2799, "常州音乐广播"},
    {1975, "MUSIC876"},
    {5022391, "Easy Fm"},
    {20500067, "FM95.9清远交通音乐广播"},
    {20211620, "流行音乐广播999正青春"},
    {20067, "贵州FM91.6音乐广播"},
    {5021902, "沧州音乐广播FM103.6"},
    {20207781, "眉山交通音乐广播"},
    {2811, "湖州交通文艺广播"},
    {5022050, "FM89.1吴江综合广播"},
    {20500053, "经典958"},
    {5022520, "盛京FM105.6"},
    {20091, "中国校园之声"},
    {4979, "89.3芒果音乐台"},
    {20835, "秦皇岛音乐广播"},
    {20211678, "廊坊飞扬105"},
    {1677, "青岛音乐体育广播"},
    {4029, "新疆MIXFM1039"},
    {5022338, "冰城1026哈尔滨古典音乐广播"},
    {20207762, "河南经典FM"},
    {4921, "郑州音乐广播"},
    {5022610, "察布查尔FM99.5"},
    {4871, "唐山音乐广播"},
    {1683, "烟台音乐广播FM105.9"},
    {5020, "滁州旅游交通广播"},
    {20440, "新疆昌吉 FM103.3综合广播"},
    {20212387, "凤凰音乐"},
    {20500187, "云梦音乐台"},
};

void setAmplifierEnabled(bool enabled) {
  digitalWrite(PIN_I2S_SD, enabled ? HIGH : LOW);
}

void inline initAudioDevice() {
  pinMode(PIN_I2S_SD, OUTPUT);
  setAmplifierEnabled(false);
  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);
  audio.setVolume(curVolume);
}

void inline initPixels() {
  pinMode(PIN_RED_LED, OUTPUT);
  digitalWrite(PIN_RED_LED, LOW);
  pixels.begin();
  pixels.setBrightness(40);
  pixels.clear();
  pixels.show();
}

bool waitForWifi(uint32_t timeoutMs) {
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool inline autoConfigWifi() {
  tft.println("Start WiFi Connect!");
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin();

  if (!waitForWifi(WIFI_CONNECT_TIMEOUT_MS)) {
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.beginSmartConfig();
    tft.println("Use ESPTouch App!");

    if (!waitForWifi(SMART_CONFIG_TIMEOUT_MS)) {
      WiFi.stopSmartConfig();
      WiFi.mode(WIFI_MODE_STA);
      tft.println("WiFi config timeout");
      return false;
    }

    WiFi.stopSmartConfig();
    WiFi.mode(WIFI_MODE_STA);
  }

  WiFi.setAutoReconnect(true);
  tft.println("WiFi Connected, Please Wait...");
  return true;
}

inline void showCurrentTime() {
  struct tm info{};
  if (!getLocalTime(&info, 10)) {
    timeSprite.fillSprite(TFT_BLACK);
    timeSprite.drawCentreString("--:--:--", 120, 0, &fonts::FreeSans24pt7b);
    timeSprite.pushSprite(0, 140);
    return;
  }

  snprintf(buf, sizeof(buf), "%d年%d月%d日 星期%s", 1900 + info.tm_year,
           info.tm_mon + 1, info.tm_mday, WEEK_DAYS[info.tm_wday]);
  dateSprite.fillSprite(TFT_BLACK);
  dateSprite.drawCentreString(buf, 120, 0);
  dateSprite.pushSprite(0, 110);
  strftime(buf, 36, "%T", &info);
  timeSprite.fillSprite(TFT_BLACK);
  timeSprite.drawCentreString(buf, 120, 0, &fonts::FreeSans24pt7b);
  timeSprite.pushSprite(0, 140);
}

bool inline startConfigTime() {
  const int timeZone = 8 * 3600;
  configTime(timeZone, 0, "ntp6.aliyun.com", "cn.ntp.org.cn", "ntp.ntsc.ac.cn");

  const uint32_t startedAt = millis();
  while (time(nullptr) < 8 * 3600 * 2 &&
         millis() - startedAt < NTP_SYNC_TIMEOUT_MS) {
    delay(300);
  }
  return time(nullptr) >= 8 * 3600 * 2;
}

bool inline setupOTAConfig() {
#if !defined(K08_OTA_PASSWORD) && !defined(K08_OTA_PASSWORD_HASH)
  Serial.println("OTA disabled: create include/secrets.h and configure a password");
  return false;
#else
  ArduinoOTA.setHostname("k08-netradio");
#if defined(K08_OTA_PASSWORD_HASH)
  ArduinoOTA.setPasswordHash(K08_OTA_PASSWORD_HASH);
#else
  ArduinoOTA.setPassword(K08_OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([] {
    audio.stopSong();
    setAmplifierEnabled(false);
    tft.setBrightness(200);
    tft.clear();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("软件升级", 120, 8, FONT16);
    tft.drawRoundRect(18, 158, 204, 10, 3, TFT_ORANGE);
    tft.drawCentreString("正在升级中，请勿断电...", 120, 190, FONT16);
  });
  ArduinoOTA.onProgress([](u32_t pro, u32_t total) {
    snprintf(buf, sizeof(buf), "升级进度: %lu / %lu",
             static_cast<unsigned long>(pro), static_cast<unsigned long>(total));
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawCentreString(buf, 120, 120, FONT16);
    if (pro > 0 && total > 0) {
      int pros = static_cast<int>(static_cast<uint64_t>(pro) * 200 / total);
      tft.fillRoundRect(20, 160, pros, 6, 2, TFT_WHITE);
    }
  });
  ArduinoOTA.onEnd([] {
    tft.clear();
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("升级成功", 120, 60, FONT16);
    tft.drawCentreString("升级已完成，正在重启...", 120, 140, FONT16);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    tft.clear();
    snprintf(buf, sizeof(buf), "升级失败: %u", static_cast<unsigned>(e));
    tft.drawCentreString(buf, 120, 100, FONT16);
    Serial.println(buf);
  });
  ArduinoOTA.begin();
  otaReady = true;
  snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
  tft.println(buf);
  return true;
#endif
}

void nextVolume(int offset) {
  int vol = curVolume + offset;
  if (vol >= 0 && vol <= 21) {
    curVolume = vol;
    audio.setVolume(curVolume);
    snprintf(buf, sizeof(buf), "音量: %d", curVolume);
    volumeSprite.fillSprite(TFT_BLACK);
    volumeSprite.drawString(buf, 8, 0);
    volumeSprite.pushSprite(0, 220);
    if (offset != 0) {
      settingsDirty = true;
      settingsChangedAt = millis();
    }
  }
}

bool connectCurrentRadio() {
  if (WiFi.status() != WL_CONNECTED || radios.empty()) {
    return false;
  }

  audio.stopSong();
  setAmplifierEnabled(false);
  const auto &radio = radios[curIndex];
  snprintf(buf, sizeof(buf), FM_URL, static_cast<unsigned long>(radio.id));
  const bool connected = audio.connecttohost(buf);
  if (connected) {
    radioConnectedAt = millis();
    setAmplifierEnabled(true);
  }
  return connected;
}

void showCurrentRadio() {
  const auto &radio = radios[curIndex];
  snprintf(buf, sizeof(buf), "%d.%s", curIndex + 1, radio.name.c_str());
  radioSprite.fillSprite(TFT_BLACK);
  radioSprite.drawCentreString(buf, 120, 0);
  radioSprite.pushSprite(0, 20);
}

void scheduleRadioRetry() {
  radioRetryPending = true;
  lastRadioAttempt = millis();
}

void playNext(int offset, bool saveSelection = false) {
  int total = radios.size();
  curIndex += offset;
  if (curIndex >= total) {
    curIndex %= total;
  } else if (curIndex < 0) {
    curIndex += total;
  }
  radioRetryCount = 0;
  showCurrentRadio();
  if (saveSelection) {
    settingsDirty = true;
    settingsChangedAt = millis();
  }
  if (!connectCurrentRadio()) {
    radioRetryCount = 1;
    scheduleRadioRetry();
  }
}

inline void showClientIP() {
  tft.clear();
  snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
  ipSprite.fillSprite(TFT_BLACK);
  ipSprite.drawRightString(buf, 112, 0);
  ipSprite.pushSprite(120, 220);
}

void onButtonClick(void *p) {
  const u32_t pin = static_cast<u32_t>(reinterpret_cast<uintptr_t>(p));
  switch (pin) {
  case PIN_KEY_MODE:
    audio.pauseResume();
    break;
  case PIN_KEY_ADD:
    playNext(1, true);
    break;
  case PIN_KEY_MINUS:
    playNext(-1, true);
    break;
  default:
    break;
  }
}

void onButtonDoubleClick(void *p) {
  const u32_t pin = static_cast<u32_t>(reinterpret_cast<uintptr_t>(p));
  switch (pin) {
  case PIN_KEY_ADD:
    nextVolume(1);
    break;
  case PIN_KEY_MINUS:
    nextVolume(-1);
    break;
  default:
    break;
  }
}

void inline setupButtons() {
  addButton.attachClick(onButtonClick, reinterpret_cast<void *>(PIN_KEY_ADD));
  addButton.attachDoubleClick(onButtonDoubleClick,
                              reinterpret_cast<void *>(PIN_KEY_ADD));
  minusButton.attachClick(onButtonClick, reinterpret_cast<void *>(PIN_KEY_MINUS));
  minusButton.attachDoubleClick(onButtonDoubleClick,
                                reinterpret_cast<void *>(PIN_KEY_MINUS));
  modeButton.attachClick(onButtonClick, reinterpret_cast<void *>(PIN_KEY_MODE));
}

void setupSprite(LGFX_Sprite &sprite, int width, int height) {
  sprite.setFont(FONT16);
  sprite.setColorDepth(8);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  sprite.createSprite(width, height);
  sprite.fillSprite(TFT_BLACK);
}

void inline initTFTDevice() {
  tft.init();
  tft.setBrightness(60);
  tft.setFont(FONT16);
  tft.setColorDepth(8);
  tft.fillScreen(TFT_BLACK);
  setupSprite(dateSprite, 240, 16);
  setupSprite(timeSprite, 240, 36);
  setupSprite(radioSprite, 240, 16);
  setupSprite(sensorSprite, 240, 16);
  setupSprite(volumeSprite, 120, 16);
  setupSprite(ipSprite, 120, 16);
}

void inline initAHT20Wire() {
  lastAhtAttempt = millis();
  if (!wireReady) {
    wireReady = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
  }
  if (!wireReady) {
    ahtAvailable = false;
    Serial.println("I2C initialization failed");
    return;
  }

  ahtAvailable = aht.begin(&Wire);
  if (!ahtAvailable) {
    Serial.println("AHT20 not found at I2C address 0x38");
  } else {
    Serial.println("AHT20 initialized");
  }
}

void inline updateAHT20Data() {
  if (!ahtAvailable) {
    sensorSprite.fillSprite(TFT_BLACK);
    sensorSprite.drawCentreString("AHT20 offline", 120, 0);
    sensorSprite.pushSprite(0, 60);
    if (millis() - lastAhtAttempt >= AHT_RETRY_INTERVAL_MS) {
      initAHT20Wire();
    }
    return;
  }

  sensors_event_t humidity, temp;
  if (!aht.getEvent(&humidity, &temp)) {
    ahtAvailable = false;
    Serial.println("AHT20 read failed; retry scheduled");
    return;
  }
  snprintf(buf, sizeof(buf), "气温: %.2f℃ 湿度: %.2f%%", temp.temperature,
           humidity.relative_humidity);
  sensorSprite.fillSprite(TFT_BLACK);
  sensorSprite.drawCentreString(buf, 120, 0);
  sensorSprite.pushSprite(0, 60);
}

void loadUserSettings() {
  preferences.begin("netradio", false);
  curVolume = constrain(preferences.getInt("volume", 6), 0, 21);
  curIndex = preferences.getUInt("station", 0);
  if (curIndex < 0 || curIndex >= static_cast<int>(radios.size())) {
    curIndex = 0;
  }
}

void handleSettingsPersistence() {
  if (!settingsDirty || millis() - settingsChangedAt < SETTINGS_SAVE_DELAY_MS) {
    return;
  }

  preferences.putUChar("volume", static_cast<uint8_t>(curVolume));
  preferences.putUShort("station", static_cast<uint16_t>(curIndex));
  settingsDirty = false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Hello ESP-S3!!");
  constexpr uint32_t bytesPerMiB = 1024U * 1024U;
  const uint32_t flashBytes = ESP.getFlashChipSize();
  const uint32_t psramBytes = ESP.getPsramSize();
  Serial.printf("Flash: %u MB, PSRAM: %u MB (%u usable bytes)\n",
                static_cast<unsigned>((flashBytes + bytesPerMiB / 2) / bytesPerMiB),
                static_cast<unsigned>((psramBytes + bytesPerMiB / 2) / bytesPerMiB),
                static_cast<unsigned>(psramBytes));
  loadUserSettings();
  initTFTDevice();
  initAHT20Wire();
  setupButtons();
  initPixels();
  initAudioDevice();
  wifiWasConnected = autoConfigWifi();
  if (wifiWasConnected) {
    startConfigTime();
    setupOTAConfig();
    showClientIP();
  }
  showCurrentTime();
  updateAHT20Data();
  nextVolume(0);
  if (wifiWasConnected) {
    playNext(0);
  }
}

void handleConnectivity() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();

  if (!connected) {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      audio.stopSong();
      setAmplifierEnabled(false);
      radioRetryPending = false;
      if (otaReady) {
        ArduinoOTA.end();
        otaReady = false;
      }
      Serial.println("WiFi disconnected");
    }

    if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiRetry = now;
      WiFi.reconnect();
    }
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Serial.println("WiFi reconnected");
    startConfigTime();
    setupOTAConfig();
    showClientIP();
    playNext(0);
  }
}

void handleRadioRetry() {
  if (!radioRetryPending || WiFi.status() != WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastRadioAttempt < RADIO_RETRY_INTERVAL_MS) {
    return;
  }

  lastRadioAttempt = now;
  if (radioRetryCount >= MAX_RADIO_RETRIES) {
    radioRetryPending = false;
    playNext(1);
    return;
  }

  ++radioRetryCount;
  radioRetryPending = !connectCurrentRadio();
}

void loop() {
  audio.loop();
  handleConnectivity();
  handleRadioRetry();
  handleSettingsPersistence();
  if (otaReady) {
    ArduinoOTA.handle();
  }

  const uint32_t ms = millis();
  if (ms - check60s >= 60000U) {
    check60s = ms;
    updateAHT20Data();
  }
  if (ms - check1s >= 1000U) {
    check1s = ms;
    redLedState = !redLedState;
    digitalWrite(PIN_RED_LED, redLedState ? HIGH : LOW);
    showCurrentTime();
    pixels.fill(pixels.Color(random(0, 256), random(0, 256), random(0, 256)));
    pixels.show();
  }
  if (ms - check10ms >= 10U) {
    check10ms = ms;
    for (auto *button : buttons) {
      button->tick();
    }
  }
}

void audio_info(const char *info) { Serial.println(info); }

void audio_eof_stream(const char *info) {
  Serial.printf("Stream ended: %s\n", info ? info : "");
  setAmplifierEnabled(false);
  if (millis() - radioConnectedAt >= RADIO_STABLE_WINDOW_MS) {
    radioRetryCount = 0;
  }
  ++radioRetryCount;
  scheduleRadioRetry();
}

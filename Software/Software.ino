#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> 
#include <RTClib.h>
#include <DFRobot_DHT20.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "driver/pulse_cnt.h"

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800  
#define COLOR_GREEN   0x07E0  
#define COLOR_YELLOW  0xFFE0  
#define COLOR_CYAN    0x07FF  

#define TFT_CS         15  
#define TFT_DC          2  
#define TFT_RST        22  

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

#define I2C_SDA_PIN    21
#define I2C_SCL_PIN    26

#define BUTTON_PIN     4   
#define RPM_PIN        14  
#define VOLTAGE_PIN    34  

const char* ssid = "KhangKhangKhang";
const char* password = "Vinh*khang2105";
const uint32_t ONE_WEEK_IN_SECONDS = 7 * 24 * 60 * 60;

class DashboardPage {
public:
  virtual void init() = 0;
  virtual void update() = 0;
  virtual void draw(bool forceRedraw) = 0;  
};

class RPMPage : public DashboardPage {
private:
  uint32_t rpm = 0;
  uint32_t lastRpm = 99999;
  uint32_t lastUpdateTime = 0;
  pcnt_unit_handle_t pcnt_unit = NULL;
  pcnt_channel_handle_t pcnt_chan = NULL;
  const float PULSES_PER_REVOLUTION = 5.5; 
public:
  void init() override {
    pcnt_unit_config_t unit_config = { .low_limit = -32768, .high_limit = 32767 };
    pcnt_new_unit(&unit_config, &pcnt_unit);
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 12500 };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);
    pcnt_chan_config_t chan_config = { .edge_gpio_num = RPM_PIN, .level_gpio_num = -1 };
    pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan);
    pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_clear_count(pcnt_unit);
    pcnt_unit_start(pcnt_unit);
    lastUpdateTime = millis();
  }
  void update() override {
    uint32_t now = millis();
    uint32_t duration = now - lastUpdateTime;
    if (duration >= 200) { 
      int pulses = 0;
      pcnt_unit_get_count(pcnt_unit, &pulses);
      pcnt_unit_clear_count(pcnt_unit);
      lastUpdateTime = now;
      if (pulses < 0) pulses = 0;
      rpm = (pulses / PULSES_PER_REVOLUTION  * 60000) / duration;
    }
  }
  void draw(bool forceRedraw) override {
    if (forceRedraw) {
      tft.setTextSize(2); 
      tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
      tft.setCursor(10, 20); 
      tft.println("ENGINE RPM");
      lastRpm = 99999;
    }
    
    if (rpm != lastRpm || forceRedraw) {
      if (rpm > 5500) tft.setTextColor(COLOR_RED, COLOR_BLACK);
      else tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
      tft.setTextSize(5); 
      tft.setCursor(30, 90);
      tft.print(rpm);
      tft.print("      ");
      lastRpm = rpm;
    }
  }
};

class VoltsPage : public DashboardPage {
private:
  float voltage = 0.0;
  float lastVoltage = -1.0;
public:
  void init() override { pinMode(VOLTAGE_PIN, INPUT); }
  void update() override {
    int adcVal = analogRead(VOLTAGE_PIN);
    float vSample = (adcVal * 3.3) / 4095.0;
    voltage = vSample * 5.855; 
  }
  void draw(bool forceRedraw) override {
    if (forceRedraw) {
      tft.setTextSize(2); 
      tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
      tft.setCursor(10, 20); 
      tft.print("BATTERY VOLTAGE");
      lastVoltage = -1.0;
    }
    
    if (abs(voltage - lastVoltage) >= 0.1 || forceRedraw) {
      if (voltage < 11.5) tft.setTextColor(COLOR_RED, COLOR_BLACK);
      else tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
      tft.setTextSize(5); 
      tft.setCursor(40, 90);
      tft.print(voltage, 1); 
      tft.setTextSize(3); 
      tft.print(" V  ");
      lastVoltage = voltage;
    }
  }
};

class EnvPage : public DashboardPage {
private:
  DFRobot_DHT20 dht20;
  float temp = 0.0; 
  float hum = 0.0;
  float lastTemp = -99.0;
  float lastHum = -99.0;
  uint32_t lastDHTUpdateTime = 0;
  bool isConnected = false;
public:
  void init() override {
    Serial.println("🔄 Đang tiến hành tìm kiếm chuyên sâu DHT20...");
    for (int i = 1; i <= 10; i++) {
      Serial.print("Thử kết nối DHT20 lần thứ ");
      Serial.print(i); Serial.println("/10...");
      if (dht20.begin() == 0) {
        isConnected = true;
        Serial.println("✅ THÀNH CÔNG: DHT20 đã phản hồi và sẵn sàng!");
        break;
      }
      delay(400);
    }
    if (!isConnected) {
      Serial.println("❌ THẤT BẠI: Không tìm thấy DHT20.");
    }
  }
  void update() override {
    if (!isConnected) return;
    uint32_t now = millis();
    if (now - lastDHTUpdateTime >= 2000) {
      temp = dht20.getTemperature();
      hum = dht20.getHumidity() * 100.0;
      lastDHTUpdateTime = now;
    }
  }
  void draw(bool forceRedraw) override {
    if (forceRedraw) {
      tft.setTextSize(2); 
      tft.setTextColor(COLOR_CYAN, COLOR_BLACK);
      tft.setCursor(10, 20); 
      tft.print("ENVIRONMENT");
      lastTemp = -99.0;
      lastHum = -99.0;
    }
    
    if (!isConnected) {
      if (forceRedraw) {
        tft.setTextColor(COLOR_RED, COLOR_BLACK); 
        tft.setTextSize(3); 
        tft.setCursor(40, 100); 
        tft.print("DHT ERR");
      }
      return;
    }
    
    if (abs(temp - lastTemp) >= 0.1 || forceRedraw) {
      tft.setTextColor(COLOR_YELLOW, COLOR_BLACK); 
      tft.setTextSize(3); 
      tft.setCursor(10, 70); 
      tft.print("TEMP: ");
      tft.setTextColor(COLOR_WHITE, COLOR_BLACK); 
      tft.print(temp, 1); 
      tft.print(" C  ");
      lastTemp = temp;
    }
    
    if (abs(hum - lastHum) >= 0.1 || forceRedraw) {
      tft.setTextColor(COLOR_GREEN, COLOR_BLACK); 
      tft.setTextSize(3); 
      tft.setCursor(10, 130); 
      tft.print("HUMI: ");
      tft.setTextColor(COLOR_WHITE, COLOR_BLACK); 
      tft.print(hum, 1); 
      tft.print(" %  ");
      lastHum = hum;
    }
  }
};

class TimePage : public DashboardPage {
private:
  RTC_DS3231 rtc;
  Preferences prefs;
  bool isRTCConnected = false;
  uint8_t lastMinute = 99;
public:
  void init() override {
    if (!rtc.begin()) {
      Serial.println("❌ Không thấy RTC DS3231!");
      return;
    }
    isRTCConnected = true;
    Serial.println("✅ RTC DS3231 OK!");
    prefs.begin("rtc_sync", false);
    uint32_t lastSync = prefs.getUInt("last_sync", 0);
    if (rtc.lostPower()) {
      if (syncWiFiTime()) prefs.putUInt("last_sync", rtc.now().unixtime());
    } else {
      uint32_t currentSec = rtc.now().unixtime();
      if (lastSync == 0 || (currentSec - lastSync) >= ONE_WEEK_IN_SECONDS) {
        if (syncWiFiTime()) prefs.putUInt("last_sync", rtc.now().unixtime());
      }
    }
    prefs.end();
  }
  void update() override {}
  bool syncWiFiTime() {
    if (!isRTCConnected) return false;
    WiFi.begin(ssid, password);
    int check = 0;
    while (WiFi.status() != WL_CONNECTED && check < 15) { delay(400); check++; }
    if (WiFi.status() == WL_CONNECTED) {
      configTime(7 * 3600, 0, "pool.ntp.org");
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
        WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
        return true;
      }
    }
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    return false;
  }
  void draw(bool forceRedraw) override {
    if (forceRedraw) {
      tft.setTextSize(2); 
      tft.setTextColor(COLOR_YELLOW, COLOR_BLACK);
      tft.setCursor(10, 20); 
      tft.print("CLOCK SYSTEM");
      lastMinute = 99;
    }
    
    if (!isRTCConnected) {
      if (forceRedraw) {
        tft.setTextColor(COLOR_RED, COLOR_BLACK); 
        tft.setTextSize(3); 
        tft.setCursor(40, 100); 
        tft.print("RTC ERR");
      }
      return;
    }
    
    DateTime now = rtc.now();
    if (now.minute() != lastMinute || forceRedraw) {
      tft.setTextColor(COLOR_CYAN, COLOR_BLACK); 
      tft.setTextSize(5); 
      tft.setCursor(35, 95);
      char timeBuffer[9];
      sprintf(timeBuffer, "%02d:%02d", now.hour(), now.minute());
      tft.print(timeBuffer);
      lastMinute = now.minute();
    }
  }
};

RPMPage rpmPage; VoltsPage voltsPage; EnvPage envPage; TimePage timePage;
DashboardPage* pages[] = { &timePage, &voltsPage, &rpmPage, &envPage };
const int TOTAL_PAGES = 4;
int currentPageIndex = 0;
bool pageChanged = true;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  delay(500);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(100);

  tft.init(240, 240);           
  tft.setRotation(2); 
  tft.fillScreen(COLOR_BLACK);  

  for (int i = 0; i < TOTAL_PAGES; i++) {
    pages[i]->init();
    delay(50);
  }
  
  ledcAttachChannel(25, 50, 10, 0);
  ledcWrite(25, 512);
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      currentPageIndex = (currentPageIndex + 1) % TOTAL_PAGES;
      tft.fillScreen(COLOR_BLACK); 
      pageChanged = true;
      while (digitalRead(BUTTON_PIN) == LOW);
    }
  }

  pages[currentPageIndex]->update();
  pages[currentPageIndex]->draw(pageChanged);
  pageChanged = false;

  delay(200);
}
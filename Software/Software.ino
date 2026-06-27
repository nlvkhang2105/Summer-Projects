#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <DFRobot_DHT20.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "driver/pulse_cnt.h"

// --- CẤU HÌNH PHẦN CỨNG ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BUTTON_PIN     4   
#define RPM_PIN        14  
#define VOLTAGE_PIN    34  

const char* ssid = "KhangKhangKhang";
const char* password = "Vinh*khang2105";
const uint32_t ONE_WEEK_IN_SECONDS = 7 * 24 * 60 * 60;

// ====================================================================
// LỚP CHA: QUẢN LÝ TRANG
// ====================================================================
class DashboardPage {
public:
  virtual void init() = 0;   
  virtual void update() = 0; 
  virtual void draw() = 0;   
};

// --- TRANG 1: RPM ---
class RPMPage : public DashboardPage {
private:
  uint32_t rpm = 0;
  uint32_t lastUpdateTime = 0;
  pcnt_unit_handle_t pcnt_unit = NULL;
  pcnt_channel_handle_t pcnt_chan = NULL;
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
      rpm = (pulses * 60000) / duration;
    }
  }
  void draw() override {
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 0); display.println("RPM");
    display.setTextSize(3); display.setCursor(20, 25);
    display.print(rpm);
  }
};

// --- TRANG 2: ĐIỆN ÁP BÌNH ---
class VoltsPage : public DashboardPage {
private:
  float voltage = 0.0;
public:
  void init() override { pinMode(VOLTAGE_PIN, INPUT); }
  void update() override {
    int adcVal = analogRead(VOLTAGE_PIN);
    float vSample = (adcVal * 3.3) / 4095.0;
    voltage = vSample * 5.412; // Cấu hình cho cặp trở 10k và 2.2k an toàn
  }
  void draw() override {
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 0); display.print("BATTERY");
    display.setCursor(90, 0); display.print("V");
    display.setTextSize(3); display.setCursor(15, 25);
    display.print(voltage, 1);
  }
};

// --- TRANG 3: DHT20 (CÓ BẢO VỆ CHỐNG TREO MẠCH) ---
// --- TRANG 3: DHT20 (TĂNG CƯỜNG THỜI GIAN TÌM KIẾM CHỐNG TREO) ---
class EnvPage : public DashboardPage {
private:
  DFRobot_DHT20 dht20; 
  float temp = 0.0; float hum = 0.0;
  uint32_t lastDHTUpdateTime = 0;
  bool isConnected = false;

public:
  void init() override {
    Serial.println("🔄 Đang tiến hành tìm kiếm chuyên sâu DHT20...");
    
    // Tăng lên 10 lần thử, mỗi lần cách nhau 400ms để chờ cảm biến ổn định nguồn
    for (int i = 1; i <= 10; i++) {
      Serial.print("Thử kết nối DHT20 lần thứ "); Serial.print(i); Serial.println("/10...");
      
      if (dht20.begin() == 0) {
        isConnected = true;
        Serial.println("✅ THÀNH CÔNG: DHT20 đã phản hồi và sẵn sàng!");
        break; // Thoát ngay vòng lặp nếu tìm thấy
      }
      
      delay(400); // Tăng thời gian chờ giữa các lần quét
    }

    if (!isConnected) {
      Serial.println("❌ THẤT BẠI: Quá thời gian chờ, không tìm thấy DHT20. Chuyển chế độ an toàn.");
    }
  }

  void update() override {
    if (!isConnected) return;
    
    uint32_t now = millis();
    // Giữ chu kỳ đọc thưa (2 giây/lần) để bus I2C không bị quá tải dữ liệu
    if (now - lastDHTUpdateTime >= 2000) { 
      temp = dht20.getTemperature();
      hum = dht20.getHumidity() * 100.0; 
      lastDHTUpdateTime = now;
    }
  }

  void draw() override {
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 0); display.print("TEMP");
    
    if (!isConnected) {
      display.setTextSize(2); display.setCursor(15, 25); display.print("DHT ERR");
      return;
    }
    
    display.setTextSize(2); display.setCursor(0, 12); display.print(temp, 1);
    display.setTextSize(1); display.setCursor(0, 35); display.print("HUMI %");
    display.setTextSize(2); display.setCursor(0, 47); display.print(hum, 1);
  }
};

// --- TRANG 4: DS3231 (CÓ BẢO VỆ CHỐNG TREO MẠCH) ---
class TimePage : public DashboardPage {
private:
  RTC_DS3231 rtc; Preferences prefs;
  bool isRTCConnected = false;
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
  void draw() override {
    if (!isRTCConnected) {
      display.setTextSize(2); display.setCursor(15, 25); display.print("RTC ERR");
      return;
    }
    DateTime now = rtc.now();
    display.setTextColor(SSD1306_WHITE); display.setTextSize(2); display.setCursor(15, 25);
    char timeBuffer[9];
    sprintf(timeBuffer, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    display.print(timeBuffer);
  }
};

// --- KERNEL HỆ THỐNG ---
RPMPage rpmPage; VoltsPage voltsPage; EnvPage envPage; TimePage timePage;
DashboardPage* pages[] = { &rpmPage, &voltsPage, &envPage, &timePage };
const int TOTAL_PAGES = 4;
int currentPageIndex = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  delay(500);

  // Khởi tạo cứng Bus I2C một lần duy nhất cho toàn hệ thống
  Wire.begin(23, 18); 
  delay(100); // Trễ nhẹ ổn định điện áp Bus

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("❌ Lỗi OLED!"));
    for(;;);
  }
  display.clearDisplay();

  // Gọi khởi tạo các lớp con
  for (int i = 0; i < TOTAL_PAGES; i++) {
    pages[i]->init();
    delay(50); // Giãn cách khởi tạo các thiết bị I2C để tránh sụt dòng đỉnh
  }
  ledcAttachChannel(25, 50, 10, 0); // Chân 25, tần số 50Hz, độ phân giải 10-bit, kênh 0
  ledcWrite(25, 512);
}

void loop() {
  // Đọc nút nhấn lật trang
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); 
    if (digitalRead(BUTTON_PIN) == LOW) {
      currentPageIndex = (currentPageIndex + 1) % TOTAL_PAGES; 
      while (digitalRead(BUTTON_PIN) == LOW); 
    }
  }

  // Cập nhật dữ liệu từ cảm biến
  pages[currentPageIndex]->update();

  // Vẽ giao diện trang hiện tại
  display.clearDisplay();
  pages[currentPageIndex]->draw();
  display.display();

  delay(33); // Khống chế tần suất quét màn hình ổn định
}
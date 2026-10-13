#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
#include <ESP32Ping.h>
#include <Preferences.h>

// ======================= KONFIGURASI =======================
const char* ssid     = "SECRET_SSID";
const char* password = "SECRET_PASS";

String apiKey = "SECRET_API_KEY";
String city   = "Bekasi,ID";

// ----- PIN TOMBOL & OUTPUT -----
#define PIN_BTN_SET    32
#define PIN_BTN_UP     33
#define PIN_BTN_DOWN   25
#define PIN_BTN_LEFT   34   // input only, pakai pull-up eksternal 10k
#define PIN_BTN_RIGHT  35   // input only, pakai pull-up eksternal 10k
#define PIN_RELAY      15
#define RELAY_ACTIVE_LOW true   // true jika modul relay aktif LOW

const unsigned long LONG_PRESS_MS   = 3000;
const unsigned long REPEAT_DELAY_MS = 500;
const unsigned long REPEAT_RATE_MS  = 120;
const unsigned long EDIT_TIMEOUT_MS = 30000;
const unsigned long ALARM_MAX_MS    = 60000;
const unsigned long weatherInterval = 600000;

// ----- SERVER -----
struct ServerHost {
  const char*  name;
  IPAddress    ip;
  volatile bool online;
  int          shown;     // status yang sedang tampil (-1 = belum digambar)
};

ServerHost hosts[] = {
  {"Gateway", IPAddress(10, 10, 10, 1),   false, -1},
  {"HG680P",  IPAddress(10, 10, 10, 56),  false, -1},
  {"TrueNas", IPAddress(10, 10, 10, 130), false, -1},
  {"Google",  IPAddress(8, 8, 8, 8),      false, -1}
};
const int HOST_COUNT = sizeof(hosts) / sizeof(hosts[0]);

// ----- WARNA (RGB565) -----
#define RGB565(r,g,b) ((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|(((b)&0xF8)>>3))
const uint16_t C_BG     = RGB565(10, 14, 24);
const uint16_t C_CARD   = RGB565(22, 28, 44);
const uint16_t C_ROW    = RGB565(32, 40, 62);
const uint16_t C_ACCENT = RGB565(0, 200, 255);
const uint16_t C_TEXT   = RGB565(235, 240, 250);
const uint16_t C_MUTED  = RGB565(120, 132, 158);
const uint16_t C_GREEN  = RGB565(0, 230, 130);
const uint16_t C_RED    = RGB565(255, 85, 95);

const char* NAMA_HARI[]  = {"Minggu","Senin","Selasa","Rabu","Kamis","Jumat","Sabtu"};
const char* NAMA_BULAN[] = {"Januari","Februari","Maret","April","Mei","Juni",
                            "Juli","Agustus","September","Oktober","November","Desember"};

// ----- TOMBOL -----
enum BtnEvent { EV_NONE, EV_PRESS, EV_SHORT, EV_LONG };

struct Btn {
  uint8_t       pin;
  bool          repeatable;   // true = tahan untuk auto-repeat (arrow)
  bool          stable;       // true = lepas
  bool          lastRaw;
  unsigned long lastChange;
  unsigned long pressedAt;
  unsigned long nextRepeat;
  bool          longFired;
};

Btn bSet   = {PIN_BTN_SET,   false, true, true, 0, 0, 0, false};
Btn bUp    = {PIN_BTN_UP,    true,  true, true, 0, 0, 0, false};
Btn bDown  = {PIN_BTN_DOWN,  true,  true, true, 0, 0, 0, false};
Btn bLeft  = {PIN_BTN_LEFT,  true,  true, true, 0, 0, 0, false};
Btn bRight = {PIN_BTN_RIGHT, true,  true, true, 0, 0, 0, false};

// ----- ALARM -----
struct Alarm { uint8_t h, m; };
Alarm alarms[2]    = {{6, 15}, {6, 30}};
Alarm tmpAlarms[2];
Preferences prefs;

bool          editMode         = false;
int           editField        = 0;      // 0=A1 jam, 1=A1 menit, 2=A2 jam, 3=A2 menit
unsigned long lastEditActivity = 0;
bool          ringing          = false;
unsigned long ringStart        = 0;
int           lastCheckedMin   = -1;
bool          alarmDirty       = true;

// ----- OBJEK & STATE -----
TFT_eSPI tft = TFT_eSPI();
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000); // WIB = UTC+7

unsigned long lastClockDraw     = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastHostDraw      = 0;
int           lastYday          = -1;

float  tempValue   = 0;
String weatherMain = "";
bool   weatherOK   = false;

// ======================= PROTOTIPE FUNGSI =======================
BtnEvent readBtn(Btn &b);
void handleSet(BtnEvent ev);
void onArrow(BtnEvent ev, int action);
void adjustField(int d);

void startEdit();
void saveEdit();
void cancelEdit();
void loadAlarms();
void saveAlarms();

void setRelay(bool on);
void startRinging();
void stopRinging();
void checkAlarm();

void getWeather();
void pingTask(void* param);

void drawStaticLayout();
void drawClock();
void updateDate();
void drawWeatherCard();
void drawAlarmCard();
void drawHostCells();
void drawHostCell(int i);

// ======================= SETUP & LOOP =======================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_SET,   INPUT_PULLUP);
  pinMode(PIN_BTN_UP,    INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,  INPUT);
  pinMode(PIN_BTN_RIGHT, INPUT);
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  loadAlarms();

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(C_BG);
  tft.setTextFont(4);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setCursor(20, 140);
  tft.print("Menghubungkan WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  tft.fillScreen(C_BG);
  tft.setCursor(20, 140);
  tft.print("Sinkronisasi waktu...");

  timeClient.begin();
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(500);
  }
  lastCheckedMin = timeClient.getHours() * 60 + timeClient.getMinutes();

  drawStaticLayout();
  updateDate();
  getWeather();
  drawWeatherCard();
  drawAlarmCard();

  // Ping dijalankan di task terpisah agar tombol tetap responsif
  xTaskCreatePinnedToCore(pingTask, "ping", 4096, NULL, 1, NULL, 0);

  lastWeatherUpdate = millis();
  lastClockDraw     = millis();
}

void loop() {
  timeClient.update();
  unsigned long now = millis();

  // --- Tombol ---
  handleSet(readBtn(bSet));
  onArrow(readBtn(bUp),    0);
  onArrow(readBtn(bDown),  1);
  onArrow(readBtn(bLeft),  2);
  onArrow(readBtn(bRight), 3);

  if (editMode && now - lastEditActivity > EDIT_TIMEOUT_MS) cancelEdit();

  checkAlarm();

  // --- Gambar ulang berkala ---
  if (now - lastClockDraw >= 1000) {
    lastClockDraw = now;
    drawClock();
    updateDate();
  }
  if (now - lastWeatherUpdate >= weatherInterval) {
    lastWeatherUpdate = now;
    getWeather();
    drawWeatherCard();
  }
  if (now - lastHostDraw >= 500) {
    lastHostDraw = now;
    drawHostCells();
  }
  if (alarmDirty) {
    alarmDirty = false;
    drawAlarmCard();
  }
}

// ======================= TOMBOL =======================

BtnEvent readBtn(Btn &b) {
  unsigned long now = millis();
  bool raw = digitalRead(b.pin);      // HIGH = lepas, LOW = tekan

  if (raw != b.lastRaw) {             // debounce
    b.lastRaw    = raw;
    b.lastChange = now;
  }
  if (now - b.lastChange >= 30 && raw != b.stable) {
    b.stable = raw;
    if (!b.stable) {                  // baru ditekan
      b.pressedAt  = now;
      b.nextRepeat = now + REPEAT_DELAY_MS;
      b.longFired  = false;
      return EV_PRESS;
    }
    return b.longFired ? EV_NONE : EV_SHORT;   // dilepas
  }

  if (!b.stable) {                    // masih ditekan
    if (!b.repeatable && !b.longFired && now - b.pressedAt >= LONG_PRESS_MS) {
      b.longFired = true;
      return EV_LONG;
    }
    if (b.repeatable && now >= b.nextRepeat) {
      b.nextRepeat = now + REPEAT_RATE_MS;
      return EV_PRESS;                // auto-repeat
    }
  }
  return EV_NONE;
}

void handleSet(BtnEvent ev) {
  if (ev == EV_SHORT) {
    if (ringing)       stopRinging();
    else if (editMode) cancelEdit();
  } else if (ev == EV_LONG && !ringing) {
    if (!editMode) startEdit();
    else           saveEdit();
  }
}

// action: 0=UP, 1=DOWN, 2=LEFT, 3=RIGHT
void onArrow(BtnEvent ev, int action) {
  if (ev != EV_PRESS) return;
  if (ringing) { stopRinging(); return; }   // tekan apa saja untuk matikan alarm
  if (!editMode) return;

  lastEditActivity = millis();
  switch (action) {
    case 0: adjustField(+1);  break;
    case 1: adjustField(-1);  break;
    case 2: editField = (editField + 3) % 4; break;
    case 3: editField = (editField + 1) % 4; break;
  }
  alarmDirty = true;
}

void adjustField(int d) {
  int idx = editField / 2;
  if (editField % 2 == 0) {
    tmpAlarms[idx].h = (tmpAlarms[idx].h + d + 24) % 24;
  } else {
    tmpAlarms[idx].m = (tmpAlarms[idx].m + d + 60) % 60;
  }
}

// ======================= EDIT ALARM =======================

void startEdit() {
  editMode = true;
  editField = 0;
  tmpAlarms[0] = alarms[0];
  tmpAlarms[1] = alarms[1];
  lastEditActivity = millis();
  alarmDirty = true;
}

void saveEdit() {
  alarms[0] = tmpAlarms[0];
  alarms[1] = tmpAlarms[1];
  saveAlarms();
  editMode = false;
  alarmDirty = true;
}

void cancelEdit() {
  editMode = false;
  alarmDirty = true;
}

void loadAlarms() {
  prefs.begin("alarm", true);
  alarms[0].h = prefs.getUChar("a0h", 6);
  alarms[0].m = prefs.getUChar("a0m", 15);
  alarms[1].h = prefs.getUChar("a1h", 6);
  alarms[1].m = prefs.getUChar("a1m", 30);
  prefs.end();
}

void saveAlarms() {
  prefs.begin("alarm", false);
  prefs.putUChar("a0h", alarms[0].h);
  prefs.putUChar("a0m", alarms[0].m);
  prefs.putUChar("a1h", alarms[1].h);
  prefs.putUChar("a1m", alarms[1].m);
  prefs.end();
}

// ======================= ALARM & RELAY =======================

void setRelay(bool on) {
  digitalWrite(PIN_RELAY, (on ^ RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void startRinging() {
  ringing    = true;
  ringStart  = millis();
  alarmDirty = true;
}

void stopRinging() {
  ringing = false;
  setRelay(false);
  alarmDirty = true;
}

void checkAlarm() {
  if (ringing) {
    unsigned long t = millis() - ringStart;
    if (t >= ALARM_MAX_MS) { stopRinging(); return; }
    setRelay((t / 500) % 2 == 0);     // buzzer berdenyut 0.5 dtk on/off
    return;
  }

  int nowMin = timeClient.getHours() * 60 + timeClient.getMinutes();
  if (nowMin == lastCheckedMin) return;
  lastCheckedMin = nowMin;

  for (int i = 0; i < 2; i++) {
    if (alarms[i].h * 60 + alarms[i].m == nowMin) {
      startRinging();
      break;
    }
  }
}

// ======================= DATA =======================

void getWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + city +
               "&appid=" + apiKey + "&units=metric";
  http.begin(url);
  int code = http.GET();

  if (code == 200) {
    DynamicJsonDocument doc(1536);
    if (!deserializeJson(doc, http.getString())) {
      tempValue   = doc["main"]["temp"].as<float>();
      weatherMain = doc["weather"][0]["main"].as<String>();
      weatherOK   = true;
    } else {
      weatherOK = false;
    }
  } else {
    weatherOK = false;
    Serial.printf("Gagal ambil cuaca, HTTP Code: %d\n", code);
  }
  http.end();
}

void pingTask(void* param) {
  for (;;) {
    for (int i = 0; i < HOST_COUNT; i++) {
      if (WiFi.status() == WL_CONNECTED) {
        hosts[i].online = Ping.ping(hosts[i].ip, 1);
      } else {
        hosts[i].online = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(8000));
  }
}

// ======================= GAMBAR =======================

void drawStaticLayout() {
  tft.fillScreen(C_BG);

  // Garis pemisah bagian atas dan bawah
  tft.drawFastHLine(10, 160, 460, C_ROW);

  // Kartu server (isi di-update terpisah)
  tft.fillRoundRect(172, 166, 302, 148, 10, C_CARD);
  tft.setTextFont(2);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setCursor(184, 174);
  tft.print("SERVER");
  tft.drawFastHLine(182, 198, 282, C_ROW);

  for (int i = 0; i < HOST_COUNT; i++) hosts[i].shown = -1;
}

void drawClock() {
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());

  // Font 7 = gaya 7-segment, dibesarkan 2x
  tft.setTextSize(2);
  tft.setTextFont(7);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setCursor(10, 10);
  tft.print(buf);

  int sx = 10 + tft.textWidth(buf) + 6;
  if (sx > 304) sx = 304;            // jaga agar tidak menabrak kartu cuaca
  tft.setTextSize(1);                // kembalikan ukuran normal untuk teks lain

  char sec[3];
  snprintf(sec, sizeof(sec), "%02d", timeClient.getSeconds());

  tft.setTextFont(4);
  tft.setTextColor(C_MUTED, C_BG);
  tft.fillRect(sx, 78, 36, 26, C_BG);
  tft.setCursor(sx, 78);
  tft.print(sec);
}

void updateDate() {
  time_t epoch = timeClient.getEpochTime();   // sudah termasuk offset WIB
  struct tm* ti = gmtime(&epoch);
  if (ti->tm_yday == lastYday) return;
  lastYday = ti->tm_yday;

  String s = String(NAMA_HARI[ti->tm_wday]) + ", " + String(ti->tm_mday) + " " +
             NAMA_BULAN[ti->tm_mon] + " " + String(ti->tm_year + 1900);

  tft.fillRect(10, 120, 330, 30, C_BG);
  // TFT_eSPI tidak punya font 3, jadi dipakai FreeSans 9pt (ukuran di antara font 2 dan 4)
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setCursor(14, 142);            // untuk FreeFont, Y = garis dasar teks
  tft.print(s);
  tft.setFreeFont(NULL);             // kembalikan ke font bitmap biasa
}

void drawWeatherCard() {
  const int x = 344, y = 8, w = 130, h = 146;
  tft.fillRoundRect(x, y, w, h, 10, C_CARD);

  tft.setTextFont(2);
  tft.setTextColor(C_MUTED, C_CARD);
  tft.setCursor(x + 10, y + 8);
  tft.print("Cuaca");

  tft.setTextColor(C_TEXT, C_CARD);
  tft.setCursor(x + 8, y + 24);
  tft.print("Bekasi, Jawa Barat");

  if (!weatherOK) {
    tft.setTextColor(C_RED, C_CARD);
    tft.setCursor(x + 10, y + 90);
    tft.print("Gagal ambil");
    return;
  }

  tft.setTextFont(4);
  tft.setTextColor(C_MUTED, C_CARD);
  int cw = tft.textWidth(weatherMain);
  tft.setCursor(x + (w - cw) / 2, y + 46);
  tft.print(weatherMain);

  String ts = String((int)round(tempValue));
  tft.setTextFont(6);
  tft.setTextColor(C_TEXT, C_CARD);
  tft.setCursor(x + 10, y + 78);
  tft.print(ts);
  int nw = tft.textWidth(ts);

  // Simbol derajat digambar manual karena font 6 tidak punya simbol °
  tft.drawCircle(x + 10 + nw + 6, y + 84, 4, C_TEXT);
  tft.setTextFont(4);
  tft.setCursor(x + 10 + nw + 12, y + 100);
  tft.print("C");
}

void drawAlarmCard() {
  const int x = 6, y = 166, w = 160, h = 148;
  tft.fillRoundRect(x, y, w, h, 10, C_CARD);

  tft.setTextFont(2);
  tft.setCursor(x + 10, y + 8);
  if (ringing) {
    tft.setTextColor(C_RED, C_CARD);
    tft.print("ALARM BERBUNYI!");
  } else {
    tft.setTextColor(C_MUTED, C_CARD);
    tft.print(editMode ? "Edit alarm" : "Alarm set");
  }

  Alarm* src = editMode ? tmpAlarms : alarms;

  for (int i = 0; i < 2; i++) {
    int ry = y + 36 + i * 52;
    char hh[3], mm[3];
    snprintf(hh, sizeof(hh), "%02d", (int)src[i].h);
    snprintf(mm, sizeof(mm), "%02d", (int)src[i].m);

    bool selH = editMode && editField == i * 2;
    bool selM = editMode && editField == i * 2 + 1;

    tft.setTextFont(4);
    int cx = x + 12;

    tft.setTextColor(selH ? C_BG : C_TEXT, selH ? C_ACCENT : C_CARD);
    tft.setCursor(cx, ry);
    tft.print(hh);
    cx += tft.textWidth(hh);

    tft.setTextColor(C_TEXT, C_CARD);
    tft.setCursor(cx, ry);
    tft.print(":");
    cx += tft.textWidth(":");

    tft.setTextColor(selM ? C_BG : C_TEXT, selM ? C_ACCENT : C_CARD);
    tft.setCursor(cx, ry);
    tft.print(mm);
  }

  if (editMode) {
    tft.setTextFont(2);
    tft.setTextColor(C_MUTED, C_CARD);
    tft.setCursor(x + 10, y + h - 22);
    tft.print("SET lama: simpan");
  }
}

void drawHostCells() {
  for (int i = 0; i < HOST_COUNT; i++) {
    int st = hosts[i].online ? 1 : 0;
    if (st != hosts[i].shown) {
      drawHostCell(i);
      hosts[i].shown = st;
    }
  }
}

void drawHostCell(int i) {
  int col = i % 2, row = i / 2;
  int x = 172 + 8 + col * 150;
  int y = 166 + 41 + row * 53;
  const int w = 139, h = 47;
  bool on = hosts[i].online;

  tft.fillRoundRect(x, y, w, h, 6, C_ROW);

  int cx = x + 22, cy = y + h / 2;
  if (on) {
    tft.fillCircle(cx, cy, 10, C_GREEN);
  } else {
    tft.drawCircle(cx, cy, 10, C_RED);
    tft.drawCircle(cx, cy, 9, C_RED);
  }

  tft.setTextFont(2);
  tft.setTextColor(C_TEXT, C_ROW);
  tft.setCursor(x + 40, y + 7);
  tft.print(hosts[i].name);

  tft.setTextColor(C_MUTED, C_ROW);
  tft.setCursor(x + 40, y + 25);
  tft.print(hosts[i].ip.toString());
}
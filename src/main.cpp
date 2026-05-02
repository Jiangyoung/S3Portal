#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>

// Copy include/secrets.example.h to include/secrets.h and fill local credentials.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h. Copy include/secrets.example.h to include/secrets.h and fill WiFi/server config."
#endif

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined in include/secrets.h"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD is not defined in include/secrets.h"
#endif
#ifndef SERVER_URL
#error "SERVER_URL is not defined in include/secrets.h"
#endif

// ── 运行参数 ────────────────────────────────────────────────────────────────
#define HTTP_TIMEOUT_MS 15000
#define DISPLAY_DURATION_MS 10000
#define WIFI_RETRY_INTERVAL_MS 30000
// ─────────────────────────────────────────────────────────────────────────────

// GPIO：按键模块 K1-K8（active-LOW，模块自带上拉）
static const int BUTTON_PINS[8] = {4, 5, 6, 7, 15, 16, 17, 18};
static const int NUM_BUTTONS    = 8;
static const char* BUTTON_TITLES[8] = {
    "今日咒语", "赛博签", "反emo", "脑洞任务",
    "冷幽默", "猫语翻译", "随机冒险", "未来预言"
};
static const char* BUTTON_LOADING_TEXTS[8] = {
    "正在给今天施法",
    "正在抽取赛博签",
    "正在驱散低气压",
    "正在加载脑洞任务",
    "正在冷却幽默",
    "正在翻译猫语",
    "正在开启小冒险",
    "正在读取明天"
};

// OLED SSD1306 128×64 I2C（SCL=14, SDA=13）
// 使用软件 I2C 避免部分 ESP32-S3 + U8g2 硬件 I2C 初始化卡在 u8g2.begin()。
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, 14, 13, U8X8_PIN_NONE);

// ── 状态机 ───────────────────────────────────────────────────────────────────
enum State { IDLE, CONNECTING_WIFI, REQUESTING, SHOWING_RESULT };
static State         state         = CONNECTING_WIFI;
static unsigned long stateEnterMs  = 0;
static bool          isRequesting  = false;
static unsigned long lastWiFiAttemptMs = 0;

// ── 按键去抖 ─────────────────────────────────────────────────────────────────
static unsigned long lastDebounceMs[8]  = {0};
static bool          lastButtonPressed[8] = {false};
static bool          lastRawPressed[8] = {false};
static const unsigned long DEBOUNCE_MS  = 50;

// ── OLED 工具：UTF-8 中文自动换行显示 ────────────────────────────────────────
static void drawWrappedText(const String& text, int startY) {
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    const int FONT_H     = 13;
    const int MAX_LINE_W = 126;

    int x = 0, y = startY;
    int i = 0;
    int len = (int)text.length();

    while (i < len && y <= 64) {
        unsigned char c = (unsigned char)text[i];
        char buf[5] = {0};
        int  step   = 1;

        if (c < 0x80) {
            buf[0] = text[i]; step = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
            buf[0] = text[i]; buf[1] = text[i+1]; step = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len) {
            buf[0] = text[i]; buf[1] = text[i+1]; buf[2] = text[i+2]; step = 3;
        } else {
            i++; continue;
        }

        int charW = u8g2.getUTF8Width(buf);
        if (x + charW > MAX_LINE_W) { x = 0; y += FONT_H; }
        if (y > 64) break;
        u8g2.drawUTF8(x, y, buf);
        x += charW;
        i += step;
    }
}

static void drawResultScreen(int buttonId, const String& text) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    char title[32];
    snprintf(title, sizeof(title), "K%d %s", buttonId, BUTTON_TITLES[buttonId - 1]);
    u8g2.drawUTF8(0, 12, title);
    u8g2.drawHLine(0, 15, 128);
    drawWrappedText(text, 29);
    u8g2.sendBuffer();
}

static void showButtonLoading(int buttonId) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    char title[32];
    snprintf(title, sizeof(title), "K%d %s", buttonId, BUTTON_TITLES[buttonId - 1]);
    u8g2.drawUTF8(0, 12, title);
    u8g2.drawHLine(0, 15, 128);
    u8g2.drawUTF8(0, 32, BUTTON_LOADING_TEXTS[buttonId - 1]);
    u8g2.sendBuffer();
}

static void showStatus(const char* line1, const char* line2 = nullptr) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.drawUTF8(0, 14, line1);
    if (line2) u8g2.drawUTF8(0, 30, line2);
    u8g2.sendBuffer();
}

static void scanI2C() {
    Serial.println("I2C scan start (SDA=13, SCL=14)");
    byte found = 0;
    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        byte err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("I2C device found at 0x%02X\n", addr);
            found++;
        } else if (err == 5) {
            Serial.printf("I2C timeout at 0x%02X, bus may be held low\n", addr);
            break;
        }
    }
    if (found == 0) {
        Serial.println("I2C scan found no devices. Check OLED VCC/GND/SCL/SDA.");
    }
}

static void printButtonLevels() {
    Serial.println("Button pin levels (idle should be HIGH/1, pressed should be LOW/0):");
    for (int i = 0; i < NUM_BUTTONS; i++) {
        Serial.printf("K%d GPIO%d = %d\n", i + 1, BUTTON_PINS[i], digitalRead(BUTTON_PINS[i]));
    }
}

// ── WiFi 连接 ─────────────────────────────────────────────────────────────────
static void connectWiFi() {
    lastWiFiAttemptMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting WiFi");
    showStatus("连接 WiFi...", WIFI_SSID);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected: " + WiFi.localIP().toString());
        showStatus("WiFi 已连接", WiFi.localIP().toString().c_str());
        delay(1000);
    } else {
        Serial.println("\nWiFi failed");
        showStatus("WiFi 连接失败", "稍后重试...");
        delay(800);
    }
}

// ── HTTP 请求服务器 ───────────────────────────────────────────────────────────
static String requestText(int buttonId) {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) return "网络未连接";
    }

    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT_MS);

    String body = "{\"button_id\":" + String(buttonId) + "}";
    int code = http.POST(body);

    String result = "服务繁忙，请稍后";
    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            const char* text = doc["text"];
            if (text) result = String(text);
        }
    } else {
        Serial.printf("HTTP error: %d\n", code);
    }
    http.end();
    return result;
}

// ── 待机界面 ──────────────────────────────────────────────────────────────────
static void showIdle() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2.drawUTF8(0, 12, "趣味生成器");
    u8g2.drawHLine(0, 15, 128);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(4, 34, "Press K1-K8");
    if (WiFi.status() == WL_CONNECTED) {
        u8g2.drawStr(4, 52, WiFi.localIP().toString().c_str());
    } else {
        u8g2.drawStr(4, 52, "WiFi disconnected");
    }
    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("ESP32-S3 Button OLED demo booting...");
    Serial.printf("WiFi SSID: %s\n", WIFI_SSID);
    Serial.printf("Server URL: %s\n", SERVER_URL);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        Serial.printf("Setup button K%d on GPIO%d\n", i + 1, BUTTON_PINS[i]);
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    }

    Serial.println("Wire.begin(SDA=13, SCL=14)...");
    Wire.begin(13, 14); // SDA=13, SCL=14
    Wire.setTimeOut(50);
    Serial.println("Wire.begin done");

    scanI2C();
    printButtonLevels();

    Serial.println("u8g2.begin() using SW I2C...");
    u8g2.setI2CAddress(0x3C << 1);
    u8g2.setBusClock(100000);
    u8g2.begin();
    u8g2.enableUTF8Print();
    Serial.println("u8g2.begin done");

    showStatus("启动中...");
    connectWiFi();

    state = IDLE;
    showIdle();
}

void loop() {
    // WiFi 掉线检测：限频重试，避免长时间阻塞按键扫描
    if (state == IDLE && WiFi.status() != WL_CONNECTED && millis() - lastWiFiAttemptMs >= WIFI_RETRY_INTERVAL_MS) {
        connectWiFi();
        showIdle();
    }

    // 显示超时返回待机
    if (state == SHOWING_RESULT && millis() - stateEnterMs >= DISPLAY_DURATION_MS) {
        state = IDLE;
        showIdle();
    }

    // 请求进行中不响应按键
    if (isRequesting) return;

    // 按键扫描（边沿检测：HIGH→LOW）
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool pressed = (digitalRead(BUTTON_PINS[i]) == LOW);

        if (pressed != lastRawPressed[i]) {
            Serial.printf("K%d GPIO%d raw changed: %s\n", i + 1, BUTTON_PINS[i], pressed ? "LOW/PRESSED" : "HIGH/RELEASED");
            lastRawPressed[i] = pressed;
        }

        if (pressed && !lastButtonPressed[i] && millis() - lastDebounceMs[i] > DEBOUNCE_MS) {
            lastDebounceMs[i] = millis();

            int buttonId = i + 1;
            Serial.printf("Button K%d pressed\n", buttonId);

            isRequesting = true;
            state = REQUESTING;
            showButtonLoading(buttonId);

            String text = requestText(buttonId);
            Serial.println("Response: " + text);

            drawResultScreen(buttonId, text);

            state = SHOWING_RESULT;
            stateEnterMs = millis();
            isRequesting = false;
        }

        lastButtonPressed[i] = pressed;
    }
}

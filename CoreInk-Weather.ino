#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "M5CoreInk.h"
#include "esp_adc_cal.h"
#include "images/background.c"
#include "images/cloudy.c"
#include "images/rainy.c"
#include "images/rainyandcloudy.c"
#include "images/snow.c"
#include "images/sunny.c"
#include "images/sunnyandcloudy.c"

Ink_Sprite dateSprite(&M5.M5Ink);
Ink_Sprite weatherSprite(&M5.M5Ink);
Ink_Sprite temperatureSprite(&M5.M5Ink);
Ink_Sprite rainfallChanceSprite(&M5.M5Ink);
Ink_Sprite statusSprite(&M5.M5Ink);

const char* endpoint = "https://www.drk7.jp/weather/json/13.js";
const char* region = "東京地方";

DynamicJsonDocument* weatherInfo = nullptr;

// ディープスリープ間隔（秒）：1時間ごとに起動して天気を更新
const int SLEEP_INTERVAL_SEC = 3600;

// WiFi設定（自分の環境に合わせて変更してください）
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ステータスメッセージ表示用ヘルパー（1〜2行）
void showStatus(const char* line1, const char* line2 = nullptr) {
    M5.M5Ink.clear();
    statusSprite.creatSprite(0, 0, 200, 200);
    statusSprite.clear();
    statusSprite.drawString(10, 50, line1, &AsciiFont8x16);
    if (line2) {
        statusSprite.drawString(10, 80, line2, &AsciiFont8x16);
    }
    statusSprite.pushSprite();
    statusSprite.deleteSprite();
}

void setup() {
    M5.begin();
    Wire.begin();
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=== M5CoreInk Weather Start ===");
    Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
    Serial.flush();

    M5.M5Ink.clear();
    showStatus("Starting...");
    delay(1000);

    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(500);

    Serial.printf("Connecting to: %s\n", WIFI_SSID);
    Serial.flush();
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    dateSprite.creatSprite(0,0,200,200);
    weatherSprite.creatSprite(0,0,200,200);
    temperatureSprite.creatSprite(0,0,200,200);
    rainfallChanceSprite.creatSprite(0,0,200,200);

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 30) {
        delay(1000);
        timeout++;

        wl_status_t status = WiFi.status();
        Serial.printf("[%2d] WiFi status: %d\n", timeout, status);
        Serial.flush();

        if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
            Serial.println("Retrying WiFi...");
            WiFi.disconnect();
            delay(1000);
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }

        if (timeout % 5 == 0) {
            String timeoutStr = "Timeout: " + String(timeout);
            showStatus("Connecting...", timeoutStr.c_str());
        }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.flush();

        showStatus("WiFi OK");
        delay(1000);

        Serial.println("Getting weather data...");
        Serial.flush();
        bool success = fetchWeatherJson();

        WiFi.disconnect();
        Serial.println("WiFi disconnected");
        Serial.flush();

        if (success) {
            Serial.println("Drawing weather...");
            Serial.flush();
            drawTodayWeather();
            Serial.println("Setup complete!");
        } else {
            showStatus("Data Error", "See Serial");
        }

    } else {
        Serial.println("WiFi connection FAILED!");
        showStatus("WiFi Error", "Check SSID/Pass");
    }

    // 描画後にディープスリープへ移行（次回起動時にsetup()から再実行される）
    Serial.printf("Going to deep sleep for %d seconds...\n", SLEEP_INTERVAL_SEC);
    Serial.flush();
    M5.shutdown(SLEEP_INTERVAL_SEC);
}
 
void loop() {
    // M5.shutdown() が失敗した場合のフォールバック
    // （USB接続中はshutdownが効かないため）
    delay(SLEEP_INTERVAL_SEC * 1000UL);
    ESP.restart();
}

bool fetchWeatherJson() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    Serial.printf("Free heap before fetch: %d\n", ESP.getFreeHeap());
    Serial.flush();
    
    WiFiClientSecure client;
    client.setInsecure();  // 証明書検証スキップ（メモリ節約）

    HTTPClient http;
    http.begin(client, endpoint);
    http.setTimeout(10000);
    
    Serial.println("HTTP GET request...");
    Serial.flush();
    
    int httpCode = http.GET();
    Serial.printf("HTTP Code: %d\n", httpCode);
    Serial.flush();
    
    bool success = false;
    
    if (httpCode == 200) {
        String payload = http.getString();
        Serial.printf("Payload length: %d\n", payload.length());
        Serial.flush();
        
        if (payload.length() == 0) {
            Serial.println("ERROR: Empty payload!");
            http.end();
            return false;
        }
        
        Serial.print("Payload start: ");
        Serial.println(payload.substring(0, 50));
        Serial.flush();
        
        // JSONP → JSON変換
        int startIdx = payload.indexOf('(');
        int endIdx = payload.lastIndexOf(')');
        if (startIdx == -1 || endIdx == -1) {
            Serial.println("ERROR: JSONP format invalid!");
            http.end();
            return false;
        }
        String jsonString = payload.substring(startIdx + 1, endIdx);
        payload = "";  // メモリ解放
        
        Serial.printf("JSON length: %d\n", jsonString.length());
        Serial.printf("Free heap before parse: %d\n", ESP.getFreeHeap());
        Serial.flush();
        
        // ヒープにJSON確保
        if (weatherInfo != nullptr) {
            delete weatherInfo;
        }
        weatherInfo = new DynamicJsonDocument(30000);
        
        DeserializationError err = deserializeJson(*weatherInfo, jsonString);
        jsonString = "";  // メモリ解放
        
        if (err) {
            Serial.print("deserializeJson FAILED: ");
            Serial.println(err.c_str());
        } else {
            Serial.println("JSON parsed OK");
            Serial.flush();
            
            // キーを段階的に検証
            Serial.printf("  pref: %s\n", (*weatherInfo)["pref"].isNull() ? "NULL" : "OK");
            Serial.printf("  area: %s\n", (*weatherInfo)["pref"]["area"].isNull() ? "NULL" : "OK");
            Serial.printf("  region: %s\n", (*weatherInfo)["pref"]["area"][region].isNull() ? "NULL" : "OK");
            Serial.printf("  info: %s\n", (*weatherInfo)["pref"]["area"][region]["info"].isNull() ? "NULL" : "OK");
            Serial.printf("  info[0]: %s\n", (*weatherInfo)["pref"]["area"][region]["info"][0].isNull() ? "NULL" : "OK");
            Serial.flush();
            
            JsonVariant info0 = (*weatherInfo)["pref"]["area"][region]["info"][0];
            if (!info0.isNull()) {
                const char* w = info0["weather"];
                Serial.printf("  weather: %s\n", w ? w : "(null)");
                success = true;
            }
        }
    } else if (httpCode < 0) {
        Serial.printf("HTTP connection failed: %s\n", http.errorToString(httpCode).c_str());
    } else {
        Serial.printf("HTTP error code: %d\n", httpCode);
    }
    
    http.end();
    Serial.printf("Free heap after all: %d\n", ESP.getFreeHeap());
    Serial.flush();
    return success;
}

void drawTodayWeather() {
    if (!weatherInfo) return;
    Serial.println("drawTodayWeather called");
    JsonObject info = (*weatherInfo)["pref"]["area"][region]["info"][0];
    drawWeather(info);
}

void drawTomorrowWeather() {
    if (!weatherInfo) return;
    Serial.println("drawTomorrowWeather called");
    JsonObject info = (*weatherInfo)["pref"]["area"][region]["info"][1];
    drawWeather(info);
}

void drawDayAfterTomorrowWeather() {
    if (!weatherInfo) return;
    Serial.println("drawDayAfterTomorrowWeather called");
    JsonObject info = (*weatherInfo)["pref"]["area"][region]["info"][2];
    drawWeather(info);
}

void drawWeather(JsonObject doc) {
    Serial.println("drawWeather called");
    
    if (doc.isNull()) {
        Serial.println("ERROR: doc is null!");
        return;
    }
    
    M5.M5Ink.clear();
    M5.M5Ink.drawBuff((uint8_t *)background);
    weatherSprite.clear();
    
    const char* weather = doc["weather"];
    if (!weather) {
        Serial.println("ERROR: weather key is null!");
        return;
    }
    Serial.printf("Weather: %s\n", weather);
    
    String weatherStr = String(weather);
    
    // APIが漢字「曇」とひらがな「くもり」の両方を返す可能性があるため両方チェック
    bool hasRain  = weatherStr.indexOf("雨") != -1;
    bool hasSun   = weatherStr.indexOf("晴") != -1;
    bool hasSnow  = weatherStr.indexOf("雪") != -1;
    bool hasCloud = weatherStr.indexOf("くもり") != -1 || weatherStr.indexOf("曇") != -1;

    if (hasRain) {
        if (hasCloud) {
            weatherSprite.drawBuff(46,36,108,96,rainyandcloudy);
        } else {
            weatherSprite.drawBuff(46,36,108,96,rainy);
        }
    } else if (hasSun) {
        if (hasCloud) {
            weatherSprite.drawBuff(46,36,108,96,sunnyandcloudy);
        } else {
            weatherSprite.drawBuff(46,36,108,96,sunny);
        }
    } else if (hasSnow) {
            weatherSprite.drawBuff(46,36,108,96,snow);
    } else if (hasCloud) {
            weatherSprite.drawBuff(46,36,108,96,cloudy);
    }
    weatherSprite.pushSprite();
 
    const char* maxTemp = doc["temperature"]["range"][0]["content"];
    const char* minTemp = doc["temperature"]["range"][1]["content"];
    drawTemperature(maxTemp ? maxTemp : "--", minTemp ? minTemp : "--");
 
    int rainfallChances[4];
    for (int i = 0; i < 4; i++) {
        const char* val = doc["rainfallchance"]["period"][i]["content"];
        rainfallChances[i] = (val && strcmp(val, "-") != 0) ? atoi(val) : 0;
    }

    int maxRainfallChance = -255;
    int minRainfallChance = 255;

    for (int item : rainfallChances) {
        if (item > maxRainfallChance) maxRainfallChance = item;
        if (item < minRainfallChance) minRainfallChance = item;
    }
    
    drawRainfallChance(String(maxRainfallChance), String(minRainfallChance));
    
    const char* date = doc["date"];
    drawDate(date ? date : "--");
    Serial.println("drawWeather complete");
}

void drawTemperature(String maxTemperature, String minTemperature) {
    temperatureSprite.clear();
    temperatureSprite.drawString(60,147,maxTemperature.c_str(),&AsciiFont8x16);
    temperatureSprite.drawString(60,169,minTemperature.c_str(),&AsciiFont8x16);
    temperatureSprite.pushSprite();
}

void drawRainfallChance(String maxRainfallChance, String minRainfallChance) {
    rainfallChanceSprite.clear();
    rainfallChanceSprite.drawString(142,147,maxRainfallChance.c_str(),&AsciiFont8x16);
    rainfallChanceSprite.drawString(142,169,minRainfallChance.c_str(),&AsciiFont8x16);
    rainfallChanceSprite.pushSprite();
}

void drawDate(String date) {
    dateSprite.clear();
    dateSprite.drawString(60,16,date.c_str(),&AsciiFont8x16);
    dateSprite.pushSprite();
}

/**************************************************************************************************
 * ESP32 E-Paper Daily Countdown Display
 *
 * A low-power countdown calendar for the Heltec Vision Master E290.
 * This version uses the official Heltec-provided display driver and includes
 *
 * Features:
 * 1.  Smart Config Mode with failsafe hotspot.
 * 2.  Battery Monitoring with on-screen icon.
 * 3.  Weekly NTP time sync for power saving.
 * 4.  Deep sleep functionality.
 * 5.  New "Featured Event" display layout with larger text.
 * 6.  Default data for WiFi and events on first run.
 * 7.  Timezone set to US Central Time (CST/CDT).
 * 8.  "Pin" feature to prioritize specific events.
 *
 * HARDWARE:
 * This code is specifically for the Heltec Vision Master E290 (HT-VME290).
 *
 * LIBRARIES TO INSTALL:
 * - ArduinoJson
 * - NTPClient
 * - Heltec ESP32 Dev-Boards (Install via Board Manager, then find this library)
 *
 * INSTRUCTIONS:
 * - Set your timezone offset around line 51, gmtOffset_sec
 * - Preload SSID, Password, and events if desired started at line 423, createDefaultConfig()
 * - If you want to enter config mode, hold down the boot button for a couple seconds. It'll either connect your wifi or create its own hotspot.
 **************************************************************************************************/

// Core ESP32 Libraries
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <math.h> // Include for floor()
#include <algorithm> 

// E-Paper Display Library
#include "HT_DEPG0290BxS800FxX_BW.h"

// WiFi and Time Libraries
#include <NTPClient.h>
#include <WiFiUdp.h>

// Data Handling
#include <ArduinoJson.h>

// --- Pin Definitions for Heltec Vision Master E290 ---
#define BUTTON_PIN 0
#define VEXT_PIN  18
#define VBAT_READ_PIN 7

// --- Timezone Configuration ---
const long gmtOffset_sec = -6 * 3600; 
const int daylightOffset_sec = 3600;

// --- Global Objects and Variables ---
DEPG0290BxS800FxX_BW display(5, 4, 3, 6, 2, 1, -1, 6000000);
WebServer server(80);
Preferences preferences;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", gmtOffset_sec, daylightOffset_sec);


bool configMode = false;
unsigned long configModeStartTime = 0;
const long configModeTimeout = 300000; // 5 minutes
const unsigned long ONE_WEEK_IN_SECONDS = 7 * 24 * 60 * 60;

// --- Structs for Data Organization ---
struct Event {
    String name;
    int day;
    int month;
    bool isAnnual;
    int year;
    bool pinned;
    long daysUntil;
};

// --- Function Prototypes ---
void VextON();
void VextOFF();
int getBatteryPercentage();
void startConfigMode();
void startCountdownMode();
void handleRoot();
void handleSave();
void handleNotFound();
void displayMessage(String msg, String subtitle = "");
void displayConfigScreen(String mode, String info);
void displayCountdowns(Event events[], int eventCount);
bool loadConfiguration(JsonDocument& doc);
void createDefaultConfig(JsonDocument& doc);
void calculateEventCountdown(JsonDocument& doc, Event sortedEvents[], int& eventCount, time_t now);
void getNextOccurrence(int day, int month, bool isAnnual, int year, struct tm& timeinfo, int& days_until, int& next_year);
bool syncTimeIfNeeded(bool& didSync);
String formatDate(int month, int day, int year);


// ================================================================================================
//   SETUP
// ================================================================================================
void setup() {
    Serial.begin(115200);

    VextON();
    delay(100);
    
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(VBAT_READ_PIN, INPUT);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

    delay(50); 
    if (digitalRead(BUTTON_PIN) == LOW) {
        configMode = true;
    }

    display.init();

    if (configMode) {
        startConfigMode();
    } else {
        startCountdownMode();
    }
}


// ================================================================================================
//   LOOP
// ================================================================================================
void loop() {
    if (configMode) {
        server.handleClient();
        if (millis() - configModeStartTime > configModeTimeout) {
            ESP.restart();
        }
        if (digitalRead(BUTTON_PIN) == LOW) {
            delay(500); // Debounce
            ESP.restart();
        }
    }
}


// ================================================================================================
//   SMART CONFIGURATION MODE
// ================================================================================================

void startConfigMode() {
    configModeStartTime = millis();
    Serial.println("Starting Smart Configuration Mode...");

    StaticJsonDocument<1024> configDoc;
    if (!loadConfiguration(configDoc)) {
        Serial.println("No configuration found. Creating default config for this session.");
        createDefaultConfig(configDoc);
    }

    String ssid = configDoc["wifi_ssid"];
    bool connectedToWifi = false;

    if (ssid.length() > 0) {
        displayMessage("Connecting to WiFi", ssid);
        String pass = configDoc["wifi_pass"];
        WiFi.begin(ssid.c_str(), pass.c_str());
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500); attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            connectedToWifi = true;
            displayConfigScreen("STA", WiFi.localIP().toString());
        } else {
            WiFi.disconnect(true);
        }
    }

    if (!connectedToWifi) {
        const char* ap_ssid = "ESP32-Countdown-Config";
        WiFi.softAP(ap_ssid);
        displayConfigScreen("AP", ap_ssid);
    }

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Web server started.");
}

void handleRoot() {
    StaticJsonDocument<1024> configDoc;
    if (!loadConfiguration(configDoc)) {
        createDefaultConfig(configDoc);
    }
    
    String ssid = configDoc["wifi_ssid"] | "";
    int lookahead = configDoc["lookahead"] | 90;
    JsonArray events = configDoc["events"];
    String html = R"rawliteral(
<!DOCTYPE html><html><head><title>ESP32 Countdown Config</title><meta name="viewport" content="width=device-width, initial-scale=1"><style>body{font-family:Arial,sans-serif;background-color:#f4f4f4;margin:0;padding:20px}.container{max-width:600px;margin:auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 0 10px rgba(0,0,0,.1)}h2{color:#333}.form-group{margin-bottom:15px}label{display:block;margin-bottom:5px;font-weight:700}input[type=text],input[type=password],input[type=number],input[type=date],select{width:100%;padding:8px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}.btn{background-color:#007bff;color:#fff;padding:10px 15px;border:none;border-radius:4px;cursor:pointer;font-size:16px}.btn-add{background-color:#28a745;margin-top:10px}.event-card{border:1px solid #eee;padding:15px;border-radius:5px;margin-bottom:10px;background:#fafafa}.remove-btn{float:right;background:#dc3545;color:#fff;border:none;border-radius:50%;width:25px;height:25px;cursor:pointer;font-weight:700}.pin-group{display:flex;align-items:center;gap:10px}</style></head><body><div class="container"><h2>WiFi Settings</h2><p style="font-size:0.9em;color:#555;">Timezone is configured for US Central Time (CST/CDT).</p><form action="/save" method="POST"><div class="form-group"><label for="ssid">WiFi SSID</label><input type="text" id="ssid" name="ssid" value=")rawliteral";
    html += ssid;
    html += R"rawliteral("></div><div class="form-group"><label for="pass">WiFi Password (leave blank to keep old)</label><input type="password" id="pass" name="pass"></div><h2>Display Settings</h2><div class="form-group"><label for="lookahead">Show events in the next (days)</label><input type="number" id="lookahead" name="lookahead" value=")rawliteral";
    html += String(lookahead);
    html += R"rawliteral("></div><h2>Countdown Events</h2><div id="events-container">)rawliteral";
    if (events) {
        int i = 0;
        for (JsonObject event : events) {
            String name = event["name"];
            String date = event["date"];
            String type = event["type"];
            bool pinned = event["pinned"] | false;
            html += R"rawliteral(<div class="event-card"><button type="button" class="remove-btn" onclick="this.parentElement.remove()">X</button><input type="hidden" name="event_name_)rawliteral";
            html += String(i);
            html += R"rawliteral(" value=")rawliteral";
            html += name;
            html += R"rawliteral("><div class="form-group"><label>Event Name</label><input type="text" name="event_name_val" value=")rawliteral";
            html += name;
            html += R"rawliteral(" disabled></div><div class="form-group"><label>Date</label><input type="date" name="event_date_val" value=")rawliteral";
            html += date;
            html += R"rawliteral("></div><div class="form-group"><label>Type</label><select name="event_type_val"><option value="onetime" )rawliteral";
            if (type == "onetime") html += "selected";
            html += R"rawliteral(>One-Time</option><option value="annual" )rawliteral";
            if (type == "annual") html += "selected";
            html += R"rawliteral(>Annual</option></select></div><div class="pin-group"><input type="checkbox" name="event_pinned_)rawliteral";
            html += String(i);
            html += R"rawliteral(" value="true" )rawliteral";
            if(pinned) html += "checked";
            html += R"rawliteral(><label>Pin this event</label></div></div>)rawliteral";
            i++;
        }
    }
    html += R"rawliteral(</div><button type="button" class="btn btn-add" onclick="addEvent()">Add Event</button><hr><button type="submit" class="btn">Save Configuration</button></form></div><script>function addEvent(){const container=document.getElementById("events-container");const i=document.querySelectorAll('.event-card').length;const eventCard=document.createElement("div");eventCard.className="event-card",eventCard.innerHTML=`<button type="button" class="remove-btn" onclick="this.parentElement.remove()">X</button><div class="form-group"><label>Event Name</label><input type="text" name="event_name" placeholder="e.g., Anniversary"></div><div class="form-group"><label>Date</label><input type="date" name="event_date"></div><div class="form-group"><label>Type</label><select name="event_type"><option value="onetime">One-Time</option><option value="annual" selected>Annual</option></select></div><div class="pin-group"><input type="checkbox" name="event_pinned_${i}" value="true"><label>Pin this event</label></div>`,container.appendChild(eventCard)}</script></body></html>)rawliteral";
    server.send(200, "text/html", html);
}


void handleSave() {
    StaticJsonDocument<1024> doc;
    doc["wifi_ssid"] = server.arg("ssid");
    if (server.hasArg("pass") && server.arg("pass").length() > 0) {
        doc["wifi_pass"] = server.arg("pass");
    } else {
        StaticJsonDocument<1024> oldDoc;
        if(loadConfiguration(oldDoc) && oldDoc.containsKey("wifi_pass")) {
             doc["wifi_pass"] = oldDoc["wifi_pass"];
        }
    }
    doc["lookahead"] = server.arg("lookahead").toInt();
    JsonArray events = doc.createNestedArray("events");

    int event_idx = 0;
    for (uint8_t i = 0; i < server.args(); i++) {
        String argName = server.argName(i);
        if (argName.startsWith("event_name_") || argName == "event_name") {
             JsonObject event = events.createNestedObject();
             event["name"] = server.arg(i);
             // Since we know the order, the next args are the corresponding ones
             event["date"] = server.arg(i + 1);
             event["type"] = server.arg(i + 2);
             // Check if the pinned checkbox exists for this index
             String pinned_arg_name = "event_pinned_" + String(event_idx);
             event["pinned"] = server.hasArg(pinned_arg_name);
             event_idx++;
        }
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    preferences.begin("countdown", false);
    preferences.putString("config", jsonString);
    preferences.putULong("lastSync", 0);
    preferences.end();
    server.send(200, "text/plain", "Configuration saved! The device will now restart.");
    displayMessage("Configuration Saved!", "Restarting...");
    delay(2000);
    ESP.restart();
}


void handleNotFound() {
    server.send(404, "text/plain", "404: Not found");
}


// ================================================================================================
//   COUNTDOWN MODE & TIME SYNC
// ================================================================================================

void startCountdownMode() {
    bool didSync = false;
    if (!syncTimeIfNeeded(didSync)) {
        displayMessage("Time Sync Failed", "Check WiFi. Sleeping...");
        delay(5000); VextOFF(); esp_deep_sleep_start();
        return;
    }

    time_t now;
    if (didSync) {
      now = timeClient.getEpochTime();
      Serial.println("Using newly synced time for calculation.");
    } else {
      time(&now);
      Serial.println("Using existing system time for calculation.");
    }

    StaticJsonDocument<1024> configDoc;
    if (!loadConfiguration(configDoc)) {
        Serial.println("No config found. Using defaults.");
        createDefaultConfig(configDoc);
    }
    
    const int MAX_EVENTS = 10;
    Event sortedEvents[MAX_EVENTS];
    int eventCount = 0;
    calculateEventCountdown(configDoc, sortedEvents, eventCount, now);
    displayCountdowns(sortedEvents, eventCount);

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    timeinfo.tm_mday += 1;
    timeinfo.tm_hour = 0; timeinfo.tm_min = 0; timeinfo.tm_sec = 1;
    time_t next_midnight = mktime(&timeinfo);
    long secondsUntilMidnight = next_midnight - now;
    if (secondsUntilMidnight < 0) secondsUntilMidnight = 60;
    uint64_t sleep_microseconds = (uint64_t)secondsUntilMidnight * 1000000;
    Serial.print("Entering deep sleep for "); Serial.print(secondsUntilMidnight); Serial.println(" seconds.");
    VextOFF();
    esp_sleep_enable_timer_wakeup(sleep_microseconds);
    esp_deep_sleep_start();
}


bool syncTimeIfNeeded(bool& didSync) {
    didSync = false;
    preferences.begin("countdown", true);
    unsigned long lastSync = preferences.getULong("lastSync", 0);
    preferences.end();
    
    time_t now;
    time(&now);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    
    bool needsSync = false;
    if (lastSync == 0) {
        Serial.println("Sync reason: First run.");
        needsSync = true;
    } 
    else if ((now - lastSync) > ONE_WEEK_IN_SECONDS) {
        Serial.println("Sync reason: Weekly sync overdue.");
        needsSync = true;
    }
    else if (timeinfo.tm_year + 1900 < 2024) {
        Serial.println("Sync reason: System time invalid, recovering from power loss.");
        needsSync = true;
    }

    if (needsSync) {
        StaticJsonDocument<1024> configDoc;
        if (!loadConfiguration(configDoc)) {
            createDefaultConfig(configDoc);
        };
        String ssid = configDoc["wifi_ssid"]; String pass = configDoc["wifi_pass"];
        
        displayMessage("Syncing Time...", ssid);
        WiFi.begin(ssid.c_str(), pass.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
            delay(500); 
            attempts++;
            Serial.print(".");
        }

        if (WiFi.status() != WL_CONNECTED) { 
            Serial.println("\nWiFi connection failed for sync.");
            WiFi.disconnect(true); 
            WiFi.mode(WIFI_OFF); 
            return false;
        }

        Serial.println("\nWiFi connected, getting NTP time.");
        timeClient.begin();
        if (timeClient.forceUpdate()) {
            didSync = true;
            time_t epochTime = timeClient.getEpochTime();
            struct timeval tv = { .tv_sec = epochTime };
            settimeofday(&tv, NULL);
            preferences.begin("countdown", false);
            preferences.putULong("lastSync", epochTime);
            preferences.end();
            Serial.println("Time successfully synced.");
        } else {
            Serial.println("NTP update failed.");
            return false;
        }
        
        WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
        return true;
    } else {
        Serial.println("Time sync not required.");
        return true;
    }
}


bool loadConfiguration(JsonDocument& doc) {
    preferences.begin("countdown", true);
    String configString = preferences.getString("config", "");
    preferences.end();
    if (configString.length() == 0) return false;
    DeserializationError error = deserializeJson(doc, configString);
    if (error) {
      Serial.print("Failed to parse config: ");
      Serial.println(error.c_str());
      return false;
    }
    return true;
}

void createDefaultConfig(JsonDocument& doc){
  //Preload wifi information and dates if desired. Events can be Onetime or Annual
    doc["wifi_ssid"] = "SSID";
    doc["wifi_pass"] = "yourpassword";
    doc["lookahead"] = 365;
    JsonArray events = doc.createNestedArray("events");

    JsonObject event1 = events.createNestedObject();
    event1["name"] = "Christmas";
    event1["date"] = "2025-12-25";
    event1["type"] = "annual";
    event1["pinned"] = false;
    
    JsonObject event3 = events.createNestedObject();
    event3["name"] = "4th of July";
    event3["date"] = "2025-07-04";
    event3["type"] = "annual";
    event3["pinned"] = true;
}

void calculateEventCountdown(JsonDocument& doc, Event sortedEvents[], int& eventCount, time_t now) {
     int lookaheadDays = doc["lookahead"] | 90;
     JsonArray eventArray = doc["events"];
     eventCount = 0;
    
     struct tm timeinfo; 
     localtime_r(&now, &timeinfo);
    
     for (JsonObject eventData : eventArray) {
         int day, month, year;
         int parsed_count = sscanf(eventData["date"], "%d-%d-%d", &year, &month, &day);
    
         if (parsed_count != 3) {
            Serial.print("Failed to parse date: ");
            Serial.println(eventData["date"].as<const char*>());
            continue;
         }

         int days_until, next_year;
         getNextOccurrence(day, month, (eventData["type"] == "annual"), year, timeinfo, days_until, next_year);

         if (days_until >= -1 && days_until <= lookaheadDays) {
            Event currentEvent;
            currentEvent.name = String(eventData["name"]);
            currentEvent.day = day; currentEvent.month = month;
            currentEvent.isAnnual = (eventData["type"] == "annual");
            currentEvent.year = next_year;
            currentEvent.pinned = eventData["pinned"] | false;
            currentEvent.daysUntil = (days_until < 0) ? 0 : days_until;
    
            if (eventCount < 10) { // Ensure we don't exceed the array bounds
                sortedEvents[eventCount++] = currentEvent;
            }
         }
     }

    // --- SORTING LOGIC ---
    // STEP 1: Prioritize pinned events to ensure they are included in the top candidates.
    // This sort brings all pinned events to the front of the list, while keeping
    // the pinned and unpinned groups sorted chronologically among themselves.
     std::sort(sortedEvents, sortedEvents + eventCount, [](const Event& a, const Event& b) {
         if (a.pinned != b.pinned) {
            return a.pinned > b.pinned; // Pinned events (true=1) come before unpinned (false=0)
        }
         return a.daysUntil < b.daysUntil; // Secondary sort is chronological
     });

    // STEP 2: Sort only the top 3 display slots chronologically.
    // After step 1, the top 3 candidates are in sortedEvents[0], [1], and [2].
    // This second pass re-sorts just those three items by date, ensuring the final
    // on-screen display is in a logical chronological order.
    int display_count = std::min(3, eventCount);
    std::sort(sortedEvents, sortedEvents + display_count, [](const Event& a, const Event& b) {
        return a.daysUntil < b.daysUntil;
    });
}

void getNextOccurrence(int day, int month, bool isAnnual, int one_time_year, struct tm& timeinfo, int& days_until, int& next_year) {
    struct tm target_date = {0};
    target_date.tm_mday = day;
    target_date.tm_mon = month - 1;
    target_date.tm_year = isAnnual ? timeinfo.tm_year : one_time_year - 1900;
    target_date.tm_hour = 12;

    time_t current_time = mktime(&timeinfo);
    time_t target_time = mktime(&target_date);

    if (isAnnual && difftime(target_time, current_time) < 0) {
        target_date.tm_year++;
        target_time = mktime(&target_date);
    }

    struct tm current_day_info = timeinfo;
    current_day_info.tm_hour = 0; current_day_info.tm_min = 0; current_day_info.tm_sec = 0;
    time_t current_day_start = mktime(&current_day_info);
    
    days_until = floor((target_time - current_day_start) / 86400.0);
    next_year = target_date.tm_year + 1900;
}


// ================================================================================================
//   E-PAPER & HARDWARE FUNCTIONS
// ================================================================================================

void VextON(void) {
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, HIGH);
}

void VextOFF(void) {
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
}

/*int getBatteryPercentage() {
    analogReadResolution(12);
 

    analogSetAttenuation(ADC_11db);
    analogRead(VBAT_READ_PIN); // First read to stabilize
    delay(10);

    uint32_t battMv = analogReadMilliVolts(7) * 4.9f;
    Serial.println(battMv);

    uint32_t adc_reading = analogReadMilliVolts(VBAT_READ_PIN);
    Serial.printf("Raw ADC Reading: %d\n", adc_reading); 

    float voltage = adc_reading * (3.3 / 4095.0) * 2.0 * 1.03;
    Serial.printf("Calculated Voltage = %.2f V\n", voltage ); 


    int percentage = map(voltage * 100, 320, 420, 0, 100);
    return constrain(percentage, 0, 100);
}*/

String formatDate(int month, int day, int year) {
    const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    if (month < 1 || month > 12) return "Invalid Date";
    return String(months[month-1]) + " " + String(day) + ", " + String(year);
}

void displayMessage(String msg, String subtitle) {
    display.clear();
    display.clear();
    display.setFont(ArialMT_Plain_16);
    display.drawString(10, 30, msg);
    if (subtitle.length() > 0) {
        display.setFont(ArialMT_Plain_10);
        display.drawString(10, 50, subtitle);
    }
    display.display();
    delay(100);
}

void displayConfigScreen(String mode, String info) {
    display.clear();
    display.clear();
    String title, line1, line2;
    if (mode == "AP") {
        title = "Setup Mode (Hotspot)";
        line1 = "1. Connect to WiFi: " + info;
        line2 = "2. Go to 192.168.4.1 in browser";
    } else {
        title = "Setup Mode (WiFi)";
        line1 = "1. You are on your home WiFi.";
        line2 = "2. Go to http://" + info + "/";
    }
    
    display.setFont(ArialMT_Plain_16); 
    display.drawString(10, 30, title);
    display.setFont(ArialMT_Plain_10);
    display.drawString(10, 60, line1); 
    display.drawString(10, 75, line2); 
    
    display.display();
    delay(100);
}

void displayCountdowns(Event events[], int eventCount) {
    //int batt_percent = getBatteryPercentage();
    display.clear();
    display.clear();
    
    display.setFont(ArialMT_Plain_10);
    //String batt_str = String(batt_percent) + "%";
    //uint16_t w = display.getStringWidth(batt_str);
    uint16_t w = 0;
    //display.drawString(display.width() - w - 22, 1, batt_str);
    //display.drawRect(display.width() - 20, 0, 18, 11);
    //display.fillRect(display.width() - 19 + (16 * (100 - batt_percent) / 100), 1, 16 - (16 * (100 - batt_percent) / 100), 9);
    //display.fillRect(display.width() - 22, 3, 2, 5);

    if (eventCount == 0) {
        display.setFont(ArialMT_Plain_16);
        display.drawString(10, 60, "No upcoming events.");
        display.setFont(ArialMT_Plain_10);
        display.drawString(10, 80, "Press button to add some!");
    } else {
        // --- NEW "Featured Event" LAYOUT ---
        int top_section_height = 58;
        display.drawLine(0, top_section_height, display.width(), top_section_height);
        
        if (eventCount > 0) {
            display.setFont(ArialMT_Plain_24);
            String featured_text = String(events[0].daysUntil) + " days until " + events[0].name;
            w = display.getStringWidth(featured_text);
            display.drawString((display.width() - w) / 2, 10, featured_text);
            
            display.setFont(ArialMT_Plain_16);
            String dateStr = formatDate(events[0].month, events[0].day, events[0].year);
            w = display.getStringWidth(dateStr);
            display.drawString((display.width() - w) / 2, 35, dateStr);
        }

        int mid_x = display.width() / 2;
        display.drawLine(mid_x, top_section_height, mid_x, display.height());
        
        if (eventCount > 1) {
            int y_pos = top_section_height + 8;
            display.setFont(ArialMT_Plain_16);
            String nameStr = events[1].name;
            w = display.getStringWidth(nameStr);
            display.drawString((mid_x - w) / 2, y_pos, nameStr);
            
            display.setFont(ArialMT_Plain_16);
            String daysStr = String(events[1].daysUntil) + " days";
            w = display.getStringWidth(daysStr);
            display.drawString((mid_x - w) / 2, y_pos +17, daysStr);

            String dateStr = formatDate(events[1].month, events[1].day, events[1].year);
            w = display.getStringWidth(dateStr);
            display.drawString((mid_x - w) / 2, y_pos + 32, dateStr);
        }

        if (eventCount > 2) {
            int y_pos = top_section_height + 8;
            display.setFont(ArialMT_Plain_16);
            String nameStr = events[2].name;
            w = display.getStringWidth(nameStr);
            display.drawString(mid_x + (mid_x - w) / 2, y_pos, nameStr);

            display.setFont(ArialMT_Plain_16);
            String daysStr = String(events[2].daysUntil) + " days";
            w = display.getStringWidth(daysStr);
            display.drawString(mid_x + (mid_x - w) / 2, y_pos + 17, daysStr);
            
            String dateStr = formatDate(events[2].month, events[2].day, events[2].year);
            w = display.getStringWidth(dateStr);
            display.drawString(mid_x + (mid_x - w) / 2, y_pos + 32, dateStr);
        }
    }
    display.display();
    delay(100);
}

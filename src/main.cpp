#include <Arduino.h>
#include <math.h> // NAN, isnan

// ***********
// PIN MAPPING
// ***********

#define PIN_HEATER 4 // marked D2 on nodeMCU v3
#define PIN_TEMP   2 // marked D4 on nodeMCU v3

// *********
// SCHEDULER
// *********

#define _TASK_SLEEP_ON_IDLE_RUN // sleep 1ms when nothing scheduled
#include <TaskScheduler.h>

Scheduler scheduler;

// Use
// Task t1(1000, -1, &callbackFn, &scheduler, true);
// for infinite repetition of callbackFn every 1000 ms

// *****************
// PERSIST VARIABLES
// *****************

#include <ESP_EEPROM.h>

namespace Config {
  struct Cfg {
    char   wlan_join_hostname[32];
    char   wlan_join_ssid[32];
    char   wlan_join_password[64];
    char   wlan_ap_ssid[32];
    char   wlan_ap_password[64];
    byte   len_wlan_join_hostname;
    byte   len_wlan_join_ssid;
    byte   len_wlan_join_password;
    byte   len_wlan_ap_ssid;
    byte   len_wlan_ap_password;
    float  setpoint;
  } cfg;

#define GET_SET_STRING(name) \
  String get_ ## name() { \
    char data[cfg.len_ ## name + 1]; \
    memcpy(data, cfg.name, cfg.len_ ## name); \
    data[cfg.len_ ## name] = '\0'; \
    return String(data); \
  } \
  void set_ ## name(String x) { \
    cfg.len_ ## name = max(x.length(), sizeof(Cfg::name)); \
    x.toCharArray(cfg.name, sizeof(Cfg::name)); \
  }

  GET_SET_STRING(wlan_join_hostname);
  GET_SET_STRING(wlan_join_ssid);
  GET_SET_STRING(wlan_join_password);
  GET_SET_STRING(wlan_ap_ssid);
  GET_SET_STRING(wlan_ap_password);

  void setup() {
    EEPROM.begin(sizeof(Cfg));
    if (EEPROM.percentUsed() >= 0) {
      EEPROM.get(0, cfg);
    } else {
      cfg.setpoint=96.;
      set_wlan_join_hostname("barepid");
      set_wlan_join_ssid("");
      set_wlan_join_password("");
      set_wlan_ap_ssid("barepid");
      set_wlan_ap_password("barepid42");
    }
  }

  void save() {
    EEPROM.put(0, cfg);
    EEPROM.commit();
  }

}

// ******************
// TEMPERATURE SENSOR
// ******************

#include <ZACwire.h> // Read TSIC temperature sensors

namespace Sensor {
  ZACwire<PIN_TEMP> Zac(306);

  float read() {
    float x = Zac.getTemp(); // get the temperature in °C
    if (x > 150 || x < 0) {
      return NAN;
    }
    return x;
  }
}

// **************
// PID CONTROLLER
// **************

#include <PID_v1.h>

// times in milliseconds:
#define PIDsampleTime 100 // compute output every n ms; PID library default
#define ControllerStep 10 // control heater every n ms
#define ControllerWindow 1000 // control precision = step / window = 1%

namespace Controller {
  bool heating;
  double setpoint, input, output;
  unsigned long windowstart;  // time in milliseconds

  PID PID(&input, &output, &setpoint, 2, 5, 1, DIRECT);

  void reload() {
    setpoint = Config::cfg.setpoint;
  }

  void setup() {
    reload();
    windowstart = millis();
    PID.SetOutputLimits(0, ControllerWindow);
    PID.SetMode(AUTOMATIC);
    pinMode(PIN_HEATER, OUTPUT);
    heating = false;
    digitalWrite(PIN_HEATER, LOW);
  }

  void step() {
    // advance clock
    unsigned long now = millis();
    if (now - windowstart > ControllerWindow) {
      windowstart += ControllerWindow;
    }

    // read temperature
    input = Sensor::read();

    heating = false;
    if (!isnan(input)){ // valid reading
      // update PID
      PID.Compute();

      // should we heat?
      if (output > now - windowstart) {
        heating = true;
      }
    } // invalid reading implies !heating

    // control heater
    digitalWrite(PIN_HEATER, heating ? HIGH : LOW);
  }

  // step() every ControllerStep milliseconds
  Task task(ControllerStep, -1, &step, &scheduler, true);

  // proportional gain  Kp
  // integral gain      Ki
  // derivative gain    Kd
  void set_KpKiKd(double Kp, double Ki, double Kd) {
    PID.SetTunings(Kp, Ki, Kd);
  }

  // Verstärkung    Kp
  // Nachstellzeit  Tn = 1/Ki
  // Vorhaltzeit    Tv = Kd
  void set_KpTnTv(double Kp, double Tn, double Tv) {
    PID.SetTunings(Kp, Tn == 0 ? 0 : Kp / Tn, Kp * Tv);
  }
}

// ****
// WIFI
// ****

#include <ESP8266WiFi.h>

#define WIFI_ATTEMPTS 20 // wait 20 seconds before starting own AP

namespace Network {
  enum state{INIT, JOINING, JOINED, SERVING, FAILED};
  state state = INIT;
  unsigned attempts = 0;

  void start_ap() {
    String ssid = Config::get_wlan_ap_ssid(),
           password = Config::get_wlan_ap_password();
    WiFi.mode(WIFI_AP);
    state = WiFi.softAP(ssid, password) ? SERVING : FAILED;
  }

  void start_joining() {
    String hostname = Config::get_wlan_join_hostname(),
           ssid = Config::get_wlan_join_ssid(),
           password = Config::get_wlan_join_password();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    WiFi.begin(ssid, password);
    state = JOINING;
    attempts = 0;
  }

  void step_joining() {
    attempts++;
    if (WiFi.status() == WL_CONNECTED) {
      state = JOINED;
    } else if (attempts > WIFI_ATTEMPTS) {
      WiFi.disconnect(true);
      start_ap();
    }
  }

  void init() {
    String s = Config::get_wlan_join_ssid();
    if (s.length() > 1) {
      start_joining();
    } else {
      start_ap();
    }
  }

  void step() {
    switch(state) {
      case INIT:
        init();
        break;
      case JOINING:
        step_joining();
        break;
      case JOINED:
        // TODO check state & rejoin
      case SERVING:
      case FAILED:
      default:
        break;
    }
  }

  // check connection every second
  Task task(1000, -1, &step, &scheduler, true);

  void print_state() {
    switch(state) {
      case INIT:
        Serial.print("loading");
        break;
      case JOINING:
        Serial.print("connecting to \"");
        Serial.print(WiFi.SSID());
        Serial.print("\" (since ");
        Serial.print(attempts);
        Serial.print(" seconds)");
        break;
      case JOINED:
        Serial.print("connected to \"");
        Serial.print(WiFi.SSID());
        Serial.print("\" (");
        Serial.print(WiFi.localIP());
        Serial.print(")");
        break;
      case SERVING:
        Serial.print("serving \"");
        Serial.print(WiFi.softAPSSID());
        Serial.print("\" network (");
        Serial.print(WiFi.softAPIP());
        Serial.print(")");
        break;
      case FAILED:
        Serial.print("failed");
        break;
      default:
        Serial.print("unknown state");
        break;
    }
  }
}

// ***
// LOG
// ***

#define LOG_SIZE 300 // 5 minutes
#define LOG_INTERVAL 1000

namespace LOG {
  struct entry {
    unsigned long time; // millis()
    float         temp; // sensor value
  };

  struct entry entries[LOG_SIZE];
  unsigned int pos = -1;
  bool partial = true;

  void log() {
    float x = Sensor::read();
    pos = (pos + 1) % LOG_SIZE;
    if (pos == LOG_SIZE - 1) partial = false;
    entries[pos].time = millis();
    entries[pos].temp = x;
  }

  // log() every LOG_INTERVAL milliseconds
  Task task(LOG_INTERVAL, -1, &log, &scheduler, true);
}

// ****
// HTTP
// ****

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace API {
  AsyncWebServer server(80);

  void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  }

  void getStatus(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    StaticJsonDocument<200> obj;
    obj["input"] = Controller::input;
    obj["output"] = Controller::output;
    obj["setpoint"] = Controller::setpoint;
    serializeJson(obj, *response);
    request->send(response);
  }

  void getLog(AsyncWebServerRequest *request) {
    // csv response
    AsyncResponseStream *response = request->beginResponseStream("text/csv");
    response->print("time,temperature");
    // // json response
    // AsyncJsonResponse *response = new AsyncJsonResponse();
    // JsonVariant& root = response->getRoot();
    // JsonArray time = root.createNestedArray("time");
    // JsonArray temp = root.createNestedArray("temperature");
    if (LOG::pos >= 0) { // otherwise log empty
      unsigned int i = LOG::partial ? -1 : LOG::pos;
      do {
        i = (i + 1) % LOG_SIZE;
        response->print("\n"); // csv
        response->print(LOG::entries[i].time); // csv
        response->print(","); // csv
        response->print(LOG::entries[i].temp); // csv
        // time.add(LOG::entries[i].time); // json
        // temp.add(LOG::entries[i].temp); // json
      } while ( i != LOG::pos );
    }
    // response->setLength(); // json
    request->send(response);
  }

  void getConfig(AsyncWebServerRequest *request) {
    AsyncJsonResponse *response = new AsyncJsonResponse();
    JsonVariant& root = response->getRoot();
    root["setpoint"]           = Config::cfg.setpoint;
    root["wlan_ap_ssid"]       = Config::cfg.wlan_ap_ssid;
    root["wlan_ap_password"]   = Config::cfg.wlan_ap_password;
    root["wlan_join_ssid"]     = Config::cfg.wlan_join_ssid;
    root["wlan_join_password"] = Config::cfg.wlan_join_password;
    root["wlan_join_hostname"] = Config::cfg.wlan_join_hostname;
    response->setLength();
    request->send(response);
  }

  AsyncCallbackJsonWebHandler* setCfgHandler = new AsyncCallbackJsonWebHandler("/api/config",
      [](AsyncWebServerRequest *request, JsonVariant &obj) {

      struct Config::Cfg old;
      memcpy(&Config::cfg, &old, sizeof old);

    if (obj.containsKey("setpoint")) {
      float setpoint = obj["setpoint"];
      if (setpoint > 0. && setpoint < 130) {
        Config::cfg.setpoint = setpoint;
      } else {
        request->send(400, "text/plain", "invalid setpoint (0 < x < 130)");
        return;
      }
    }

#define SET_STRING_CFG(name) \
  if (obj.containsKey(#name)) { \
    Config::set_ ## name(obj[#name]); \
  }

    SET_STRING_CFG(wlan_join_hostname);
    SET_STRING_CFG(wlan_join_ssid);
    SET_STRING_CFG(wlan_join_password);
    SET_STRING_CFG(wlan_ap_ssid);
    SET_STRING_CFG(wlan_ap_password);

    if (memcmp(&old, &Config::cfg, sizeof old) != 0) {
      Config::save();
    }

    if (old.setpoint != Config::cfg.setpoint) {
      Controller::reload();
    }

    request->redirect("/api/status");
  });

  void restart(AsyncWebServerRequest *request) {
    Serial.println("Received restart request from API.");
    ESP.restart();
  }

  void setup() {
    LittleFS.begin();
    server.on("/api/status", HTTP_GET, getStatus);
    server.on("/api/log", HTTP_GET, getLog);
    server.on("/api/config", HTTP_GET, getConfig);
    server.addHandler(setCfgHandler);
    server.on("/api/restart", HTTP_POST, restart);
    server
      .serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html");
    server.onNotFound(notFound);
    server.begin();
  }
}

// ****************
// Serial Interface
// ****************

namespace SerialInterface {
  void setup() {
    // init serial interface
    Serial.begin(74880);
    Serial.println();
  }

  void print_status() {
    // welcome message
    Serial.println("=== barepid status ===");
    Serial.print("Network:     ");
    Network::print_state();
    Serial.print("\033[K\r\n");
    Serial.print("Setpoint:    ");
    Serial.print(Controller::setpoint);
    Serial.print("\033[K\r\n");
    Serial.print("Temperature: ");
    Serial.print(Controller::input);
    Serial.print("\033[K\r\n");
    Serial.print("Heater:      ");
    Serial.print(floor(Controller::output / ControllerStep));
    Serial.print("%");
    Serial.print("\033[K\r\n");
    Serial.print("\033[5A");
  }

  // print_status() every second
  Task task(1000, -1, &print_status, &scheduler, true);
}

// ****
// MAIN
// ****

void setup() {
  Config::setup();
  SerialInterface::setup();
  API::setup();
  Controller::setup();
  Controller::set_KpTnTv(69, 399, 0); // defaults from rancilio-pid
}

void loop() {
  scheduler.execute();
}

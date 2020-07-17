#include <Arduino.h>
#include <LittleFS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

#include <ESP8266mDNS.h>
#include <EasyButton.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266HTTPClient.h>

#if __has_include("vars.h")
# include "vars.h"
#else
const String url = "http://example.org/cal/index.php?f="; //FILL IN THE URL TO YOUR SERVER HERE
#endif

void setBrts();
int checkBrt(int brt);

// free, coffee, meeting, recording
uint8_t LEDS[4] = {D8, D7, D6, D5};

//define your default values here, if there are different values in config.json, they are overwritten.
char icalurl[1024];
char default_brt[] = "100";

int brts[4] = {100, 100, 100, 100};

// The extra parameters to be configured (can be either global or just in the setup)
// After connecting, parameter.getValue() will get you the configured value
// id/name placeholder/prompt default length
WiFiManagerParameter _icalurl("icalurl", "iCal feed URL", icalurl, 1024);
WiFiManagerParameter _brt_free("brt_free", "'Free' Brightness'", default_brt, 3);
WiFiManagerParameter _brt_coffee("brt_coffee", "'Coffee' Brightness'", default_brt, 3);
WiFiManagerParameter _brt_meeting("brt_meeting", "'Meeting' Brightness'", default_brt, 3);
WiFiManagerParameter _brt_recording("brt_recording", "'Recording' Brightness'", default_brt, 3);

//flag for saving data
// bool shouldSaveConfig = false;

WiFiManager wifiManager;

// Arduino pin where the button is connected to.
#define BUTTON_PIN 0

// Instance of the button.
EasyButton button(BUTTON_PIN);

// Callback function to be called when the button is pressed.
void onPressed() {
    Serial.println("Button has been pressed!");
    wifiManager.resetSettings();
    wifiManager.reboot();
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  // shouldSaveConfig = true;

  strcpy(icalurl, _icalurl.getValue());
  brts[0] =  checkBrt(atoi(_brt_free.getValue()));
  brts[1] =  checkBrt(atoi(_brt_coffee.getValue()));
  brts[2] =  checkBrt(atoi(_brt_meeting.getValue()));
  brts[3] =  checkBrt(atoi(_brt_recording.getValue()));

  Serial.println("saving config");
    DynamicJsonDocument json(1024);
    json["icalurl"] = icalurl;
    json["brt_f"] = brts[0];
    json["brt_c"] = brts[1];
    json["brt_m"] = brts[2];
    json["brt_r"] = brts[3];

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);
    configFile.close();

    setBrts();
}

int checkBrt(int brt) {
  if (brt>100) return 100;
  if (brt<0) return 0;
  return brt;
}

void setBrts() {
  char tmp[4];
  itoa(brts[0], tmp, 10);
  _brt_free.setValue(tmp, strlen(tmp));

  itoa(brts[1], tmp, 10);
  _brt_coffee.setValue(tmp, strlen(tmp));
  
  itoa(brts[2], tmp, 10);
  _brt_meeting.setValue(tmp, strlen(tmp));

  itoa(brts[3], tmp, 10);
  _brt_recording.setValue(tmp, strlen(tmp));
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  Serial.println(WiFi.macAddress());

  for (int i=0; i<4; i++) {
    pinMode(LEDS[i], OUTPUT);
    analogWrite(LEDS[i], 512);
  }

  //clean FS, for testing
  //LittleFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  button.begin();
  button.onPressed(onPressed);

  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!error) {
          Serial.println("\nparsed json");

          strcpy(icalurl, json["icalurl"]);
          brts[0] = checkBrt(json["brt_f"]);
          brts[1] = checkBrt(json["brt_c"]);
          brts[2] = checkBrt(json["brt_m"]);
          brts[3] = checkBrt(json["brt_r"]);

          setBrts();
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setBreakAfterConfig(true);

  //add all your parameters here
  wifiManager.addParameter(&_icalurl);
  wifiManager.addParameter(&_brt_free);
  wifiManager.addParameter(&_brt_coffee);
  wifiManager.addParameter(&_brt_meeting);
  wifiManager.addParameter(&_brt_recording);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  char name[13];
  String mac = WiFi.macAddress();
  snprintf(name, 13, "OnAir-%c%c%c%c%c%c", mac[9], mac[10], mac[12], mac[13], mac[15], mac[16]);

  if (!wifiManager.autoConnect(name)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  } else {
    //WiFi.mode(WIFI_STA);
    WiFi.enableAP(false);
    wifiManager.startWebPortal();
    if (!MDNS.begin("onair")) {
      Serial.println("mdns failed");
    } else {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mdns started");
    }
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(icalurl, _icalurl.getValue());
  brts[0] =  atoi(_brt_free.getValue());
  brts[1] =  atoi(_brt_coffee.getValue());
  brts[2] =  atoi(_brt_meeting.getValue());
  brts[3] =  atoi(_brt_recording.getValue());
  
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}

String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    //char code2;
    for (unsigned int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        //code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;
    
}

typedef enum _EVENT_TYPE {
  FREE=0,
  COFFEE,
  MEETING,
  RECORDING
} EVENT_TYPE;

EVENT_TYPE parseEventType(const char * str) {
  Serial.printf("%s %d\n", str, strcasecmp("meeting", str));
  if (strcasecmp("coffee", str)==0)
    return COFFEE;
  if (strcasecmp("meeting", str)==0)
    return MEETING;
  if (strcasecmp("recording", str)==0)
    return RECORDING;
  return FREE;
}

long long current_end;
EVENT_TYPE current_type;
long long next_start;
long long next_end;
EVENT_TYPE next_type;

void updateFeed() {
  Serial.printf("Updating feed\n");
  
  WiFiClient client;
  HTTPClient http;
  
  if (http.begin(client, url + icalurl)) {
    Serial.printf("going to GET\n");
    int httpCode = http.GET();
    if(httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      // StaticJsonDocument<300> doc;
      DynamicJsonDocument doc(300);
      deserializeJson(doc, payload);
      unsigned long ct = millis();
      if (doc["current"]) {
        current_end = (long)doc["current"]["end"] + ct;
        const char* current_type_str = doc["current"]["type"];
        current_type = parseEventType(current_type_str);
      } else {
        current_end = -1;
        current_type = parseEventType("free");
      }

      JsonObject next = doc["next"];
      if (next) {
        next_start = ct - (long)next["start"];
        next_end = (long)next["end"] + ct;
        const char* next_type_str = next["type"];
        next_type = parseEventType(next_type_str);
      } else {
        next_start = -1;
        next_end = -1;
        next_type = parseEventType("free");
      }
      Serial.printf("Current type: %d; next type: %d\n", current_type, next_type);
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}

void _setLED(int idx) {
  for (int i=0; i<4; i++) {
    if (i == idx) {
      analogWrite(LEDS[i], 1023 / 100 * brts[i]);
    } else {
      analogWrite(LEDS[i], 0);
    }
  }
}

void setLEDs() {
  _setLED(current_type);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    const unsigned long fiveMinutes = 1 * 60 * 1000UL;
    static unsigned long lastSampleTime = 0 - fiveMinutes; 

    unsigned long now = millis();
    if (now - lastSampleTime >= fiveMinutes)
    {
        lastSampleTime += fiveMinutes;
        Serial.printf("%lld %d\n", current_end, current_type);
        updateFeed();
        Serial.printf("%lld %d\n", current_end, current_type);
    }

    if (current_end > 0) {
      //we have a current event
      if (current_end < now) {
        //but it's finished
        current_end = -1;
        current_type = FREE;
      }
    }
    if (next_start > 0) {
      //we have a next event
      if (next_start < now && next_end > now) {
        //it's currently ongoing
        current_end = next_end;
        current_type = next_type;
        next_start=-1;
        next_end=-1;
        next_type=FREE;
      }
      if (next_start < now && next_end < now) {
        //next event is done
        next_start=-1;
        next_end=-1;
        next_type=FREE;
      }
    }
    //Serial.printf("Current event type: %d\n", current_type);
    setLEDs();
  }
  wifiManager.process();
  MDNS.update();
  button.read();
}


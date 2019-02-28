/*
   1MB flash sizee

   sonoff header
   1 - vcc 3v3
   2 - rx
   3 - tx
   4 - gnd
   5 - gpio 14

   esp8266 connections
   gpio  0 - button 1
   gpio  9 - button 3
   gpio  10 - button 2
   gpio 12 - relay 1
   gpio 4 - relay 3
   gpio 5 - relay 2
   gpio 13 - green led - active low
   gpio 14 - pin 5 on header

*/

#define   SONOFF_INPUT              14
#define   SONOFF_LED                13
#define   SONOFF_AVAILABLE_CHANNELS 3
const int SONOFF_RELAY_PINS[3] =    {12, 5, 4};
const int SONOFF_BUTTONS[3] =    {0, 9, 10};

//if this is false, led is used to signal startup state, then always on
//if it s true, it is used to signal startup state, then mirrors relay state
//S20 Smart Socket works better with it false
#define SONOFF_LED_RELAY_STATE      false

#define HOSTNAME "RYS-IoT-Device"

//comment out to completly disable respective technology
#define INCLUDE_BLYNK_SUPPORT

/********************************************
   Should not need to edit below this line *
 * *****************************************/
#include <ESP8266WiFi.h>

#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include <BlynkSimpleEsp8266.h>

static bool BLYNK_ENABLED = true;

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <EEPROM.h>

#define EEPROM_SALT 12667
typedef struct {
  char  bootState[4]      = "on";
  char  blynkToken[33]    = "e4695f84e17b4ab8bc103da61eb984b0";
  char  blynkServer[33]   = "blynk.rys-informatica.com.ar";
  char  blynkPort[6]      = "8080";
  int   salt              = EEPROM_SALT;
} WMSettings;

WMSettings settings;

#include <ArduinoOTA.h>


//for LED status
#include <Ticker.h>
Ticker ticker;


const int CMD_WAIT = 0;
const int CMD_BUTTON_CHANGE = 1;

int cmd = CMD_WAIT;
//int relayState = HIGH;

//inverted button state
int buttonState[3] = {HIGH, HIGH, HIGH};
int currentState[3];

static long startPress = 0;

//http://stackoverflow.com/questions/9072320/split-string-into-string-array
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void tick()
{
  //toggle state
  int state = digitalRead(SONOFF_LED);  // get the current state of GPIO1 pin
  digitalWrite(SONOFF_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

void updateBlynk(int channel) {
  int state = digitalRead(SONOFF_RELAY_PINS[channel]);
  Blynk.virtualWrite(channel, state);
}

void setState(int state, int channel) {
  //relay
  digitalWrite(SONOFF_RELAY_PINS[channel], state);
/*
  //led
  if (SONOFF_LED_RELAY_STATE) {
    digitalWrite(SONOFF_LED, (state + 1) % 2); // led is active low
  }
*/
  //blynk
  updateBlynk(channel);

}

void toggleState() {
  cmd = CMD_BUTTON_CHANGE;
}

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void toggle(int channel) {
  Serial.println("toggle state");
  Serial.println(digitalRead(SONOFF_RELAY_PINS[channel]));
  int relayState = digitalRead(SONOFF_RELAY_PINS[channel]) == HIGH ? LOW : HIGH;
  setState(relayState, channel);
}

void restart() {
  //TODO turn off relays before restarting
  ESP.restart();
  delay(1000);
}

void reset() {
  //reset wifi credentials
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
  delay(1000);
}

#ifdef INCLUDE_BLYNK_SUPPORT
/**********
 * VPIN % 5
 * 0 off
 * 1 on
 * 2 toggle
 * 3 value
 * 4 led
 ***********/

BLYNK_WRITE_DEFAULT() {
  int pin = request.pin;
  int channel = pin;
  toggle(channel);
}

BLYNK_READ_DEFAULT() {
  // Generate random response
  int pin = request.pin;
  int channel = pin;
//  Blynk.virtualWrite(pin, digitalRead(SONOFF_RELAY_PINS[channel]));

}

//restart - button
BLYNK_WRITE(30) {
  int a = param.asInt();
  if (a != 0) {
    restart();
  }
}

//reset - button
BLYNK_WRITE(31) {
  int a = param.asInt();
  if (a != 0) {
    reset();
  }
}

#endif

void setup()
{
  Serial.begin(115200);

  //set led pin as output
  pinMode(SONOFF_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);


  const char *hostname = HOSTNAME;

  WiFiManager wifiManager;
  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  //wifiManager.setAPCallback(configModeCallback);

  //timeout - this will quit WiFiManager if it's not configured in 3 minutes, causing a restart
  wifiManager.setConfigPortalTimeout(180);

  //custom params
  EEPROM.begin(512);
  EEPROM.get(0, settings);
  EEPROM.end();

  if (settings.salt != EEPROM_SALT) {
    Serial.println("Invalid settings in EEPROM, trying with defaults");
    WMSettings defaults;
    settings = defaults;
  }


  WiFiManagerParameter custom_boot_state("boot-state", "on/off on boot", settings.bootState, 33);
  wifiManager.addParameter(&custom_boot_state);


  Serial.println(settings.bootState);

  Serial.println(settings.blynkToken);
  Serial.println(settings.blynkServer);
  Serial.println(settings.blynkPort);

  WiFiManagerParameter custom_blynk_text("<br/>Blynk config.<br/>");
  wifiManager.addParameter(&custom_blynk_text);

  WiFiManagerParameter custom_blynk_token("blynk-token", "blynk token", settings.blynkToken, 33);
  wifiManager.addParameter(&custom_blynk_token);

  WiFiManagerParameter custom_blynk_server("blynk-server", "blynk server", settings.blynkServer, 33);
  wifiManager.addParameter(&custom_blynk_server);

  WiFiManagerParameter custom_blynk_port("blynk-port", "port", settings.blynkPort, 6);
  wifiManager.addParameter(&custom_blynk_port);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(hostname)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    //ESP.reset();
    delay(1000);
  }

  //Serial.println(custom_blynk_token.getValue());
  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("Saving config");

    strcpy(settings.bootState, custom_boot_state.getValue());

    strcpy(settings.blynkToken, custom_blynk_token.getValue());
    strcpy(settings.blynkServer, custom_blynk_server.getValue());
    strcpy(settings.blynkPort, custom_blynk_port.getValue());

    Serial.println(settings.bootState);
    Serial.println(settings.blynkToken);
    Serial.println(settings.blynkServer);
    Serial.println(settings.blynkPort);

    EEPROM.begin(512);
    EEPROM.put(0, settings);
    EEPROM.end();
  }

  //config blynk
  if (strlen(settings.blynkToken) == 0) {
    BLYNK_ENABLED = false;
  }
  if (BLYNK_ENABLED) {
    Blynk.config(settings.blynkToken, settings.blynkServer, atoi(settings.blynkPort));
  }

  //OTA
  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.begin();

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();

  //setup button
  pinMode(SONOFF_BUTTONS[0], INPUT);
  pinMode(SONOFF_BUTTONS[1], INPUT);
  pinMode(SONOFF_BUTTONS[2], INPUT);
  
  attachInterrupt(SONOFF_BUTTONS[0], toggleState, CHANGE);
  attachInterrupt(SONOFF_BUTTONS[1], toggleState, CHANGE);
  attachInterrupt(SONOFF_BUTTONS[2], toggleState, CHANGE);

  //setup relay
  //TODO multiple relays
  pinMode(SONOFF_RELAY_PINS[0], OUTPUT);
  pinMode(SONOFF_RELAY_PINS[1], OUTPUT);
  pinMode(SONOFF_RELAY_PINS[2], OUTPUT);

  Serial.println("done setup");
  
}


void loop(){

  //ota loop
  ArduinoOTA.handle();

  //blynk connect and run loop
  if (BLYNK_ENABLED) {
    Blynk.run();
  }

  //Check Buttons
  
  currentState[1] = digitalRead(SONOFF_BUTTONS[1]);
  if (currentState[1] != buttonState[1]) {
    if (buttonState[1] == LOW && currentState[1] == HIGH) {
          toggle(1);
    }
  buttonState[1] = currentState[1];
  }
  
  currentState[2] = digitalRead(SONOFF_BUTTONS[2]);
  if (currentState[2] != buttonState[2]) {
    if (buttonState[2] == LOW && currentState[2] == HIGH) {
          toggle(2);
    }
  buttonState[2] = currentState[2];
  }
  
  switch (cmd) {
    case CMD_WAIT:
      break;
    case CMD_BUTTON_CHANGE:
      currentState[0] = digitalRead(SONOFF_BUTTONS[0]);
      if (currentState[0] != buttonState[0]) {
        if (buttonState[0] == LOW && currentState[0] == HIGH) {
          long duration = millis() - startPress;
          if (duration < 1000) {
            Serial.println("short press - toggle relay");
            toggle(0);
          } else if (duration < 30000) {
            Serial.println("medium press - restart");
            restart();
          } else if (duration < 60000) {
            Serial.println("long press - reset settings");
            reset();
          }
        } else if (buttonState[0] == HIGH && currentState[0] == LOW) {
          startPress = millis();
        }
        buttonState[0] = currentState[0];
      }
      break;
  }

}

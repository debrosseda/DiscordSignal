#include "config.h"

#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebSockets2_Generic.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

using namespace websockets2_generic;

#define DEBUG_APP bool true
#ifdef DEBUG_APP
#define DEBUG_MSG Serial.println
#else
#define DEBUG_MSG(MSG)
#endif

#define NUM_LEDS 45
#define DATA_PIN 12 
#define NUM_USERS 14 //Should evenly divide known_IDs from privateConfig.h

CRGBArray<NUM_LEDS> show;
CRGBArray<NUM_USERS> leds;
CRGBArray<NUM_USERS> userColors;

WebsocketsClient client;
DynamicJsonDocument doc(8096);
StaticJsonDocument<512> filter;

const char* host = "wss://gateway.discord.gg"; //hard cached. Check Get Gateway endpoint if this url no longer works.
const int httpsPort = 443;  //HTTPS= 443 and HTTP = 80

unsigned long heartbeatInterval = 0;
unsigned long lastHeartbeatAck = 0;
unsigned long lastHeartbeatSend = 0;
unsigned long now = millis();

uint8_t userActive[NUM_USERS]; //Array tracks user activity, decremented each hearbeat
int curUserPixel[NUM_USERS]; //Array that tracks user pixel, if any
uint8_t userTimer[NUM_USERS]; //chase velocity timers, decrement every 10ms
int userDelay[NUM_USERS]; //randomly assigned to vary chase speed and direction
bool isVoiceActive[NUM_USERS * USER_OPTIONS];
uint8_t defDelay = 8;

bool ownerPinged = false;
const uint8_t ownerID = 1;
uint8_t pulseLevel = 0;
const String usflag = "🇺🇸";


bool hasWsSession = false;
String websocketSessionId;
String resumeURL;
bool hasReceivedWSSequence = false;
unsigned long lastWebsocketSequence = 0;
bool needsReconnect = false;




void setup() {  
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(show, NUM_LEDS);
  FastLED.show();
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);

  Serial.begin(115200);
  delay(1000);

  //build filter json to save memory
  filter["t"] = true;
  filter["s"] = true;
  filter["op"] = true;
  filter["d"]["voice_states"][0]["user_id"] = true;
  filter["d"]["voice_states"][0]["mute"] = true;
  filter["d"]["voice_states"][0]["channel_id"] = true;
  filter["d"]["member"] = true;
  filter["d"]["member"]["user_id"] = true;
  filter["d"]["channel_id"] = true;
  filter["d"]["user_id"] = true;
  filter["d"]["self_mute"] = true;
  filter["d"]["mute"] = true;
  filter["d"]["author"]["id"] = true;
  filter["d"]["emoji"]["name"] = true;
  filter["d"]["mentions"][0]["id"] = true;
  filter["d"]["mention_roles"] = true;
  filter["d"]["mention_everyone"] = true;
  filter["d"]["heartbeat_interval"] = true;
  filter["d"]["resume_gateway_url"] = true;
  filter["d"]["session_id"] = true;


  WiFiManager wm;
  //wm.resetSettings();  //if needed to test wifi setup

  bool res;
  wm.setConfigPortalTimeout(300);
  //wm.setTimeout(10);
  wm.setConnectTimeout(15);

  // res = wm.autoConnect(); // auto generated AP name from chipid
  res = wm.autoConnect(portalSSID); // anonymous ap
  // res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  Serial.print("\nStart InSecured-ESP32-Client on ");
  Serial.println(ARDUINO_BOARD);
  Serial.println(WEBSOCKETS2_GENERIC_VERSION);


  // Wait some time to connect to wifi
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    Serial.print(".");
    delay(1000);
  }

  // Check if connected to wifi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No Wifi!");
    delay(10000);
    ESP.restart();
  }

  Serial.print("\nConnected to Wifi, Connecting to WebSockets Server @");
  Serial.println(websockets_connection_string);

  // run callback when messages are received
  client.onMessage(onMessageCallback);

  // run callback when events are occuring
  client.onEvent(onEventsCallback);

#if USING_INSECURE_MODE
  client.setInsecure();
#else
  // Before connecting, set the ssl fingerprint of the server
  client.setCACert(echo_org_ssl_ca_cert);
#endif

  // Connect to server
  bool connected = client.connect(websockets_connection_string);

  if (connected) {
    Serial.println("Connected!");

  } else {
    Serial.println("Not Connected!");
  }
  for (uint8_t thisUser=0; thisUser<NUM_USERS; thisUser++){ //initialize CRDB array from hues
    userColors[thisUser].setHSV(userHues[thisUser],255,128);
  }
}

void loop() {
  EVERY_N_MILLIS(10){updateChasePattern();}

  client.poll();
  now = millis();
  
  if (heartbeatInterval > 0) {
    if (now > lastHeartbeatSend + heartbeatInterval) {
      if (hasReceivedWSSequence) {
        DEBUG_MSG("Send:: {\"op\":1,\"d\":" + String(lastWebsocketSequence, 10) + "}");
        client.send("{\"op\":1,\"d\":" + String(lastWebsocketSequence, 10) + "}");
      } else {
        DEBUG_MSG("Send:: {\"op\":1,\"d\":null}");
        client.send("{\"op\":1,\"d\":null}");
      }

      lastHeartbeatSend = now;      
      DEBUG_MSG("Last Ack: " + String(lastHeartbeatAck) + "Last Send: " + String(lastHeartbeatSend));

      activityDecay();
      
      if (needsReconnect){
        bool connected = client.connect(resumeURL);
        delay(2000);
        String msg = "{\"op\":6,\"d\":{\"token\":\"" + String(bot_token) + "\",\"session_id\":\"" + websocketSessionId + "\",\"seq\":\"" + String(lastWebsocketSequence, 10) + "\"}}";
        DEBUG_MSG("Send:: " + msg);
        client.send(msg);
        delay(1000);        
      }



    }
    // Backup timeout catch. Resets everything
    if (lastHeartbeatAck + 4 * heartbeatInterval < millis()) {
      DEBUG_MSG("Heartbeat ack timeout");
      client.close();
      heartbeatInterval = 0;
      needsReconnect = false;
      ESP.restart();
    }
  }
}

void onMessageCallback(WebsocketsMessage message) {
  Serial.print("Got Message: ");
  Serial.println(message.c_str());
  deserializeJson(doc, message.c_str(), DeserializationOption::Filter(filter));

  now = millis();
  int idx = -1;
  String curUser = "";
 
  if (doc["op"] == 0)  { // Message
    needsReconnect = false;
    if (doc.containsKey("s")) {
      lastWebsocketSequence = doc["s"];
      //DEBUG_MSG("Last Seq: " + String(lastWebsocketSequence, 10));
      hasReceivedWSSequence = true;
    }

    if (doc["t"] == "READY") {
      websocketSessionId = doc["d"]["session_id"].as<String>();
      hasWsSession = true;
      DEBUG_MSG("SessionID is:" + websocketSessionId);
      resumeURL = doc["d"]["resume_gateway_url"].as<String>();
      DEBUG_MSG("Resume URL is:" + resumeURL);

    } else if (doc["t"] == "MESSAGE_REACTION_ADD") {
      curUser = doc["d"]["user_id"].as<String>();
      idx = findKnownUser(curUser);
      if (idx == ownerID){
        ownerPinged = false;
      }
      DEBUG_MSG(idx);
      makeUserActive(idx);
      //leds[idx]= CRGB(0, 32, 0);
      //Special React Effects
      if (doc["d"]["emoji"]["name"] =="🌈"){
        rainbow_wave(5,11);
      }
      if (doc["d"]["emoji"]["name"] == "❤️" || doc["d"]["emoji"]["name"] =="😍" || doc["d"]["emoji"]["name"] =="🥰"|| doc["d"]["emoji"]["name"] =="💯" || doc["d"]["emoji"]["name"] =="🔥"){
        color_sparkle(20, 5, 200, CRGB(64, 0, 0));
      }
      if (doc["d"]["emoji"]["name"] == "🤣" || doc["d"]["emoji"]["name"] =="😂" || doc["d"]["emoji"]["name"] =="😛" || doc["d"]["emoji"]["name"] =="😝" || doc["d"]["emoji"]["name"] =="💀"){
        color_sparkle(20, 5, 200, CRGB(0, 0, 64));
      }
      if (doc["d"]["emoji"]["name"] == "🤗"|| doc["d"]["emoji"]["name"] =="👍"|| doc["d"]["emoji"]["name"] =="🙌"|| doc["d"]["emoji"]["name"] =="👆"|| doc["d"]["emoji"]["name"] =="🫂"){
        color_sparkle(20, 5, 200, CRGB(32, 32, 0));
      }
      if (doc["d"]["emoji"]["name"] == "🤮"|| doc["d"]["emoji"]["name"] =="🤑"|| doc["d"]["emoji"]["name"] =="🤢"|| doc["d"]["emoji"]["name"] =="💰"|| doc["d"]["emoji"]["name"] =="💸"|| doc["d"]["emoji"]["name"] =="💵"){
        color_sparkle(20, 5, 200, CRGB(0, 64, 0));
      }
      if (doc["d"]["emoji"]["name"] == "🐶"){
        color_sparkle(20, 5, 200, CRGB(24, 24, 24));
      }
      if (doc["d"]["emoji"]["name"] == "🦅"||doc["d"]["emoji"]["name"] == "🗽" ||doc["d"]["emoji"]["name"].as<String>() ==  usflag){
        murica_sparkle(30, 5, 200);
      }
      if (doc["d"]["emoji"]["name"] == "⏩" && doc["d"]["channel_id"] == ctrlChannel && idx == ownerID){
        defDelay = 4;
        DEBUG_MSG("Delay is 4");
      }
      if (doc["d"]["emoji"]["name"] == "▶️" && doc["d"]["channel_id"] == ctrlChannel && idx == ownerID){
        defDelay = 8;
        DEBUG_MSG("Delay is 8 (default)");
      }
      if (doc["d"]["emoji"]["name"] == "⏸️" && doc["d"]["channel_id"] == ctrlChannel && idx == ownerID){
        defDelay = 16;
        DEBUG_MSG("Delay is 16");
      }

      
    } else if (doc["t"] == "TYPING_START") {
      curUser = doc["d"]["user_id"].as<String>();
      idx = findKnownUser(curUser);
      makeUserActive(idx);
      DEBUG_MSG(idx);

    } else if (doc["t"] == "MESSAGE_CREATE" || doc["t"] == "MESSAGE_UPDATE") {
      if (doc["d"]["author"]["id"]){
        curUser = doc["d"]["author"]["id"].as<String>();     
        idx = findKnownUser(curUser);
        if (idx == ownerID){
          ownerPinged = false;
        }
        DEBUG_MSG(idx);
        makeUserActive(idx);
      }
      
      if (doc["d"]["mentions"]){
        for (JsonVariant v : doc["d"]["mentions"].as<JsonArray>()){
          curUser = v["id"].as<String>();
          idx = findKnownUser(curUser);
          if (idx == ownerID){
            ownerPinged = true;
          }
          color_sparkle(40, 3, 20, userColors[idx]);
          delay(700);
          color_sparkle(40, 3, 20, userColors[idx]);
          delay(700);
          color_sparkle(40, 3, 20, userColors[idx]);
          DEBUG_MSG(idx);
        }
      }

      JsonArray mRoles = doc["d"]["mention_roles"].as<JsonArray>();
      for (JsonVariant thisRole : mRoles){
        if (thisRole.as<String>() == KNOWN_ROLE){
          color_sparkle(20, 3, 200, CRGB(32, 32, 32));
          ownerPinged = true;
        }
      }
      

      if (doc["d"]["mention_everyone"]==true){
        color_sparkle(20, 3, 200, CRGB(32, 32, 32)); //special effect for "@everyone" messages
        ownerPinged = true;
      }
        
    } else if (doc["t"] == "GUILD_CREATE") { //Guild Create Messages get very large even for moderate numbers of channels and users, easily resulting in memory overflow on the ESP32. Use caution if enabling GUILDS_INTENT to capture users initially in voice chat.
      for (JsonVariant v : doc["d"]["voice_states"].as<JsonArray>()) {
        curUser = v["user_id"].as<String>();
        idx = findVoiceUser(curUser);
        DEBUG_MSG(idx);
        if (idx != -1){
          isVoiceActive[idx]=true;
          idx = idx % NUM_USERS;
          makeUserActive(idx);
        }
      }

    } else if (doc["t"] == "VOICE_STATE_UPDATE") {
      curUser = doc["d"]["member"]["user"]["id"].as<String>();
      idx = findVoiceUser(curUser);
      DEBUG_MSG(idx);
      if (idx != -1){
        if (!doc["d"]["channel_id"].isNull() ) {
          isVoiceActive[idx]=true;
          idx = idx % NUM_USERS;
          makeUserActive(idx);

        } else {
          isVoiceActive[idx]=false;
          idx = idx % NUM_USERS;
          userActive[idx] = 1;

        }
      }
    }
  }else if (doc["op"] == 7) // Needs to reconnect
  {
    needsReconnect = true;
    client.close();
    DEBUG_MSG("Reconnect Requested");
  
  } else if (doc["op"] == 9)  // Connection invalid
  {
    client.close();
    hasWsSession = false;
    bool connected = client.connect(websockets_connection_string);

  } else if (doc["op"] == 11)  // Heartbeat ACK
  {
    lastHeartbeatAck = now;

  } else if (doc["op"] == 10)  // Start
  {
    heartbeatInterval = doc["d"]["heartbeat_interval"];
    DEBUG_MSG("Recieved Heartbeat:" + String(heartbeatInterval));

    if (hasWsSession) {

      needsReconnect = false;
      DEBUG_MSG("Reconnect Successful");
      
    } else {
      String msg = "{\"op\":2,\"d\":{\"token\":\"" + String(bot_token) + "\",\"intents\":" + gateway_intents + ",\"properties\":{\"$os\":\"linux\",\"$browser\":\"ESP32\",\"$device\":\"ESP32\"},\"compress\":false,\"large_threshold\":250}}";
      DEBUG_MSG("Send:: " + msg);
      client.send(msg);
    }

    lastHeartbeatSend = now;
    lastHeartbeatAck = now;
  }
}

void onEventsCallback(WebsocketsEvent event, String data) {
  (void)data;

  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("Connnection Opened");
  } else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("Connnection Closed");
    needsReconnect = true;
  } else if (event == WebsocketsEvent::GotPing) {
    Serial.println("Got a Ping!");
  } else if (event == WebsocketsEvent::GotPong) {
    Serial.println("Got a Pong!");
  }
}

int findKnownUser(String user_ID){
  int i;
  for (i = 0; i < (NUM_USERS * USER_OPTIONS); i++) {
    if (known_IDs[i] == user_ID) {
      return i % NUM_USERS;
    
    }}
    return -1;
}

int findVoiceUser(String user_ID){
  int i;
  for (i = 0; i < (NUM_USERS * USER_OPTIONS); i++) {
    if (known_IDs[i] == user_ID) {
      return i; //no mod operation to avoid issues with double joining. OR values before sending to lights.
    }}
    return -1;
}

void makeUserActive(int thisUser){
  if (userActive[thisUser] == 0){
    curUserPixel[thisUser] = random(NUM_LEDS);}

  userActive[thisUser] = defDelay;
  userDelay[thisUser] = random(2,7);
  userTimer[thisUser] = abs(userDelay[thisUser]);
  if (random(100)>49){
    userDelay[thisUser] = -userDelay[thisUser];
  }
}

void activityDecay(){
  for (uint8_t thisUser=0; thisUser<NUM_USERS; thisUser++){
    if (userActive[thisUser] > 0){
      userActive[thisUser]--;
    }
    if (isVoiceActive[thisUser] || isVoiceActive[thisUser + NUM_USERS]){
      userActive[thisUser] = defDelay;
    }
  }
}

void updateChasePattern(){
  show.fadeToBlackBy(255);
  if (ownerPinged){
    uint8_t curBrightness = qsub8(pulseLevel/8,8);
    fill_solid(show, NUM_LEDS, CRGB(curBrightness, curBrightness, curBrightness));
    show.fadeToBlackBy(8);
  }
  for (uint8_t thisUser = 0; thisUser < NUM_USERS; thisUser++){
    if (userActive[thisUser] > 0){
      userTimer[thisUser]--;
      if (userTimer[thisUser] == 0){ //update position if timer has decayed
        userTimer[thisUser]= abs(userDelay[thisUser]); //reset timer
        if (userDelay[thisUser] > 0){
          curUserPixel[thisUser]++;
          }else{
          curUserPixel[thisUser]--;        
        }
        if (curUserPixel[thisUser] < 0){
          curUserPixel[thisUser] = NUM_LEDS-1;
        }
        if (curUserPixel[thisUser] >= NUM_LEDS){
          curUserPixel[thisUser] = 0;
        }
      }
      show[curUserPixel[thisUser]] = show[curUserPixel[thisUser]] + userColors[thisUser];
    }
  }
  FastLED.show();
  //implement sawtooth white pulse for pings
  pulseLevel++;
  pulseLevel = pulseLevel % 128;
}

void rainbow_wave(uint8_t steps, uint8_t deltaHue) {     // The fill_rainbow call doesn't support brightness levels.
 for (int i=0; i<steps; i++){
  for (int hue = 0; hue<255; hue++){
    fill_rainbow(&show[0], NUM_LEDS, hue, deltaHue); // Use FastLED's fill_rainbow routine.
    show.fadeToBlackBy(250); //backup brightness limit
    FastLED.show();            
   }
 }
 show.fadeToBlackBy(255); //clears RGB data from unmapped regions of show array
 FastLED.show();
}

void color_sparkle(uint8_t holdTime, uint8_t actives, uint16_t loops, CRGB thisColor){
  show.fadeToBlackBy(255);
  for (uint16_t i=0; i<loops; i++){
    for (uint8_t k=0; k<actives; k++){
      show[random(NUM_LEDS)] = thisColor;
    }
    FastLED.show();
    FastLED.delay(holdTime);
    show.fadeToBlackBy(255);    
  }
  FastLED.show();
}

void murica_sparkle(uint8_t holdTime, uint8_t actives, uint16_t loops){
  show.fadeToBlackBy(255);
  uint8_t colorcase = 0;

  for (uint16_t i=0; i<loops; i++){
    for (uint8_t k=0; k<actives; k++){
      colorcase = colorcase % 3;
      if (colorcase == 0){
        show[random(NUM_LEDS)] = CRGB(64, 0, 0);
      }
      if (colorcase == 1){
        show[random(NUM_LEDS)] = CRGB(32, 32, 32);
      }
      if (colorcase == 2){
        show[random(NUM_LEDS)] = CRGB(0, 0, 64);
      }
     colorcase++;
    }
    FastLED.show();
    FastLED.delay(holdTime);
    show.fadeToBlackBy(255);    
  }
  FastLED.show();
}

#include <WiFiClient.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <secrets.h>

// Add this library: https://github.com/markszabo/IRremoteESP8266
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

#define IR_SEND_PIN D2
#define TIMEOUT 15U  // Suits most messages, while not swallowing repeats.
#define DELAY_BETWEEN_COMMANDS 1000
#define MQTT_VERSION                        MQTT_VERSION_3_1_1

const char* MQTT_CLIENT_ID =                "livingroom_ir";
const char* MQTT_IR_TV_STATE_TOPIC =        "/house/livingroom/tv/status";
const char* MQTT_IR_HIFI_STATE_TOPIC =      "/house/livingroom/hifi/status";
const char* MQTT_IR_TV_COMMAND_TOPIC =      "/house/livingroom/tv/switch";
const char* MQTT_IR_HIFI_COMMAND_TOPIC =    "/house/livingroom/hifi/switch";

uint16_t RECV_PIN = 14;
uint16_t CAPTURE_BUFFER_SIZE = 1024;

IRsend irsend(IR_SEND_PIN);
IRrecv irrecv(RECV_PIN, CAPTURE_BUFFER_SIZE, TIMEOUT, true);

decode_results results;  // Somewhere to store the results
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void encoding(decode_results *results) {
  switch (results->decode_type) {
    default:
    case UNKNOWN:      Serial.print("UNKNOWN");       break;
    case NEC:          Serial.print("NEC");           break;
    case NEC_LIKE:     Serial.print("NEC (non-strict)");  break;
    case SONY:         Serial.print("SONY");          break;
    case RC5:          Serial.print("RC5");           break;
    case RC5X:         Serial.print("RC5X");          break;
    case RC6:          Serial.print("RC6");           break;
    case RCMM:         Serial.print("RCMM");          break;
    case DISH:         Serial.print("DISH");          break;
    case SHARP:        Serial.print("SHARP");         break;
    case JVC:          Serial.print("JVC");           break;
    case SANYO:        Serial.print("SANYO");         break;
    case SANYO_LC7461: Serial.print("SANYO_LC7461");  break;
    case MITSUBISHI:   Serial.print("MITSUBISHI");    break;
    case SAMSUNG:      Serial.print("SAMSUNG");       break;
    case LG:           Serial.print("LG");            break;
    case WHYNTER:      Serial.print("WHYNTER");       break;
    case AIWA_RC_T501: Serial.print("AIWA_RC_T501");  break;
    case PANASONIC:    Serial.print("PANASONIC");     break;
    case DENON:        Serial.print("DENON");         break;
    case COOLIX:       Serial.print("COOLIX");        break;
  }
  if (results->repeat) Serial.print(" (Repeat)");
}

uint16_t getCookedLength(decode_results *results) {
  uint16_t length = results->rawlen - 1;
  for (uint16_t i = 0; i < results->rawlen - 1; i++) {
    uint32_t usecs = results->rawbuf[i] * RAWTICK;
    // Add two extra entries for multiple larger than UINT16_MAX it is.
    length += (usecs / UINT16_MAX) * 2;
  }
  return length;
}

void dumpInfo(decode_results *results) {
  if (results->overflow)
    Serial.printf("WARNING: IR code too big for buffer (>= %d). "
                  "These results shouldn't be trusted until this is resolved. "
                  "Edit & increase CAPTURE_BUFFER_SIZE.\n",
                  CAPTURE_BUFFER_SIZE);

  Serial.print("Encoding  : ");
  encoding(results);
  Serial.println("");

  Serial.print("Code      : ");
  serialPrintUint64(results->value, 16);
  Serial.print(" (");
  Serial.print(results->bits, DEC);
  Serial.println(" bits)");
}

void dumpRaw(decode_results *results) {
  // Print Raw data
  Serial.print("Timing[");
  Serial.print(results->rawlen - 1, DEC);
  Serial.println("]: ");

  for (uint16_t i = 1; i < results->rawlen; i++) {
    if (i % 100 == 0)
      yield();  // Preemptive yield every 100th entry to feed the WDT.
    if (i % 2 == 0) {  // even
      Serial.print("-");
    } else {  // odd
      Serial.print("   +");
    }
    Serial.printf("%6d", results->rawbuf[i] * RAWTICK);
    if (i < results->rawlen - 1)
      Serial.print(", ");  // ',' not needed for last one
    if (!(i % 8)) Serial.println("");
  }
  Serial.println("");  // Newline
}

void dumpCode(decode_results *results) {
  // Start declaration
  Serial.print("uint16_t ");               // variable type
  Serial.print("rawData[");                // array name
  Serial.print(getCookedLength(results), DEC);  // array size
  Serial.print("] = {");                   // Start declaration

  // Dump data
  for (uint16_t i = 1; i < results->rawlen; i++) {
    uint32_t usecs;
    for (usecs = results->rawbuf[i] * RAWTICK;
         usecs > UINT16_MAX;
         usecs -= UINT16_MAX)
      Serial.printf("%d, 0", UINT16_MAX);
    Serial.print(usecs, DEC);
    if (i < results->rawlen - 1)
      Serial.print(", ");  // ',' not needed on last one
    if (i % 2 == 0) Serial.print(" ");  // Extra if it was even.
  }

  // End declaration
  Serial.print("};");  //

  // Comment
  Serial.print("  // ");
  encoding(results);
  Serial.print(" ");
  serialPrintUint64(results->value, HEX);

  // Newline
  Serial.println("");

  // Now dump "known" codes
  if (results->decode_type != UNKNOWN) {
    // Some protocols have an address &/or command.
    // NOTE: It will ignore the atypical case when a message has been decoded
    // but the address & the command are both 0.
    if (results->address > 0 || results->command > 0) {
      Serial.print("uint32_t address = 0x");
      Serial.print(results->address, HEX);
      Serial.println(";");
      Serial.print("uint32_t command = 0x");
      Serial.print(results->command, HEX);
      Serial.println(";");
    }

    // All protocols have data
    Serial.print("uint64_t data = 0x");
    serialPrintUint64(results->value, 16);
    Serial.println(";");
  }
}


void mqttReconnect() {
  while (!mqttClient.connected()) { //loop until we're reconnected
    Serial.print("[MQTT] INFO: Attempting connection to: ");
    Serial.println(MQTT_SERVER_IP);
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("[MQTT] INFO: connected");
      Serial.print("[MQTT] INFO: subscribing to: ");
      Serial.println(MQTT_IR_HIFI_COMMAND_TOPIC);
      mqttClient.subscribe(MQTT_IR_HIFI_COMMAND_TOPIC);
      Serial.print("[MQTT] INFO: subscribing to: ");
      Serial.println(MQTT_IR_TV_COMMAND_TOPIC);
      mqttClient.subscribe(MQTT_IR_TV_COMMAND_TOPIC);
      }
    else {
      Serial.print("[MQTT] ERROR: failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println("[MQTT] DEBUG: try again in 5 seconds");
      delay(5000); //wait 5 seconds before retrying
    }
  }
}

void wifiSetup() {
  Serial.print("[WIFI] INFO: Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD); //connect to wifi
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("[WIFI] INFO: WiFi connected");
  Serial.println("[WIFI] INFO: IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* p_topic, byte* p_payload, unsigned int p_length) { //handle mqtt callbacks
  String payload;
  for (uint8_t i = 0; i < p_length; i++) { //concatenate payload
    payload.concat((char)p_payload[i]);
  }

  if(String(p_topic).equals(MQTT_IR_HIFI_COMMAND_TOPIC)) {
    Serial.print("[MQTT] TOPIC: ");
    Serial.println(p_topic);
    Serial.print("[MQTT] PAYLOAD:");
    Serial.println(payload);
    Serial.println("Surround Sound power");
    irsend.sendNEC(0x10EF08F7, 32);
    irsend.sendNEC(0x10EF08F7, 32);
    irsend.sendNEC(0x10EF08F7, 32);
  }
  if(String(p_topic).equals(MQTT_IR_TV_COMMAND_TOPIC)) {
    Serial.print("[MQTT] TOPIC: ");
    Serial.println(p_topic);
    Serial.print("[MQTT] PAYLOAD:");
    Serial.println(payload);
    Serial.println("TV power");
    irsend.sendSAMSUNG(0xE0E040BF, 32);
  }
  /* Serial.println("Surround Sound Down");
  irsend.sendNEC(0x10EF708F, 32);
  Serial.println("Surround Sound Up");
  irsend.sendNEC(0x10EF58A7, 32); */
}

void setup(void){
  Serial.begin(115200);
  irsend.begin();
  irrecv.enableIRIn();  // Start the receiver

  ArduinoOTA.setHostname(MQTT_CLIENT_ID);
  ArduinoOTA.onStart([]() {
    String type;
    type = "sketch";
    Serial.println("Start updating " + type);
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
  ArduinoOTA.begin();

  wifiSetup();
  mqttClient.setServer(MQTT_SERVER_IP, MQTT_SERVER_PORT);
  mqttClient.setCallback(mqttCallback);
  Serial.println("");
}

void loop(void){
  yield();
  ArduinoOTA.handle();
  if (!mqttClient.connected()) {
    mqttReconnect();
  }
  mqttClient.loop();
  if (irrecv.decode(&results)) {
    dumpInfo(&results);           // Output the results
    dumpRaw(&results);            // Output the results in RAW format
    dumpCode(&results);           // Output the results as source code
    Serial.println("");           // Blank line between entries
  }
}

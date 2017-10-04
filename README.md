# Intro
Hey guys, i created a replacement, or lets say enhancement, for some of my remotes laying on my livingroom table. So far most of the remotes have been consolidated into smartphone apps (like FireTV or my MediaReceiver) already but some of my old stuff like the TV and the HIFI soundsystem just dont provide stuff like this. The idea was to capture the IR codes and put them into a sort of library that i can then play from a ESP8266 with a IR LED. Since i also own an Amazon Echo and Homeassistant appliance, i wanted to make sure its compatible with those two systems.

# Requirements
You dont really have to have (all of) these things but they would be supported, so why not using them :-)
I am a really big fan of homeassistant by the way. I can only recommend to check it out.

**Mandatory**
- MQTT Server
- ESP8266 (i use NodeMCU V1 ESP12E)
- IR Receiver (i used VS1838B)
- IR LED (use anything you like)
- Transistor (General Purpose / i used [2N3904](https://www.sparkfun.com/datasheets/Components/2N3904.pdf)
- 430'ish OHM resistor (should be 470 but 430 works for me)

**Nice to have**
- [Homeassistant](https://www.home-assistant.io)
- Amazon Echo

### How does it work
Basically the whole thing gets triggered by the MQTT command topics described in the code.
Once the MQTT Callback is being triggered it will execute the code below it.
```sh
void mqttCallback(char* p_topic, byte* p_payload, unsigned int p_length) { //handle mqtt callbacks
  String payload;
  for (uint8_t i = 0; i < p_length; i++) { //concatenate payload
    payload.concat((char)p_payload[i]);
  }
  if(String(p_topic).equals(MQTT_IR_TV_COMMAND_TOPIC)) {
    Serial.print("[MQTT] TOPIC: ");
    Serial.println(p_topic);
    Serial.print("[MQTT] PAYLOAD:");
    Serial.println(payload);
    Serial.println("TV power");
    irsend.sendSAMSUNG(0xE0E040BF, 32);
  }
}
```
The actual `irsend.sendXXX` value comes from recording the codes.
If you have the IR receiver attached as i do, you can just use your remote and it will show you the exact code you need to put in here.

### Installation

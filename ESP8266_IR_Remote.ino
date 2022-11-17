#include <ESP8266WiFi.h>
//#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <FS.h>
#include <WiFiUdp.h>
#include <ArduinoMqttClient.h>

#define _OTA_
#define SerialOn

#ifdef _OTA_
#include <ArduinoOTA.h>
#endif

//21:27:22.754 -> Timestamp : 000012.796
//21:27:22.754 -> Library   : v2.8.4
//21:27:22.754 -> 
//21:27:22.754 -> Protocol  : PANASONIC
//21:27:22.754 -> Code      : 0x40040100BCBD (48 Bits)
uint16_t PANASONIC_TV_POWER[99] = {
  3514, 1702,  472, 396,  468, 1270,  494, 374,  494, 374,  496, 368,  500, 370,  498, 370,  472, 396,  472, 398,  472, 396,
  466, 402,  494, 374,  494, 374,  494, 1244,  494, 368,  500, 368,  500, 370,  472, 396,  472, 396,  472, 396,  472, 398,
  466, 402,  466, 402,  494, 1244,  494, 374,  494, 374,  496, 374,  494, 368,  500, 370,  472, 396,  472, 396,  472, 398,
  472, 1266,  466, 402,  496, 1242,  494, 1238,  500, 1238,  470, 1266,  472, 396,  468, 402,  494, 1242,  496, 374,  494,
  1238,  498, 1238,  472, 1268,  494, 1244,  494, 374,  494, 1238,  472};  // PANASONIC 40040100BCBD
uint64_t PANASONIC_ADDRESS = 0x400400000000;
//21:27:22.832 -> uint32_t command = 0x100BCBD;
//uint64_t data = 0x40040100BCBD;

namespace {
const char* HOSTNAME = "esp8266-ir";
}
int SERIAL_SPEED = 115200;
int LED_PIN = 1;
int RECV_PIN = 0; //1:tx //an IR detector/demodulator is connected to GPIO  //D1 //D3 on a nodemcu
int SEND_PIN = 3; //3:rx // D2 paired with a 1.6 Ohm resistor
String CONFIG_PATH = "/config.json";
String CONFIG_BACKUP_PATH = "/config.bak";

WiFiManager wifiManager;
MDNSResponder mdns;
ESP8266WebServer server(80);

File fsUploadFile;

IRrecv irrecv(RECV_PIN);
IRsend irsend(SEND_PIN);

decode_results results1;        // Somewhere to store the results
decode_results results;        // Somewhere to store the results
decode_results results3;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.1.84";
int        port     = 1883;
const char topic[]  = "ir_remote/learn";
const char action_topic[]  = "ir_remote/action";

unsigned long StrToUL(String str) {
  char tarray[15];
  str.toCharArray(tarray, sizeof(tarray));
  unsigned long code = strtoul(tarray, NULL, 10);
  return (code);
}

unsigned long combineBytes(int a1, int a2, int a3, int a4) {
  unsigned long code = 0;
  code = code + a1;
  code = (code << 8) + a2;
  code = (code << 8) + a3;
  code = (code << 8) + a4;
  return (code);
}

int flipBits(unsigned char b) {
  return ((b * 0x0202020202ULL & 0x010884422010ULL) % 1023);
}

void dump(int a1, int a2, int a3) {
  Serial.print(a1, HEX);
  Serial.print("-");
  Serial.print(a2, HEX);
  Serial.print("-");
  Serial.println(a3, HEX);
}

void setOnBoardLedOn() {
#ifndef SerialOn
  digitalWrite(LED_PIN, LOW); // Turn the LED on.
#endif
}

void setOnBoardLedOff() {
#ifndef SerialOn
  digitalWrite(LED_PIN, HIGH); // Turn the LED off.
#endif
}


void handleRoot() {
  server.send(200, "text/html",
      "Please specify command! Form: /ir?code=xxx&bits=xx&protocol=x");
}

void handleIr() {

  setOnBoardLedOn();

  String codestring = server.arg("code");
  String protocol = server.arg("protocol");
  String bitsstring = server.arg("bits");

  String deviceCode = server.arg("deviceCode");
  String subDeviceCode = server.arg("subDeviceCode");
  String obc = server.arg("obc");
  String pronto = server.arg("pronto");

  //  String webOutput = "Protocol: "+protocol+"; Code: "+codestring+"; Bits: "+bitsstring + " - ("+deviceCode + subDeviceCode + obc +")";
  String webOutput = "";

  unsigned long code = 0;
  int rc5_control_bit = 0;

  if ((codestring != "") && (bitsstring != "")) {
    //unsigned long code = codestring.toInt();
    char tarray[15];
    codestring.toCharArray(tarray, sizeof(tarray));
    code = strtoul(tarray, NULL, 16);
    // unsigned long code = atol(codestring);
  }
  int bits = bitsstring.toInt();

  if ((obc != "") && (deviceCode != "")) {
    //convert OBC & deviceCode to hex CodeString

    int iDeviceCode = StrToUL(deviceCode);
    int iDeviceCodeLSB = flipBits(iDeviceCode);
    int iDeviceCodeLSB_INV = iDeviceCodeLSB ^ 0xFF;
    dump(iDeviceCode, iDeviceCodeLSB, iDeviceCodeLSB_INV);

    int iSubDeviceCode;
    int iSubDeviceCodeLSB;
    int iSubDeviceCodeLSB_INV;
    if ((subDeviceCode == "")) {
      iSubDeviceCode = iDeviceCode;
      iSubDeviceCodeLSB = iDeviceCodeLSB;
      iSubDeviceCodeLSB_INV = iDeviceCodeLSB_INV;
    } else {
      iSubDeviceCode = StrToUL(subDeviceCode);
      iSubDeviceCodeLSB = flipBits(iSubDeviceCode);
      iSubDeviceCodeLSB_INV = iSubDeviceCodeLSB ^ 0xFF;
    }

    int iOBC = StrToUL(obc);
    int iOBCLSB = flipBits(iOBC);
    int iOBCLSB_INV = iOBCLSB ^ 0xFF;
    dump(iOBC, iOBCLSB, iOBCLSB_INV);

    Serial.println(iDeviceCodeLSB, HEX);
    Serial.println(iOBCLSB, HEX);
    Serial.println("----");
    if (protocol == "Samsung") {
      code = combineBytes(iDeviceCodeLSB, iSubDeviceCodeLSB, iOBCLSB,
          iOBCLSB_INV);
    } else if (protocol == "NEC" || protocol == "NECx2") {
      code = combineBytes(iDeviceCodeLSB, iDeviceCodeLSB_INV, iOBCLSB,
          iOBCLSB_INV);
    } else if (protocol == "RC6") {
      /*NOT TESTED*/
      code = combineBytes(0, 0, iDeviceCode, iOBC);
      bits = 20;
    } else if (protocol == "RC5") {
      /*NOT TESTED*/
      /*control=1,device=5,command=6*/
      rc5_control_bit = abs(rc5_control_bit - 1);
      code = rc5_control_bit;
      code += code << 5 + iDeviceCodeLSB;
      code += code << 6 + iOBCLSB;
      bits = 12;
    } else if (protocol == "JVC") {
      /*NOT TESTED*/
      code = combineBytes(0, 0, iDeviceCodeLSB, iOBCLSB);
      bits = 16;
    } else if (protocol == "Sony") {
      /*NOT TESTED & highly suspect, need to seem some example codes*/
      code = iOBCLSB;
      if (subDeviceCode != "") {
        bits = 20;
        code = code << 5 + iDeviceCodeLSB;
        code = code << 8 + iSubDeviceCodeLSB;

      } else if (iDeviceCode > 2 ^ 5) {
        bits = 15;
        code = code << 8 + iDeviceCodeLSB;
      } else {
        bits = 12;
        code = code << 5 + iDeviceCodeLSB;
      }
    } else {
      code = 0;
      server.send(404, "text/html", "Protocol not implemented for OBC!");
    }

    Serial.println("---");
    Serial.println(code, HEX);
    Serial.println("---");

  }

  if (code != 0) {
    int str_len = protocol.length() + 1;
    char proto[str_len];
    protocol.toCharArray(proto, str_len);
    if (sendIrByCode(proto, code, bits)) {
      server.send(200, "text/html", webOutput);
    } else {
      server.send(404, "text/html", "Protocol not implemented!");
    }
  } else if (pronto != "") {
    if (sendIrByPronto(pronto)) {
      server.send(200, "text/html", "pronto code!");
    } else {
      server.send(404, "text/html", "unknown pronto format!");
    }
  } else {
    server.send(404, "text/html", "Missing code or bits!");
  }
}

bool sendIrByCode(char* protocol, unsigned long code, int bits) {

  bool result = true;
  if (strcmp(protocol, "NEC") == 0) {
    irsend.sendNEC(code, bits);
  } else if (strcmp(protocol, "Sony") == 0) {
    irsend.sendSony(code, bits);
  } else if (strcmp(protocol, "Whynter") == 0) {
    irsend.sendWhynter(code, bits);
  } else if (strcmp(protocol, "LG") == 0) {
    irsend.sendLG(code, bits);
  } else if (strcmp(protocol, "RC5") == 0){
    Serial.print("sending RC5 ");
    Serial.print(code, HEX);
    Serial.print(" ");
    Serial.println(bits);
    irsend.sendRC5(code, bits);
  } else if (strcmp(protocol, "RC6") == 0) {
    irsend.sendRC6(code, bits);
  } else if (strcmp(protocol, "DISH") == 0) {
    irsend.sendDISH(code, bits);
  } else if (strcmp(protocol, "SharpRaw") == 0) {
    irsend.sendSharpRaw(code, bits);
  } else if (strcmp(protocol, "Samsung") == 0) {
    irsend.sendSAMSUNG(code, bits);
  } else if (strcmp(protocol, "Panasonic") == 0) {
    Serial.print("sending Panasonic ");
    Serial.print(code, HEX);
    Serial.print(" ");
    Serial.println(bits);    
    // irsend.sendPanasonic64(code, bits);
    uint64_t addrCommand = PANASONIC_ADDRESS + code;
    irsend.sendPanasonic64(addrCommand);
//    irsend.sendRaw(rawData,99,38);
  
  } else {
    result = false;
  }
  return result;
}

bool sendIrByPronto(String pronto) {
  //pronto code
  //blocks of 4 digits in hex
  //preample is 0000 FREQ LEN1 LEN2
  //followed by ON/OFF durations in FREQ cycles
  //Freq needs a multiplier
  //blocks seperated by %20
  //we are ignoring LEN1 & LEN2 for this use case as not allowing for repeats
  //just pumping all
  int spacing = 5;
  int len = pronto.length();
  int out_len = ((len - 4) / spacing) - 3;
  uint16_t prontoCode[out_len];
  unsigned long timeperiod;
  unsigned long multiplier = .241246;
  bool result = true;

  int pos = 0;
  unsigned long hz;
  if (pronto.substring(pos, 4) != "0000") {
    result = false;
    //unknown pronto format
  } else {
    pos += spacing;

    hz = strtol(pronto.substring(pos, pos + 4).c_str(), NULL, 16);
    hz = (hz * .241246);
    hz = 1000000 / hz;
    //XXX TIMING IS OUT
    timeperiod = 1000000 / hz;
    pos += spacing; //hz
    pos += spacing; //LEN1
    pos += spacing; //LEN2
    delay(0);
    for (int i = 0; i < out_len; i++) {
      prontoCode[i] = (strtol(pronto.substring(pos, pos + 4).c_str(), NULL,
          16) * timeperiod) + 0.5;
      pos += spacing;
    }
    //sendRaw
    yield();

    irsend.sendRaw(prontoCode, out_len, hz / 1000);
    return result;
  }
  setOnBoardLedOff();
}

void handleNotFound() {
  server.send(404, "text/plain", "404");
}

void handleReset() {
  wifiManager.resetSettings();
}

void handleLoadConfig() {
  File f;
  if (!SPIFFS.exists(CONFIG_PATH)) {
    //doesn't exist create blank config
    f = SPIFFS.open(CONFIG_PATH, "w");
    f.println("{\"pages\":[{\"name\":\"New Page\",\"buttons\":[]}]}");
    Serial.println("CREATED");
    f.close();
  }
  f = SPIFFS.open(CONFIG_PATH, "r");
  String s = f.readString();
  Serial.println(s);
  f.close();
  String callback = server.arg("callback");
  server.send(200, "text/plain", callback + "(" + s + ")");
}

void handleLoadBackupConfig() {
  File f;
  if (!SPIFFS.exists(CONFIG_BACKUP_PATH)) {
    f = SPIFFS.open(CONFIG_BACKUP_PATH, "w");
    f.println("{\"pages\":[{\"name\":\"New Page\",\"buttons\":[]}]}");
    Serial.println("CREATED");
    f.close();
  }
  f = SPIFFS.open(CONFIG_BACKUP_PATH, "r");
  String s = f.readStringUntil('\n');
  Serial.println(s);
  f.close();
  String callback = server.arg("callback");
  server.send(200, "text/plain", callback + "(" + s + ")");

}

void handleSaveConfig() {
  Serial.println("Saving");
  File f;
  File f2;
  if (SPIFFS.exists(CONFIG_PATH)) {
    f = SPIFFS.open(CONFIG_PATH, "r");
    f2 = SPIFFS.open(CONFIG_BACKUP_PATH, "w");
    String s = f.readStringUntil('\n');
    f2.println(s);
    Serial.println("BACKED UP");
    f.close();
    f2.close();
    Serial.println("Config backuped");
  }
  f = SPIFFS.open(CONFIG_PATH, "w");
  String newConfig = server.arg("config");
  f.println(newConfig);
  Serial.println(newConfig);
  f.close();
  String callback = server.arg("callback");
  server.send(200, "text/plain", callback + "('SAVED')");

}

void handleDeleteConfig() {
  File f;
  String callback = server.arg("callback");
  if (!SPIFFS.exists(CONFIG_PATH)) {
    Serial.println("FILE NOT FOUND");
  } else {
    SPIFFS.remove(CONFIG_PATH);
    Serial.println("FILE DELETED");
  }
  server.send(200, "text/plain", callback + "(\"File Deleted\")");
}
;

void learnHandler() {
  Serial.println("In Learning Handling");
  String callback = server.arg("callback");

  String proto = "";
  { // Grab an IR code
    //dumpInfo(&results);           // Output the results
    switch (results.decode_type) {
    default:
    case UNKNOWN:
      proto = ("UNKNOWN");
      break;
    case NEC:
      proto = ("NEC");
      break;
    case SONY:
      proto = ("Sony");
      break;
    case RC5:
      proto = ("RC5");
      break;
    case RC6:
      proto = ("RC6");
      break;
    case DISH:
      proto = ("DISH");
      break;
    case SHARP:
      proto = ("SHARP");
      break;
    case JVC:
      proto = ("JVC");
      break;
    case SANYO:
      proto = ("Sanyo");
      break;
    case MITSUBISHI:
      proto = ("MITSUBISHI");
      break;
    case SAMSUNG:
      proto = ("Samsung");
      break;
    case LG:
      proto = ("LG");
      break;
    case WHYNTER:
      proto = ("Whynter");
      break;
      // case AIWA_RC_T501: Serial.print("AIWA_RC_T501");  break ;
    case PANASONIC:
      proto = ("PANASONIC");
      break;
    }
    //results->value
    //Serial.print(results->value, HEX);

    Serial.println("Here");           // Blank line between entries
    irrecv.resume();              // Prepare for the next value
    String output = callback + "({protocol:\"" + proto + "\", value:\""
        + String((unsigned long) results.value, HEX) + "\", bits:\""
        + String(results.bits) + "\"})";
    Serial.println(output);
    server.send(200, "text/html", output);
  }
}

void handleUploadRequest() {
  Serial.println("in Upload Request");
  server.send(200, "text/html", "");
}

void handleFileUpload() { // upload a new file to the SPIFFS
  //if (server.uri() != "/upload") return;
  Serial.println("in upload");
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    Serial.print("handleFileUpload Name: ");
    Serial.println(filename);

    fsUploadFile = SPIFFS.open(filename, "w"); // Open the file for writing in SPIFFS (create if it doesn't exist)
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                  // If the file was successfully created
      fsUploadFile.close();                              // Close the file again
      Serial.print("handleFileUpload Size: ");
      Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html"); // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }

  }
}

void setup(void) {
#ifdef SerialOn
  Serial.begin(SERIAL_SPEED, SERIAL_8N1, SERIAL_TX_ONLY);
#endif
#ifndef SerialOn
  pinMode(LED_PIN, OUTPUT);
  setOnBoardLedOn();
#endif
  irsend.begin();
  SPIFFS.begin();

  Serial.println("v5.2");

  WiFi.hostname(HOSTNAME);
  wifiManager.autoConnect(HOSTNAME, "1234567890");

#ifdef _OTA_
//OTA
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPort(8266);
  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"irsvr");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    ESP.restart();
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
#endif

  server.on("/ir", handleIr);
  server.on("/reset", handleReset);
  server.on("/learn", learnHandler);
  server.on("/test", handleRoot);

  server.on("/loadConfig", handleLoadConfig);
  server.on("/loadBackupConfig", handleLoadBackupConfig);

  server.on("/saveConfig", handleSaveConfig);
  server.on("/deleteConfig", handleDeleteConfig);
  server.onFileUpload(handleFileUpload);
  server.on("/upload", HTTP_POST, handleUploadRequest, handleFileUpload);

  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/index.html", SPIFFS, "/index.html");
  server.serveStatic("/app.js", SPIFFS, "/app.js");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/success.html", SPIFFS, "/success.html");
  server.serveStatic("/config.json", SPIFFS, "/config.json");

  server.onNotFound(handleNotFound);

  // You can provide a unique client ID, if not set the library uses Arduino-millis()
  // Each client must have a unique client ID
  mqttClient.setId("ir_remote");

  // You can provide a username and password for authentication
  // mqttClient.setUsernamePassword("username", "password");  

  Serial.println("starting connection to mqtt broker");
  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  // set the message receive callback
  mqttClient.onMessage(onMqttMessage);

  Serial.print("Subscribing to topic: ");
  Serial.println(action_topic);
  Serial.println();

  // subscribe to a topic
  mqttClient.subscribe(action_topic);

  // topics can be unsubscribed using:
  // mqttClient.unsubscribe(topic);

  Serial.print("Waiting for messages on topic: ");
  Serial.println(action_topic);
  Serial.println();



  server.begin();
  Serial.println("HTTP server startedx");
  irrecv.enableIRIn();

  if (mdns.begin(HOSTNAME, WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }



  setOnBoardLedOff();
}

void onMqttMessage(int messageSize) {

  // we received a message, print out the topic and contents

  Serial.print("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  if (mqttClient.messageTopic().equalsIgnoreCase(action_topic)){
    Serial.println("received action message");
    char buffer[64];
    int loc = 0;
    while (mqttClient.available()) {
      char aChar = mqttClient.read();
//      Serial.print("loc=");
//      Serial.print(loc);
//      Serial.print(",char=");
//      Serial.println(aChar);
      if (loc <= sizeof(buffer) - 1) {
        buffer[loc++] = aChar; 
      } else {
        Serial.print("x:");
        Serial.println(mqttClient.read());
      }

    }
    buffer[loc] = '\n';
    char *parts[3];

    int i=0;
    char *ptr = strtok(buffer, ",");
    while (ptr != NULL) {
      if (i<3) {
        parts[i++] = ptr;
      }
      ptr = strtok(NULL, ",");
    }

    unsigned long code = strtoul(parts[2], NULL, 16);
    int bits = atoi(parts[1]);

    Serial.print("protocol=");
    Serial.println(parts[0]);
    Serial.print("bits=");
    Serial.println(bits);
    Serial.print("msg=");
    Serial.println(code, HEX);

    sendIrByCode(parts[0], code, bits);
    Serial.println("end action message");

  }

  Serial.println();
}

void loop(void) {

  // call poll() regularly to allow the library to send MQTT keep alives which
  // avoids being disconnected by the broker
  mqttClient.poll();  

  server.handleClient();
  mdns.update();
#ifdef _OTA_
  ArduinoOTA.handle();
#endif

  if (irrecv.decode(&results1)) {
    if (results1.value == 0xffffffff) {
      Serial.println("ffffffff recieved ignoring");
    } else {
      String proto = "";
      { // Grab an IR code
        //dumpInfo(&results);           // Output the results
        // decodePanasonic(&results3);
        switch (results1.decode_type) {
        default:
        case UNKNOWN:
          proto = ("UNKNOWN");
          break;
        case NEC:
          proto = ("NEC");
          break;
        case SONY:
          proto = ("Sony");
          break;
        case RC5:
          proto = ("RC5");
          break;
        case RC6:
          proto = ("RC6");
          break;
        case DISH:
          proto = ("DISH");
          break;
        case SHARP:
          proto = ("SHARP");
          break;
        case JVC:
          proto = ("JVC");
          break;
        case SANYO:
          proto = ("Sanyo");
          break;
        case MITSUBISHI:
          proto = ("MITSUBISHI");
          break;
        case SAMSUNG:
          proto = ("Samsung");
          break;
        case LG:
          proto = ("LG");
          break;
        case WHYNTER:
          proto = ("Whynter");
          break;
          // case AIWA_RC_T501: Serial.print("AIWA_RC_T501");  break ;
        case PANASONIC:
          proto = ("PANASONIC");
          break;
        }
      }
      Serial.print("Signal recveived " + proto + " " + results1.bits + " ");
      Serial.println((unsigned long) results1.value, HEX);

      
      // send message, the Print interface can be used to set the message contents
      mqttClient.beginMessage(topic);
      mqttClient.print(proto + ":" + String((unsigned long) results.value, HEX) + ":" + results1.bits);
      mqttClient.endMessage();

      irrecv.decode(&results);
    }
    irrecv.resume();
  }

}

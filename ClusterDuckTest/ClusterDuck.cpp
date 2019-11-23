#include "ClusterDuck.h"

U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

IPAddress apIP(192, 168, 1, 1);
WebServer webServer(80);

ClusterDuck::ClusterDuck(String deviceId, const int formLength) {
  _deviceId = deviceId;

  String * values = new String[formLength];

  formArray = values;
  fLength = formLength;

}

void ClusterDuck::begin(int baudRate) {
  Serial.begin(baudRate);
  Serial.println("Serial start");
  //Serial.println(_deviceId + " says Quack!");
}

void ClusterDuck::setupDisplay()  {
  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
}

// Initial LoRa settings
void ClusterDuck::setupLoRa(long BAND, int SS, int RST, int DI0, int TxPower) {
  SPI.begin(5, 19, 27, 18);
  LoRa.setPins(SS, RST, DI0);
  LoRa.setTxPower(TxPower);
  //LoRa.setSignalBandwidth(62.5E3);

  //Initialize LoRa
  if (!LoRa.begin(BAND))
  {
    u8x8.clear();
    u8x8.drawString(0, 0, "Starting LoRa failed!");
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  else
  {
    Serial.println("LoRa On");
  }

  //  LoRa.setSyncWord(0xF3);         // ranges from 0-0xFF, default 0x34
  LoRa.enableCrc();             // Activate crc
}

//Setup Captive Portal
void ClusterDuck::setupPortal(const char *AP) {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP);
  delay(200); // wait for 200ms for the access point to start before configuring

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  Serial.println("Created Hotspot");

  dnsServer.start(DNS_PORT, "*", apIP);

  webServer.onNotFound([&]()
  {
    webServer.send(200, "text/html", portal);
  });

  webServer.on("/", [&]()
  {
    webServer.send(200, "text/html", portal);
  });

  webServer.on("/id", [&]() {
    webServer.send(200, "text/html", _deviceId);
  });

  webServer.on("/restart", [&]()
  {
    webServer.send(200, "text/plain", "Restarting...");
    delay(1000);
    restartDuck();
  });

  String    page = "<h1>Duck Mac Address</h1><h3>Data:</h3> <h4>" + _deviceId + "</h4>";
  webServer.on("/mac", [&]() {
    webServer.send(200, "text/html", page);
  });

  // Test 👍👌😅

  webServer.begin();

  if (!MDNS.begin(DNS))
  {
    Serial.println("Error setting up MDNS responder!");
  }
  else
  {
    Serial.println("Created local DNS");
    MDNS.addService("http", "tcp", 80);
  }
}

//Run Captive Portal
bool ClusterDuck::runCaptivePortal() {
  dnsServer.processNextRequest();
  webServer.handleClient();
  if (webServer.arg(1) != "" || webServer.arg(1) != NULL) {
    Serial.println("data received");
    Serial.println(webServer.arg(1));
    return true;
  } else {
    return false;
  }
}


//Setup premade DuckLink with default settings
void ClusterDuck::setupDuckLink() {
  setupDisplay();
  setupLoRa();
  setupPortal();

  Serial.println("Duck Online");
  u8x8.drawString(0, 1, "Duck Online");

}

void ClusterDuck::runDuckLink() { //TODO: Add webserver clearing after message sent

  if (runCaptivePortal()) {
    Serial.println("Portal data received");
    sendPayload(_deviceId, uuidCreator(), getPortalData());
    Serial.println("Message Sent");
  }

}

void ClusterDuck::setupMamaDuck() {
  setupDisplay();
  setupPortal();
  setupLoRa();

  LoRa.onReceive(repeatLoRaPacket);
  LoRa.receive();

  Serial.println("MamaDuck Online");
  u8x8.drawString(0, 1, "Duck Online");
}

void ClusterDuck::runMamaDuck() {
  
  if (runCaptivePortal()) {
    Serial.println("Portal data received");
    sendPayload(_deviceId, uuidCreator(), getPortalData());
    Serial.println("Message Sent");

    LoRa.receive();
  }
  
}

/**
  getPortalData
  Reads WebServer Parameters and couples into Data Struct
  @return coupled Data Struct
*/
String * ClusterDuck::getPortalData() {
  //Array holding all form values
  String * val = formArray;

  for (int i = 0; i < fLength; ++i) {
    val[i] = webServer.arg(i);
  }

  return val;
}

void ClusterDuck::sendPayload(String senderId, String messageId, String * arr, String lastPath) {
  
  if(arr[0] == "0xF8") { //Send pong to a ping
    LoRa.beginPacket();
    couple(iamhere_B, "1");
    LoRa.endPacket();
  } else if(arr[0] != "0xF7") { //Don't send a pong to a pong
    LoRa.beginPacket();
    couple(senderId_B, senderId);
    couple(messageId_B, messageId);
    for (int i = 0; i < fLength; i++) {
      couple(byteCodes[i], arr[i]);
    }
    if(lastPath == "") {
      couple(path_B, _deviceId);
    } else {
      couple(path_B, lastPath + "," + _deviceId);
    }
    LoRa.endPacket(); 
  }
}

void ClusterDuck::couple(byte byteCode, String outgoing) {
  LoRa.write(byteCode);               // add byteCode
  LoRa.write(outgoing.length());      // add payload length
  LoRa.print(outgoing);               // add payload
}

//Decode LoRa message
String ClusterDuck::readMessages(byte mLength)  {
  String incoming = "";

  for (int i = 0; i < mLength; i++)
  {
    incoming += (char)LoRa.read();
  }
  return incoming;
}

char * ClusterDuck::readPath(byte mLength)  {
  char * arr = new char[mLength];

  for (int i = 0; i < mLength; i++)
  {
    arr[i] = (char)LoRa.read();
  }
  return arr;
  
}

/**
  receive
  Reads and Parses Received Packets
  @param packetSize
*/

static void ClusterDuck::repeatLoRaPacket(int packetSize) {
  if (packetSize != 0)  {    
    Serial.print("Packet Received");
    // read packet

    _rssi = LoRa.packetRssi();
    _snr = LoRa.packetSnr();
    _freqErr = LoRa.packetFrequencyError();
    _availableBytes = LoRa.available();

    String * arr = getPacketData(packetSize);

    if(checkPath(_lastPacket.path)) {
      sendPayload(_lastPacket.senderId, _lastPacket.messageId, arr, _lastPacket.path); 
    }
  }
}

bool ClusterDuck::checkPath(String path) {
  String temp = "";
  int len = path.length() + 1;
  char arr[len];
  path.toCharArray(arr, len);

  for (int i = 0; i < len; i++) {
    if (arr[i] == ',' || i == len - 1) {
      if (temp == _deviceId) {
        return false;
      }
      temp = "";
    } else {
      temp += arr[i];
    }
  }
  return true;
}

String * ClusterDuck::getPacketData(int pSize) {
  String * packetData = new String[pSize];
  int i = 0;
  byte byteCode, mLength;
  
  while (LoRa.available())
  {
    byteCode = LoRa.read();
    mLength  = LoRa.read();
    if (byteCode == senderId_B)
    {
      _lastPacket.senderId  = readMessages(mLength);
      Serial.println("User ID: " + packetData[i]);
      i++;
    }
    else if (byteCode == messageId_B) {
      _lastPacket.messageId = readMessages(mLength);
      Serial.println("Message ID: " + packetData[i]);
      i++;
    }
    else if (byteCode == payload_B) {
      _lastPacket.payload = readMessages(mLength);
      Serial.println("Message: " + packetData[i]);
      i++;
    }
    else if (byteCode == iamhere_B) { //DetectorDuck
      String ping = readMessages(mLength);
      if (ping == "0") {
        packetData[i] = "0xF8";
      } else {
        packetData[i] = "0xF7";
      }
    }
    else if (byteCode == path_B) {
      _lastPacket.path = readMessages(mLength);
      Serial.println("Path: " + packetData[i]);
      i++;
    } else {
      packetData[i] = readMessages(mLength);
      //Serial.println("Data" + i + ": " + packetData[i]);
      i++;
    }
  }
  return packetData;
}

/**
  restart
  Only restarts ESP
*/
void ClusterDuck::restartDuck()
{
  Serial.println("Restarting Duck...");
  ESP.restart();
}

//Timer reboot
//bool ClusterDuck::reboot(void *) {
//  char * r[6] = {'R','e','b','o','o','t'};
//  sendPayload(_deviceId, uuidCreator(), r);
//  restartDuck();
//
//  return true;
//}

//Get Duck MAC address
String ClusterDuck::duckID()
{
  char id1[15];
  char id2[15];

  uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
  uint16_t chip = (uint16_t)(chipid >> 32);

  snprintf(id1, 15, "%04X", chip);
  snprintf(id2, 15, "%08X", (uint32_t)chipid);

  String ID1 = id1;
  String ID2 = id2;

  return ID1 + ID2;
}

//Create a uuid
String ClusterDuck::uuidCreator() {
  byte randomValue;
  char msg[50];     // Keep in mind SRAM limits
  int numBytes = 0;
  int i;

  numBytes = atoi("8");
  if (numBytes > 0)
  {
    memset(msg, 0, sizeof(msg));
    for (i = 0; i < numBytes; i++) {
      randomValue = random(0, 37);
      msg[i] = randomValue + 'a';
      if (randomValue > 26) {
        msg[i] = (randomValue - 26) + '0';
      }
    }
  }

  return String(msg);
}



//Getters

String ClusterDuck::getDeviceId() {
  return _deviceId;
}
#include "IoTScapeService.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <map>

using IoTScapeFunction = std::function<DynamicJsonDocument*(JsonArray)>;

bool IoTScapeService::udpStarted = false;
WiFiUDP IoTScapeService::Udp = WiFiUDP();
IPAddress IoTScapeService::_serverIP = IPAddress(127,0,0,1);
unsigned int IoTScapeService::_serverPort = 0;
unsigned int IoTScapeService::_localPort = 0;
char IoTScapeService::macString[13] = "";
char IoTScapeService::outputJSONBuffer[2048] = "";
char IoTScapeService::packetBuffer[UDP_TX_PACKET_MAX_SIZE + 1] = {'\0'};
std::map<String, IoTScapeService*> IoTScapeService::devices = {};

IoTScapeService::IoTScapeService(const char* definition, String idSuffix) {
  // Parse definition, insert ID and cache
  this->definition = definition;
  this->idSuffix = idSuffix;
  
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, definition);
  
  JsonObject documentRoot = doc.as<JsonObject>();
  for (JsonPair keyValue : documentRoot) {
    this->serviceName = keyValue.key().c_str();
  }
  
  // Setup heartbeat by default
  this->addHandler("heartbeat", [](JsonArray args) -> DynamicJsonDocument*{
      DynamicJsonDocument* result = new DynamicJsonDocument(64);
      (*result)[0] = true;
      return result;
  }); 
}

IoTScapeService::~IoTScapeService() {}

void IoTScapeService::Initialize(const IPAddress serverIP, const unsigned int serverPort, const unsigned int localPort) {
  // Get MAC address as hex string
  byte mac[6];
  WiFi.macAddress(mac);

  for (byte i = 0; i < sizeof(mac); i++) {
    IoTScapeService::macString[2 * i] = "0123456789ABCDEF"[mac[i] >> 4];
    IoTScapeService::macString[2 * i + 1] = "0123456789ABCDEF"[mac[i] % 16];
  }
  
  // Set up connection
  IoTScapeService::_serverIP = serverIP;
  IoTScapeService::_serverPort = serverPort;
  IoTScapeService::_localPort = localPort;

  if (!IoTScapeService::udpStarted) {
    IoTScapeService::Udp.begin(localPort);
    IoTScapeService::udpStarted = true;
  }
}

void IoTScapeService::sendEvent(const char* eventName) {
  StaticJsonDocument<64> empty;
  this->sendEvent(eventName, empty.as<JsonObject>());
}

void IoTScapeService::sendEvent(const char* eventName, JsonObject args) {
  DynamicJsonDocument event(512);

  event["id"] = this->id;
  event["service"] = this->serviceName;
  event["event"]["type"] = eventName;

  if(args.size() > 0){
    for (JsonPair keyValue : args) {
	    event["event"]["args"][keyValue.key()] = keyValue.value();
    }
  }

  serializeJson(event, outputJSONBuffer);

  // send event
  IoTScapeService::send(outputJSONBuffer);
}

void IoTScapeService::sendAnnounce() {
  if(this->id.length() == 0){
    this->createID();
  }
  
  IoTScapeService::send(this->definition);
  Serial.println("Sent service announce");
  this->lastAnnounce = millis();
}

void IoTScapeService::createID() {
  String oldID(id);
  
  this->id = IoTScapeService::macString;
  this->id += this->idSuffix;  
  
  if(oldID != this->id){
    this->updateDefinition();
    
    IoTScapeService::devices.insert_or_assign(this->serviceName + ":" + this->id, this);
  }
}

void IoTScapeService::updateDefinition() {
  // Replace ID element
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, this->definition);
  
  JsonObject documentRoot = doc.as<JsonObject>();
  for (JsonPair keyValue : documentRoot) {
    keyValue.value()["id"] = id;
  }

  this->definition = "";
  serializeJson(doc, this->definition);
}

void IoTScapeService::send(String& msg) {
  msg.toCharArray(IoTScapeService::outputJSONBuffer, sizeof(IoTScapeService::outputJSONBuffer));
  IoTScapeService::send(IoTScapeService::outputJSONBuffer);
}

void IoTScapeService::send(const char * msg) {
  IoTScapeService::Udp.beginPacket(IoTScapeService::_serverIP, IoTScapeService::_serverPort);
  IoTScapeService::Udp.write(msg);
  IoTScapeService::Udp.endPacket();
}

void IoTScapeService::update() {
  if (millis() - this->lastAnnounce > 60 * 1000) {
    this->sendAnnounce();
  }
}

void IoTScapeService::addHandler(const char * methodName, IoTScapeFunction func){
  this->handlers.insert_or_assign(methodName, func);
}

void IoTScapeService::handleMessage(JsonDocument * msg){
  
    DynamicJsonDocument reply(500);

    reply["id"] = this->id;
    reply["request"] = (const char *)(*msg)["id"];
    reply["service"] = (*msg)["service"];
    
    if(this->handlers.find((*msg)["function"]) != this->handlers.end()){
      DynamicJsonDocument * response = this->handlers[(*msg)["function"]]((*msg)["params"].as<JsonArray>());
      
      reply["response"] = response->as<JsonArray>();
      
      delete response;
    } else {
      return;
    }
    
    serializeJson(reply, outputJSONBuffer);
    
    // send a reply
    IoTScapeService::send(outputJSONBuffer);
}

void IoTScapeService::Update() {
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize) {

    // read the packet into packetBufffer
    int n = Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[n] = 0;
    
    
#ifdef IOTSCAPE_DEBUG
    Serial.printf("Received packet of size %d from %s:%d\n    (to %s:%d, free heap = %d B)\n",
                packetSize,
                Udp.remoteIP().toString().c_str(), Udp.remotePort(),
                Udp.destinationIP().toString().c_str(), Udp.localPort(),
                ESP.getFreeHeap());

    Serial.println("Contents:");
    Serial.println(packetBuffer);
#endif
    
    DynamicJsonDocument doc(500);
    deserializeJson(doc, packetBuffer);
    
    // Request the device run its method
    IoTScapeService::devices[(String)doc["service"] + ":" + (String)doc["device"]]->handleMessage(&doc);
  }
  
  for (const auto& [key, value] : IoTScapeService::devices) {
      value->update();
  }
}

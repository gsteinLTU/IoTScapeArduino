#pragma once

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <functional>

using IoTScapeFunction = std::function<DynamicJsonDocument*(JsonArray)>;

// An IoTScape device running on this hardware
// IoTScapeService::Initialize() must be run to connect to the NetsBlox server
// IoTScapeService::Update() must be run to receive messages
class IoTScapeService {
  public:
    IoTScapeService(const char* definition, String idSuffix = "");

    ~IoTScapeService();
    
    // Send an event without arguments
    void sendEvent(const char* eventName);
    
    // Send an event, with arguments
    void sendEvent(const char* eventName, JsonObject args);
    
    // Send the service definition to the NetsBlox server
    void sendAnnounce();
    
    // Definition used to create this service
    String definition;
    
    // ID of this device
    String id;
    
    // Name of service this device belongs to
    String serviceName;
    
    // Add a handler for a specific message type
    void addHandler(const char * methodName, IoTScapeFunction func);
    
    // Establish the UDP connection to the server
    static void Initialize(const IPAddress serverIP = IPAddress(129, 59, 105, 37), const unsigned int serverPort = 1975, const unsigned int localPort = 8888);
    
    // Handles incoming messages and sends any announces that need resending
    static void Update();
  protected:
    void update();
    
    // Map of function names to functions
    std::map<std::string, IoTScapeFunction> handlers;
    
    // Handle an incoming message, must also send a reply if necessary
    void handleMessage(JsonDocument * msg);
    
    // Map of IDs to IoTScapeServices
    static std::map<String, IoTScapeService*> devices;
    
    String idSuffix;
    
    long lastAnnounce;
    
    // Create an ID from the MAC address and the provided suffix
    void createID();
    
    // Update stored service definition with ID
    void updateDefinition();
    
    // UDP socket
    static WiFiUDP Udp;
    static IPAddress _serverIP;
    static unsigned int _serverPort;
    static unsigned int _localPort;
    
    // MAC address for identification
    static char macString[13];

    // Has the UDP socket been opened?
    static bool udpStarted;
    
    //buffer to hold incoming packet
    static char packetBuffer[UDP_TX_PACKET_MAX_SIZE + 1]; 
    static char outputJSONBuffer[2048];
    
    // Send a message to the server
    static void send(const char * msg);
    static void send(String& msg);    
};

#include <master.h>

// ALSO DOUBLES AS AWS_ENDPOINT
// const char MQTT_HOST[] = "a2eis0wug3zm6u-ats.iot.us-east-2.amazonaws.com";
// const int MQTT_PORT = 8883;
const char MQTT_HOST[] = "192.168.2.14";
IPAddress address;
const int MQTT_PORT = 8883;
const int MQTT_KEEP_ALIVE = 3600; // 1 hour.

ESP8266WebServer server(80);
WiFiClientSecure wiFiClient;

// All about the button state.
bool isOn = 0;
bool isPressed = 0;
bool wasPressed = 0;
const uint8_t OUTPUT_PIN = 5;
const uint8_t PUSH_PIN = 10;

int8_t TIME_ZONE = -5; //NYC(USA): -5 UTC
//#define USE_SUMMER_TIME_DST  //uncomment to use DST

/// AWS setup ///
BearSSL::X509List cert(cacert); // for AWS server check.
BearSSL::X509List clientcrt(client_cert); // CERT for the thing.
BearSSL::PrivateKey key(privkey); // PK for the thing.
PubSubClient psClient(wiFiClient);

const char MQTT_PUB_TOPIC[] = "thing/switch/" THINGNAME "/state";
// Sub topics
const char* MQTT_SUB_TOPICS[] = {
    MQTT_PUB_TOPIC
};
time_t now;
time_t nowish = 1510592825;
#ifdef USE_SUMMER_TIME_DST
uint8_t DST = 1;
#else
uint8_t DST = 0;
#endif

// Set these to your desired credentials for the accesspoint.
const char *ssid = THINGNAME;

const std::string baseHtmlStart = "<html><head><meta/><title>Connect to a Network</title><style>body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }</style></head>";
const std::string baseHtmlEnd = "</html>";
std::string html = "<h1>Available Networks:</h1>";
std::string network = ""; // don't delete, it IS used.

// Wipes EEPROM. Generally just used for testing purposes.
void factoryReset() {
    EEPROM.begin(512);
    // write 0 to all 512 bytes.
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.end();
}

void scanNetworks() {
    html = "<h1>Available Networks:</h1>";
    // WiFi.scanNetworks will return the number of networks found
    int n = WiFi.scanNetworks();

    if (n == 0) {
        Serial.println("no networks found");
    } else {
        for (int i = 0; i < n; ++i) {
            html += ("<input type=\"submit\" name=\"network\" value=\"" + WiFi.SSID(i) + "\"><br/>").c_str();
            delay(10);
        }
    }
}

void handleRoot() {
    scanNetworks();
    std::string temp = baseHtmlStart + "<body><form action=\"/connect\" method=\"post\">"  + html + "</form></body>" + baseHtmlEnd;
    server.send(200, "text/html", temp.c_str());
}

bool connectToNetwork(const char* ntwrk, const char* pwd) {
    WiFi.begin(ntwrk, pwd);
    Serial.print("Connecting...");
    int j = 0;
    while (WiFi.status() != WL_CONNECTED && j < 30) { // wait for 15 seconds.
        delay(500);
        Serial.print(".");
        j++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return false;        
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
}

void saveCredentials(const char* ntwrk, const char* pwd) {
    uint addr = 0;
    EEPROM.begin(512);
    struct {
        char network[100];
        char password[412];
    } data;

    int network_length = strlen(ntwrk);
    int password_length = strlen(pwd);

    if (network_length > 99) {
        throw std::out_of_range{ "Network length exceeds 99 characters." };
    } else if (password_length > 412) {
        throw std::out_of_range{ "Password length exceeds 412 characters." };
    }

    strncpy(data.network, ntwrk, network_length + 1);
    strncpy(data.password, pwd, password_length + 1);
    data.network[network_length] = '\0';
    data.password[password_length] = '\0';

    Serial.println("New values are: "+String(data.network)+","+String(data.password));

    EEPROM.put(addr,data);
    EEPROM.commit();
}

void handleWiFiConnect(const char* ntwrk, const char* pwd) {
    // save the credentials to the file.
    saveCredentials(ntwrk, pwd);

    // Stop the server and close the access point.
    server.stop();
    WiFi.softAPdisconnect(true);
}

void handleButton() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  } else {
    network = server.arg(0).c_str();
    std::string temp = baseHtmlStart + "<body><h1>Enter the password for " + network + "</h1><form action=\"/login\" method=\"post\"><input type=\"password\" id=\"pwd\" name=\"pwd\"><br/><input type=\"submit\" name=\"network\" value=\"Connect\"><br/></form></body>" + baseHtmlEnd;
    server.send(200, "text/html", temp.c_str());
  }
}

void handleForm() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
    } else {
        std::string password = server.arg(0).c_str();
        bool isSuccess = connectToNetwork(network.c_str(), password.c_str());

        if (!isSuccess) {
            Serial.println("failed to connect.");
            // TODO make the error handling form the form more robust.
            return;
        }

        // save credentials.
        handleWiFiConnect(network.c_str(), password.c_str());
        server.send(200, "text/plain", ("Successfully connected to " + network).c_str());
    }
}

bool attemptNetworkConnection() {
    uint addr = 0;
    EEPROM.begin(512);
    struct {
        char network[100];
        char password[412];
    } data;
    EEPROM.get(addr,data);
    Serial.println("Old values are: "+String(data.network)+","+String(data.password));

    if (data.network[0] == '\0') {
        Serial.println("No network found.");
        return false;
    } else if (data.password[0] == '\0') {
        Serial.println("Found network, but no password.");
        return false;
    }
    return connectToNetwork(data.network, data.password);
}

void buildInitServer() {
    /// WifiAccessPoint setup ///
    delay(1000);
    Serial.println();
    Serial.print("Configuring access point...");
    
    // You can remove the password parameter if you want the AP to be open.
    WiFi.softAP(ssid, password);

    IPAddress myIP = WiFi.softAPIP(); // 192.168.4.1 by default.
    Serial.print("AP IP address: ");
    Serial.println(myIP);
    server.on("/", handleRoot);
    server.on("/connect", handleButton);
    server.on("/login", handleForm);
    server.begin();
    Serial.println("HTTP server started");
}

// so AWS can validate the certs. Correctly.
void NTPConnect(void) {
    Serial.print("Setting time using SNTP");
    configTime(TIME_ZONE * 3600, DST * 3600, "pool.ntp.org", "time.nist.gov");
    now = time(nullptr);
    while (now < nowish) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println("done!");
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));
}

void pubSubErr(int8_t MQTTErr) {
    if (MQTTErr == MQTT_CONNECTION_TIMEOUT) {
        Serial.print("Connection tiemout");
    } else if (MQTTErr == MQTT_CONNECTION_LOST) {
        Serial.print("Connection lost");
    } else if (MQTTErr == MQTT_CONNECT_FAILED) {
        Serial.print("Connect failed");
    } else if (MQTTErr == MQTT_DISCONNECTED) {
        Serial.print("Disconnected");
    } else if (MQTTErr == MQTT_CONNECTED) {
        Serial.print("Connected");
    } else if (MQTTErr == MQTT_CONNECT_BAD_PROTOCOL) {
        Serial.print("Connect bad protocol");
    } else if (MQTTErr == MQTT_CONNECT_BAD_CLIENT_ID) {
        Serial.print("Connect bad Client-ID");
    } else if (MQTTErr == MQTT_CONNECT_UNAVAILABLE) {
        Serial.print("Connect unavailable");
    } else if (MQTTErr == MQTT_CONNECT_BAD_CREDENTIALS) {
        Serial.print("Connect bad credentials");
    } else if (MQTTErr == MQTT_CONNECT_UNAUTHORIZED) {
        Serial.print("Connect unauthorized");
    }
}

void attemptPub(const char* topic, const char* payload, boolean retained) {
    if (!psClient.publish(topic, payload, retained)) {
        pubSubErr(psClient.state());
    } else {
        // Serial.println("got it apparently");
    }
}

void attemptSub(const char* topic) {
    if (!psClient.subscribe(topic)) {
        pubSubErr(psClient.state());
    } else {
        Serial.print("Successfully subbed to ");
        Serial.println(topic);
    }
}

void attemptBrokerConnection() {
    if (psClient.connect(THINGNAME, THINGNAME, "pass1", 0, 1, 0, 0, 1)) {
        Serial.println("connected!");

        size_t i = 0;
        for( i = 0; i < sizeof(MQTT_SUB_TOPICS) / sizeof(MQTT_SUB_TOPICS[0]); i++) {
            attemptSub(MQTT_SUB_TOPICS[i]);
        }

        Serial.println("Getting shadow state");
    } else {
        Serial.print("failed, reason -> ");
        pubSubErr(psClient.state());
        Serial.println(" < try again");
    }
}

void toggleSwitch(bool turnOn) {
    if (turnOn && !isOn) {
        // turn me on.
        Serial.println("ON");
        digitalWrite(OUTPUT_PIN, HIGH);
        isOn = true;
    } else if (!turnOn && isOn) {
        // turn me off.
        Serial.println("OFF");
        digitalWrite(OUTPUT_PIN, LOW);
        isOn = false;
    }
}
void toggleSwitchAndUpdateState(bool turnOn) {
    toggleSwitch(turnOn);
    char numStr[30];
    std::string state = itoa(int(turnOn), numStr, 10);
    const char* payload = ("{\"state\":{\"on\":" + state + "}}").c_str();
    attemptPub(MQTT_PUB_TOPIC, payload, true);
}

void printPayload(byte *payload, unsigned int length) {
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();
}

// This is the expected payload: {\"state\":{\"on\":1}}
void messageReceived(char *topic, byte *payload, unsigned int length) {
    Serial.print("Received [");
    Serial.print(topic);
    Serial.print("]: ");

    std::string topic_s = topic;
    if (topic_s == MQTT_SUB_TOPICS[0]) { // THINGNAME "/state",
        // parse the json and update current state.
        const std::string json_s = (char *) payload;
        DynamicJsonDocument doc(128);
        deserializeJson(doc, json_s);
        const int shouldTurnOn = doc["state"]["on"];
        Serial.println(shouldTurnOn);
        toggleSwitch(shouldTurnOn);
    } else {
        printPayload(payload, length);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(OUTPUT_PIN, OUTPUT);
    pinMode(PUSH_PIN, INPUT_PULLUP);

    // Set WiFi to station mode and disconnect from an AP if it was previously connected
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(1000);
    Serial.println();

    // Try reading from the saved file.
    Serial.println("attemptNetworkConnection Start");
    bool connected = attemptNetworkConnection();
    Serial.println("attemptNetworkConnection Complete");

    if (!connected) {
        buildInitServer();
    } else {
        Serial.println("We are GOLDEN!");
        NTPConnect(); // get current time, otherwise certificates are flagged as expired

        wiFiClient.setTrustAnchors(&cert);
        wiFiClient.setClientRSACert(&clientcrt, &key);

        // Only if we use a raw IP Address for MQTT_HOST.
        address.fromString(MQTT_HOST);
        psClient.setServer(address, MQTT_PORT);
        // psClient.setServer(MQTT_HOST, MQTT_PORT);
        psClient.setCallback(messageReceived);

        psClient.setKeepAlive(MQTT_KEEP_ALIVE);

        // connectToMqtt();
    }
    
}

void loop() {
    // First handle the switch.
    if (digitalRead(PUSH_PIN) == LOW) {
        isPressed = 1;
        wasPressed = 1;
    } else {
        isPressed = 0;
    }
    if (!isPressed && wasPressed) {
        toggleSwitchAndUpdateState(!isOn);
        wasPressed = 0;
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (psClient.connected()) {
            if (psClient.loop()) {
                // Serial.println("The client is still connected");
            } else {
                Serial.println("The client is no longer connected");
            }
            delay(50);
        } else { // not connected to broker.
            // Serial.println("not connected to broker");
            attemptBrokerConnection();
        }
    } else { // not connected to WiFi.
        server.handleClient();
    }
}
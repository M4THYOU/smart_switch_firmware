#include <master.h>

ESP8266WebServer server(80);
WiFiClientSecure wiFiClient;

const uint8_t OUTPUT_PIN = 5;

/// AWS setup ///
BearSSL::X509List cert(cacert); // for AWS server check.
BearSSL::X509List client_crt(client_cert); // CERT for the thing.
BearSSL::PrivateKey key(privkey); // PK for the thing.
PubSubClient psClient(wiFiClient);
const char* awsEndpoint = "a2eis0wug3zm6u-ats.iot.us-east-2.amazonaws.com"; // HTTPS Rest endpoint

const int MQTT_PORT = 8883;
const char MQTT_SUB_TOPIC[] = "$aws/things/" THINGNAME "/shadow/update";
const char MQTT_PUB_TOPIC[] = "$aws/things/" THINGNAME "/shadow/update";
time_t now;
time_t nowish = 1510592825;
#ifdef USE_SUMMER_TIME_DST
uint8_t DST = 1;
#else
uint8_t DST = 0;
#endif

// Set these to your desired credentials for the accesspoint.
const char *ssid = "myAP";
const char *password = "password"; // update this later on. Don't just use "password", obviously.

const std::string baseHtmlStart = "<html><head><meta/><title>Connect to a Network</title><style>body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }</style></head>";
const std::string baseHtmlEnd = "</html>";
std::string html = "<h1>Available Networks:</h1>";
std::string network = ""; // don't delete, it IS used.

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

bool attemptConnection() {
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

// Wipes EEPROM. Generally just used for testing purposes.
void factoryReset() {
    EEPROM.begin(512);
    // write 0 to all 512 bytes.
    for (int i = 0; i < 512; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.end();
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

void connectToMqtt(bool nonBlocking = false) {
    Serial.print("MQTT connecting ");
    while (!psClient.connected()) {
        if (psClient.connect(THINGNAME)) {

            Serial.println("connected!");
            if (!psClient.subscribe(MQTT_SUB_TOPIC)) {
                pubSubErr(psClient.state());
            }
            
        } else {
            Serial.print("failed, reason -> ");

            pubSubErr(psClient.state());
            if (!nonBlocking) {
                Serial.println(" < try again in 5 seconds");
                delay(5000);
            } else {
                Serial.println(" <");
            }
        }
        if (nonBlocking) {
            break;
        }
    }
}

// Publish to topic: $aws/things/<thing-name>/shadow/update
//      In our case: $aws/things/esp8266/shadow/update
// Assuming the message is of the form:
//      {"power_on":1}
// without spaces. That means, payload[12] will have the value to return;
bool parseMessage(byte *payload, unsigned int len) {
    // len should always be 14
    if (len < 14) {
        throw std::out_of_range{ "len: < 14" };
    } else if (len != 14) {
        Serial.println("***");
        Serial.println("len is not 14");
        Serial.println("***");
    }
    const char c = (char)payload[12];
    if (!isdigit(c)) {
        throw std::out_of_range{ "c: is not a number." };
    }
    const int i = atoi(&c);
    if ((i != 0) && (i != 1)) {
        Serial.println("An error occurred, line 274. payload value is not 0 or 1.");
        // No error throw, just let it be converted to bool.
    }
    return i; // TODO this is most certainly unsafe.
}

void toggleSwitch(bool turnOn) {
    if (turnOn) {
        // turn me on.
        Serial.println("ON");
        digitalWrite(OUTPUT_PIN, HIGH);
    } else {
        // turn me off.
        Serial.println("OFF");
        digitalWrite(OUTPUT_PIN, LOW);
    }
}

void messageReceived(char *topic, byte *payload, unsigned int length) {
    Serial.print("Received [");
    Serial.print(topic);
    Serial.print("]: ");
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    bool turnOn = parseMessage(payload, length);
    toggleSwitch(turnOn);
}

void setup() {
    Serial.begin(115200);
    pinMode(OUTPUT_PIN, OUTPUT);

    // Set WiFi to station mode and disconnect from an AP if it was previously connected
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(1000);
    Serial.println();

    // Try reading from the saved file.
    Serial.println("attemptConnection Start");
    bool connected = attemptConnection();
    Serial.println("attemptConnection Complete");

    if (!connected) {
        buildInitServer();
    } else {
        Serial.println("We are GOLDEN!");
        NTPConnect(); // get current time, otherwise certificates are flagged as expired

        wiFiClient.setTrustAnchors(&cert);
        wiFiClient.setClientRSACert(&client_crt, &key);

        psClient.setServer(awsEndpoint, MQTT_PORT);
        psClient.setCallback(messageReceived);

        connectToMqtt();
    }
    
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (psClient.connected()) {
            if (psClient.loop()) {
                // Serial.println("The client is still connected");
            } else {
                Serial.println("The client is no longer connected");
            }
            delay(50);
        } else { // not connected to Cloud.
            connectToMqtt();
        }
    } else { // not connected to WiFi.
        server.handleClient();
    }
}
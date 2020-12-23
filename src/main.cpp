#include <Arduino.h>
#include <ESP8266WiFi.h>

// WifiAccessPoint
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

// I/O
#include <EEPROM.h>
#include <stdexcept>

#ifndef APSSID
#define APSSID "myAP"
#define APPSK  "password" // update this later on. Don't just use "password", obviously.
#endif

// Set these to your desired credentials.
const char *ssid = APSSID;
const char *password = APPSK;

ESP8266WebServer server(80);

const std::string baseHtmlStart = "<html><head><meta/><title>Connect to a Network</title><style>body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }</style></head>";
const std::string baseHtmlEnd = "</html>";

std::string html = "<h1>Available Networks:</h1>";
std::string network = "";

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

void connectToNetwork(const char* ntwrk, const char* pwd) {
    WiFi.begin(ntwrk, pwd);
    Serial.print("Connecting...");
    int j = 0;
    while (WiFi.status() != WL_CONNECTED && j < 30) { // wait for 15 seconds.
        delay(500);
        Serial.print(".");
        j++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        throw std::out_of_range{ "Failed to connect" };
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
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
        connectToNetwork(network.c_str(), password.c_str());

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

    try {
        connectToNetwork(data.network, data.password);
    } catch (std::out_of_range e) {
        return false;
    }
    return true;
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

void setup() {
    Serial.begin(115200);

    // Set WiFi to station mode and disconnect from an AP if it was previously connected
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(1000);
    Serial.println();

    // Try reading from the saved file.
    bool connected = attemptConnection();

    if (!connected) {
        buildInitServer();
    } else {
        Serial.println("We are GOLDEN!");
    }
    
}

void loop() {
    server.handleClient();
}
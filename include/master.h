// Required
#include <Arduino.h>
#include <ArduinoJson.h>

// WiFi stuff
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

// Storage
#include <EEPROM.h>

// MQTT
#include <PubSubClient.h>

// Utility
#include <stdexcept>

// AWS Certs
#include <certs.h>
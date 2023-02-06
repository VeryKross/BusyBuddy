/*
 * Author: Ken Ross
 * https://github.com/verykross
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the MIT License.
 *
 * It is my hope that this program will be as useful to others as it is
 * for me, but comes WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * MIT License for more details.
 *
 * A copy of the MIT License is included with this program.
 *
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <string.h>
#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include <ESP8266WebServer.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels
#define OLED_RESET -1     // Reset pin # (or -1 if sharing Arduino reset pin)

#define BB_VER 1.0        // Current version of Busy Buddy, displayed at startup

IPAddress local_IP(10,10,10,10);
IPAddress gateway(10,10,10,1);
IPAddress subnet(255,255,255,0);

ESP8266WebServer server(80);

// Defaults for configurable values
String dnsName = "BusyBuddy";
String statusText = "My status is";
String padlock = "";
bool anodeMode = false;

WiFiEventHandler stationConnectedHandler;

WiFiClientSecure client;

bool askReset = false;
bool wifiInitialized = false;
long apNumber;
String apName = "BusyBuddyPortal";

struct settings {
  int initialized;      // Indicates that BB has saved initial setup
  char ssid[30];        // The SSID of your local WiFi network
  char password[30];    // The password for your WiFi network
  char dnsName[30];     // The DNS name for Busy Buddy on your network
  char statusText[20];  // The text to show at the top of the status display
  bool anodeMode;       // True if using a Common Adode RGB LED
  char padlock[20];     // An optional secret/password/token to lock access to your Busy Buddy (so your friends can't punk you)
} user_settings = {};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
GFXcanvas1 dispCanvas(SCREEN_WIDTH, SCREEN_HEIGHT);

const int redLedPin = 14;   // this corresponds to pin D5 on the ESP8266
const int greenLedPin = 12; // this corresponds to pin D6
const int blueLedPin = 13;  // this corresponds to pin D7

// JSON data buffer
StaticJsonDocument<250> jsonDocument;
char buffer[250];

void setup() {
  Serial.begin(115200);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  randomSeed(analogRead(A0));
  apNumber = random(100,999);
  apName = apName + apNumber;
  
  // Let the user know we're awake
  display.clearDisplay();
  display.setCursor(0,16);
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.println("Busy Buddy");
  display.setCursor(0,32);
  display.println("Init...");
  display.setTextSize(1);
  display.println();
  display.print("Version ");
  display.println(BB_VER);
  display.display();

  // Setup the pin to control the LEDs
  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  pinMode(blueLedPin, OUTPUT);
  
  EEPROM.begin(sizeof(struct settings) );
  EEPROM.get( 0, user_settings );

  if(user_settings.initialized != 1) {
    wifiInitialized = false;
  } else {
    wifiInitialized = true;
    dnsName = user_settings.dnsName;
    statusText = user_settings.statusText;
    anodeMode = user_settings.anodeMode;
    padlock = user_settings.padlock;
  }
  
  Serial.println("Busy Buddy");
  Serial.print("Version ");
  Serial.println(BB_VER);
  Serial.println("---------------------");

  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(dnsName);
  WiFi.begin(user_settings.ssid, user_settings.password);

  byte tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    if (tries++ > 15) {
      Serial.print("Switching to Access Point mode ... ");
      Serial.println(WiFi.softAPConfig(local_IP, gateway, subnet) ? "Ready" : "Failed!");
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apName, "12345678");
      Serial.println("Starting Access Point at 10.10.10.10 ...");

      stationConnectedHandler = WiFi.onSoftAPModeStationConnected(&onStationConnected);
      break;
    }
  }

  if(WiFi.getMode() == WIFI_AP){
    Serial.println("Could not connect to WiFi - ask user to connect to Buddy.");

    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Connect to WiFi:");
    display.setCursor(0,16);
    display.println(apName);
    display.setCursor(0,32);
    display.println("Pwd: 12345678");
    display.display();
  } else {
    Serial.println("Connected to WiFi: ");
    printWiFiStatus();

    // Since we're connected to WiFi, let's display our IP address and DNS name
    // In case the user wants to connect to our configuration portal
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.println("Status Unknown");
    display.setCursor(0,16);
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.println("Busy Buddy");
    display.setCursor(0,38);
    display.setTextSize(1);
    display.print(dnsName);
    display.println(".local");
    display.setCursor(0,50);
    display.println(WiFi.localIP());
    display.display();

    // Setup mDNS so we can serve our web page at http://busybuddy.local/
    // which is easier to remember than the IP address
    MDNS.begin(dnsName);
  }

  // Start the internal web server for the configuration page.
  server.on("/",  handlePortal);
  server.on("/status", HTTP_POST, handlePost);
  server.begin();

  client.setInsecure();
}

void loop() 
{
  MDNS.update();

  server.handleClient();

  // If user had connected to the Busy Buddy access point, 
  // remind them of the IP address to browse to for setup.
  if (askReset == true) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.println("Once WiFi Connected");
    display.println("Browse to:");
    display.println("10.10.10.10");
    display.display();
    askReset = false;
  }  
}

int showStatus(String status){
  Serial.print("status: ");
  Serial.println(status);

  displayInfo(status);
  return 1;
}

void handlePost() {
Serial.println("handlePost...");

  String status = "???";
  String color = "#FFFFFF";
  String key = "?";
  
  showLedStatus(0,0,0);
  
  // If padlock is enforced and key is missing or wrong, do nothing.
  if(padlock != ""){
    key = (String)server.arg("key");
    if(padlock != key){
      server.send(418, "application/json", "{}"); // No status update for you - I'm a teapot
      return;
    }
  }
  
  if (server.hasArg("text") == true) {
    status = (String)server.arg("text");
  }

  if (server.hasArg("color") == true) {
    color = (String)server.arg("color");
  }

  String message;
  for (uint8_t i = 0; i < server.args(); i++) { 
    message = " " + server.argName(i) + ": " + server.arg(i);
    Serial.println(message);
  }

  int r, g, b;
  char const *hexColor = color.c_str();
  std::sscanf(hexColor, "%02x%02x%02x", &r, &g, &b);

  Serial.print("status: ");
  Serial.println(status);
  Serial.print("color: ");
  Serial.println(color);

  displayInfo(status);
  showLedStatus(r, g, b);

  // Respond to the client
  server.send(200, "application/json", "{}");
}

// If using 3 separate LEDs instead of a single RGB LED, think
// of these 3 values as the "brightness" level of each LED where
// a value of 0 is Off and 254 is full brightness.
void showLedStatus(int red, int green, int blue){

  // If using a Common Anode RGB LED, invert the values
  if(anodeMode)  {
    red=255-red;
    green=255-green;
    blue=255-blue;    
  }
  
  analogWrite(redLedPin, red);
  analogWrite(greenLedPin, green);
  analogWrite(blueLedPin, blue);
}

// Mostly used as a debugging aid for WiFi connectivity.
// Outputs connected SSID and IP address to the serial monitor.
void printWiFiStatus() {
  // Print the SSID of the network we connected to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // Print the WiFi IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // Print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.println();
}

void onStationConnected(const WiFiEventSoftAPModeStationConnected& evt) {
  Serial.print("Station connected: ");
  Serial.println(macToString(evt.mac));
  Serial.print("AID: ");
  Serial.println(evt.aid);

  if (evt.aid > 0) {
    askReset = true;
  }
}

// Utility function to return a formatted MAC address
String macToString(const unsigned char* mac) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// This method is called when a user connects to Busy Buddy via a web browser
// and provides an easy web-based configuration interface.
void handlePortal() {
  IPAddress userIP = server.client().remoteIP();
  Serial.print("Remote user connected to portal: ");
  Serial.println(userIP);
  String pg;
  String saveKey;
  String ledType;
  String curKey = user_settings.padlock;

  if (server.method() == HTTP_POST) {
    saveKey = server.arg("saveKey").c_str();

    // Check to see if we're padlocked and need a key - ignore if not yet initialized.
    if(curKey.length() > 0 && curKey != saveKey && user_settings.initialized == 1){
      pg = "<!doctype html><html lang=\"en\" style=\"background: linear-gradient(90deg, rgba(36,0,0,1) 0%, rgba(121,9,9,1) 35%, rgba(200,42,42,1) 100%);\">";
      pg += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Busy Buddy Setup</title>";
      pg += "<style>h1 {color: yellow;} h2 {color: cyan;} .label {margin-top: 3px;}</style></head>";
      pg += "<body style=\"background-color: transparent; color: white;\">";
      pg += "<h1>Busy Buddy Setup</h1><h3>Setup failed due to security key mismatch.</h3>";
      pg += "<p>If you've forgotten your Security Key, please re-install the software to force a reset of the key. Press the browser's back button to retry.";
      pg += "</body></html>";

      server.send(400, "text/html", pg);
      return;
    }

    strncpy(user_settings.ssid, server.arg("ssid").c_str(), sizeof(user_settings.ssid));
    strncpy(user_settings.password, server.arg("password").c_str(), sizeof(user_settings.password));
    strncpy(user_settings.dnsName, server.arg("dns").c_str(), sizeof(user_settings.dnsName));
    strncpy(user_settings.statusText, server.arg("status").c_str(), sizeof(user_settings.statusText));
    strncpy(user_settings.padlock, server.arg("key").c_str(), sizeof(user_settings.padlock));
    ledType = server.arg("ledType").c_str();

    user_settings.anodeMode = false;
    if(ledType == "anode") user_settings.anodeMode = true;
    
    user_settings.initialized=1;
    user_settings.ssid[server.arg("ssid").length()] = user_settings.password[server.arg("password").length()] = '\0';

    EEPROM.put(0, user_settings);
    EEPROM.commit();

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0,16);
    display.println("Please");
    display.println("Reset");
    display.display();

    // Save confirmation page    
    Serial.println("Configuration saved to internal memory. Please reset the device.");
    wifiInitialized = false;

      pg = "<!doctype html><html lang=\"en\" style=\"background: linear-gradient(90deg, rgba(0,36,3,1) 0%, rgba(13,121,9,1) 35%, rgba(42,200,68,1) 100%);\">";
      pg += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Busy Buddy Setup</title>";
      pg += "<style>h1 {color: yellow;} h2 {color: cyan;} .label {margin-top: 3px;}</style></head>";
      pg += "<body style=\"background-color: transparent; color: white;\">";
      pg += "<h1>Busy Buddy Setup</h1><h3>Your settings have been saved successfully!</h3>";
      pg += "<p>Please restart the device for the changes to take effect.";
      pg += "</body></html>";

  } else {

    String ssid = user_settings.ssid;
    String password = user_settings.password;
    String dns = user_settings.dnsName;
    String status = user_settings.statusText;
    String key = user_settings.padlock;
    bool anode = user_settings.anodeMode;

    // If we haven't initialized, all of these values will be junk and need to be cleared
    if(!wifiInitialized){
      ssid = "";
      password = "";
      dns=dnsName; // Use default startup value
      status = statusText;
      key = "";
      anode = false;
    }

    String saveKey = "";
    String ledType = "cathode";
    if(anode) ledType = "anode";

    // Setup/Configuration page
    Serial.println("Configuration web page requested.");

    pg = "<!doctype html><html lang=\"en\" style=\"background: linear-gradient(90deg, rgba(2,0,36,1) 0%, rgba(9,9,121,1) 35%, rgba(42,42,200,1) 100%);\">";
    pg += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Busy Buddy Setup</title>";
    pg += "<style>h1 {color: yellow;} h2 {color: cyan;} .label {margin-top: 3px;} select.list1 option.option2 { background-color: lightgreen;}</style></head>";
    pg += "<body style=\"background-color: transparent; color: white;\"><main><form action=\"/\" method=\"post\">";
    pg += "<h1>Busy Buddy Setup</h1><p>The following settings allow you to connect Busy Buddy to your ";
    pg += " local WiFi access point and allow Busy Buddy to listen for API calls from your PresenceLight";
    pg += " app. Here you can also set a custom DNS name for this device - if you have more than one on ";
    pg += " the same network, setting a name that is unique to each device will ensure your";
    pg += " PresenceLight app is talking to the right one. Keep it simple and don't use spaces in the name";
    pg += " (e.g., BusyBuddy2, BusyBuddy-KR, MyBlinky1, etc.)</p>The Status Text is the";
    pg += " message that's displayed on the OLED screen above the status. It can be up to 11 upper-case ";
    pg += " characters (a couple more if lower-case) or can be blank if you don't want it at all. It is";
    pg += " set to 'My status is' by default, but if you're using Busy Buddy for something like a DevOps Build ";
    pg += " indicator, something like 'Last Build:' might be more appropriate.</p>";
    pg += "<style>.my-checkbox { transform: scale(1.8); margin-right: 11px; margin-top: 12px; margin-left: 6px;}</style>";
    pg += "<h2>Network Settings</h2>";
    pg += "<div class=\"label\"><label for=\"ssid\">WiFi SSID: </label></div><div><input id=\"ssid\" name=\"ssid\" type=\"text\" value=\"" + ssid + "\"/></div>";
    pg += "<div class=\"label\"><label for=\"password\">Password: </label></div><div><input id=\"password\" name=\"password\" type=\"password\" value=\"" + password + "\"/></div>";
    pg += "<div class=\"label\"><label for=\"dns\">DNS Name: </label></div><div><input id=\"dns\" name=\"dns\" type=\"text\" value=\"" + dns + "\"/></div>";
    pg += "<h2>Other Settings</h2>";
    pg += "<div class=\"label\"><label for=\"status\">Status Text: </label></div><div><input id=\"status\" name=\"status\" type=\"text\" value=\"" + status + "\"/></div>";
    pg += "<div class=\"label\"><label for=\"key\">Security Key: </label></div><div><input id=\"key\" name=\"key\" type=\"password\" value=\"" + key + "\"/></div>";
    pg += "<div><div class=\"label\"><label for=\"ledType\">RGB LED Type: </label></div><div><select id=\"ledType\" class=\"list1\" name=\"ledType\">";
    if(anode) {
      pg += "<option value=\"cathode\">Common Cathode</option>";
      pg += "<option class=\"option1\" value=\"anode\" selected>Common Anode</option>";
    } else {
      pg += "<option class=\"option1\" value=\"cathode\" selected>Common Cathode</option>";
      pg += "<option value=\"anode\">Common Anode</option>";
    }
    pg += "</select></div>";
    pg += "<br/><hr/><br/><button type=\"submit\">Save Changes</button>";
    if(key.length() > 0){
      pg += "<div class=\"label\"><label for=\"saveKey\" style=\"color:orange;\" >Security Key entry REQUIRED to save: </label><input id=\"saveKey\" name=\"saveKey\" type=\"text\" /></div>";
    }
    pg += "<br/><b>Always restart Busy Buddy after saving for the changes to take effect.</b></form></main></body></html>";

  }

  server.send(200, "text/html", pg);
}

// Refresh the display with current status
// Canvas is used in order to avoid any update flickering
void displayInfo(String status){
  IPAddress ip = WiFi.localIP();

  display.clearDisplay();
  dispCanvas.setFont(&FreeSans9pt7b);

  dispCanvas.fillScreen(BLACK);
  dispCanvas.setCursor(0,12);
  dispCanvas.setTextSize(1);
  dispCanvas.setTextColor(WHITE);

  dispCanvas.print(statusText);
  
  dispCanvas.setCursor(0,45);
  dispCanvas.setTextSize(2);
  dispCanvas.setTextColor(WHITE);
  dispCanvas.print(status);

  display.drawBitmap(0,0,dispCanvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT, WHITE, BLACK);
  display.display();
}

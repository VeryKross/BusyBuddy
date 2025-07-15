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

#define BB_VER 1.5        // Current version of Busy Buddy, displayed at startup

IPAddress local_IP(10,10,10,10);
IPAddress gateway(10,10,10,1);
IPAddress subnet(255,255,255,0);

ESP8266WebServer server(80);

// Defaults for configurable values
String dnsName = "BusyBuddy";
String headingText = "My status is";
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
  char headingText[20];  // The text to show at the top of the status display
  bool anodeMode;       // True if using a Common Adode RGB LED
  char padlock[20];     // An optional secret/password/token to lock access to your Busy Buddy (so your friends can't punk you)
} user_settings = {};

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
GFXcanvas1 dispCanvas(SCREEN_WIDTH, SCREEN_HEIGHT);

const int redLedPin = 14;   // this corresponds to pin D5 on the ESP8266
const int greenLedPin = 12; // this corresponds to pin D6
const int blueLedPin = 13;  // this corresponds to pin D7

// Variables to control LED blinking
unsigned long curMiliCount = 0;
unsigned long prevMiliCount = 0;
int blinkInterval = 500;
String curColor;
bool blinkLed = false;
bool blinkOnState = true;

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
  
  // Setup and turn off the built-in LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  EEPROM.begin(sizeof(struct settings) );
  EEPROM.get( 0, user_settings );

  if(user_settings.initialized != 1) {
    wifiInitialized = false;
  } else {
    wifiInitialized = true;
    dnsName = user_settings.dnsName;
    headingText = user_settings.headingText;
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

  curMiliCount = millis();
  if(curMiliCount - prevMiliCount >= blinkInterval && blinkLed){
    prevMiliCount = curMiliCount;
    checkBlink();
  }
}

void checkBlink(){
  blinkOnState = !blinkOnState;
  int r, g, b;
  if(blinkOnState){
    char const *hexColor = curColor.c_str();
    std::sscanf(hexColor, "%02x%02x%02x", &r, &g, &b);
  } else {
    r = 0;
    g = 0;
    b = 0;    
  }
  showLedStatus(r, g, b);
}

void handlePost() {
Serial.println("handlePost...");
  digitalWrite(LED_BUILTIN, LOW); // Signal POST received

  String status = "???";
  String color = "000000";
  String key = "?";
  String heading = headingText;
  
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
    curColor = color;
  }

  if (server.hasArg("heading") == true){
    heading = (String)server.arg("heading");
  }

  blinkLed = false;
  if (server.hasArg("blink") == true){
    int blinkRate = server.arg("blink").toInt();
    if(blinkRate > 0){
      blinkInterval = blinkRate;      
    }    
    blinkLed = true;    
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

  displayInfo(status, heading);
  showLedStatus(r, g, b);

  // Respond to the client
  server.send(200, "application/json", "{}");

  digitalWrite(LED_BUILTIN, HIGH);
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
      pg = "<!doctype html><html lang=\"en\">";
      pg += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Busy Buddy Setup - Error</title>";
      pg += "<style>";
      pg += "* { box-sizing: border-box; margin: 0; padding: 0; }";
      pg += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; ";
      pg += "background: linear-gradient(135deg, #e74c3c, #c0392b); min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }";
      pg += ".container { max-width: 500px; background: white; border-radius: 12px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); padding: 40px; text-align: center; }";
      pg += ".error-icon { font-size: 4em; color: #e74c3c; margin-bottom: 20px; }";
      pg += "h1 { color: #2c3e50; font-size: 2em; margin-bottom: 15px; }";
      pg += "h3 { color: #e74c3c; font-size: 1.3em; margin-bottom: 20px; }";
      pg += ".message { color: #555; font-size: 1.1em; line-height: 1.6; margin-bottom: 20px; }";
      pg += ".btn { background: #3498db; color: white; padding: 12px 24px; border: none; border-radius: 6px; ";
      pg += "text-decoration: none; display: inline-block; cursor: pointer; }";
      pg += ".btn:hover { background: #2980b9; }";
      pg += "</style></head>";
      pg += "<body><div class=\"container\">";
      pg += "<div class=\"error-icon\">🔒</div>";
      pg += "<h1>Access Denied</h1>";
      pg += "<h3>Security key mismatch</h3>";
      pg += "<div class=\"message\">The security key you entered does not match. If you've forgotten your key, ";
      pg += "please re-install the software to reset it.</div>";
      pg += "<button onclick=\"history.back()\" class=\"btn\">← Go Back</button>";
      pg += "</div></body></html>";

      server.send(400, "text/html", pg);
      return;
    }

    strncpy(user_settings.ssid, server.arg("ssid").c_str(), sizeof(user_settings.ssid));
    strncpy(user_settings.password, server.arg("password").c_str(), sizeof(user_settings.password));
    strncpy(user_settings.dnsName, server.arg("dns").c_str(), sizeof(user_settings.dnsName));
    strncpy(user_settings.headingText, server.arg("heading").c_str(), sizeof(user_settings.headingText));
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

      pg = "<!doctype html><html lang=\"en\">";
      pg += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Busy Buddy Setup - Success</title>";
      pg += "<style>";
      pg += "* { box-sizing: border-box; margin: 0; padding: 0; }";
      pg += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; ";
      pg += "background: linear-gradient(135deg, #2ecc71, #27ae60); min-height: 100vh; display: flex; align-items: center; justify-content: center; padding: 20px; }";
      pg += ".container { max-width: 500px; background: white; border-radius: 12px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); padding: 40px; text-align: center; }";
      pg += ".success-icon { font-size: 4em; color: #27ae60; margin-bottom: 20px; }";
      pg += "h1 { color: #2c3e50; font-size: 2em; margin-bottom: 15px; }";
      pg += "h3 { color: #27ae60; font-size: 1.3em; margin-bottom: 20px; }";
      pg += ".message { color: #555; font-size: 1.1em; line-height: 1.6; }";
      pg += ".restart-note { background: #d1ecf1; border: 1px solid #bee5eb; padding: 15px; border-radius: 6px; margin-top: 20px; color: #0c5460; }";
      pg += "</style></head>";
      pg += "<body><div class=\"container\">";
      pg += "<div class=\"success-icon\">✅</div>";
      pg += "<h1>Configuration Saved!</h1>";
      pg += "<h3>Your settings have been saved successfully</h3>";
      pg += "<div class=\"message\">Your Busy Buddy is now configured with your new settings.</div>";
      pg += "<div class=\"restart-note\"><strong>Next Step:</strong> Please restart your device for the changes to take effect.</div>";
      pg += "</div></body></html>";

  } else {

    String ssid = user_settings.ssid;
    String password = user_settings.password;
    String dns = user_settings.dnsName;
    String heading = user_settings.headingText;
    String key = user_settings.padlock;
    bool anode = user_settings.anodeMode;

    // If we haven't initialized, all of these values will be junk and need to be cleared
    if(!wifiInitialized){
      ssid = "";
      password = "";
      dns=dnsName; // Use default startup value
      heading = headingText;
      key = "";
      anode = false;
    }

    String saveKey = "";
    String ledType = "cathode";
    if(anode) ledType = "anode";

    // Setup/Configuration page
    Serial.println("Configuration web page requested.");

    pg = "<!doctype html><html lang=\"en\">";
    pg += "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Busy Buddy Setup</title>";
    pg += "<style>";
    pg += "* { box-sizing: border-box; margin: 0; padding: 0; }";
    pg += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; line-height: 1.6; ";
    pg += "background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; padding: 20px; }";
    pg += ".container { max-width: 600px; margin: 0 auto; background: rgba(255,255,255,0.95); ";
    pg += "border-radius: 12px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); padding: 30px; }";
    pg += "h1 { color: #2c3e50; font-size: 2.2em; margin-bottom: 10px; text-align: center; }";
    pg += "h2 { color: #34495e; font-size: 1.3em; margin: 25px 0 15px 0; padding-bottom: 8px; ";
    pg += "border-bottom: 2px solid #3498db; }";
    pg += ".description { color: #555; margin-bottom: 25px; font-size: 0.95em; }";
    pg += ".form-group { margin-bottom: 20px; }";
    pg += "label { display: block; margin-bottom: 5px; color: #2c3e50; font-weight: 500; }";
    pg += "input[type='text'], input[type='password'], select { width: 100%; padding: 12px; ";
    pg += "border: 2px solid #e1e8ed; border-radius: 6px; font-size: 16px; transition: border-color 0.3s; }";
    pg += "input:focus, select:focus { outline: none; border-color: #3498db; }";
    pg += "select { background: white; cursor: pointer; }";
    pg += ".btn { background: linear-gradient(135deg, #3498db, #2980b9); color: white; ";
    pg += "padding: 14px 30px; border: none; border-radius: 6px; font-size: 16px; font-weight: 600; ";
    pg += "cursor: pointer; transition: transform 0.2s; width: 100%; margin-top: 20px; }";
    pg += ".btn:hover { transform: translateY(-2px); }";
    pg += ".security-note { background: #fff3cd; border: 1px solid #ffeaa7; padding: 12px; ";
    pg += "border-radius: 6px; margin-top: 15px; color: #856404; }";
    pg += ".restart-note { background: #d1ecf1; border: 1px solid #bee5eb; padding: 12px; ";
    pg += "border-radius: 6px; margin-top: 20px; color: #0c5460; font-weight: 500; }";
    pg += "@media (max-width: 480px) { .container { padding: 20px; } h1 { font-size: 1.8em; } }";
    pg += "</style></head>";
    pg += "<body><div class=\"container\"><form action=\"/\" method=\"post\">";
    pg += "<h1>🤖 Busy Buddy Setup</h1>";
    pg += "<div class=\"description\">Configure your Busy Buddy device to connect to WiFi and customize its behavior. ";
    pg += "Set a unique DNS name if you have multiple devices, and customize the display text and LED settings.</div>";
    pg += "<h2>📶 Network Settings</h2>";
    pg += "<div class=\"form-group\"><label for=\"ssid\">WiFi Network Name (SSID)</label>";
    pg += "<input id=\"ssid\" name=\"ssid\" type=\"text\" value=\"" + ssid + "\" placeholder=\"Enter your WiFi network name\"/></div>";
    pg += "<div class=\"form-group\"><label for=\"password\">WiFi Password</label>";
    pg += "<input id=\"password\" name=\"password\" type=\"password\" value=\"" + password + "\" placeholder=\"Enter your WiFi password\"/></div>";
    pg += "<div class=\"form-group\"><label for=\"dns\">Device Name</label>";
    pg += "<input id=\"dns\" name=\"dns\" type=\"text\" value=\"" + dns + "\" placeholder=\"e.g., BusyBuddy, MyDevice\"/></div>";
    pg += "<h2>⚙️ Display & LED Settings</h2>";
    pg += "<div class=\"form-group\"><label for=\"heading\">Display Heading Text</label>";
    pg += "<input id=\"heading\" name=\"heading\" type=\"text\" value=\"" + heading + "\" placeholder=\"e.g., My status is, Build status\"/></div>";
    pg += "<div class=\"form-group\"><label for=\"key\">Security Key (Optional)</label>";
    pg += "<input id=\"key\" name=\"key\" type=\"password\" value=\"" + key + "\" placeholder=\"Leave blank for no security\"/></div>";
    pg += "<div class=\"form-group\"><label for=\"ledType\">RGB LED Type</label>";
    pg += "<select id=\"ledType\" name=\"ledType\">";
    if(anode) {
      pg += "<option value=\"cathode\">Common Cathode</option>";
      pg += "<option value=\"anode\" selected>Common Anode</option>";
    } else {
      pg += "<option value=\"cathode\" selected>Common Cathode</option>";
      pg += "<option value=\"anode\">Common Anode</option>";
    }
    pg += "</select></div>";
    if(key.length() > 0){
      pg += "<div class=\"security-note\"><label for=\"saveKey\">🔐 Enter Security Key to Save Changes</label>";
      pg += "<input id=\"saveKey\" name=\"saveKey\" type=\"text\" placeholder=\"Enter your current security key\" style=\"margin-top: 8px;\"/></div>";
    }
    pg += "<button type=\"submit\" class=\"btn\">💾 Save Configuration</button>";
    pg += "<div class=\"restart-note\">⚠️ <strong>Important:</strong> Always restart your Busy Buddy device after saving changes for them to take effect.</div>";
    pg += "</form></div></body></html>";

  }

  server.send(200, "text/html", pg);
}

// Refresh the display with current status
// Canvas is used in order to avoid any update flickering
void displayInfo(String status, String heading){
  IPAddress ip = WiFi.localIP();

  display.clearDisplay();
  dispCanvas.setFont(&FreeSans9pt7b);

  dispCanvas.fillScreen(BLACK);
  dispCanvas.setCursor(0,12);
  dispCanvas.setTextSize(1);
  dispCanvas.setTextColor(WHITE);

  dispCanvas.print(heading);
  
  dispCanvas.setCursor(0,45);
  dispCanvas.setTextSize(2);
  dispCanvas.setTextColor(WHITE);
  dispCanvas.print(status);

  display.drawBitmap(0,0,dispCanvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT, WHITE, BLACK);
  display.display();
}

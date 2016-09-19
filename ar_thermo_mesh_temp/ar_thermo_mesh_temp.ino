#include <ESP8266WiFi.h>
#include <OneWire.h>

//for websockets
#include <WebSocketsServer.h>
#include <Hash.h>

//for multicast
#include <WiFiUdp.h>
const uint8_t MY_ID = 21;
const unsigned int udpMultiPort = 12345; // local port to listen for UDP packets
char udpMultiBuffer[128]; //buffer to hold incoming and outgoing packets
WiFiUDP Udp;
IPAddress ipMulti(239, 0, 0, 57);

//needed for wifimanager
#include <DNSServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
ESP8266WebServer server = ESP8266WebServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);
byte webSocketClients = 0;

//needed for OTA
/* don't use on -01
#include <ESP8266mDNS.h>
#include <FS.h>
#include <ArduinoOTA.h>
#define HOSTNAME "ESP8266-OTA-"
*/
//automation variables
unsigned long timersendnext = 0;
unsigned long timersendinterval = 60000; //one minute
unsigned long timerreadnext = 0;

//ds18b20 stuffs
const int pin1wire = 2;
#include <DallasTemperature.h>
#include "RunningMedian.h"
const int ds18Resolution = 12;
const int ds18count = 2;
DeviceAddress ds18addr[] = {
   { 0x28, 0xC1, 0x02, 0x64, 0x04, 0x00, 0x00, 0x35 },
   { 0x28, 0xE7, 0x0B, 0x63, 0x04, 0x00, 0x00, 0x44 }
};
unsigned int ds18delay;
unsigned long ds18lastreq = 1; //zero is special
const unsigned int ds18reqdelay = 15000; //request every 15 seconds
unsigned long ds18reqlastreq;
OneWire oneWire(pin1wire);
DallasTemperature ds18(&oneWire);
RunningMedian<float,20> tempMedian[ds18count]; //agragate over 20 samples, at 15s intervals that's a 5m period

//functions
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
  char msg[50];
  switch(type) {
    case WStype_DISCONNECTED:
      sprintf(msg, "WSdcn:%u", num);
      serLog(msg);
      webSocketClients--;
      break;
    case WStype_CONNECTED: 
      {
        //IPAddress ip = webSocket.remoteIP(num);
        //USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
  
        // send message to client
        webSocket.sendTXT(num, "Connected");
        webSocketClients++;
        sprintf(msg, "WScon:%u", num);
        serLog(msg);
      }
      break;
    case WStype_TEXT:
      sprintf(msg, "WStxt:%s", payload);
      serLog(msg);
      break;
    default:
      sprintf(msg, "WStyp:%s", type);
      serLog(msg);
  }
}

//root page can be accessed only if authentification is ok
void handleRoot(){
  String header;
  if (!is_authentified()){
    String header = "HTTP/1.1 301 OK\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }
  String content = "<html><head>\n\
<script>\n\
var con = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);\n\
con.onopen = function () {\n\
  con.send('Connect ' + new Date());\n\
};\n\
con.onerror = function (error) {\n\
  console.log('WebSocket Error ', error);\n\
};\n\
con.onmessage = function (e) {\n\
  document.all('output').innerHTML = e.data + '<br>' + document.all('output').innerHTML;\n\
};\n\
</script></head>\n\
<body><div id='output'></div>\n\
<a href='/login?DISCONNECT=YES'>disconnect</a></body></html>";
  server.send(200, "text/html", content);
}

//no need authentification
void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: "; message += server.uri();
  message += "\nMethod: "; message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";  message += server.args(); message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

//Check if header is present and correct
bool is_authentified(){
  if (server.hasHeader("Cookie")){   
    String cookie = server.header("Cookie");
    if (cookie.indexOf("ESPSESSIONID=1") != -1) {
      return true;
    }
  }
  return false;  
}

//login page, also called for disconnect
void handleLogin(){
  String msg;
  if (server.hasHeader("Cookie")){   
    String cookie = server.header("Cookie");
  }
  if (server.hasArg("DISCONNECT")){
    String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=0\r\nLocation: /login\r\nCache-Control: no-cache\r\n\r\n";
    server.sendContent(header);
    return;
  }
  if (server.hasArg("USERNAME") && server.hasArg("PASSWORD")){
    if (server.arg("USERNAME") == "admin" &&  server.arg("PASSWORD") == "admin" ){
      String header = "HTTP/1.1 301 OK\r\nSet-Cookie: ESPSESSIONID=1\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n";
      server.sendContent(header);
      return;
    }
    msg = "Wrong username/password! try again.";
  }
  server.send(200, "text/html", 

"<html>\
<body><form action='/login' method='POST'>\
User:<input type='text' name='USERNAME'><br>\
Password:<input type='password' name='PASSWORD'><br>\
<input type='submit' name='SUBMIT' value='Submit'></form>" + msg + "<br>\
</body></html>"

);
}

void configModeCallback (WiFiManager *myWiFiManager) {
  serLog("Entered Wifi config mode");
  serLog(myWiFiManager->getConfigPortalSSID());
  serLog(WiFi.softAPIP().toString());
}

void wifiSetup() {
  serLog("Starting Wifi");
  WiFiManager wifiManager; //local because we only need it durring bootup
  wifiManager.setDebugOutput(false); //make it quiet
  wifiManager.setAPCallback(configModeCallback);
  
  if(!wifiManager.autoConnect()) {
    serLog("Wifi config timeout");
    delay(1000);
    //reset and try again
    ESP.reset();
    delay(1000);
  } 

  serLog("Wifi Connected");
  serLog(WiFi.localIP().toString());

  // Start OTA server.
/* don't use on -01  
  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();
*/
  // start webSocket server
  serLog("Starting Websocket");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  serLog("Starting Webserver");
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.onNotFound(handleNotFound);

  //headers to be recorded for the auth
  const char * headerkeys[] = {"User-Agent","Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
  server.collectHeaders(headerkeys, headerkeyssize );
  
  server.begin();
  serLog("Wifi Complete!");
}

//log to serial
void serLog(char *msg) {
  static byte i = 0;
  if(strcmp(msg,"") == 0) {
    i=0;
    return;
  }
  if(webSocketClients>0) {
    //sprintf(msg, "L:%d\n", pageID);
    webSocket.broadcastTXT(msg, strlen(msg));
  }
  Serial.println(msg);
}
void serLog(String msg) {
  char b[30];
  msg.toCharArray(b, 28);
  serLog(b);
}
void serLog(char msg) {
  char b[2];
  b[0] = msg; b[1] = '\0';
  serLog(b);
}

void udpSend(uint8_t *packet, size_t size) {
  Udp.beginPacket(ipMulti, udpMultiPort);
  Udp.write(MY_ID);
  Udp.write(packet, size);
  Udp.endPacket();
}

void udpRecive() {
  int bytes = Udp.parsePacket();
  uint8_t sender;
  if (bytes){
    //Udp.remoteIP(), Udp.remotePort()
    Udp.read(&sender, 1);
    Udp.read(udpMultiBuffer, bytes-1);
    switch((char)udpMultiBuffer[0]) {
      case 'W': //someone woke up, send the ??
        serLog('W');
        timersendnext = millis() + timersendinterval;
        Serial.println("sending");
        ds18report();
        break;
    }
  }
}

void setup() {
  Serial.begin(9600);

  delay(10);

  Serial.println();
  Serial.println();
  wifiSetup();

  serLog("Starting UDP");
  Udp.beginMulticast(WiFi.localIP(), ipMulti, udpMultiPort);
  //broadcast that we're alive
  udpSend((uint8_t*)"W", 1);

  serLog("Starting ds18");
  for (byte i=0; i<ds18count; i++) {
    ds18.setResolution(ds18addr[i], ds18Resolution);
  }
  ds18.setWaitForConversion(false); //this enables asyncronous calls
  ds18.requestTemperatures(); //fire off the first request
  ds18lastreq = millis();
  ds18delay = 750 / (1 << (12 - ds18Resolution)); //delay based on resolution

  serLog("All Done!");
}

void loop() {
  webSocket.loop();
  server.handleClient();
  ds18process(); 
  udpRecive();

  //send on interval
  if(millis() >= timersendnext) {
    timersendnext = millis() + timersendinterval;
    Serial.println("sending");
    ds18report();
  }
  // Handle OTA server.
  // not on -01 
  //ArduinoOTA.handle();
  yield();
}

void ds18process() {
  if(ds18lastreq > 0 && millis() - ds18lastreq >= ds18delay) {
    for(byte i=0; i<ds18count; i++) {
      tempMedian[i].add(ds18.getTempF(ds18addr[i]));
    }
    ds18lastreq = 0;
  }

  if(millis() - ds18reqlastreq >= ds18reqdelay) {
    ds18.requestTemperatures(); 
    ds18lastreq = millis();
    ds18reqlastreq = ds18lastreq;
  }
}

void ds18report() {
  float tempCur;
  for(byte i=0; i<ds18count; i++) {
    if (tempMedian[i].getMedian(tempCur) == tempMedian[i].OK) {
      uint8_t buf[3] = {
        (uint8_t)'T', 
        (uint8_t)i, //send configured index
        (uint8_t)round(tempCur)
      };
      udpSend(buf, 3);
      char msg[10];
      sprintf(msg, "Temp %u:%u", i, round(tempCur));
      serLog(msg);
    }
  }
}


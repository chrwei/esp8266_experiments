#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

//for NTP
#include <TimeLib.h> 
#include <NtpClientLib.h>
#include <WiFiUdp.h>
ntpClient *ntp;
unsigned long timeDispLast = 0;
const unsigned int timeDispInterval = 15000; //15 seconds

//for websockets
#include <WebSocketsServer.h>
#include <Hash.h>

//for multicast
const uint8_t MY_ID = 20;
const unsigned int udpMultiPort = 12345; // local port to listen for UDP packets
char udpMultiBuffer[128]; //buffer to hold incoming and outgoing packets
WiFiUDP Udp;
IPAddress ipMulti(239, 0, 0, 57);

//needed for OTA
#include <ESP8266mDNS.h>
#include <FS.h>
#include <ArduinoOTA.h>
#define HOSTNAME "ESP8266-OTA-"

//needed for wifimanager
#include <DNSServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
ESP8266WebServer server = ESP8266WebServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);
byte webSocketClients = 0;

//LCD stuff
const byte btnOccupied = 9;
const byte btnAway = 10;
const unsigned long nexOrange = 64512;
const unsigned long nexGreen = 34784;
byte pageLast = 0;
byte pageID = 0;
#define nex Serial
#define NEXBAUD 9600

#define DATA_ITEMS 4
//this beast will translate our lcd data for us
struct data_t {
  byte type;
  union { //15 bytes total, access .bin, .txt, .val.num, or .evnt.[page|id|touch]
    byte bin[15]; //raw data
    char txt[15]; //string
    struct {
      long num; //number      4 bytes
      unsigned char x[11]; // 11 unused bytes
    } val;//15 bytes to match above
    struct {
      byte page;
      byte id;
      byte touch; //press=1 release=0
      unsigned char y[12]; //12 unused
    }evnt; //15 bytes to match above
  };
} data[DATA_ITEMS]; //make room for DATA_ITEMS packets
byte datapos = 0;

const byte tempOccSummer = 75;
const byte tempOccWinter = 68;
const byte tempAwaySummer = 85;
const byte tempAwayWinter = 55;
uint8_t tempcur[] = { 0, 0 };
uint8_t tempset = 0;
char curmode = ' ';

//functions
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
  char msg[50];
  switch(type) {
    case WStype_DISCONNECTED:
      sprintf(msg, "WSdcn:%u", num);
      lcdLog(msg);
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
        lcdLog(msg);
      }
      break;
    case WStype_TEXT:
      sprintf(msg, "WStxt:%s", payload);
      lcdLog(msg);
      break;
    default:
      sprintf(msg, "WStyp:%s", type);
      lcdLog(msg);
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
  lcdLog("Entered Wifi config mode");
  lcdLog(myWiFiManager->getConfigPortalSSID());
  lcdLog(WiFi.softAPIP().toString());
}

void wifiSetup() {
  lcdLog("Starting Wifi");
  WiFiManager wifiManager; //local because we only need it durring bootup
  wifiManager.setDebugOutput(false); //make it quiet
  wifiManager.setAPCallback(configModeCallback);
  
  WiFi.hostname("esplcd");
  if(!wifiManager.autoConnect()) {
    lcdLog("Wifi config timeout");
    delay(1000);
    //reset and try again
    ESP.reset();
    delay(1000);
  } 

  lcdLog("Wifi Connected");
  lcdLog(WiFi.localIP().toString());
  
  // Start OTA server.
  String hostname(HOSTNAME);
  hostname += String(ESP.getChipId(), HEX);
  WiFi.hostname(hostname);
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.begin();

  // start webSocket server
  lcdLog("Starting Websocket");
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  lcdLog("Starting Webserver");
  server.on("/", handleRoot);
  server.on("/login", handleLogin);
  server.onNotFound(handleNotFound);

  //headers to be recorded for the auth
  const char * headerkeys[] = {"User-Agent","Cookie"} ;
  size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
  server.collectHeaders(headerkeys, headerkeyssize );
  
  server.begin();
  lcdLog("Wifi Complete!");
}

void lcdSetup() {
  //init type 
  data[0].type=0;
  //reset LCD to clear the esp boot stuff
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
  nex.print("rest");
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
  delay(300);
  lcdCmd("page boot");
}

void lcdText(char *label, char *msg) {
  if(webSocketClients>0) {
    char m[30];
    sprintf(m, "txt:%s:%s\n", label, msg);
    webSocket.broadcastTXT(m, strlen(m));
  }
  if (pageID != 1) return; //only allow txt on 'home'
  nex.print(label);nex.print(".txt=\"");
  nex.print(msg);nex.print("\"");
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
}
void lcdText(char *label, String msg) {
  char b[30];
  msg.toCharArray(b, 30);
  lcdText(label, b);
}

void lcdNum(char *label, long val) {
  if(webSocketClients>0) {
    char m[30];
    sprintf(m, "num:%s:%d\n", label, val);
    webSocket.broadcastTXT(m, strlen(m));
  }
  if (pageID != 1) return; //only allow num on 'home'
  nex.print(label);nex.print(".val=");
  nex.print(val);
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
}

void lcdCmd(char *cmd) {
  if(webSocketClients>0) {
    char m[30];
    sprintf(m, "cmd:%s\n", cmd);
    webSocket.broadcastTXT(m, strlen(m));
  }
  nex.print(cmd);
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
}

//show things on the log screen.  make sure you are on pageID 0 "boot" for this
void lcdLog(char *msg) {
  static byte i = 0;
  if(strcmp(msg,"") == 0) {
    i=0;
    return;
  }
  if(webSocketClients>0) {
    //sprintf(msg, "L:%d\n", pageID);
    webSocket.broadcastTXT(msg, strlen(msg));
  }
  if (pageID != 0) return; //only allow log on 'boot'
  if(i<13) { //clear the next line as long as we're not the last line
    nex.print("tLog");nex.print(i+1);nex.print(".txt=\"\"");
    nex.write(0xff); nex.write(0xff); nex.write(0xff);
  }
  nex.print("tLog");nex.print(i);nex.print(".txt=\"");
  nex.print(msg);nex.print("\"");
  nex.write(0xff); nex.write(0xff); nex.write(0xff);
  i++;
  if(i>13) i=0; //overflo back to top
}
void lcdLog(String msg) {
  char b[30];
  msg.toCharArray(b, 28);
  lcdLog(b);
}
void lcdLog(char msg) {
  char b[2];
  b[0] = msg; b[1] = '\0';
  lcdLog(b);
}

void lcdRedraw(){
  switch(pageID){
    case 0: //boot screen, but if we're calling this show some info
      lcdLog(""); //resets line counter
      nex.print("t1.txt=\"Info\"");
      nex.write(0xff); nex.write(0xff); nex.write(0xff);
      lcdLog("Wifi Connected");
      lcdLog(WiFi.localIP().toString());
      break;
    case 1: //home
      lcdNum("nSet", tempset);
      switch(curmode) {
        case 'C':
          lcdText("tState", "Cool");
          break;
        case 'H':
          lcdText("tState", "Heat");
          break;
        default:
          lcdText("tState", "Idle");
      }
      lcdText("tTime", ntp->getTimeStr12());
      lcdText("tAwayTime", " ");
      lcdNum("nTemp0", tempcur[0]);
      lcdNum("nTemp1", tempcur[1]);
      lcdNum("nSet", 0);
      break;
  }
  pageLast = pageID;
}

void lcdProcess() {
  //process the saved data packets
  byte i = datapos+1;
  do {
    if(i>=DATA_ITEMS) i=0; //loop around
    //HOST_PORT.print("checking:");HOST_PORT.print(i);HOST_PORT.print(' ');
    //HOST_PORT.println(data[i].type, HEX);
    if(data[i].type > 0){ //we have a packet!
      //HOST_PORT.print("processing ");HOST_PORT.print(i);HOST_PORT.print(' ');
      //HOST_PORT.println(data[i].type, HEX);
      switch(data[i].type) { //http://wiki.iteadstudio.com/Nextion_Instruction_Set#Format_of_Device_Return_Data
        case 0x65: //press event 
          if(webSocketClients>0) {
            char msg[50];
            sprintf(msg, "p:%d:%d:%d\n", data[i].evnt.page, data[i].evnt.id, data[i].evnt.touch);
            webSocket.broadcastTXT(msg, strlen(msg));
          }
          break;
        case 0x66: //page ID 
          char msg[50];
          pageID = data[i].bin[0];
          if(webSocketClients>0) {
            sprintf(msg, "s:%d\n", pageID);
            webSocket.broadcastTXT(msg, strlen(msg));
          }
          break;
        case 0x70: //string data
          //HOST_PORT.println("string");
          //HOST_PORT.print("t:");
          //for(byte c=0; c<sizeof(data[i].txt) && data[i].txt[c] != '\0'; c++) {
          //  HOST_PORT.print(data[i].txt[c]);
          //}
          //HOST_PORT.println();
          break;
        case 0x71: //4 byte number data
          //HOST_PORT.println("number");
          //HOST_PORT.print("n:");
          //HOST_PORT.println(data[i].val.num);
          break;
        //default:
          //HOST_PORT.print("code ");
          //HOST_PORT.print(data[i].type, HEX);HOST_PORT.print(' ');
          //for(byte c=0; c<sizeof(data[i].bin) && data[i].bin[c] != '\0'; c++) {
          //  HOST_PORT.println(data[i].bin[c], HEX);HOST_PORT.print(' ');
          //}
          //HOST_PORT.println();
      }
      data[i].type =0; //reset for reuse
    }
  } while(i++ != datapos);
}

void lcdRead() {
  if(nex.available()){
    //HOST_PORT.print("reading"); HOST_PORT.println(datapos);
    if(data[datapos].type > 0){
      return; //leave and wait for a free slot
    }
    data[datapos].type=nex.read(); //http://wiki.iteadstudio.com/Nextion_Instruction_Set#Format_of_Device_Return_Data
    uint8_t ffcount=0;
    uint8_t bufpos=0;
    while (ffcount<3 && bufpos < sizeof(data[datapos].bin)) {
      
      while(!nex.available()) delay(1);//wait for something in the buffer
      data[datapos].bin[bufpos] = nex.read();
      if(data[datapos].bin[bufpos] == 0xff){
        ffcount++;
      }
      //HOST_PORT.print(ffcount);HOST_PORT.print(' ');
      //HOST_PORT.print(data[datapos].bin[bufpos], HEX);HOST_PORT.print(' ');
      //HOST_PORT.print(data[datapos].bin[bufpos], DEC);HOST_PORT.print(' ');
      //HOST_PORT.println((char)data[datapos].bin[bufpos]);
      bufpos++;
    }
    if(ffcount != 3) {
      data[datapos].type = 0; //bad data, clear it.
      return; //bail
    }
    bufpos = bufpos-3; //back up to the FFs
    while(bufpos < sizeof(data[datapos].bin)) {
      data[datapos].bin[bufpos] = '\0'; //null the rest of the array
      bufpos++;
    }
    datapos++;
    if(datapos>=DATA_ITEMS) datapos=0; //loop around
  }
}


void checkNTP() {
  if (timeDispLast + timeDispInterval < millis()) {
    timeDispLast = millis();
    lcdText("tTime", ntp->getTimeStr12());
    lcdLog(ntp->getTimeStr12());
  }
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
      case 'M': //mode change
        curmode = udpMultiBuffer[1];
        switch(curmode) {
          case 'C':
            lcdText("tState", "Cool");
            break;
          case 'H':
            lcdText("tState", "Heat");
            break;
          default:
            lcdText("tState", "Idle");
        }
        lcdLog(curmode);
        break;
      case 'S': //we're being told what the set temp is
        tempset = udpMultiBuffer[1];
        lcdNum("nSet", tempset);
        break;
      case 'Z': //off time set
        unsigned long timerofftime;
        memcpy(&timerofftime, udpMultiBuffer + 1,sizeof(timerofftime));
        lcdLog(timerofftime);
      case 'W': //someone woke up, send the ??
        //char pkt[2] = {'T', (char)tempcur}; //send temp
        //bus.send(id, pkt, 2); //just to the new guy
        lcdLog('W');
        break;
      case 'T': //temp reported
        tempcur[udpMultiBuffer[1]] = udpMultiBuffer[2];
        char msg[10];
        sprintf(msg, "nTemp%u", udpMultiBuffer[1]);
        lcdNum(msg, tempcur[udpMultiBuffer[1]]);
        sprintf(msg, "Temp %u:%u", udpMultiBuffer[1], tempcur[udpMultiBuffer[1]]);
        lcdLog(msg);
    }
  }
}

void setup() {
  nex.begin(NEXBAUD);
  nex.swap(); //swap to gpio 15/13
  lcdSetup();
  wifiSetup();
  lcdLog("Starting NTP");
  ntp = ntpClient::getInstance("pool.ntp.org", -6, 1); // CST, dst enabled
  ntp->setInterval(15, 1800); // OPTIONAL. Set sync interval
  ntp->begin(); //Starts time synchronization
  //  Serial.println("Done");

  lcdLog("Starting UDP");
  Udp.beginMulticast(WiFi.localIP(), ipMulti, udpMultiPort);
  //broadcast that we're alive
  udpSend((uint8_t*)"W", 1);
  
  lcdLog("All Done!");
  
  lcdCmd("page home");
  
  lcdText("tTime", " ");
  lcdText("tAwayTime", " ");
  lcdText("tState", " ");
  lcdNum("nTemp0", 0);
  lcdNum("nTemp1", 0);
  lcdNum("nSet", 0);
}

void loop() {
  if(pageLast != pageID) {
    lcdRedraw();
  }
  webSocket.loop();
  server.handleClient();

  lcdProcess(); //process buffer
  lcdRead(); //read LCD into buffer
  checkNTP();//finish NTP sync

  udpRecive();

  // Handle OTA server.
  ArduinoOTA.handle();
  yield();
}

void handlerPress(byte dataid) {
  if(data[dataid].evnt.page == 1 && data[dataid].evnt.id == btnOccupied && data[dataid].evnt.touch == 0) { //zero is release
    tempset = tempOccSummer;
    lcdNum("nSet", tempset);
  }
}


/*
 * From http://www.esp8266.com/viewtopic.php?f=29&t=2464&start=8#sthash.Pb7Prbqu.dpuf
 * 
 */
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>

int status = WL_IDLE_STATUS;
const char* ssid = "ssid"; // your network SSID (name)
const char* pass = "pass"; // your network password


unsigned int localPort = 12345; // local port to listen for UDP packets
char packetBuffer[128]; //buffer to hold incoming and outgoing packets
//////////////////////////////////////////////////////////////////////
// A UDP instance to let us send and receive packets over UDP
//////////////////////////////////////////////////////////////////////
WiFiUDP Udp;

// Multicast declarations
IPAddress ipMulti(239, 0, 0, 57);

unsigned int portMulti = 12345; // local port to listen on

void setup()
{
  // Open serial communications and wait for port to open:
  Serial.begin(115200);

  // setting up Station AP
  WiFi.begin(ssid, pass);

  // Wait for connect to AP
  Serial.print("[Connecting]");
  Serial.print(ssid);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    tries++;
    if (tries > 30) {
      break;
    }
  }
  Serial.println();


  printWifiStatus();

  Serial.println("Connected to wifi");
  Serial.print("Udp Multicast listener started at : ");
  Serial.print(ipMulti);
  Serial.print(":");
  Serial.println(portMulti);
  Udp.beginMulticast(WiFi.localIP(), ipMulti, portMulti);
  delay(500);
    Udp.beginPacket(ipMulti, portMulti);
    Udp.write("My ChipId:");
    packetBuffer = ESP.getChipId();
    Udp.write(packetBuffer[0]);
    Udp.write(packetBuffer[1]);
    Udp.write(packetBuffer[2]);
    Udp.write(packetBuffer[3]);
    Udp.endPacket();
}

void loop()
{
  checkUDP();
}
void checkUDP()
{
  int noBytes = Udp.parsePacket();
  if ( noBytes )
  {
    //////// dk notes
    /////// UDP packet can be a multicast packet or a specific to this device's own IP
    Serial.print(millis() / 1000); Serial.print(":Packet of "); Serial.print(noBytes);
    Serial.print(" received from "); Serial.print(Udp.remoteIP()); Serial.print(":");
    Serial.println(Udp.remotePort());
    //////////////////////////////////////////////////////////////////////
    // We've received a packet, read the data from it
    //////////////////////////////////////////////////////////////////////
    Udp.read(packetBuffer, noBytes); // read the packet into the buffer

    // display the packet contents in HEX
    for (int i = 1; i <= noBytes; i++) {
      Serial.print(packetBuffer[i - 1], HEX);
      if (i % 32 == 0) {
        Serial.println();
      }
      else Serial.print(' ');
    } // end for
    Serial.println();
    Serial.print("test raw:"); Serial.println(packetBuffer);
    char pre[4]; //one extra for the null terminator
    strncpy(pre, packetBuffer, 3); pre[3] = 0; //strncpy doesn't add it for it
    Serial.print("test char 3:"); Serial.println(pre);
    //if(
    
    //////////////////////////////////////////////////////////////////////
    // send a reply, to the IP address and port that sent us the packet we received
    // the receipient will get a packet with this device's specific IP and port
    //////////////////////////////////////////////////////////////////////
    //Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    //Udp.write("My ChipId:");
    //Udp.write(ESP.getChipId());
    //Udp.endPacket();
  } // end if

  delay(20);

}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
} 

#include <OneWire.h>
#include <DallasTemperature.h>
#include "RunningMedian.h"

//pin definitions
const int pin1wire = 2;

//ds18b20 stuff
const int ds18Resolution = 12;
const int ds18count = 2;
const DeviceAddress ds18addr[] = {
   { 0x28, 0xC1, 0x02, 0x64, 0x04, 0x00, 0x00, 0x35 },
   { 0x28, 0xE7, 0x0B, 0x63, 0x04, 0x00, 0x00, 0x44 }
};
unsigned int ds18delay;
unsigned long ds18lastreq = 1; //zero is special
const unsigned int ds18reqdelay = 15000; //request every 15 seconds
unsigned long ds18reqlastreq;
OneWire oneWire(pin1wire);
DallasTemperature ds18(&oneWire);

const unsigned int tempdelay = 15000; //report every 15 seconds
unsigned long templast;

RunningMedian<float,20> tempMedian[ds18count]; //agragate over 20 samples, at 15s intervals that's a 5m period

void setup() {
  Serial.begin(115200);

  delay(10);
  ds18.begin();
  Serial.println();
  Serial.println();
  Serial.print("Starting, reports on ");
  Serial.print(tempdelay/1000);
  Serial.println("s intervals");
  for (byte i=0; i<ds18count; i++) {
    ds18.setResolution(ds18addr[i], ds18Resolution);
  }
  ds18.setWaitForConversion(false); //this enables asyncronous calls
  ds18.requestTemperatures(); //fire off the first request
  ds18lastreq = millis();
  ds18delay = 750 / (1 << (12 - ds18Resolution)); //delay based on resolution
}

void loop() {
  if(ds18lastreq > 0 && millis() - ds18lastreq >= ds18delay) {
    for(byte i=0; i<ds18count; i++) {
      tempMedian[i].add(ds18.getTempF(ds18addr[i]));
//      Serial.print("read   "); Serial.print(i); Serial.print(":"); Serial.print(ds18.getTempF(ds18addr[i])); Serial.print('\t');
    }
//    Serial.println();

    ds18lastreq = 0;
  }

  if(millis() - ds18reqlastreq >= ds18reqdelay) {
    ds18.requestTemperatures(); 
    ds18lastreq = millis();
    ds18reqlastreq = ds18lastreq;
  }

  if(millis() - templast >= tempdelay) {
    float tempCur;
    for(byte i=0; i<ds18count; i++) {
      Serial.print(i);Serial.print(":"); 
      Serial.print(tempMedian[i].getCount());Serial.print(":"); 
      if (tempMedian[i].getLowest(tempCur) == tempMedian[i].OK) {
        Serial.print(tempCur);Serial.print(":");
      } else {
        Serial.print("WW.ww:");
      }
      if (tempMedian[i].getHighest(tempCur) == tempMedian[i].OK) {
        Serial.print(tempCur);Serial.print(":");
      } else {
        Serial.print("WW.ww:");
      }
      if (tempMedian[i].getAverage(tempCur) == tempMedian[i].OK) {
        Serial.print(tempCur);Serial.print(":");
      } else {
        Serial.print("WW.ww:");
      }
      
      if (tempMedian[i].getMedian(tempCur) == tempMedian[i].OK) {
        Serial.print(tempCur, 2);Serial.print(",");
        Serial.print((uint8_t)round(tempCur), HEX);
      } else {
        Serial.print("WW.ww,WW");
      }
      Serial.print('\t');
    }
    Serial.println();
    templast = millis();
  }
}



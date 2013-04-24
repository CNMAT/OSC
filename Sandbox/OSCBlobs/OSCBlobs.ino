/*
An OSC Blob is an array of bytes. 
 
 This data type can be used to send chunks of data with an address
 */

#include <OSCMessage.h>


void setup() {
  Serial.begin(38400);
}

void loop(){
  //put 6 analog readings all the same blob
  byte blob[6];
  for (int i = 0; i < 6; i++){
    int anlgVal = analogRead(i);
    //scale the reading to fit in a byte
    byte analogByte = map(anlgVal, 0, 1023, 0, 255);
    blob[i] = analogByte;
  }
  OSCMessage msg("/analogBlob"); msg.add(blob, 6);
  msg.send(Serial);
  delay(1000);
}



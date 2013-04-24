/*
  Retreiving the data stored in the OSCBundle or OSCMessage:
 
 OSCBundle has a 'getMessage' method which will return messages using the position or address of the message.
 
 OSCMessage has getters for each of the data types which is stored in the message.
 OSCMessage's data is stored in a FIFO so the first datum place in the message is the first read.
 Reset the internal pointer using the 'reset' method.
 */

#include <OSCBundle.h>

OSCBundle bndl;

void setup() {
  Serial.begin(38400);
  while(!Serial)
    ;
  //add a bunch of data to the bundle to 'get'
  bndl.add("/ints").add(0).add(1).add(2).add(3);
  bndl.add("/floats").add(1.0f).add(2.0f).add(3.0f);
  bndl.add("/letters").add("a").add("b").add("c");
  bndl.add("/mix").add("a").add(2).add(3.0f);
}

void loop(){
  //iterate over all the messages and print their contents
  for ( int i = 0; i < bndl.size(); i++){
     printContentsOfMessage(bndl.getOSCMessage(i));  
  }
  delay(1000);
}


void printContentsOfMessage(OSCMessage msg){ 
  //puts the data pointer back at the start
  //grab the address
  char addrBuff[msg.getAddressLength()];
  msg.getAddress(addrBuff, msg.getAddressLength());
  
  Serial.print(addrBuff);
  Serial.print(": ");
  //iterate over all of the data in the message
  for (int i = 0; i < msg.size(); i++){
    //test for each of the data types
    //use the getter specific to that type
    if (msg.isInt(i)){
      Serial.print(msg.getInt(i));
      Serial.print(" ");
    } 
    else if (msg.isFloat(i)){
      Serial.print(msg.getFloat(i));
      Serial.print("f ");
    } 
    else if (msg.isString(i)){
      char strBuff[msg.getDataLength(i)]; //Length includes room for the null in strings
      msg.getString(i,strBuff,msg.getDataLength(i));
      Serial.print(strBuff);
      Serial.print(" ");
    } 
  } 
  Serial.println(" ");
}



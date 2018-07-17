/*
Receives and visualizes OSCBundles sent over UDP

Use with /examples/UDPSendMessage

or with /examples/SerialSendMessage in conjunction 
with /Applications/Processing/SLIPSerialToUDP
*/

import oscP5.*;
import netP5.*;
  
OscP5 oscP5;

void setup() {
  size(150,300);
  frameRate(30);
  //set this to the receiving port
  oscP5 = new OscP5(this,9001);
}


void draw() {
  background(0); 
  //draw the analog values
  float analog0Height = map(analogValue0, 0, 1024, 0, 200);
  fill(255);
  rect(50, 250, 50, -analog0Height);
  //and the labels
  textSize(12);
  text("/analog/0", 50, 270);
}

int analogValue0 = 50;

// incoming osc message are forwarded to the oscEvent method. 
void oscEvent(OscMessage theOscMessage) {
  //println(theOscMessage.addrPattern());
  if (theOscMessage.addrPattern().equals("/analog/0")){
    analogValue0 = theOscMessage.get(0).intValue();
  } 
}

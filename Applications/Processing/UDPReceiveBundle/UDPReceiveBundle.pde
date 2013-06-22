/*
Receives and visualizes OSCBundles sent over UDP

Use with /examples/UDPSendBundle

or with /examples/SerialSendBundle in conjunction 
with /Applicaitons/Processing/SLIPSerialToUDP
*/

import oscP5.*;
import netP5.*;
  
OscP5 oscP5;

void setup() {
  size(600,300);
  frameRate(30);
  //set this to the receiving port
  oscP5 = new OscP5(this,9001);
}


void draw() {
  background(0); 
  //draw the analog values
  float analog0Height = map(analogValue0, 0, 1024, 0, 200);
  float analog1Height = map(analogValue1, 0, 1024, 0, 200);
  fill(255);
  rect(50, 250, 50, -analog0Height);
  rect(150, 250, 50, -analog1Height);
  //and the labels
  textSize(12);
  text("/analog/0", 50, 270);
  text("/analog/1", 150, 270);
  //and the digital pin label
  text("/digital/5", 250, 270);
  textSize(25);
  text(digitalValue5, 250, 250);
  //now do the mouse part
  //add the label
  textSize(12);
  text("/mouse/step", 350, 270);
  //make a box where it should go
  noFill();
  stroke(255);
  rect(350, 50, 200, 200);
  //and a square where the mouse is
  fill(255);
  float mouseXPos = map(mouseStepX, 0, 1024, 350, 530);
  float mouseYPos = map(mouseStepY, 0, 1024, 50, 230);
  rect(mouseXPos, mouseYPos, 20, 20);
}

int analogValue0 = 50;
int analogValue1 = 50;
String digitalValue5 = "LOW";

int mouseStepX = 0;
int mouseStepY = 0;

// incoming osc message are forwarded to the oscEvent method. 
void oscEvent(OscMessage theOscMessage) {
  //println(theOscMessage.addrPattern());
  if (theOscMessage.addrPattern().equals("/analog/0")){
    analogValue0 = theOscMessage.get(0).intValue();
  } else if(theOscMessage.addrPattern().equals("/analog/1")){
    analogValue1 = theOscMessage.get(0).intValue();
  } else if(theOscMessage.addrPattern().equals("/digital/5")){
    digitalValue5 = theOscMessage.get(0).stringValue();
  } else if(theOscMessage.addrPattern().equals("/mouse/step")){
     mouseStepX = theOscMessage.get(0).intValue();
     mouseStepY = theOscMessage.get(1).intValue();
  }
}

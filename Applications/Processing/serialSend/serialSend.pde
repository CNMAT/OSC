import oscP5.*;
import netP5.*;

OscP5 oscP5;
NetAddress arduinoAddress;
//the number of analog pins on this controller
int analogPins = 16;
//an array of all of the pin values
int[] pinVals = new int[analogPins];

void setup() {
  frameRate(60);
  size(320, 100);
  background(0);
  //initialize the listening port
  oscP5 = new OscP5(this, 9999);
  //the outgoing communication to the arduino
  arduinoAddress = new NetAddress("128,32.122.252", 8888);
}


void draw() {
  //clear the previous bars
  fill(0);
  rect(0, 0, width, height);
  //draw each of the bars showing the pin value
  for (int i = 0; i < analogPins; i++) {
    int value = pinVals[i];
    int barWidth = width/analogPins;
    float barHeight = (value/1024.)*height;
    //draws the new bar in white
    fill(255);
    rect(barWidth*i, height - barHeight, barWidth, barHeight);
  }
}

// incoming osc message are forwarded to the oscEvent method.
void oscEvent(OscMessage msg) {
  String address = msg.addrPattern();
  if (address.startsWith("/analog/")) {
    //then it's an analog reading
    //split the address 
    String[] splitAddr = address.split("/");
    //the third element should be the number
    int pinNum = Integer.parseInt(splitAddr[2]);   
    int val = msg.get(0).intValue();
    pinVals[pinNum] = val;
  }
}


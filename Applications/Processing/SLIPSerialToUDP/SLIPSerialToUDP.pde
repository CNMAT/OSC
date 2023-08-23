import processing.serial.*;
//download at http://ubaa.net/shared/processing/udp/
import hypermedia.net.*;
//download at www.sojamo.de/libraries/controlp5
import controlP5.*;

boolean applicationRunning = false;


void setup() {
  // configure the screen size and frame rate
  size(550, 350, P3D);
  frameRate(30);
  setupGUI();
}

void draw() {
  background(128);
  if (applicationRunning) {
    drawIncomingPackets();
  }
}


/************************************************************************************
 VISUALIZING INCOMING PACKETS
 ************************************************************************************/

int lastSerialPacket = 0;
int lastUDPPacket = 0;

void drawIncomingPackets() {
  //the serial packet
  fill(0);
  rect(75, 50, 100, 100);
  //the udp packet
  rect(325, 50, 100, 100);
  int now = millis();
  int lightDuration = 75;
  if (now - lastSerialPacket < lightDuration) {
    fill(255);
    rect(85, 60, 80, 80);
  }
  if (now - lastUDPPacket < lightDuration) {
    fill(255);
    rect(335, 60, 80, 80);
  }
}

void drawIncomingSerial() {
  lastSerialPacket = millis();
}

void drawIncomingUDP() {
  lastUDPPacket = millis();
}

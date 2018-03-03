
/*
Make an OSC bundle and send it over UDP

OSCBundles allow OSCMessages to be grouped together
to  preserve the order and completeness of related messages.
They also allow for timetags to be carried to represent the presentation time
of the messages.
 */
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    
#include <OSCBundle.h>
#include <OSCTiming.h>


EthernetUDP Udp;

//the Arduino's IP
IPAddress ip(128, 32, 122, 26);
//destination IP
IPAddress outIp(128, 32, 122, 25);
const unsigned int outPort = 9999;

 byte mac[] = {  
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // you can find this written on the board of some Arduino Ethernets or shields
void setup() {
  Ethernet.begin(mac,ip);
  Udp.begin(8888);
}



void loop(){
  //declare the bundle
     OSCBundle bndl;
    osctime_t timetag;
    
    //OSCBundle's add' returns the OSCMessage so the message's 'add' can be composed together
    bndl.add("/analog/0").add((int32_t)adcRead(0, &timetag));
    bndl.add("/analog/0/time").add(timetag);
    
    bndl.add("/analog/1").add((int32_t)adcRead(1, &timetag));
   bndl.add("/analog/1/time").add(timetag);
   
    Udp.beginPacket(outIp, outPort);
    bndl.setTimetag(oscTime());
    bndl.send(Udp); // send the bytes to the SLIP stream
    Udp.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    bndl.add("/mouse/step").add((int32_t)analogRead(0)).add((int32_t)analogRead(1));
    bndl.add("/units").add("pixels");

    Udp.beginPacket(outIp, outPort);
    bndl.setTimetag(oscTime());
    bndl.send(Udp); // send the bytes to the SLIP stream
    Udp.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    delay(100);
}

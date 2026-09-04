
/*
Make an OSC bundle and send it over UDP

OSCBundles allow OSCMessages to be grouped together
to  preserve the order and completeness of related messages.
They also allow for timetags to be carried to represent the presentation time
of the messages. Here the bundle's timetag is the time the readings were taken, so no
message carries a time of its own.
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
    osctime_t when;
    
    //OSCBundle's add' returns the OSCMessage so the message's 'add' can be composed together
    //adcRead also reports the time the sample was taken
    bndl.add("/a/0").add((intOSC_t)adcRead(0, &when));
    
    bndl.add("/a/1").add((intOSC_t)adcRead(1, &when));
   
    Udp.beginPacket(outIp, outPort);
    //the bundle's own timetag carries the time of the readings (the last
    //adcRead's); there is no separate per-pin time message
    bndl.setTimetag(when);
    bndl.send(Udp); // send the bytes into the UDP packet
    Udp.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    //a message can carry several arguments; /diag carries a label and free
    //values that no page parses
    bndl.add("/diag").add("A0 A1").add((intOSC_t)analogRead(0)).add((intOSC_t)analogRead(1));
    bndl.add("/diag").add("units").add("ADC counts");

    Udp.beginPacket(outIp, outPort);
    bndl.setTimetag(oscTime());
    bndl.send(Udp); // send the bytes into the UDP packet
    Udp.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    delay(100);
}

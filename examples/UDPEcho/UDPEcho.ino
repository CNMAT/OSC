/*

Leverage the UDP source IP and port calls to 
return OSC information back

This example can be extended to build routers and forwarders of OSC packets

 */
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    
#include <OSCBundle.h>

EthernetUDP Udp;

//the Arduino's IP
IPAddress ip(128, 32, 122, 252);

//port numbers
const unsigned int inPort = 8888;
const unsigned int outPort = 9999;


 byte mac[] = {  
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // you can find this written on the board of some Arduino Ethernets or shields
void setup() {
  Ethernet.begin(mac,ip);
  Udp.begin(inPort);
}

void loop(){
    OSCBundle bndl;
   int size;
   
    //receive a bundle
   if( (size = Udp.parsePacket())>0)
   {
//        unsigned int outPort = Udp.remotePort();

         while(size--)
           bndl.fill(Udp.read());

        if(!bndl.hasError())
        {
              //and echo it back
             if(bndl.size() > 0)
             {
                static int32_t sequencenumber=0;

                // we can sneak an addition onto the end of the bundle
               bndl.add("/micros").add((int32_t)micros()); // (int32_t) is the type of OSC Integers
                bndl.add("/sequencenumber").add(sequencenumber++);

                Udp.beginPacket(Udp.remoteIP(), outPort);
                bndl.send(Udp);
                Udp.endPacket();     
             }
        }
   }
}


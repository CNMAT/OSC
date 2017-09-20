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

//everything on the network needs a unique MAC
#if defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MKL26Z64__) 
// Teensy 3.x has MAC burned in
static byte mac[6];
void read(uint8_t word, uint8_t *mac, uint8_t offset) {
      FTFL_FCCOB0 = 0x41;             // Selects the READONCE command
      FTFL_FCCOB1 = word;             // read the given word of read once area
    
      // launch command and wait until complete
      FTFL_FSTAT = FTFL_FSTAT_CCIF;
      while(!(FTFL_FSTAT & FTFL_FSTAT_CCIF));
    
      *(mac+offset) =   FTFL_FCCOB5;       // collect only the top three bytes,
      *(mac+offset+1) = FTFL_FCCOB6;       // in the right orientation (big endian).
      *(mac+offset+2) = FTFL_FCCOB7;       // Skip FTFL_FCCOB4 as it's always 0.
}
void read_mac() {
      read(0xe,mac,0);
      read(0xf,mac,3);
}
#else
void read_mac() {}
 byte mac[] = {  
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // you can find this written on the board of some Arduino Ethernets or shields
#endif

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


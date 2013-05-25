/*

Serial USB ports are bidirectional.

This example can be extended to build routers and forwarders of OSC packets
*/

#include <OSCBundle.h>


//Teensy and Leonardo variants have special USB serial
#if defined(CORE_TEENSY)|| defined(__AVR_ATmega32U4__)
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(Serial);
#else
// any hardware serial port
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif

void setup() {
    //begin SLIPSerial just like Serial
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
    while(!Serial)
      ; //Leonardo "feature"
}

void loop(){
    OSCBundle bndl;
    int size;
    //receive a bundle

    while(!SLIPSerial.endofPacket())
        if( (size =SLIPSerial.available()) > 0)
        {
            while(size--)
              bndl.fill(SLIPSerial.read());
        }

    if(!bndl.hasError())
    {
        static int32_t sequencenumber=0;
        // we can sneak an addition onto the end of the bundle
        bndl.add("/micros").add((int32_t)micros()); // (int32_t) is the type of OSC Integers
        bndl.add("/sequencenumber").add(sequencenumber++);
        SLIPSerial.beginPacket(); // mark the beginning of the OSC Packet
            bndl.send(SLIPSerial);
        SLIPSerial.endPacket();     
    }
}


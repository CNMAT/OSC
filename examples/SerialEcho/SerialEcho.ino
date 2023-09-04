/*

Serial USB ports are bidirectional.

This example can be extended to build routers and forwarders of OSC packets
*/

#include <OSCBundle.h>

#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
 SLIPEncodedSerial SLIPSerial(Serial); // Change to Serial1 or Serial2 etc. for boards with multiple serial ports that don’t have Serial
#endif

void setup() {
    //begin SLIPSerial just like Serial
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
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
        bndl.add("/digital/5").add(digitalRead(5)==HIGH);
        bndl.add("/lsb").add((sequencenumber &1)==1);
        SLIPSerial.beginPacket(); // mark the beginning of the OSC Packet
            bndl.send(SLIPSerial);
        SLIPSerial.endPacket();     
    }
}


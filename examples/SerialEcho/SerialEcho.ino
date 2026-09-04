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
              {
                // read() returns int and -1 on underrun; passing that straight
                // to fill() narrowed it to an ordinary 0xFF data byte
                int c = SLIPSerial.read();
                if (c >= 0) bndl.fill((uint8_t)c);
              }
        }

    if(!bndl.hasError())
    {
        static intOSC_t sequencenumber=0;
        // we can sneak additions onto the end of the bundle: the /state
        // heartbeat every stream carries (sequence, millis), a pin reading,
        // and a /diag label with a value no page parses
        bndl.add("/state").add(sequencenumber++).add((intOSC_t)millis()); // (intOSC_t) is the type of OSC Integers
        bndl.add("/d/5").add((intOSC_t)digitalRead(5));
        bndl.add("/diag").add("lsb").add((sequencenumber &1)==1);
        SLIPSerial.beginPacket(); // mark the beginning of the OSC Packet
            bndl.send(SLIPSerial);
        SLIPSerial.endPacket();     
    }
}


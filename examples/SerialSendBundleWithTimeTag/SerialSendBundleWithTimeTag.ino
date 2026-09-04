
/*
    Make an OSC bundle and send it over SLIP serial

    OSCBundles allow OSCMessages to be grouped together to  preserve the order and completeness of related messages.
    They also allow for timetags to be carried to represent the presentation time of the messages.
    Here the bundle's timetag is the time the readings were taken, so no message carries a time of its own.
*/
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <OSCTiming.h>
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
    //declare the bundle
    OSCBundle bndl;
    osctime_t when;
    
    //OSCBundle's add' returns the OSCMessage so the message's 'add' can be composed together
    //adcRead also reports the time the sample was taken
    bndl.add("/a/0").add((intOSC_t)adcRead(0, &when));
    
    bndl.add("/a/1").add((intOSC_t)adcRead(1, &when));
   
    bndl.add("/d/5").add((intOSC_t)digitalRead(5));

    SLIPSerial.beginPacket();
    //the bundle's own timetag carries the time of the readings (the last
    //adcRead's); there is no separate per-pin time message
    bndl.setTimetag(when);
        bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    delay(100);
}


/*
    Make an OSC bundle and send it over SLIP serial

    OSCBundles allow OSCMessages to be grouped together to  preserve the order and completeness of related messages.
    They also allow for timetags to be carried to represent the presentation time of the messages.
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
    osctime_t timetag;
    
    //OSCBundle's add' returns the OSCMessage so the message's 'add' can be composed together
    bndl.add("/analog/0").add((intOSC_t)adcRead(0, &timetag));
    bndl.add("/analog/0/time").add(timetag);
    
    bndl.add("/analog/1").add((intOSC_t)adcRead(1, &timetag));
    bndl.add("/analog/1/time").add(timetag);
   
    bndl.add("/digital/5").add((digitalRead(5)==HIGH)?"HIGH":"LOW");

    SLIPSerial.beginPacket();
    bndl.setTimetag(oscTime());
        bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    delay(100);
}

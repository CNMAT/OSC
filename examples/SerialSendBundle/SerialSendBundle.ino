
/*
    Make an OSC bundle and send it over SLIP serial

    OSCBundles allow OSCMessages to be grouped together to  preserve the order and completeness of related messages.
    They also allow for timetags to be carried to represent the presentation time of the messages.
*/
#include <OSCBundle.h>
#include <OSCBoards.h>

#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
#include <SLIPEncodedSerial.h>
 SLIPEncodedSerial SLIPSerial(Serial1);
#endif


void setup() {
  //begin SLIPSerial just like Serial
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
#if ARDUINO >= 100
    while(!Serial)
      ;   // Leonardo bug
#endif

}

void loop(){
    //declare the bundle
    OSCBundle bndl;
    //BOSCBundle's add' returns the OSCMessage so the message's 'add' can be composed together
    bndl.add("/analog/0").add((int32_t)analogRead(0));
    bndl.add("/analog/1").add((int32_t)analogRead(1));
    bndl.add("/digital/5").add((digitalRead(5)==HIGH)?"HIGH":"LOW");


    SLIPSerial.beginPacket();
        bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    bndl.add("/mouse/step").add((int32_t)analogRead(0)).add((int32_t)analogRead(1));
    bndl.add("/units").add("pixels");
    SLIPSerial.beginPacket();
        bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty(); // empty the bundle to free room for a new one

    delay(100);
}

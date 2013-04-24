/*
Serial Call Response

Send responses to calls for information from a remote host
*/

#include <Arduino.h>  
#include <OSCBundle.h>

#include <stdlib.h>

#if !defined(CORE_TEENSY) && !defined(__AVR_ATmega32U4__)
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#else
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(Serial);
#endif

OSCBundle bundleOUT;

/**
 * ANALOG
 * 
 * called when the address matches "/a"
 * 
 * format:
 * /analog/(pin)
 *   /u = analogRead with pullup
 * 
 **/

void routeAnalog(OSCMessage &msg, int addrOffset ){
    int pinMatched;
    pinMatched = msg.match("/0", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(0), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/0").add(analogRead(0));         
     }
    pinMatched = msg.match("/1", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(1), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/1").add(analogRead(1));         
     }
    pinMatched = msg.match("/2", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(2), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/2").add(analogRead(2));         
     }
    pinMatched = msg.match("/3", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(3), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/3").add(analogRead(3));         
     }
    pinMatched = msg.match("/4", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(4), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/4").add(analogRead(4));         
     }
    pinMatched = msg.match("/5", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(5), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/5").add(analogRead(5));         
     }
}

/**
 * MAIN METHODS
 * 
 * setup and loop, bundle receiving/sending, initial routing
 */
void setup() {
#ifdef USEETHERNET
    //setup ethernet part
    read_mac();
  Ethernet.begin(mac,ip);
  Udp.begin(inPort);
#else
  SLIPSerial.begin(115200);
    while(!Serial)
    ;   // Leonardo bug
#endif

}

//reads and routes the incoming messages
void loop(){ 
    OSCBundle bundleIN;
   int size;
 
  while(!SLIPSerial.endofPacket())
   if ((size =SLIPSerial.available()) > 0)
    {
       while(size--)
          bundleIN.fill(SLIPSerial.read());
    }
  if(!bundleIN.hasError())
     {
      bundleIN.route("/analog", routeAnalog);      
//send the outgoing response message
      bundleOUT.send(SLIPSerial);
      SLIPSerial.endPacket();
        bundleOUT.empty(); // empty the bundle ready to use for new messages
   }
}









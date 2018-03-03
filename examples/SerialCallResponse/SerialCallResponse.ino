/*
    Serial Call Response
    Send responses to calls for information from a remote host
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
        bundleOUT.add("/analog/0").add((int32_t)analogRead(0));         
    }
    pinMatched = msg.match("/1", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(1), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/1").add((int32_t)analogRead(1));         
    }
    pinMatched = msg.match("/2", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(2), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/2").add((int32_t)analogRead(2));         
    }
    pinMatched = msg.match("/3", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(3), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/3").add((int32_t)analogRead(3));         
    }
    pinMatched = msg.match("/4", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(4), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/4").add((int32_t)analogRead(4));         
    }
    pinMatched = msg.match("/5", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(5), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/5").add((int32_t)analogRead(5));         
    }
}

/**
 * MAIN METHODS
 * 
 * setup and loop, bundle receiving/sending, initial routing
 */
void setup() {
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
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
      SLIPSerial.beginPacket();
        bundleOUT.send(SLIPSerial);
      SLIPSerial.endPacket();
      bundleOUT.empty(); // empty the bundle ready to use for new messages
    }
}









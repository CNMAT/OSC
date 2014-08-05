/*
* Set the LED according to incoming OSC control
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

void LEDcontrol(OSCMessage &msg)
{
    if (msg.isInt(0))
    {
         pinMode(LED_BUILTIN, OUTPUT);
         digitalWrite(LED_BUILTIN, (msg.getInt(0) > 0)? HIGH: LOW);
    }
    else if(msg.isString(0))
    {
         int length=msg.getDataLength(0);
         if(length<5)
         {
           char str[length];
           msg.getString(0,str,length);
           if((strcmp("on", str)==0)|| (strcmp("On",str)==0))
           {
                pinMode(LED_BUILTIN, OUTPUT); 
                digitalWrite(LED_BUILTIN, HIGH);
           }
           else if((strcmp("Of", str)==0)|| (strcmp("off",str)==0))
           {
                pinMode(LED_BUILTIN, OUTPUT); 
                digitalWrite(LED_BUILTIN, LOW);
           }
         }
    }

}


void setup() {
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
#if ARDUINO >= 100
    while(!Serial)
      ;   // Leonardo bug
#endif

}
//reads and dispatches the incoming message
void loop(){ 
  OSCBundle bundleIN;
  int size;

  while(!SLIPSerial.endofPacket())
    if( (size =SLIPSerial.available()) > 0)
    {
       while(size--)
          bundleIN.fill(SLIPSerial.read());
     }
  
  if(!bundleIN.hasError())
   bundleIN.dispatch("/led", LEDcontrol);

}



/*
* Set the built-in LED according to incoming OSC control
* 
*/
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <SoftPWM.h>


#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
#include <SLIPEncodedSerial.h>
 SLIPEncodedSerial SLIPSerial(Serial);
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
      else  if (msg.isFloat(0))
      {
        //test if that pin is a PWM
        if (digitalPinHasPWM(LED_BUILTIN)) 
        {
          pinMode(LED_BUILTIN, OUTPUT);
          analogWrite(LED_BUILTIN, (int)(msg.getFloat(0)*127.0f));
        }
        else
         SoftPWMSet(LED_BUILTIN, (int)(msg.getFloat(0)*127.0f));
      } 
   
}

void setup() {
  SLIPSerial.begin(9600);

    SoftPWMBegin();

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



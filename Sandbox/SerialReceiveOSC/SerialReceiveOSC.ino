/*
* Set the LED according to incoming OSC control
* 
*/
#include <OSCBundle.h>
#include <SoftPWM.h>

#include <stdlib.h>


#if !defined(CORE_TEENSY) && !defined(__AVR_ATmega32U4__)
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#else
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(Serial);
#endif


void LEDcontrol(OSCMessage &msg)
{
 // I had to add this to make it work on Leonardo: static const int LEDBUILTIN=13;
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
        if (digitalPinHasPWM(LED_BUILTIN)) // missingn on leonardo
        {
          pinMode(LED_BUILTIN, OUTPUT);
          analogWrite(LED_BUILTIN, (int)(msg.getFloat(0)*127));
        }
        else
         SoftPWMSet(LED_BUILTIN, (int)(msg.getFloat(0)*127));
      } 
   
}





void setup() {
  SLIPSerial.begin(115200);

    SoftPWMBegin();

  while(!Serial)
    ;   // Leonardo bug

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



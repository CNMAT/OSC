/*
* Control a servo according to incoming OSC control
* 
*/
#include <OSCBundle.h>
#include <OSCBoards.h>
#include <Servo.h>

#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
 SLIPEncodedSerial SLIPSerial(Serial); // Change to Serial1 or Serial2 etc. for boards with multiple serial ports that don’t have Serial
#endif


Servo myservo;

void servoControl(OSCMessage &msg)
{
      if (msg.isInt(0))
      {
             myservo.write(msg.getInt(0));
       }
#ifdef TEMPoraray
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
#endif
   
}

void setup() {
  SLIPSerial.begin(9600);

    myservo.attach(13);
    myservo.write(90);
    

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
   bundleIN.dispatch("/servo", servoControl);

}



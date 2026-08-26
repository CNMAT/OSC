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
          {
            // read() returns int and -1 on underrun; passing that straight
            // to fill() narrowed it to an ordinary 0xFF data byte
            int c = SLIPSerial.read();
            if (c >= 0) bundleIN.fill((uint8_t)c);
          }
     }
  
  if(!bundleIN.hasError())
   bundleIN.dispatch("/servo", servoControl);

}



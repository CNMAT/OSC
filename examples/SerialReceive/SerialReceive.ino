/*
* Set the LED according to incoming OSC control on /s/l: an int (0 = off, else on)
* or a string, on or off
*/
#include <OSCBundle.h>
#include <OSCBoards.h>

// No fallback pin here on purpose. OSCBoards.h defines BOARD_HAS_LED only when
// the core actually names an on-board LED, and guessing one is not harmless:
// pin 13 is SDA on the M5Dial and MISO on the T-Display-S3, and GPIO 2 is a
// strapping pin on several ESP32 parts. Where no LED is named this sketch
// drives nothing; -DLED_BUILTIN=<pin> points it at one and, because
// BOARD_HAS_LED is derived from defined(LED_BUILTIN), switches this back on.

#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
 SLIPEncodedSerial SLIPSerial(Serial); // Change to Serial1 or Serial2 etc. for boards with multiple serial ports that don’t have Serial
#endif


void LEDcontrol(OSCMessage &msg)
{
#ifdef BOARD_HAS_LED
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
#else
    (void)msg;          // no on-board LED named for this board
#endif
}


void setup() {
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
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
   bundleIN.dispatch("/s/l", LEDcontrol);

}




#include <OSCBundle.h>
#include <OSCBoards.h>

#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
 SLIPEncodedSerial SLIPSerial(Serial); // Change to Serial1 or Serial2 etc. for boards with multiple serial ports that don’t have Serial
#endif

//outgoing messages

OSCBundle bundleOUT;

//converts the pin to an osc address
char * numToOSCAddress( int pin){
    static char s[10];
    int i = 9;
	
    s[i--]= '\0';
	do
    {
		s[i] = "0123456789"[pin % 10];
                --i;
                pin /= 10;
    }
    while(pin && i);
    s[i] = '/';
    return &s[i];
}

/**
 * ROUTES
 * 
 * these are where the routing functions go
 * 
 */

/**
 * DIGITAL
 * 
 * called when address matched "/d"
 * expected format:
 * /d/(pin)
 *   /u = digitalRead with pullup
 *   (no value) = digitalRead without pullup
 *   (value) = digital write on that pin
 * 
 */

void routeDigital(OSCMessage &msg, int addrOffset ){
  //match input or output
  for(byte pin = 0; pin < NUM_DIGITAL_PINS; pin++){
    //match against the pin number strings
    int pinMatched = msg.match(numToOSCAddress(pin), addrOffset);
    if(pinMatched){
      //if it has an int, then it's a digital write
      if (msg.isInt(0)){
        pinMode(pin, OUTPUT);
        digitalWrite(pin, (msg.getInt(0)>0) ? HIGH:LOW);
       } 
      else if(msg.isFloat(0)){
        analogWrite(pin, (int)(msg.getFloat(0)*255.0f));
      }
     
      //otherwise it's an digital read
      //with a pullup?
      else if (msg.fullMatch("/u", pinMatched+addrOffset)){
        //set the pullup
        pinMode(pin, INPUT_PULLUP);
        //setup the output address which should be /d/(pin)/u
        char outputAddress[9];
        strcpy(outputAddress, "/d");
        strcat(outputAddress, numToOSCAddress(pin));
        strcat(outputAddress,"/u");
        //do the digital read and send the results
        bundleOUT.add(outputAddress).add(digitalRead(pin));       
      } //else without a pullup   
      else {
        //set the pinmode
        pinMode(pin, INPUT);
        //setup the output address which should be /d/(pin)
        char outputAddress[6];
        strcpy(outputAddress, "/d");
        strcat(outputAddress, numToOSCAddress(pin));
        //do the digital read and send the results
        bundleOUT.add(outputAddress).add(digitalRead(pin));         
      }
    }
  }
}

/**
 * ANALOG
 * 
 * called when the address matches "/a"
 * 
 * format:
 * /a/(pin)
 *   /u = analogRead with pullup
 *   (no value) = analogRead without pullup
 *   (digital value) = digital write on that pin
 *    (float value) = analogWrite on that pin
 * 
 **/

void routeAnalog(OSCMessage &msg, int addrOffset ){
  //iterate through all the analog pins
  for(byte pin = 0; pin < NUM_ANALOG_INPUTS; pin++){
    //match against the pin number strings
    int pinMatched = msg.match(numToOSCAddress(pin), addrOffset);
    if(pinMatched){
      //if it has an int, then it's a digital write
      if (msg.isInt(0)){
        pinMode(analogInputToDigitalPin(pin), OUTPUT);
        digitalWrite(analogInputToDigitalPin(pin), (msg.getInt(0) > 0)? HIGH: LOW);
      } 
      else if(msg.isFloat(0)){
        analogWrite(pin, (int)(msg.getFloat(0)*255.0f));
      }
#ifdef BOARD_HAS_ANALOG_PULLUP
    //with a pullup?
      else if (msg.fullMatch("/u", pinMatched+addrOffset)){
        //set the pullup

        pinMode(analogInputToDigitalPin(pin), INPUT_PULLUP);

        //setup the output address which should be /a/(pin)/u
        char outputAddress[9];
        strcpy(outputAddress, "/a");
        strcat(outputAddress, numToOSCAddress(pin));
        strcat(outputAddress,"/u");
       //do the analog read and send the results
        bundleOUT.add(outputAddress).add((intOSC_t)analogRead(pin));       
      } //else without a pullup 
#endif

      else {
         //otherwise it's an analog read
       //set the pinmode

        pinMode(analogInputToDigitalPin(pin), INPUT);
        //setup the output address which should be /a/(pin)
        char outputAddress[6];
        strcpy(outputAddress, "/a");
        strcat(outputAddress, numToOSCAddress(pin));
        //do the analog read and send the results
        bundleOUT.add(outputAddress).add((intOSC_t)analogRead(pin));         
      }
    }
  }
}

#ifdef BOARD_HAS_TONE
/**
 * TONE
 * 
 * square wave output "/tone"
 * 
 * format:
 * /tone/pin
 *   
 *   (digital value) (float value) = frequency in Hz
 *   (no value) disable tone
 * 
 **/

void routeTone(OSCMessage &msg, int addrOffset ){
  //iterate through all the analog pins
  for(byte pin = 0; pin < NUM_DIGITAL_PINS; pin++){
    //match against the pin number strings
    int pinMatched = msg.match(numToOSCAddress(pin), addrOffset);
    if(pinMatched){
      unsigned int frequency = 0;
      //if it has an int, then it's an integers frequency in Hz
      if (msg.isInt(0)){        
        frequency = msg.getInt(0);
      } //otherwise it's a floating point frequency in Hz
      else if(msg.isFloat(0)){
        frequency = msg.getFloat(0);
      }
      else
        noTone(pin);
      if(frequency>0)
      {
         if(msg.isInt(1))
           tone(pin, frequency, msg.getInt(1));
         else
           tone(pin, frequency);
      }
    }
  }
}
#endif


#ifdef  BOARD_HAS_CAPACITANCE_SENSING
#if  defined(__MKL26Z64__) 
// teensy 3.0LC
#define NTPINS 11
const int cpins[NTPINS] = {22,23,19,18,17,16,15,0,1,3,4 }; 
#elif defined(__MK66FX1M0__)
// teensy 3.6
#define NTPINS 12
const int cpins[NTPINS] = {0,1,14,15,16,17,18,19,22,23,29,30 }; 
#else 
//Teensy 3.1 3.2
#define NTPINS 12
const int cpins[NTPINS] = {22,23,19,18,17,16,15,0,1,25,32, 33 }; 
#endif

void routeTouch(OSCMessage &msg, int addrOffset )
{
    for(int i=0;i<NTPINS;++i)
    {
        const char *name = numToOSCAddress(cpins[i]);
        int pinMatched = msg.match(name, addrOffset);
        if(pinMatched)
        {
           char outputAddress[9];
            strcpy(outputAddress, "/c");
            strcat(outputAddress, name);
            bundleOUT.add(outputAddress).add(touchRead(cpins[i]));

        }
    }
}
#endif

/**
 * SYSTEM MESSAGES
 * 
 * expected format:
 * /s
 *   /m = microseconds
 *   /d = number of digital pins
 *   /a = number of analog pins
 *  /l integer = set the led
 *  /t = temperature
 *  /s = power supply voltage
 */
// 
void routeSystem(OSCMessage &msg, int addrOffset ){
#ifdef BOARD_HAS_DIE_TEMPERATURE_SENSOR
  if (msg.fullMatch("/t", addrOffset)){
    bundleOUT.add("/s/t").add(getTemperature());
  }
#endif 
#ifdef BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
  if (msg.fullMatch("/s", addrOffset)){
    bundleOUT.add("/s/s").add(getSupplyVoltage());
  }
#endif
  if (msg.fullMatch("/m", addrOffset)){
    bundleOUT.add("/s/m").add((intOSC_t)micros());
  }
  if (msg.fullMatch("/d", addrOffset)){
    bundleOUT.add("/s/d").add(NUM_DIGITAL_PINS);
  }
  if (msg.fullMatch("/a", addrOffset)){
    bundleOUT.add("/s/a").add(NUM_ANALOG_INPUTS);
  }
  if (msg.fullMatch("/l", addrOffset)){
    if (msg.isInt(0)){
             pinMode(LED_BUILTIN, OUTPUT);
      int i = msg.getInt(0);
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, (i > 0)? HIGH: LOW);
        bundleOUT.add("/s/l").add(i);
      }
  }
}

/**
 * MAIN METHODS
 * 
 * setup and loop, bundle receiving/sending, initial routing
 */


void setup() {
    SLIPSerial.begin(115200);   // set this as high as you can reliably run on your platform
#if ARDUINO >= 100
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
      bundleIN.route("/s", routeSystem);
      bundleIN.route("/a", routeAnalog);
      bundleIN.route("/d", routeDigital);
#ifdef BOARD_HAS_TONE
      bundleIN.route("/tone", routeTone);
#endif
#ifdef    BOARD_HAS_CAPACITANCE_SENSING
      bundleIN.route("/c", routeTouch);
#endif
    }
    bundleIN.empty();

 
//send the outgoing message
  SLIPSerial.beginPacket();
    bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();

}








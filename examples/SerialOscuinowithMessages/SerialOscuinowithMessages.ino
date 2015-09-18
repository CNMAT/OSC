
#include <OSCBundle.h>
#include <OSCBoards.h>

#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
#include <SLIPEncodedSerial.h>
 SLIPEncodedSerial SLIPSerial(Serial1);
#endif


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
      }      //otherwise it's an analog read
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
        { 
            OSCMessage  msgOut(outputAddress); msgOut.add(digitalRead(pin)); 
            SLIPSerial.beginPacket(); msgOut.send(SLIPSerial); SLIPSerial.endPacket(); 
        } 
      } //else without a pullup   
      else {
        //set the pinmode
        pinMode(pin, INPUT);
        //setup the output address which should be /d/(pin)
        char outputAddress[6];
        strcpy(outputAddress, "/d");
        strcat(outputAddress, numToOSCAddress(pin));
        //do the digital read and send the results
        {
            OSCMessage  msgOut(outputAddress); msgOut.add(digitalRead(pin));
            SLIPSerial.beginPacket(); msgOut.send(SLIPSerial); SLIPSerial.endPacket();
        }  
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
      } //otherwise it's an analog read
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
        {
            OSCMessage  msgOut(outputAddress); msgOut.add((int32_t)analogRead(pin));
            SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket();
        }  
      } //else without a pullup 
#endif
      else {
        //set the pinmode
        // This fails on Arduino 1.04 on Leanardo, I added this to fix it: #define analogInputToDigitalPin(p)  (p+18)

        pinMode(analogInputToDigitalPin(pin), INPUT);
        //setup the output address which should be /a/(pin)
        char outputAddress[6];
        strcpy(outputAddress, "/a");
        strcat(outputAddress, numToOSCAddress(pin));
        //do the analog read and send the results
        {
            OSCMessage  msgOut(outputAddress); msgOut.add((int32_t)analogRead(pin));
            SLIPSerial.beginPacket(); msgOut.send(SLIPSerial); SLIPSerial.endPacket();
        }
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
 *   (digital value) (float value) = freqency in Hz
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
#define NTPINS 11
const int cpins[NTPINS] = {22,23,19,18,17,16,15,0,1,3,4 }; 
#else
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
            {
                OSCMessage  msgOut(outputAddress); msgOut.add(touchRead(cpins[i]));
                SLIPSerial.beginPacket(); msgOut.send(SLIPSerial); SLIPSerial.endPacket();
            }

        }
    }
}
#endif

#ifdef BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
#if defined(__MK20DX128__) || defined(__MK20DX256__)|| defined(__MKL26Z64__) 
float getSupplyVoltage()
{
  int val = analogRead(39); 
  return val>0? (1.20f*1023/val):0.0f; 
}

#else
// power supply measurement on some Arduinos 
float getSupplyVoltage(){
    // powersupply
    int result;
    // Read 1.1V reference against AVcc
#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #elif  defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)   
    ADMUX = 0x40| _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1) ;
    ADCSRB =  0;
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  
    delayMicroseconds(300); // wait for Vref to settle
    ADCSRA |= _BV(ADSC); // Convert
    while (bit_is_set(ADCSRA,ADSC));
	result = ADCL;
    result |= ADCH<<8;
      
    float supplyvoltage = 1.1264f *1023 / result;
    return supplyvoltage;	
}
#endif

#endif

#ifdef BOARD_HAS_DIE_TEMPERATURE_SENSOR


#if defined(__MK20DX128__) || defined(__MK20DX256__)|| defined(__MKL26Z64__) 
float getTemperature()
{
        analogReference(INTERNAL);
        delay(1);
        int val = analogRead(38); // seems to be flakey
        analogReference(DEFAULT);

        return val; //need to compute something here to get to degrees C
}
#else
// temperature
float getTemperature(){	
	int result;
	
#if defined(__AVR_ATmega32U4__)
	ADMUX =  _BV(REFS1) | _BV(REFS0) | _BV(MUX2) | _BV(MUX1) | _BV(MUX0);
	ADCSRB =  _BV(MUX5);
#else
	ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX3);
#endif	
	delayMicroseconds(200); // wait for Vref to settle
	ADCSRA |= _BV(ADSC); // Convert
	while (bit_is_set(ADCSRA,ADSC));
	result = ADCL;
	result |= ADCH<<8;

	analogReference(DEFAULT);
	
	return  result/1023.0f;
}
#endif  

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
    { OSCMessage  msgOut("/s/t"); msgOut.add(getTemperature());         SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
#endif 
#ifdef BOARD_HAS_DIE_POWER_SUPPLY_MEASUREMENT
  if (msg.fullMatch("/s", addrOffset)){
    { OSCMessage  msgOut("/s/s"); msgOut.add(getSupplyVoltage());         SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
#endif
  if (msg.fullMatch("/m", addrOffset)){
    { OSCMessage  msgOut("/s/m"); msgOut.add((int32_t)micros());         SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
  if (msg.fullMatch("/d", addrOffset)){
    { OSCMessage  msgOut("/s/d"); msgOut.add(NUM_DIGITAL_PINS);         SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
  if (msg.fullMatch("/a", addrOffset)){
    { OSCMessage  msgOut("/s/a"); msgOut.add(NUM_ANALOG_INPUTS);         SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
  if (msg.fullMatch("/l", addrOffset)){

    if (msg.isInt(0)){
         pinMode(LED_BUILTIN, OUTPUT);
        int i = msg.getInt(0);
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, (i > 0)? HIGH: LOW);
        { OSCMessage  msgOut("/s/l"); msgOut.add(i);         SLIPSerial.beginPacket();msgOut.send(SLIPSerial); SLIPSerial.endPacket(); }
        }
  }
}

/**
 * MAIN METHODS
 * 
 * setup and loop, bundle receiving/sending, initial routing
 */
void setup() {
    SLIPSerial.begin(9600);   // set this as high as you can reliably run on your platform
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
#ifdef BOARD_HAS_CAPACITANCE_SENSING
    bundleIN.route("/c", routeTouch);
#endif
    }
}

















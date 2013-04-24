
#include <OSCBundle.h>

#define xUSEETHERNET
// serial or Ethernet

#ifdef USEETHERNET

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    

//UDP communication
EthernetUDP Udp;
//the Arduino's IP
// pull thise from board? xxx
IPAddress ip(128, 32, 122, 252);

//port numbers
const unsigned int inPort = 8888;
//const unsigned int outPort = 9999;
//everything on the network needs a unique MAC 
#if defined(__MK20DX128__)
// Teensy 3 has MAC burned in
static byte mac[6];
void read(uint8_t word, uint8_t *mac, uint8_t offset) {
  FTFL_FCCOB0 = 0x41;             // Selects the READONCE command
  FTFL_FCCOB1 = word;             // read the given word of read once area

  // launch command and wait until complete
  FTFL_FSTAT = FTFL_FSTAT_CCIF;
  while(!(FTFL_FSTAT & FTFL_FSTAT_CCIF));

  *(mac+offset) =   FTFL_FCCOB5;       // collect only the top three bytes,
  *(mac+offset+1) = FTFL_FCCOB6;       // in the right orientation (big endian).
  *(mac+offset+2) = FTFL_FCCOB7;       // Skip FTFL_FCCOB4 as it's always 0.
}
void read_mac() {
  read(0xe,mac,0);
  read(0xf,mac,3);
}
#else
void read_mac() {}
 byte mac[] = {  
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // you can find this written on the board of some Arduino Ethernets or shields
#endif
  
#else
#if defined(CORE_TEENSY)|| defined(__AVR_ATmega32U4__)
#include <SLIPEncodedUSBSerial.h>
#else
 #include <SLIPEncodedSerial.h>
#endif
#endif


#include <Servo.h> 
#include <Stepper.h>
#include <SoftPWM.h>

//outgoing messages

OSCBundle bundleOUT;

#include <stdlib.h>


//converts the pin to an osc address
const char * numToOSCAddress( int pin){
  static char s[10] ="/";
   
   itoa(pin, s+1, 10);
   return s;
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
      } //otherwise it's an digital read
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
      } //otherwise it's an analog read
      else if(msg.isFloat(0)){
        analogWrite(pin, (int)(msg.getFloat(0)*255.0f));
      }
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
        bundleOUT.add(outputAddress).add(analogRead(pin));       
      } //else without a pullup   
      else {
        //set the pinmode
        // This fails on Arduino 1.04 on Leanardo, I added this to fix it: #define analogInputToDigitalPin(p)  (p+18)

        pinMode(analogInputToDigitalPin(pin), INPUT);
        //setup the output address which should be /a/(pin)
        char outputAddress[6];
        strcpy(outputAddress, "/a");
        strcat(outputAddress, numToOSCAddress(pin));
        //do the analog read and send the results
        bundleOUT.add(outputAddress).add(analogRead(pin));         
      }
    }
  }
}


/**
 * TONE
 * 
 * square wave output "/tone"
 * 
 * format:
 * /a/(pin)
 *   /u = analogRead with pullup
 *   (no value) = analogRead without pullup
 *   (digital value) = digital write on that pin
 *    (float value) = analogWrite on that pin
 * 
 **/

void routeTone(OSCMessage &msg, int addrOffset ){
  //iterate through all the analog pins
  for(byte pin = 0; pin < NUM_DIGITAL_PINS; pin++){
    //match against the pin number strings
    int pinMatched = msg.match(numToOSCAddress(pin), addrOffset);
    if(pinMatched){
      unsigned int frequency = 0;
      //if it has an int, then it's a digital write
      if (msg.isInt(0)){        
        frequency = msg.getInt(0);
      } //otherwise it's an analog read
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
#if defined (__MK20DX128__)
#define TOUCHSUPPORT
#endif

#ifdef TOUCHSUPPORT
#define NTPINS 12
const int cpins[NTPINS] = {22,23,19,18,17,16,15,0,1,25,32, 33 }; 
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

/*
 * PWM
 * 
 * called when address matched "/p"
 * expected format:
 * /p/(pin)
 *     (value) = analogWrite with that value
 *     /e = end the SoftPWM
 */

void routePWM(OSCMessage &msg, int addrOffset ){
  //iterate over all the pins
  for(byte pin = 0; pin < NUM_DIGITAL_PINS; pin++){
    //match against the pin number strings
    int pinMatched = msg.match(numToOSCAddress(pin), addrOffset);
    if(pinMatched){
      //tests if the pin is hardware PWM
      if (msg.fullMatch("/h", addrOffset+pinMatched)){
        char outputAddress[9];
        strcpy(outputAddress, "/p");
        strcat(outputAddress, numToOSCAddress(pin));
        strcat(outputAddress,"/h");
        //do the analog read and send the results
        bundleOUT.add(outputAddress).add(digitalPinHasPWM(pin));   
      }
      //test if that pin is a PWM
      if (digitalPinHasPWM(pin)){
        //if it is, then analog write
        if (msg.isInt(0)){
          pinMode(pin, OUTPUT);
          analogWrite(pin, msg.getInt(0));
        } 
      } //if not, do a software pwm
      else {
        //test if it is a command to end the software pwm
        if (msg.fullMatch("/e", pinMatched+addrOffset)){
          //set it to 0 first 
          //(this seems to keep it from turning back on)
          SoftPWMSet(pin, 0);
          //then remove the timer from the pin
          SoftPWMEnd(pin);
        } //otherwise it's a softpwm write command
        else if (msg.isInt(0)){
          SoftPWMSet(pin, msg.getInt(0));
        } 
      }
    }
  }
}

/**
 * SERVO
 * 
 * called when address matched "/v"
 * expected format:
 * /v/(servo)
 *     (value) = write that value
 *     /a (pin) = attach the servo to a pin
 *     /e = detach the servo from the pin
 */

//this is the number of servos
#define NUM_SERVOS 8

Servo servo1;
Servo servo2;
Servo servo3;
Servo servo4;
Servo servo5;
Servo servo6;
Servo servo7;
Servo servo8;
Servo servos[] = {
  servo1, servo2, servo3, servo4, servo5, servo6, servo7, servo8};

void routeServo(OSCMessage &msg, int addrOffset ){
  //iterate over all the pins
  for(byte s = 0; s < NUM_SERVOS; s++){
    int servoMatched = msg.match(numToOSCAddress(s), addrOffset);
    if(servoMatched){
      //the servo being referenced:
      Servo servo = servos[s];
      //if it matches "/e" - detach the servo from the pin
      if (msg.fullMatch("/e", servoMatched+addrOffset)){
        //if it's attached, detach it
        if (servo.attached()){
          servo.detach();
        }
      } //if it matches "/a" - attach the servo to the pin
      else if (msg.fullMatch("/a", servoMatched+addrOffset)){
        if (msg.isInt(0)){
          servo.attach(msg.getInt(0)); 
        }
      } //if it didn't match either of those, it's a servo write
      else if (msg.isInt(0)){
        if (msg.isInt(0)){
          servo.write(msg.getInt(0)); 
        }
      }
    }
  }
}

/**
 * STEPPER
 * 
 * called when address matched "/t"
 * expected format:
 * /t/[stepper]
 *    (value) = step to that value
 *    /s (value) = set the speed
 * 
 * there are four stepper motors available. assign their pins before using them
 */

#define NUM_STEPPERS 4

Stepper stepper0 = Stepper(100, 0, 1);
Stepper stepper1 = Stepper(100, 0, 1);
Stepper stepper2 = Stepper(100, 0, 1);
Stepper stepper3 = Stepper(100, 0, 1);
Stepper stepperArray[] = {
  stepper0, stepper1, stepper2, stepper3};

void routeStepper(OSCMessage &msg, int addrOffset ){
  for(byte s = 0; s < NUM_STEPPERS; s++){
    int stepperMatched = msg.match(numToOSCAddress(s), addrOffset);
    if(stepperMatched){
      //the matched stepper
      Stepper stepper = stepperArray[s];
      if (msg.fullMatch("/a", stepperMatched+addrOffset)){
        if (msg.size()==5){
          int steps = msg.getInt(0);
          int pin1 = msg.getInt(1);
          int pin2 = msg.getInt(2);
          int pin3 = msg.getInt(3);
          int pin4 = msg.getInt(4);
          stepper = Stepper(steps, pin1, pin2, pin3, pin4);
          //place it in the stepper Array
          stepperArray[s] = stepper;
        } 
        else if (msg.size()==3){
          int steps = msg.getInt(1);
          int pin1 = msg.getInt(2);
          int pin2 = msg.getInt(3);
          stepper = Stepper(steps, pin1, pin2);
          stepperArray[s] = stepper;
        }
      } 
      else if (msg.fullMatch("/s", stepperMatched+addrOffset)){
        if (msg.isInt(0)){
          stepper.setSpeed(msg.getInt(0)); 
        }
      }
      //if it didn't match either of those, it's a stepper write
      else if (msg.isInt(0)){
        if (msg.isInt(0)){
          stepper.step(msg.getInt(0)); 
        }
      }

    }
  }
}

#if !defined(__AVR_ATmega8__) && !defined(__MK20DX128__)

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
  #elif  defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)    || defined(__AVR_ATmega1280__) 
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

// temperature
float getTemperature(){	
	int result;
#if defined(__AVR_ATmega32U4__) ||    (!defined(__AVR_ATmega1280__) && !defined(__AVR_ATmega8__) && !defined(__AVR_AT90USB646__) && !defined(__AVR_AT90USB1286__) &&! defined(__AVR_ATmega2560__))
	
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
#else
      return 0.0f;
#endif	
}
#endif  

#if defined(__MK20DX128__)
float getTemperature()
{
        analogReference(INTERNAL);
        delay(1);
    int val = analogRead(38); // seems to be flakey
  analogReference(DEFAULT);

  return val; //need to compute something here to get to degrees C
}
float getSupplyVoltage()
{
  int val = analogRead(39); 
  return val>0? (1.20f*1023/val):0.0f; 
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
  if (msg.fullMatch("/t", addrOffset)){
    bundleOUT.add("/s/t").add(getTemperature());
  }
  if (msg.fullMatch("/s", addrOffset)){
    bundleOUT.add("/s/s").add(getSupplyVoltage());
  }
  if (msg.fullMatch("/m", addrOffset)){
    bundleOUT.add("/s/m").add((int32_t)micros());
  }
  if (msg.fullMatch("/d", addrOffset)){
    bundleOUT.add("/s/d").add(NUM_DIGITAL_PINS);
  }
  if (msg.fullMatch("/a", addrOffset)){
    bundleOUT.add("/s/a").add(NUM_ANALOG_INPUTS);
  }
  if (msg.fullMatch("/l", addrOffset)){
 // I had to add this to make it work on Leonardo: static const int LEDBUILTIN=13;

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
 #ifndef USEETHERNET

#if defined(CORE_TEENSY)|| defined(__AVR_ATmega32U4__)
SLIPEncodedUSBSerial SLIPSerial(Serial);
#else
SLIPEncodedSerial SLIPSerial(Serial);
#endif
#endif



extern void read_mac();
void setup() {
  SoftPWMBegin();
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

#ifdef  USEETHERNET
 
 if( (size = Udp.parsePacket())>0)
 {
     while(size--)
       bundleIN.fill(Udp.read());

#else
  while(!SLIPSerial.endofPacket())
   if ((size =SLIPSerial.available()) > 0)
    {
       while(size--)
          bundleIN.fill(SLIPSerial.read());
     }
   {
#endif
if(!bundleIN.hasError())
 {
    bundleIN.route("/s", routeSystem);
    bundleIN.route("/a", routeAnalog);
    bundleIN.route("/d", routeDigital);
    bundleIN.route("/tone", routeTone);
#ifdef TOUCHSUPPORT
    bundleIN.route("/c", routeTouch);
#endif

    bundleIN.route("/p", routePWM);
    bundleIN.route("/v", routeServo);
    bundleIN.route("/t", routeStepper);

}
 

#ifdef  USEETHERNET
Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
    bundleOUT.send(Udp);
    Udp.endPacket();
#else
//send the outgoing message
  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
#endif 
    bundleOUT.empty();
   }
}









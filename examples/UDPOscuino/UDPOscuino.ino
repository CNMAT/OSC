
// UDP OSCuino
// system, analog and digital pin control and monitoring for Arduino
// Yotam Mann and Adrian Freed


#include <Ethernet.h>  
#include <EthernetUdp.h> 
#include <SPI.h>    
#include <OSCBundle.h>
#include <OSCBoards.h>

EthernetUDP Udp;

//the Arduino's IP
IPAddress ip(128, 32, 122, 252);

//port numbers
const unsigned int inPort = 8888;
const unsigned int outPort = 9999;

//everything on the network needs a unique MAC 
#if defined(__MK20DX128__) || defined(__MK20DX256__) || defined(__MKL26Z64__) || defined(__MK66FX1M0__)
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
        strcat(outputAddress,"/u");
        //do the analog read and send the results
        bundleOUT.add(outputAddress).add((int32_t)analogRead(pin));
      } //else without a pullup   
#endif
      else {
        //set the pinmode

        pinMode(analogInputToDigitalPin(pin), INPUT);
        //setup the output address which should be /a/(pin)
        char outputAddress[6];
        strcpy(outputAddress, "/a");
        strcat(outputAddress, numToOSCAddress(pin));
        //do the analog read and send the results
        bundleOUT.add(outputAddress).add((int32_t)analogRead(pin));
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

#ifdef BOARD_HAS_CAPACITANCE_SENSING

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
    bundleOUT.add("/s/m").add((int32_t)micros());
  }
  if (msg.fullMatch("/d", addrOffset)){
    bundleOUT.add("/s/d").add(NUM_DIGITAL_PINS);
  }
  if (msg.fullMatch("/a", addrOffset)){
    bundleOUT.add("/s/a").add(NUM_ANALOG_INPUTS);
  }
  
// this is disabled because many ethernet boards use the
// LED pin for ethernet pin 13
#if LED_BUILTIN!=13

  if (msg.fullMatch("/l", addrOffset)){

    if (msg.isInt(0)){
             pinMode(LED_BUILTIN, OUTPUT);
      int i = msg.getInt(0);
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, (i > 0)? HIGH: LOW);
        bundleOUT.add("/s/l").add(i);
      }
  }
#endif
}

/**
 * MAIN METHODS
 * 
 * setup and loop, bundle receiving/sending, initial routing
 */
void setup() {
  //setup ethernet port
  read_mac();
  Ethernet.begin(mac,ip);
  Udp.begin(inPort);

}

//reads and routes the incoming messages
void loop(){ 
    OSCBundle bundleIN;
   int size;
 
   if( (size = Udp.parsePacket())>0)
   {
//       unsigned int outPort = Udp.remotePort();

     while(size--)
       bundleIN.fill(Udp.read());

    if(!bundleIN.hasError())
     {
        bundleIN.route("/s", routeSystem);
        bundleIN.route("/a", routeAnalog);
        bundleIN.route("/d", routeDigital);
#ifdef BOARD_HAS_TONE
        bundleIN.route("/tone", routeTone);
#endif

#ifdef TOUCHSUPPORT
        bundleIN.route("/c", routeTouch);
#endif

    }
    // send the response bundle back to where the request came from
    Udp.beginPacket(Udp.remoteIP(),outPort); 
    bundleOUT.send(Udp);
    Udp.endPacket();
    bundleOUT.empty(); // empty the bundle ready to use for new messages
   }
}









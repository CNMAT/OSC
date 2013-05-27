/*
UDP Call Response

Send responses to calls for information from a remote host
*/

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    
#include <OSCBundle.h>
#include <OSCBoards.h>

//UDP communication
EthernetUDP Udp;

//the Arduino's IP
IPAddress ip(128, 32, 122, 252);

//port numbers
const unsigned int inPort = 8888;
const unsigned int outPort = 9999;


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
 * ANALOG
 * 
 * called when the address matches "/a"
 * 
 * format:
 * /analog/(pin)
 *   /u = analogRead with pullup
 * 
 **/

void routeAnalog(OSCMessage &msg, int addrOffset ){
    int pinMatched;
    pinMatched = msg.match("/0", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(0), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/0").add((int32_t)analogRead(0));         
     }
    pinMatched = msg.match("/1", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(1), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/1").add((int32_t)analogRead(1));         
     }
    pinMatched = msg.match("/2", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(2), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/2").add((int32_t)analogRead(2));         
     }
    pinMatched = msg.match("/3", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(3), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/3").add((int32_t)analogRead(3));         
     }
    pinMatched = msg.match("/4", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(4), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/4").add((int32_t)analogRead(4));         
     }
    pinMatched = msg.match("/5", addrOffset);
    if(pinMatched){   
      if (msg.fullMatch("/u", pinMatched+addrOffset)) pinMode(analogInputToDigitalPin(5), INPUT_PULLUP); //set the pullup
        //do the analog read and send the results
        bundleOUT.add("/analog/5").add((int32_t)analogRead(5));         
     }
}

/**
 * MAIN METHODS
 * 
 * setup and loop, bundle receiving/sending, initial routing
 */
void setup() {
    //setup ethernet part
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
//         unsigned int outPort = Udp.remotePort();

         while(size--)
           bundleIN.fill(Udp.read());

        if(!bundleIN.hasError())
              bundleIN.route("/analog", routeAnalog);

        // send the response bundle back to where the request came from
        Udp.beginPacket(Udp.remoteIP(), outPort); 
        bundleOUT.send(Udp);
        Udp.endPacket();
        bundleOUT.empty(); // empty the bundle ready to use for new messages
    }
}









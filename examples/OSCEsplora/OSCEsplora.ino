  
/*
 Bidirectional Esplora OSC communications  using SLIP
 
 Adrian Freed, Jeff Lubow 2013
 
 Includes some examples of common "best practices" for OSC name space and parameter 
 mapping design.

 
*/

#include <Esplora.h>
#include <OSCBundle.h>
//Teensy and Leonardo variants have special USB serial
#include <SLIPEncodedUSBSerial.h>

#if !defined(__AVR_ATmega32U4__)
#error select Arduino Esplora in board menu
#endif

// temperature
float getTemperature(){	
  int result;

  ADMUX =  _BV(REFS1) | _BV(REFS0) | _BV(MUX2) | _BV(MUX1) | _BV(MUX0);
  ADCSRB =  _BV(MUX5);
  delayMicroseconds(200); // wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;

  analogReference(DEFAULT);
  return  result/1023.0f;
}

float getSupplyVoltage(){
  // powersupply
  int result;
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delayMicroseconds(300); // wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC));
  result = ADCL;
  result |= ADCH<<8;

  float supplyvoltage = 1.1264f *1023 / result;
  return supplyvoltage;	
}

// Esplora has  a dinky green led at the top left and a big RGB led at the bottom right
void routeLed(OSCMessage &msg, int addrOffset ){
  if(msg.match("/red", addrOffset)) {
    if (msg.isInt(0))  Esplora.writeRed( (byte)msg.getInt(0)); 
  }
  else 
    if(msg.match("/green", addrOffset)) {
    if (msg.isInt(0))  Esplora.writeGreen( (byte)msg.getInt(0)); 
  }
  else 
    if(msg.match("/blue", addrOffset)) {
    if (msg.isInt(0))  Esplora.writeBlue( (byte)msg.getInt(0)); 
  }
  else 
    if(msg.match("/rgb", addrOffset)) {

    if (msg.isInt(0)&&msg.isInt(1)&&msg.isInt(2))
    {
      Esplora.writeRGB((byte)msg.getInt(0),(byte)msg.getInt(1),(byte)msg.getInt(2));
    }
  }
  else
  {     
    if (msg.isInt(0))
    {
      digitalWrite(13, msg.getInt(0)>0?HIGH:LOW);
    }
  }
}

// Esplora has  a dinky green led at the top left and a big RGB led at the bottom right
void routeOut(OSCMessage &msg, int addrOffset ){
  if(msg.match("/B", addrOffset) || msg.match("/b", addrOffset)) {
    if (msg.isInt(0)) { 
      pinMode(11,OUTPUT);   
      digitalWrite(11, msg.getInt(0)>0?HIGH:LOW);  
    } 
    else    
      pinMode(11,INPUT);   // add pull up logic some day     
  }
  else 
    if(msg.match("/A", addrOffset) ||msg.match("/a", addrOffset)) {
    if (msg.isInt(0)) { 
      pinMode(3,OUTPUT);   
      digitalWrite(3, msg.getInt(0)>0?HIGH:LOW);  
    }          
    else    
      pinMode(3,INPUT);   // add pull up logic some day     
  }
}

/**
 * TONE
 * 
 * square wave output "/tone"
 * 
 * format:
 * /tone 
 *   (no value) = notome
 *  float or int = frequency
 *   optional length of time as an integer in milliseconds afterwards
 * 
 **/

void routeTone(OSCMessage &msg, int addrOffset ){

  unsigned int frequency = 0;
  if (msg.isInt(0)){        
    frequency = msg.getInt(0);
  } 
  else if(msg.isFloat(0)){
    frequency = msg.getFloat(0); // this doesn't work due to problems with double to float conversion?
  }
  else
    Esplora.noTone();
  if(frequency>0)
  {
    if(msg.isInt(1))
      Esplora.tone(frequency, msg.getInt(1));
    else
      Esplora.tone( frequency);
  }
}
const char *released = "released";
const char *pressed = "pressed";

const byte MUX_ADDR_PINS[] = { 
  A0, A1, A2, A3 };
const byte MUX_COM_PIN = A4;

unsigned int myReadChannel(byte channel) {
  digitalWrite(MUX_ADDR_PINS[0], (channel & 1) ? HIGH : LOW);
  digitalWrite(MUX_ADDR_PINS[1], (channel & 2) ? HIGH : LOW);
  digitalWrite(MUX_ADDR_PINS[2], (channel & 4) ? HIGH : LOW);
  digitalWrite(MUX_ADDR_PINS[3], (channel & 8) ? HIGH : LOW);

  return analogRead(MUX_COM_PIN);
}

SLIPEncodedUSBSerial SLIPSerial(Serial);
void setup() {
  SLIPSerial.begin(115200);   // set this as high as you can reliably run on your platform
}

int32_t counter = 0; 
int32_t serialnumber = 2;   //hard coded; beware
int32_t num_components = 3;  //currently break the bundle up into 3 components

void loop(){
  OSCBundle bndl;
  int32_t manifest_count = 1;

  if(!SLIPSerial.available())
  {
    
    // The RAW OSC address space and parameter mappngs try to capture
    // the data at lowest level without calibration or scaling
    // The names are chosen to match what is on the silkscreen of the board where it is found
    #define RAW
    #ifdef RAW  
      SLIPSerial.beginPacket(); 
      bndl.add("/mic").add((int32_t)Esplora.readMicrophone());
      bndl.add("/temp/sensor/celsius").add((int32_t)Esplora.readTemperature(DEGREES_C));
      bndl.add("/temp/sensor/fahrenheit").add((int32_t)Esplora.readTemperature(DEGREES_F));
      bndl.add("/linear/potentiometer").add((int32_t)Esplora.readSlider());
      bndl.add("/light/sensor").add((int32_t)Esplora.readLightSensor());
      bndl.add("/switch/1").add((int32_t)Esplora.readButton(SWITCH_1)); 
      bndl.add("/switch/2").add((int32_t)Esplora.readButton(SWITCH_2)); 
      bndl.add("/switch/3").add((int32_t)Esplora.readButton(SWITCH_3)); 
      bndl.add("/switch/4").add((int32_t)Esplora.readButton(SWITCH_4)); 
      bndl.add("/joystick/X").add((int32_t)Esplora.readJoystickX());    
      bndl.add("/joystick/Y").add((int32_t)Esplora.readJoystickY());      
      bndl.add("/joystick/switch").add((int32_t)Esplora.readJoystickSwitch());  
      bndl.add("/joystick/switch/1").add((int32_t)Esplora.readButton(JOYSTICK_DOWN)); 
      bndl.add("/joystick/switch/2").add((int32_t)Esplora.readButton(JOYSTICK_LEFT)); 
      bndl.add("/joystick/switch/3").add((int32_t)Esplora.readButton(JOYSTICK_UP)); 
      bndl.add("/joystick/switch/4").add((int32_t)Esplora.readButton(JOYSTICK_RIGHT)); 
      bndl.add("/accelerometer/x").add(Esplora.readAccelerometer(X_AXIS)); 
      bndl.add("/accelerometer/y").add(Esplora.readAccelerometer(Y_AXIS)); 
      bndl.add("/accelerometer/z").add(Esplora.readAccelerometer(Z_AXIS)); 
      bndl.send(SLIPSerial); // send the bytes to the SLIP stream
      SLIPSerial.endPacket(); // mark the end of the OSC Packet
      bndl.empty();
    #endif //RAW
    

    // The COOKED OSC address space and parameter mappings 
    // encode data for ease of use and legibility at the host. Unit intervals replace integers
    // The names are chosen to clarify usage rather than adherence to the silkscreen
    // also values are acquired as close together as reasonably possible to increase
    // their usability in sensor fusion contexts, i.e. in this case with the accelerometer

    
    SLIPSerial.beginPacket(); // mark the beginning of the OSC Packet    
    
    //bundle 1
    bndl.add("/acceleration/x").add(Esplora.readAccelerometer(X_AXIS)/512.0f); 
    bndl.add("/acceleration/y").add(Esplora.readAccelerometer(Y_AXIS)/512.0f); 
    bndl.add("/acceleration/z").add(Esplora.readAccelerometer(Z_AXIS)/512.0f); 
    bndl.add("/photoresistor").add(Esplora.readLightSensor()/1023.0f);
    bndl.add("/joystick/horizontal").add(-1.0 * (int32_t)Esplora.readJoystickX()/512.0f);    
    bndl.add("/joystick/vertical").add(-1.0 * (int32_t)Esplora.readJoystickY()/512.0f);      
    bndl.add("/joystick/button").add(Esplora.readJoystickSwitch()>0? released:pressed); 
    bndl.add("/joystick/backward").add((int32_t)Esplora.readButton(JOYSTICK_DOWN)?released:pressed); 
    bndl.add("/joystick/left").add((int32_t)Esplora.readButton(JOYSTICK_LEFT)?released:pressed); 
    bndl.add("/joystick/forward").add((int32_t)Esplora.readButton(JOYSTICK_UP)?released:pressed); 
    bndl.add("/joystick/right").add((int32_t)Esplora.readButton(JOYSTICK_RIGHT)?released:pressed);     
    bndl.add("/serialnumber").add(serialnumber);    
    //bndl.add("/manifest").add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty();  //bundle ending early due to current memory limitations

    //bundle 2
    bndl.add("/diamond/backward").add((int32_t)Esplora.readButton(SWITCH_1)?released:pressed); 
    bndl.add("/diamond/left").add((int32_t)Esplora.readButton(SWITCH_2)?released:pressed); 
    bndl.add("/diamond/forward").add((int32_t)Esplora.readButton(SWITCH_3)?released:pressed); 
    bndl.add("/diamond/right").add((int32_t)Esplora.readButton(SWITCH_4)?released:pressed); 
    bndl.add("/microphone/loudness").add(Esplora.readMicrophone()/1023.0f);
    bndl.add("/temperature/fahrenheit").add((float)Esplora.readTemperature(DEGREES_F));
    bndl.add("/temperature/celsius").add((float)Esplora.readTemperature(DEGREES_C));
    bndl.add("/slider/horizontal").add(1.0f - ((float)Esplora.readSlider()/1023.0f));   
    bndl.add("/serialnumber").add(serialnumber);    
    //bndl.add("/manifest").add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty();  //bundle ending early due to current memory limitations
 
    //bundle 3
    bndl.add("/connector/white/left").add(myReadChannel(CH_MIC  +1)/1023.0);
    bndl.add("/connector/white/right").add(myReadChannel(CH_MIC  +2)/1023.0);
    bndl.add("/led/red").add((int32_t)Esplora.readRed());
    bndl.add("/led/green").add((int32_t)Esplora.readGreen());
    bndl.add("/led/blue").add((int32_t)Esplora.readBlue());
    bndl.add("/led/rgb").add((int32_t)Esplora.readRed()).add((int32_t)Esplora.readGreen()).add((int32_t)Esplora.readBlue());
    bndl.add("/connector/orange/right").add((digitalRead(3)==HIGH)?1:0);
    bndl.add("/connector/orange/left").add((digitalRead(11)==HIGH)?1:0);
    bndl.add("/vendor").add("Arduino");
    bndl.add("/productname").add("Esplora");
    bndl.add("/serialnumber").add(serialnumber);    
    //bndl.add("/manifest").add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    bndl.empty();
  
  counter += 1;
    //   bndl.add("/32u4/supplyVoltage").add(getSupplyVoltage());
    //   bndl.add("/32u4/temperature").add(getTemperature());

  }
  else
  {
    OSCBundle bundleIN;
    int size;

    while(!SLIPSerial.endofPacket())
      if ((size =SLIPSerial.available()) > 0)
      {
        while(size--)
          bundleIN.fill(SLIPSerial.read());
      }
    {
      if(!bundleIN.hasError())
      {
        bundleIN.route("/led", routeLed);
        bundleIN.route("/L", routeLed);    // this is how it is marked on the silkscreen
        bundleIN.route("/out", routeOut);   // for the TinkerIt output connectors
        bundleIN.route("/tone", routeTone);
        bundleIN.route("/squarewave", routeTone);
        bundleIN.route("/notone", routeTone);
      }
    }
  }
}

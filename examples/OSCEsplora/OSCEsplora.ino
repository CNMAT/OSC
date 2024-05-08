
/*
 Bidirectional Esplora OSC communications  using SLIP
 
 Adrian Freed, Jeff Lubow 2013
 
 Includes some examples of common "best practices" for OSC name space and parameter 
 mapping design.

 
*/

#include <Esplora.h>
#include <OSCBundle.h>
//Teensy and Leonardo variants have special USB serial
#include <SLIPEncodedSerial.h>
#include <OSCBoards.h>

#if !defined(__AVR_ATmega32U4__)
#error select Arduino Esplora in board menu
#endif

// Esplora has  a dinky green led at the top left and a big RGB led at the bottom right
void routeLed(OSCMessage &msg, int addrOffset) {
  if (msg.match("/red", addrOffset)) {
    if (msg.isInt(0)) Esplora.writeRed((byte)msg.getInt(0));
  } else if (msg.match("/green", addrOffset)) {
    if (msg.isInt(0)) Esplora.writeGreen((byte)msg.getInt(0));
  } else if (msg.match("/blue", addrOffset)) {
    if (msg.isInt(0)) Esplora.writeBlue((byte)msg.getInt(0));
  } else if (msg.match("/rgb", addrOffset)) {

    if (msg.isInt(0) && msg.isInt(1) && msg.isInt(2)) {
      Esplora.writeRGB((byte)msg.getInt(0), (byte)msg.getInt(1), (byte)msg.getInt(2));
    }
  } else {
    if (msg.isInt(0)) {
      digitalWrite(13, msg.getInt(0) > 0 ? HIGH : LOW);
    }
  }
}

// Esplora has  a dinky green led at the top left and a big RGB led at the bottom right
void routeOut(OSCMessage &msg, int addrOffset) {
  if (msg.match("/B", addrOffset) || msg.match("/b", addrOffset)) {
    if (msg.isInt(0)) {
      pinMode(11, OUTPUT);
      digitalWrite(11, msg.getInt(0) > 0 ? HIGH : LOW);
    } else
      pinMode(11, INPUT);  // add pull up logic some day
  } else if (msg.match("/A", addrOffset) || msg.match("/a", addrOffset)) {
    if (msg.isInt(0)) {
      pinMode(3, OUTPUT);
      digitalWrite(3, msg.getInt(0) > 0 ? HIGH : LOW);
    } else
      pinMode(3, INPUT);  // add pull up logic some day
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

void routeTone(OSCMessage &msg, int addrOffset) {

  unsigned int frequency = 0;
  if (msg.isInt(0)) {
    frequency = msg.getInt(0);
  } else if (msg.isFloat(0)) {
    frequency = msg.getFloat(0);
  } else
    Esplora.noTone();
  if (frequency > 0) {
    if (msg.isInt(1))
      Esplora.tone(frequency, msg.getInt(1));
    else
      Esplora.tone(frequency);
  }
}

const byte MUX_ADDR_PINS[] = {
  A0, A1, A2, A3
};
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
  SLIPSerial.begin(115200);  // set this as high as you can reliably run on your platform
}

intOSC_t counter = 0;
const intOSC_t serialnumber = 2;
const intOSC_t num_components = 4;  //currently break the bundle up into 4 bundles

void loop() {
  OSCBundle bndl;
  intOSC_t manifest_count = 1;

  if (!SLIPSerial.available()) {


    // The COOKED OSC address space and parameter mappings
    // encode data for ease of use and legibility at the host. Unit intervals replace integers
    // The names are chosen to clarify usage rather than adherence to the silkscreen
    // also values are acquired as close together as reasonably possible to increase
    // their usability in sensor fusion contexts, i.e. in this case with the accelerometer

    static constexpr float oneover511 = 1.0f / 511.0f;
    static constexpr float oneover1023 = 1.0f / 1023.0f;
    static constexpr float minusoneover511 = -1.0f / 1023.0f;

    static const auto manifest = "/manifest";
    static const auto serialnumber = "/serialnumber";
    // The RAW OSC address space and parameter mappngs try to capture
    // the data at lowest level without calibration or scaling
    // The names are chosen to match what is on the silkscreen of the board where it is found
    SLIPSerial.beginPacket();
    bndl.add("/mic").add((intOSC_t)Esplora.readMicrophone());
    bndl.add("/temp/sensor/celsius").add((intOSC_t)Esplora.readTemperature(DEGREES_C));
    bndl.add("/temp/sensor/fahrenheit").add((intOSC_t)Esplora.readTemperature(DEGREES_F));
    bndl.add("/linear/potentiometer").add((intOSC_t)Esplora.readSlider());
    bndl.add("/light/sensor").add((intOSC_t)Esplora.readLightSensor());
    bndl.add("/switch/1").add((intOSC_t)Esplora.readButton(SWITCH_1));
    bndl.add("/switch/2").add((intOSC_t)Esplora.readButton(SWITCH_2));
    bndl.add("/switch/3").add((intOSC_t)Esplora.readButton(SWITCH_3));
    bndl.add("/switch/4").add((intOSC_t)Esplora.readButton(SWITCH_4));
    bndl.add("/joystick/X").add((intOSC_t)Esplora.readJoystickX());
    bndl.add("/joystick/Y").add((intOSC_t)Esplora.readJoystickY());
    bndl.add("/joystick/switch").add((intOSC_t)Esplora.readJoystickSwitch());
    bndl.add("/joystick/switch/1").add((intOSC_t)Esplora.readButton(JOYSTICK_DOWN));
    bndl.add("/joystick/switch/2").add((intOSC_t)Esplora.readButton(JOYSTICK_LEFT));
    bndl.add("/joystick/switch/3").add((intOSC_t)Esplora.readButton(JOYSTICK_UP));
    bndl.add("/joystick/switch/4").add((intOSC_t)Esplora.readButton(JOYSTICK_RIGHT));
    bndl.add("/accelerometer/x").add(Esplora.readAccelerometer(X_AXIS));
    bndl.add("/accelerometer/y").add(Esplora.readAccelerometer(Y_AXIS));
    bndl.add("/accelerometer/z").add(Esplora.readAccelerometer(Z_AXIS));
    bndl.add(serialnumber).add(serialnumber);
    bndl.add(manifest).add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial);   // send the bytes to the SLIP stream
    SLIPSerial.endPacket();  // mark the end of the OSC Packet
    bndl.empty();

    SLIPSerial.beginPacket();  // mark the beginning of the OSC Packet
    //bundle 1
    bndl.add("/acceleration/x").add(Esplora.readAccelerometer(X_AXIS) * oneover511);
    bndl.add("/acceleration/y").add(Esplora.readAccelerometer(Y_AXIS) * oneover511);
    bndl.add("/acceleration/z").add(Esplora.readAccelerometer(Z_AXIS) * oneover511);
    bndl.add("/photoresistor").add(Esplora.readLightSensor() * oneover1023);
    bndl.add("/joystick/horizontal").add(Esplora.readJoystickX() * minusoneover511);
    bndl.add("/joystick/vertical").add(Esplora.readJoystickY() * minusoneover511);
    bndl.add("/joystick/button").add(Esplora.readJoystickSwitch() > 0);
    bndl.add("/joystick/backward").add((bool)Esplora.readButton(JOYSTICK_DOWN));
    bndl.add("/joystick/left").add((bool)Esplora.readButton(JOYSTICK_LEFT));
    bndl.add("/joystick/forward").add((bool)Esplora.readButton(JOYSTICK_UP));
    bndl.add("/joystick/right").add((bool)Esplora.readButton(JOYSTICK_RIGHT));
    bndl.add(serialnumber).add(serialnumber);
    bndl.add(manifest).add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial);   // send the bytes to the SLIP stream
    SLIPSerial.endPacket();  // mark the end of the OSC Packet
    bndl.empty();            //bundle ending early due to current memory limitations

    //bundle 2
    SLIPSerial.beginPacket();

    bndl.add("/diamond/backward").add((bool)Esplora.readButton(SWITCH_1));
    bndl.add("/diamond/left").add((bool)Esplora.readButton(SWITCH_2));
    bndl.add("/diamond/forward").add((bool)Esplora.readButton(SWITCH_3));
    bndl.add("/diamond/right").add((bool)Esplora.readButton(SWITCH_4));
    bndl.add("/microphone/loudness").add(Esplora.readMicrophone() * oneover1023);
    bndl.add("/temperature/fahrenheit").add((float)Esplora.readTemperature(DEGREES_F));
    bndl.add("/temperature/celsius").add((float)Esplora.readTemperature(DEGREES_C));
    bndl.add("/slider/horizontal").add(1.0f - (Esplora.readSlider() * oneover1023));
    bndl.add(serialnumber).add(serialnumber);
    bndl.add(manifest).add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial);   // send the bytes to the SLIP stream
    SLIPSerial.endPacket();  // mark the end of the OSC Packet
    bndl.empty();            //bundle ending early due to current memory limitations

    //bundle 3
    SLIPSerial.beginPacket();
    bndl.add("/connector/white/left").add(myReadChannel(CH_MIC + 1) * oneover1023);
    bndl.add("/connector/white/right").add(myReadChannel(CH_MIC + 2) * oneover1023);
    bndl.add("/led/red").add((intOSC_t)Esplora.readRed());
    bndl.add("/led/green").add((intOSC_t)Esplora.readGreen());
    bndl.add("/led/blue").add((intOSC_t)Esplora.readBlue());
    bndl.add("/led/rgb").add((intOSC_t)Esplora.readRed()).add((intOSC_t)Esplora.readGreen()).add((intOSC_t)Esplora.readBlue());
    bndl.add("/connector/orange/right").add(digitalRead(3) == HIGH);
    bndl.add("/connector/orange/left").add(digitalRead(11) == HIGH);
    bndl.add("/vendor").add("Arduino");
    bndl.add("/productname").add("Esplora");
    bndl.add("/32u4/supplyVoltage").add(getSupplyVoltage());
    bndl.add("/32u4/temperature").add(getTemperature());
    bndl.add(serialnumber).add(serialnumber);
    bndl.add(manifest).add(manifest_count++).add(num_components).add(counter);
    bndl.send(SLIPSerial);   // send the bytes to the SLIP stream
    SLIPSerial.endPacket();  // mark the end of the OSC Packet
    bndl.empty();

    counter++;

  } else {
    OSCBundle bundleIN;
    int size;

    while (!SLIPSerial.endofPacket())
      if ((size = SLIPSerial.available()) > 0) {
        while (size--)
          bundleIN.fill(SLIPSerial.read());
      }
    {
      if (!bundleIN.hasError()) {
        bundleIN.route("/led", routeLed);
        bundleIN.route("/L", routeLed);    // this is how it is marked on the silkscreen
        bundleIN.route("/out", routeOut);  // for the TinkerIt output connectors
        bundleIN.route("/tone", routeTone);
        bundleIN.route("/squarewave", routeTone);
        bundleIN.route("/notone", routeTone);
      }
    }
  }
}

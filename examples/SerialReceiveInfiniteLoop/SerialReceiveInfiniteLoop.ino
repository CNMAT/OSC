/*
* Blink the LED according to incoming OSC on/off rates in quasi-asynchronous way
*/
#include <OSCBoards.h>
#include <OSCBundle.h>

#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial( thisBoardsSerialUSB );
#else
 SLIPEncodedSerial SLIPSerial(Serial); // Change to Serial1 or Serial2 etc. for boards with multiple serial ports that don’t have Serial
#endif


constexpr unsigned long blinkInterval = 2500;
unsigned long blinkNow;

unsigned long blinkRateOn = 50;
unsigned long blinkRateOff = 50;

void LEDcontrol(OSCMessage& msg)
{
    if (msg.isInt(0)) {
        blinkRateOn = msg.getInt(0);
    }

    if (msg.isInt(1)) {
        blinkRateOff = msg.getInt(1);
    }
}

void setup()
{
    Serial.begin(9600);

    const unsigned long startNow = millis() + 5000;
    while(!Serial && millis() < startNow);

    SLIPSerial.begin(9600); // set this as high as you can reliably run on your platform

    blinkNow = millis() + blinkInterval;
}

//reads and dispatches the incoming message
void loop()
{
    OSCBundle bundleIN;
    int size;

    if (SLIPSerial.available())
        while (!SLIPSerial.endofPacket())
            while (SLIPSerial.available())
                bundleIN.fill(SLIPSerial.read());

    if (!bundleIN.hasError())
        bundleIN.dispatch("/led", LEDcontrol);
    
    if (millis() >= blinkNow) {
        pinMode(LED_BUILTIN, OUTPUT);

        Serial.print("Blinking at ");
        Serial.print(blinkRateOn);
        Serial.print("/");
        Serial.print(blinkRateOff);
        Serial.println();

        for (auto i = 0; i <= 5; i ++) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(blinkRateOn);
            digitalWrite(LED_BUILTIN, LOW);
            delay(blinkRateOff);
        }
        // blinkNow = millis() + blinkInterval;
        blinkNow = millis() + blinkInterval - 5 * (blinkRateOn + blinkRateOff);
    }
}

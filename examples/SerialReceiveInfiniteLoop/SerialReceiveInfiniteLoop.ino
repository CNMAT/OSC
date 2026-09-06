/*
* Blink the LED according to incoming OSC on/off rates in quasi-asynchronous way:
* /s/l <on ms> <off ms>, as SerialSendMessageInfiniteLoop sends
*/
#include <OSCBoards.h>
#include <OSCBundle.h>

#include <SLIPEncodedSerial.h>

// No fallback pin here on purpose. OSCBoards.h defines BOARD_HAS_LED only when
// the core actually names an on-board LED, and guessing one is not harmless:
// pin 13 is SDA on the M5Dial and MISO on the T-Display-S3, and GPIO 2 is a
// strapping pin on several ESP32 parts. Where no LED is named this sketch
// drives nothing; -DLED_BUILTIN=<pin> points it at one and, because
// BOARD_HAS_LED is derived from defined(LED_BUILTIN), switches this back on.


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
                {
                  // read() returns int and -1 on underrun; passing that straight
                  // to fill() narrowed it to an ordinary 0xFF data byte
                  int c = SLIPSerial.read();
                  if (c >= 0) bundleIN.fill((uint8_t)c);
                }

    if (!bundleIN.hasError())
        bundleIN.dispatch("/s/l", LEDcontrol);
    
    if (millis() >= blinkNow) {
#ifdef BOARD_HAS_LED
        pinMode(LED_BUILTIN, OUTPUT);
#endif

        Serial.print("Blinking at ");
        Serial.print(blinkRateOn);
        Serial.print("/");
        Serial.print(blinkRateOff);
        Serial.println();

        for (auto i = 0; i <= 5; i ++) {
#ifdef BOARD_HAS_LED
            digitalWrite(LED_BUILTIN, HIGH);
#endif
            delay(blinkRateOn);
#ifdef BOARD_HAS_LED
            digitalWrite(LED_BUILTIN, LOW);
#endif
            delay(blinkRateOff);
        }
        // blinkNow = millis() + blinkInterval;
        blinkNow = millis() + blinkInterval - 5 * (blinkRateOn + blinkRateOff);
    }
}

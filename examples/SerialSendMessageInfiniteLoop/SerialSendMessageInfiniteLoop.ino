/*
*   Make an OSC message for controlling a remote LED blinking rates.
*/

#include <OSCBoards.h>
#include <OSCMessage.h>

#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial1); // Change to Serial1 or Serial2 etc. for boards with multiple serial ports that don’t have Serial
#endif

constexpr auto blinkRatesSize = 8;
int blinkRates[blinkRatesSize] = { 25, 50, 100, 125, 150, 175, 200, 225 };

void setup()
{
    //begin SLIPSerial just like Serial
    SLIPSerial.begin(9600); // set this as high as you can reliably run on your platform
}

void loop()
{
    //the message wants an OSC address as first argument
    OSCMessage msg("/led");

    auto i = rand() % (blinkRatesSize - 1);
    auto blinkRateOn = blinkRates[i];

    auto j = rand() % (blinkRatesSize - 1);
    auto blinkRateOff = blinkRates[j];

    msg.add(blinkRateOn).add(blinkRateOff);

    SLIPSerial.beginPacket();
    msg.send(SLIPSerial); // send the bytes to the SLIP stream
    SLIPSerial.endPacket(); // mark the end of the OSC Packet
    msg.empty(); // free space occupied by message

    delay(20);
}

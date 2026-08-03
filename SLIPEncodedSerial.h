/*
Extends the Serial class to encode SLIP over serial
*/
#include "Arduino.h"

#ifndef SLIPEncodedSerial_h
#define SLIPEncodedSerial_h

#include <Stream.h>
#ifdef ARDUINO_API_VERSION
#include <api/HardwareSerial.h>
#else
#include <HardwareSerial.h>
#endif



#if (defined(TEENSYDUINO) && (defined(USB_SERIAL) || defined(USB_DUAL_SERIAL) || defined(USB_TRIPLE_SERIAL) || defined(USB_SERIAL_HID) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI_AUDIO_DUAL_SERIAL) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16_SERIAL) || defined(USB_MIDI_AUDIO_SERIAL) || defined(USB_MIDI16_AUDIO_SERIAL))) || (!defined(TEENSYDUINO) && defined(__AVR_ATmega32U4__)) || defined(ARDUINO_SAMD_ADAFRUIT)|| defined(__SAM3X8E__) || (defined(_USB) && defined(_USE_USB_FOR_SERIAL_))  || defined(_SAMD21_) || defined(__PIC32MX__) || defined(__PIC32MZ__) || defined(ARDUINO_USB_CDC_ON_BOOT) || defined(ARDUINO_ARCH_RP2040)
#define BOARD_HAS_USB_SERIAL


//import the serial USB object
#if defined(TEENSYDUINO) && defined (__arm__)
#if !defined(USB_HOST_TEENSY36_)
#include <usb_serial.h>
#endif
#elif defined(TEENSYDUINO) && defined (__AVR__)
#include <usb_api.h>
#elif defined(__SAM3X8E__)  || defined(_SAMD21_) 
#include <USB/USBAPI.h>
#elif (defined(__PIC32MX__) || defined(__PIC32MZ__) || defined(ARDUINO_USB_CDC_ON_BOOT))
#include <USB.h>
#elif defined(__AVR_ATmega32U4__)
#include "USBAPI.h"
#include <avr/wdt.h>    
#elif defined(ARDUINO_ARCH_RP2040)
#include <SerialUSB.h>
#elif defined(ARDUINO_SAMD_ADAFRUIT)
#include "USB/USBAPI.h"
#else
#error Unknown USB port
#endif

#endif
 
/*
	Workaround for a permanent stall in Arduino's ATmega32U4 CDC receive path.

	A USB host ends any transfer that is an exact multiple of the endpoint size
	with a zero-length packet, and the 32U4 CDC endpoint is 64 bytes. Stock
	USBCore.cpp never releases the endpoint bank such a packet lands in:
	USB_Available() only reports FifoByteCount() and releases nothing, and
	USB_Recv() releases only `if (len && !FifoByteCount())`, which is false when
	the bank arrived empty. Reception then stops for good while transmit carries
	on, and only a reset clears it. Measured on a LilyPad USB: 63, 100, 127 and
	191 byte writes are fine indefinitely; one 64 or 128 byte write kills it.

	Teensy's core, and ATUSB_Core which is derived from it, handle this in their
	own available()/read(). Rather than depend on a replacement core, release the
	bank here, using the same UEINTX value as the core's own ReleaseRX().

	Only ever fires when a packet has been received (RXOUTI) and there is nothing
	to read (RWAL clear) -- an empty bank. It can therefore never discard data.
*/
#if defined(__AVR_ATmega32U4__) && !defined(TEENSYDUINO) && defined(CDC_RX)
static inline void oscReleaseStuckCdcRxBank()
{
	uint8_t sreg = SREG;
	cli();
	uint8_t ep = UENUM;               // the core leaves this pointing anywhere
	UENUM = CDC_RX;
	uint8_t i = UEINTX;
	if ((i & (1 << RXOUTI)) && !(i & (1 << RWAL))) {
		UEINTX = 0x6B;                // == ReleaseRX() in USBCore.cpp
	}
	UENUM = ep;
	SREG = sreg;
}
#else
static inline void oscReleaseStuckCdcRxBank() {}
#endif

template <class T>
class _SLIPSerial: public Stream{
	
private:
	// state machine for SLIP escape characters
	enum erstate {CHAR, FIRSTEOT, SECONDEOT, SLIPESC } rstate;
	
	//the serial port used
	T * serial;
	
public:
	_SLIPSerial(T &s)
	{
		serial = &s;
		rstate = CHAR;
	}

	static const uint8_t eot = 0300;
	static const uint8_t slipesc = 0333;
	static const uint8_t slipescend = 0334;
	static const uint8_t slipescesc = 0335;
	/*
	SERIAL METHODS
	*/
	bool endofPacket()
	{
		oscReleaseStuckCdcRxBank();
		if(rstate == SECONDEOT)
		{
			rstate = CHAR; 
			return true;
		}
		if (rstate==FIRSTEOT)
		{
			if(serial->available() || (serial->peek() != -1))
			{
				uint8_t c =serial->peek();
				if(c==eot)
				{
					serial->read(); // throw it on the floor
				}
			}
			rstate = CHAR;
			return true;
		}
		return false;
	}
	int available(){
		oscReleaseStuckCdcRxBank();
	back:
		uint8_t cnt = serial->available();
		
		if((cnt==0) && (serial->peek()==-1))
			return 0;
		if(rstate==CHAR)
		{
			uint8_t c =serial->peek();
			if(c==slipesc)
			{
				rstate = SLIPESC;
				serial->read(); // throw it on the floor
				goto back;
			}
			else if( c==eot)
			{
				rstate = FIRSTEOT;
				serial->read(); // throw it on the floor
				goto back;
			}
			return 1; // we may have more but this is the only sure bet
		}
		else if(rstate==SLIPESC)	
			return 1;
		else if(rstate==FIRSTEOT)
		{
			if(serial->peek()==eot)
			{
				rstate = SECONDEOT;
				serial->read(); // throw it on the floor
				return 0;
			}		
			rstate = CHAR;
		}else if (rstate==SECONDEOT) {
			rstate = CHAR;
		}
		
		return 0;
			
	}

	//reads a byte from the buffer
	int read(){
	back:
		//Take the underlying read() as an int and reject its -1 before
		//narrowing.  Assigning it straight into a uint8_t turned "nothing
		//available" into a perfectly ordinary 0xFF data byte, which was then
		//fed into the message as content.
		int ci = serial->read();
		if (ci < 0){
			return -1;
		}
		uint8_t c = (uint8_t) ci;
		if(rstate==CHAR)
		{
			if(c==slipesc)
			{
				rstate=SLIPESC;
				goto back;
			}	
			else if(c==eot){
			
				return -1; // xxx this is an error
			}
			return c;
		}
		else
		if(rstate==SLIPESC)
		{
			rstate=CHAR;
			if(c==slipescend)
				return eot;
			else if(c==slipescesc)
				return slipesc;
				else {
					// insert some error code here
					return -1;
				}

		}
		else
			return -1;
	}
	size_t readBytes( uint8_t *buffer, size_t size)
	{
		size_t count = 0;
		while(!endofPacket() && available() && (size>0))
		{
			int c = read();
			if(c>=0)
			{
				*buffer++ = c;
				++count;
				--size;
				
			}
			else
				break;
		}
		return count;
	}
	// as close as we can get to correct behavior
	int peek(){
		int c = serial->peek();
		if(rstate==SLIPESC)
		{
			if(c==slipescend)
				return eot;
			else if(c==slipescesc)
				return slipesc;
		}
		return c; 
	}

	//encode SLIP
	size_t write(uint8_t b){
		if(b == eot){ 
			serial->write(slipesc);
			return serial->write(slipescend); 
		} else if(b==slipesc) {  
			serial->write(slipesc);
			return serial->write(slipescesc); 
		} else {
			return serial->write(b);
		}	
	}
	size_t write(const uint8_t *buffer, size_t size)
	{ 
		size_t result=0;
		while(size--)
		{
			result = write(*buffer++);
		}
		return result;
	}


	void begin(unsigned long baudrate){
		serial->begin(baudrate);
	}
	// for bluetooth
	void begin(char *name){
		serial->begin(name);
	}
	//SLIP specific method which begins a transmitted packet
	void beginPacket() { 	serial->write(eot); }

	//signify the end of the packet with an EOT
	void endPacket(){
		serial->write(eot);
	}

	void flush(){
		serial->flush();
	}

};

using SLIPEncodedSerial =  _SLIPSerial<HardwareSerial> ;
// template <>  void _SLIPSerial<HardwareSerial>::endPacket(){
// 		serial->write(eot);

// 	}

#ifdef BOARD_HAS_USB_SERIAL
// Ask the core which port is the USB CDC port rather than guessing from board
// macros.  Every core built on the Arduino variant convention defines
// SERIAL_PORT_USBVIRTUAL in its variant header; the hand-maintained ladder
// below kept missing variants.  Adafruit's Gemma M0 is the case that broke:
// its native USB port is named Serial, but it is a SAMD21 that does not pass
// -DARDUINO_SAMD_ZERO, so it fell through to the SerialUSB branch and failed
// to compile on a name that does not exist on that board.  OSCData.h defines
// this macro to the same token so the two definitions stay identical.
// Zero-derived boards keep their historical binding. ARDUINO_SAMD_ZERO is
// auto-defined from build.board=SAMD_ZERO, so it covers both Arduino Zero
// variants, where Serial is a hardware Uart rather than the USB CDC port.
// Selecting SERIAL_PORT_USBVIRTUAL for them would move an existing sketch's
// output from the programming-port UART to native USB -- a behaviour change,
// not a compile fix, so it is deliberately left alone here.
#if defined(_SAMD21_) && defined(ARDUINO_SAMD_ZERO)
#define thisBoardsSerialUSB Serial
typedef decltype(Serial) actualUSBtype;

#elif defined(SERIAL_PORT_USBVIRTUAL)
#define thisBoardsSerialUSB SERIAL_PORT_USBVIRTUAL
typedef decltype(SERIAL_PORT_USBVIRTUAL) actualUSBtype;

#elif defined(_SAMD21_)
#define thisBoardsSerialUSB SerialUSB
typedef decltype(SerialUSB) actualUSBtype;

#elif defined(__SAM3X8E__)
// Required for Serial on Zero based boards
#define thisBoardsSerialUSB SerialUSB
typedef decltype(SerialUSB) actualUSBtype;

// defined(__SAM3X8E__)
#elif  defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_SAMD_ADAFRUIT) || defined(ARDUINO_USB_CDC_ON_BOOT) || defined(CORE_TEENSY)  || defined(__AVR_ATmega32U4__) || (defined(__PIC32MX__) || defined(__PIC32MZ__))
#define thisBoardsSerialUSB Serial
typedef decltype(Serial) actualUSBtype;
#endif
using  SLIPEncodedUSBSerial =  _SLIPSerial<actualUSBtype>;
#if defined(CORE_TEENSY)
template <> void _SLIPSerial<actualUSBtype>::endPacket(){
		serial->write(eot);
  		serial->send_now();
}
#endif

#endif // BOARD_HAS_USB_SERIAL 

// Bluetooth Example

// #if BOARD_HAS_BLUETOOTH_SERIAL
// #include "BluetoothSerial.h"
// BluetoothSerial bluetoothserialinstance;
// SLIPEncodedBluetoothSerial SLIPSerial(bluetoothserialinstance);

// BluetoothSerial is Bluetooth Classic, which only the original ESP32 has.
// CONFIG_IDF_TARGET_ESP32C3 was listed twice here and C6/C2/H2 not at all;
// harmless so far only because those parts do not enable Bluedroid, so the
// second term short-circuits first. Named properly rather than relying on that.
#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED) \
    && !defined(CONFIG_IDF_TARGET_ESP32C2) && !defined(CONFIG_IDF_TARGET_ESP32C3) \
    && !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(CONFIG_IDF_TARGET_ESP32H2) \
    && !defined(CONFIG_IDF_TARGET_ESP32S2) && !defined(CONFIG_IDF_TARGET_ESP32S3)
#include "BluetoothSerial.h"
using  SLIPEncodedBluetoothSerial =  _SLIPSerial<BluetoothSerial>;
#define BOARD_HAS_BLUETOOTH_SERIAL

#endif
#endif

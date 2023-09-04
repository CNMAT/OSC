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



#if (defined(TEENSYDUINO) && (defined(USB_SERIAL) || defined(USB_DUAL_SERIAL) || defined(USB_TRIPLE_SERIAL) || defined(USB_SERIAL_HID) || defined(USB_MIDI_SERIAL) || defined(USB_MIDI_AUDIO_DUAL_SERIAL) || defined(USB_MIDI4_SERIAL) || defined(USB_MIDI16_SERIAL) || defined(USB_MIDI_AUDIO_SERIAL) || defined(USB_MIDI16_AUDIO_SERIAL))) || (!defined(TEENSYDUINO) && defined(__AVR_ATmega32U4__)) || defined(__SAM3X8E__) || (defined(_USB) && defined(_USE_USB_FOR_SERIAL_))  || defined(_SAMD21_) || defined(__PIC32MX__) || defined(__PIC32MZ__) || defined(ARDUINO_USB_CDC_ON_BOOT) || defined(ARDUINO_ARCH_RP2040)
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
#else
#error Unknown USB port
#endif

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
		if(rstate == SECONDEOT)
		{
			rstate = CHAR; 
			return true;
		}
		if (rstate==FIRSTEOT)
		{
			if(serial->available())
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
	back:
		uint8_t cnt = serial->available();
		
		if(cnt==0)
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
		uint8_t c = serial->read();
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
		uint8_t c = serial->peek();
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

#if defined(_SAMD21_)
// Required for Serial on Zero based boards
#if defined(ARDUINO_SAMD_ZERO)
// Adafruit breaks with tradition here
#define thisBoardsSerialUSB Serial
typedef decltype(Serial) actualUSBtype;
#else
#define thisBoardsSerialUSB SerialUSB
typedef decltype(SerialUSB) actualUSBtype;
#endif
#elif defined(__SAM3X8E__)
// Required for Serial on Zero based boards
#define thisBoardsSerialUSB SerialUSB
typedef decltype(SerialUSB) actualUSBtype;

// defined(__SAM3X8E__)
#elif  defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_USB_CDC_ON_BOOT) || defined(CORE_TEENSY)  || defined(__AVR_ATmega32U4__) || (defined(__PIC32MX__) || defined(__PIC32MZ__))
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

#endif

// Bluetooth Example

// #if BOARD_HAS_BLUETOOTH_SERIAL
// #include "BluetoothSerial.h"
// BluetoothSerial bluetoothserialinstance;
// SLIPEncodedBluetoothSerial SLIPSerial(bluetoothserialinstance);

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BLUEDROID_ENABLED) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32C3) && !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32S2)
#include "BluetoothSerial.h"
using  SLIPEncodedBluetoothSerial =  _SLIPSerial<BluetoothSerial>;
#define BOARD_HAS_BLUETOOTH_SERIAL

#endif
#endif
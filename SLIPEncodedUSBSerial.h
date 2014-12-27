/*
Extends the Serial class to encode SLIP over serial
*/
#include "Arduino.h"

#ifndef SLIPEncodedUSBSerial_h
#define SLIPEncodedUSBSerial_h


#include <Stream.h>


#if (defined(CORE_TEENSY) && defined(USB_SERIAL)) || (!defined(CORE_TEENSY) && defined(__AVR_ATmega32U4__)) || defined(__SAM3X8E__) || (defined(_USB) && defined(_USE_USB_FOR_SERIAL_)) || defined(BOARD_maple_mini)


//import the serial USB object
#if defined (__MK20DX128__) || defined (__MK20DX512__) || defined(BOARD_maple_mini) || defined(CORE_TEENSY)
#include <usb_serial.h>
#elif defined(__SAM3X8E__)
#include <USB/USBAPI.h>
#elif defined(__PIC32MX__)
#include "HardwareSerial.h"
#elif defined(__AVR_ATmega32U4__)
// leonardo
//#include "Platform.h"
#include "USBAPI.h"
#include <avr/wdt.h>    
#else
#error Unknown USB port
#endif



class SLIPEncodedUSBSerial: public Stream{
	
private:
	enum erstate {CHAR, FIRSTEOT, SECONDEOT, SLIPESC } rstate;
//different type for each platform

#if  defined(CORE_TEENSY) 
    usb_serial_class
#elif defined(__SAM3X8E__) || defined(__AVR_ATmega32U4__)
Serial_
#elif defined(__PIC32MX__) || defined(BOARD_maple_mini)
    USBSerial
#else
#error Unknown USBserial type	
#endif
							* serial;
	
public:
	SLIPEncodedUSBSerial(
//different constructor for each platform
#if  defined(CORE_TEENSY)
    usb_serial_class
#elif defined(__SAM3X8E__) || defined(__AVR_ATmega32U4__)
    Serial_
#elif defined(__PIC32MX__) || defined(BOARD_maple_mini)
    USBSerial
#else
#error Unknown USBserial type
#endif
						 &		);
	
	int available();
	int read();
    int readBytes( uint8_t *buffer, size_t size);

	int peek();
	void flush();
	
	//same as Serial.begin
	void begin(unsigned long);
    //SLIP specific method which begins a transmitted packet
	void beginPacket();
	//SLIP specific method which ends a transmittedpacket
	void endPacket();
	// SLIP specific method which indicates that an EOT was received 
	bool endofPacket();
	
	
//the arduino and wiring libraries have different return types for the write function
#if  defined(WIRING) || defined(BOARD_DEFS_H)
	void write(uint8_t b);
   void write(const uint8_t *buffer, size_t size);

#else
	//overrides the Stream's write function to encode SLIP
	size_t write(uint8_t b);
    size_t write(const uint8_t *buffer, size_t size);
	//using Print::write;	
#endif

};
#endif



#endif
/*
Extends the Serial class to encode SLIP over serial
*/

#ifndef SLIPEncodedUSBSerial_h
#define SLIPEncodedUSBSerial_h
#include <Arduino.h>
#include <Stream.h>

#if defined(CORE_TEENSY)|| defined(__AVR_ATmega32U4__) || defined(__SAM3X8E__)
	//import the serial object
#if defined (__MK20DX128__)
#include <usb_serial.h>
#elif defined(CORE_TEENSY)
#include <usb_api.h>
#elif defined(__SAM3X8E__)
#include <USB/USBAPI.h>
#else
#include "Platform.h"
#include "USBAPI.h"
#include <avr/wdt.h>
    // leonardo

#endif



class SLIPEncodedUSBSerial: public Stream{
	
private:
	enum erstate {CHAR, FIRSTEOT, SECONDEOT, SLIPESC } rstate;

#if !defined(CORE_TEENSY) || defined(__SAM3X8E__)
Serial_
#else	
	usb_serial_class
#endif
							* serial;
	
public:
	
//different constructor for teensies
	SLIPEncodedUSBSerial(
#if !defined(CORE_TEENSY)
Serial_
#else
	usb_serial_class
#endif
						 &		);
	
	int available();
	int read();
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
#ifdef WIRING
	void write(uint8_t b);
#else
	//overrides the Stream's write function to encode SLIP
	size_t write(uint8_t b);
	//using Print::write;	
#endif

};
#endif



#endif
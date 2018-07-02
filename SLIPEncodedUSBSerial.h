/*
Extends the Serial class to encode SLIP over serial
*/
#include "Arduino.h"

#ifndef SLIPEncodedUSBSerial_h
#define SLIPEncodedUSBSerial_h


#include <Stream.h>


#if (defined(TEENSYDUINO) && defined(USB_SERIAL)) || (!defined(TEENSYDUINO) && defined(__AVR_ATmega32U4__)) || defined(__SAM3X8E__) || (defined(_USB) && defined(_USE_USB_FOR_SERIAL_))  || defined(_SAMD21_) || (defined(__PIC32MX__) || defined(__PIC32MZ__))


//import the serial USB object
#if defined(TEENSYDUINO) && defined (__arm__)
#if !defined(USB_HOST_TEENSY36_)
#include <usb_serial.h>
#endif
#elif defined(TEENSYDUINO) && defined (__AVR__)
#include <usb_api.h>
#elif defined(__SAM3X8E__)  || defined(_SAMD21_) 
#include <USB/USBAPI.h>
#elif (defined(__PIC32MX__) || defined(__PIC32MZ__))
#include <USB.h>
#elif defined(__AVR_ATmega32U4__)
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
#if defined(USB_HOST_TEENSY36)
    USBSerial
#else
    usb_serial_class
#endif
#elif defined(__SAM3X8E__) || defined(__AVR_ATmega32U4__) || defined(_SAMD21_)  || defined(__ARM__)
Serial_
    
#elif (defined(__PIC32MX__) || defined(__PIC32MZ__))
    CDCACM
#else
#error Unknown USBserial type	
#endif
							* serial;
	
public:
	SLIPEncodedUSBSerial(
//different constructor for each platform
#if  defined(CORE_TEENSY)
#if defined(USB_HOST_TEENSY36)
                         USBSerial
#else
                         usb_serial_class
#endif

#elif defined(__SAM3X8E__) || defined(__AVR_ATmega32U4__)  || defined(_SAMD21_)  || defined(__ARM__)
    Serial_
                         
#elif (defined(__PIC32MX__) || defined(__PIC32MZ__))
    CDCACM
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
	
	

	//overrides the Stream's write function to encode SLIP
	size_t write(uint8_t b);
    size_t write(const uint8_t *buffer, size_t size);
	//using Print::write;	


};
#endif



#endif

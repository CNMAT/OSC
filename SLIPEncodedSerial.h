/*
Extends the Serial class to encode SLIP over serial
*/

#ifndef SLIPEncodedSerial_h
#define SLIPEncodedSerial_h


#include "Arduino.h"
#include <Stream.h>
#include <HardwareSerial.h>


class SLIPEncodedSerial: public Stream{
	
private:
	enum erstate {CHAR, FIRSTEOT, SECONDEOT, SLIPESC } rstate;
	
	//the serial port used
	HardwareSerial * serial;

	
public:
	
	//the serial port used
	SLIPEncodedSerial(HardwareSerial & );

	
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
#if defined(WIRING) || defined(BOARD_DEFS_H)
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
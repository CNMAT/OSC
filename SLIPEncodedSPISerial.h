/*
Extends the Serial class to encode SLIP over serial
*/

#ifndef SLIPEncodedSPISerial_h
#define SLIPEncodedSPISerial_h
#if ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif
#include <Stream.h>
#include <StreamSPI.h>


class SLIPEncodedSPISerial: public Stream{
	
private:
	enum erstate {CHAR, FIRSTEOT, SECONDEOT, SLIPESC } rstate;
	
	//the serial port used
	StreamSPI * serial;

	
public:
	
	//the serial port used
	SLIPEncodedSPISerial(StreamSPI & );

	
	int available();
	int read();
	int peek();
	void flush();
	
	//same as Serial.begin
	void begin();
    
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
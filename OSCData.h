/*
 Written by Yotam Mann, The Center for New Music and Audio Technologies,
 University of California, Berkeley.  Copyright (c) 2013, The Regents of
 the University of California (Regents).
 
 Permission to use, copy, modify, distribute, and distribute modified versions
 of this software and its documentation without fee and without a signed
 licensing agreement, is hereby granted, provided that the above copyright
 notice, this paragraph and the following two paragraphs appear in all copies,
 modifications, and distributions.
 
 IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
 SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING
 OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF REGENTS HAS
 BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE. THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED
 HEREUNDER IS PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE
 MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 
 For bug reports and feature requests please email me at yotam@cnmat.berkeley.edu
 */

#ifndef OSCDATA_h
#define OSCDATA_h

#include "Arduino.h"

#include <stdlib.h> 
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "OSCTiming.h"

#if (defined(TEENSYDUINO) && defined(USB_SERIAL)) || (!defined(TEENSYDUINO) && defined(__AVR_ATmega32U4__)) || defined(__SAM3X8E__) || (defined(_USB) && defined(_USE_USB_FOR_SERIAL_)) || defined(BOARD_maple_mini)

#define BOARD_HAS_USB_SERIAL
#if defined(__SAM3X8E__)
#define thisBoardsSerialUSB SerialUSB
#else
#define thisBoardsSerialUSB Serial
#endif
#endif

#define ESPxx		(defined(ESP8266) || defined(ESP32))

//ERRORS/////////////////////////////////////////////////
typedef enum { OSC_OK = 0,
	BUFFER_FULL, INVALID_OSC, ALLOCFAILED, INDEX_OUT_OF_BOUNDS
} OSCErrorCode;

class OSCData 
{
    
private:
    
    //friends
	friend class OSCMessage;
    
    //should only be used while decoding
    //leaves an invalid OSCMessage with a type, but no data
    OSCData(char t);
       
public:

	//an error flag
	OSCErrorCode error;

	//the size (in bytes) of the data
	int bytes;

	//the type of the data
	int type;

	//the data
	union {
		char * s; //string
		int32_t i; //int
		float f; //float
		double d; //double
        uint64_t l; //long
		uint8_t * b; //blob
        osctime_t time;
	} data;

	//overload the constructor to account for all the types and sizes
	OSCData(const char * s);
#if defined(__SAM3X8E__)
	OSCData (int16_t);
#endif
	OSCData (int32_t);
#ifndef ESPxx
    OSCData (int);
#endif
    OSCData (unsigned int);
	OSCData (float);
	OSCData (double);
	OSCData (uint8_t *, int);
    //accepts another OSCData objects and clones it
	OSCData (OSCData *);
    OSCData  (boolean);
    OSCData  (osctime_t);

	//destructor
	~OSCData();
    
    //GETTERS
    int32_t getInt();
    float getFloat();
    double getDouble();
    int getString(char *, int);
    int getBlob(uint8_t *, int);
    bool getBoolean();
    osctime_t getTime();
    
    //constructor from byte array with type and length
	OSCData(char, uint8_t *, int);
    //fill the passed in buffer with the data
	//uint8_t * asByteArray();

};

/*
 based on http://stackoverflow.com/questions/809902/64-bit-ntohl-in-c
 
 if the system is little endian, it will flip the bits
 if the system is big endian, it'll do nothing
 */
template<typename T> 
static inline T BigEndian(const T& x)
{
    const int one = 1;
    const char sig = *(char*)&one;
    if (sig == 0) return x; // for big endian machine just return the input
    T ret;
    int size = sizeof(T);
    char* src = (char*)&x + sizeof(T) - 1;
    char* dst = (char*)&ret;
    while (size-- > 0){
        *dst++ = *src--;
    }
    return ret;
}

#endif

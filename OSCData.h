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

#if defined(ESP8266) || defined(ESP32)
#define ESPxx
#endif
#if INT_MAX!=2147483647
typedef int32_t intOSC_t;
#else
typedef int intOSC_t;
#endif

//ERRORS/////////////////////////////////////////////////
typedef enum { OSC_OK = 0,
	BUFFER_FULL, INVALID_OSC, ALLOCFAILED, INDEX_OUT_OF_BOUNDS
} OSCErrorCode;
//OSC 1.0 'r': a 32-bit RGBA colour, sent as the four bytes r,g,b,a in that
//order.  These are already in wire order - they must not be byte-swapped.
typedef struct  {
	uint8_t r,g,b,a;
	} oscrgba_t;

//OSC 1.0 'm': a 4-byte MIDI message, sent as port id, status, data1, data2.
//
//OSC has no separate slot for the MIDI channel - a channel-voice channel lives
//in the low nibble of the status byte.  `channel` is kept here as a
//convenience and is folded into `status` when the message is encoded (see
//oscMidiStatusByte below), so both of these put 0x92 on the wire:
//
//    oscmidi_t m = {1, 0x90, 2, 0x3C, 0x40};  //status + separate channel
//    oscmidi_t m = {1, 0x92, 0, 0x3C, 0x40};  //channel already in the status
//
//On decode `status` holds the status byte exactly as received and `channel`
//mirrors its low nibble for channel-voice messages, so a decoded message
//re-encodes to the identical bytes.
typedef struct  {
	uint8_t port, status, channel, data1, data2;
} oscmidi_t;
extern osctime_t zerotime;
//OSC 1.0 reserves the timetag value 1 for "immediately"; 0 is 1900-01-01
extern osctime_t immediatetime;
extern oscrgba_t zeroRgba;
extern oscmidi_t zeroMidi;

//merges oscmidi_t's convenience `channel` field into the MIDI status byte.
//System messages (0xF0..0xFF) have no channel nibble and are left alone.
static inline uint8_t oscMidiStatusByte(const oscmidi_t & m){
	return (m.status < 0xF0) ? (uint8_t)(m.status | (m.channel & 0x0F)) : m.status;
}

//the channel a received status byte implies, or 0 if it carries no channel
static inline uint8_t oscMidiChannelOf(uint8_t status){
	return (status < 0xF0) ? (uint8_t)(status & 0x0F) : 0;
}

typedef enum {
OSC_NULL, OSC_IMPULSE
} oscevent_t;
	
	
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
        int64_t l; //long
		uint8_t * b; //blob
		oscrgba_t rgba;
		oscmidi_t midi;
        osctime_t time;
        oscevent_t event;
	} data;

	//overload the constructor to account for all the types and sizes
	OSCData(const char * s);
#if defined(__SAM3X8E__)
	OSCData (int16_t);
	OSCData (uint16_t);
#endif

	 OSCData (intOSC_t);
	 OSCData (int64_t);
#if INT_MAX!=2147483647
    OSCData (int);
#endif
    OSCData (unsigned int);
	OSCData (float);
	OSCData (double);
	OSCData (uint8_t *, int);
    //accepts another OSCData objects and clones it
	OSCData (OSCData *);
    OSCData  (boolean);
    OSCData  (oscrgba_t);
    OSCData  (oscmidi_t);
	OSCData  (oscevent_t);
    OSCData  (osctime_t);

	//destructor
	~OSCData();

    //GETTERS
    int32_t getInt();
    int64_t getInt64();

    float getFloat();
    double getDouble();
    int getString(char *);
    int getString(char *, int);
    int getString(char *, int, int, int);
    int getBlob(uint8_t *);
    int getBlob(uint8_t *, int);
    int getBlob(uint8_t *, int, int, int); 
    const uint8_t* getBlob();
    int getBlobLength();
    bool getBoolean();
    oscrgba_t getRgba();
    oscmidi_t getMidi();
    oscevent_t getEvent();

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

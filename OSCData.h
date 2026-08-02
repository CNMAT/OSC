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
// Prefer the core's own declaration of which port is the USB CDC port.
// Enumerating board macros here has repeatedly missed variants: Adafruit's
// Gemma M0 names its native USB port Serial while other SAMD21 variants name
// theirs SerialUSB, and only the ones that happen to pass -DARDUINO_SAMD_ZERO
// were being detected. SLIPEncodedSerial.h uses the same token so that the two
// definitions of this macro stay textually identical.
#if defined(SERIAL_PORT_USBVIRTUAL)
#define thisBoardsSerialUSB SERIAL_PORT_USBVIRTUAL
#elif defined(__SAM3X8E__)
#define thisBoardsSerialUSB SerialUSB
#else
#define thisBoardsSerialUSB Serial
#endif
#endif

#if defined(ESP8266) || defined(ESP32)
#define ESPxx
#endif
// The OSC 'i' type is 32 bits wide on every platform, so this is simply
// int32_t.  It used to be selected with an INT_MAX test that assumed
// "int is 32 bits" implied "int32_t is int"; on ARM newlib int32_t is
// long int -- a third type distinct from both int and long long -- which
// left add((int32_t)x) with no exact-match OSCData constructor and made the
// call ambiguous.  Constructor overloads below now cover every fundamental
// integer type, so this typedef no longer has to steer overload resolution.
typedef int32_t intOSC_t;

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

    //shared bodies for the integer constructors below
    void initInt32(int32_t v);
    void initInt64(int64_t v);

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

	// Integers.  C++ guarantees that these ten are distinct types on every
	// implementation, and that every fixed-width typedef (int32_t, uint32_t,
	// int64_t, size_t, ...) is an alias for one of them.  Declaring all ten
	// therefore gives an exact match for any integer argument on any target,
	// with no possibility of two declarations collapsing onto the same
	// signature -- which is what the old INT_MAX ladder could not guarantee.
	// Which OSC type code results is decided by width, not by spelling:
	// <= 4 bytes -> 'i' (int32), 8 bytes -> 'h' (int64).  OSC has no unsigned
	// type codes, so unsigned values are reinterpreted as signed of the same
	// width; the bit pattern is preserved and the receiver must know the
	// intended signedness.
	OSCData (signed char);
	OSCData (unsigned char);
	OSCData (short);
	OSCData (unsigned short);
	OSCData (int);
	OSCData (unsigned int);
	OSCData (long);
	OSCData (unsigned long);
	OSCData (long long);
	OSCData (unsigned long long);

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

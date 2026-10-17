/*
 Written by Yotam Mann, The Center for New Music and Audio Technologies,
 University of California, Berkeley.  Copyright (c) 2012, The Regents of
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

#ifndef OSCMESSAGE_h
#define OSCMESSAGE_h

#include "OSCData.h"
#include <Print.h>


class OSCMessage
{

private:

    //friends
	friend class OSCBundle;


/*=============================================================================
	PRIVATE VARIABLES
=============================================================================*/

	//the address
	char * address;

	//the data
	OSCData ** data;

	//the number of OSCData in the data array
	int dataCount;

	//how many slots the data array actually holds. It used to be realloc'd to
	//exactly dataCount+1 on every add(), i.e. one realloc per argument growing
	//by a single pointer. On a part with no heap compaction that is both slow
	//and fragmenting; it now doubles.
	int dataCapacity;

	//error codes for potential runtime problems
	OSCErrorCode error;

/*=============================================================================
    DECODING INCOMING BYTES
 =============================================================================*/

    //the decoding states for incoming bytes
    enum DecodeState {
        STANDBY,
        ADDRESS,
        ADDRESS_PADDING,
        TYPES,
        TYPES_PADDING,
        DATA,
        DATA_PADDING,
        DONE,
    } decodeState;

    //stores incoming bytes until they can be decoded
    uint8_t * incomingBuffer;
    int incomingBufferSize; // how many bytes are stored
    int incomingBufferFree; // how many bytes are allocated but unused

    //adds a byte to the buffer
    void addToIncomingBuffer(uint8_t);
    //clears the incoming buffer, keeping its capacity
    void clearIncomingBuffer();
    //releases capacity acquired by an unusually large packet
    void shrinkIncomingBuffer();

    //decoding function
    void decode(uint8_t);
    void decodeAddress();
    void decodeType(uint8_t);
    void decodeData(uint8_t);

/*=============================================================================
	HELPER FUNCTIONS
=============================================================================*/

	void setupMessage();

	//makes room for one more entry in the data array, doubling when full
	bool reserveOneMore();

	//appends one blob: the body of every blob add()
	OSCMessage& addBlob(uint8_t * blob, int length);

	//A blob length as an int, or -1, which OSCData refuses, when converting it
	//would change it. The conversion used to be unchecked, so a 64-bit
	//-4294967292 arrived as 4 and got past the negative-length check. For an
	//int length this compiles to nothing.
	template <typename N>
	static int intBlobLength(N length){
		int n = length;
		return (N)n == length ? n : -1;
	}

	//compares the OSCData's type char to a test char
	bool testType(int position, char type);

	//returns the number of bytes to pad to make it 4-bit aligned
    //	int padSize(int bytes);

public:

	//returns the OSCData at that position
	OSCData * getOSCData(int);

/*=============================================================================
	CONSTRUCTORS / DESTRUCTOR
=============================================================================*/

	//new constructor needs an address
	OSCMessage (const char * _address);
    //no address
    //placeholder since it's invalid OSC
	OSCMessage();

	//can accept all of the data after the address, each argument typed as
	//add() types it: OSCMessage msg("/xy", x, y);
	template <typename T, typename... Rest>
	OSCMessage (const char * _address, T first, Rest... rest) : OSCMessage(_address){
		add(first, rest...);
	}
    //created from another OSCMessage
    OSCMessage (OSCMessage *);

	//DESTRUCTOR
	~OSCMessage();

	//Deep copy. Without these the implicit versions copied the address, data
	//and incomingBuffer pointers, so the copy and the original both freed the
	//same memory -- `OSCMessage m = bundle.add("/a")` was an outright double
	//free, and the data went to the copy rather than to the bundle.
	OSCMessage (const OSCMessage &);
	OSCMessage& operator= (const OSCMessage &);

	//empties all of the data
	OSCMessage& empty();

/*=============================================================================
	SETTING  DATA
=============================================================================*/

	//returns the OSCMessage so that multiple 'add's can be strung together
	template <typename T>
	OSCMessage& add(T datum){
		//make a piece of data
		OSCData * d = new OSCData(datum);
		//check if it has any errors
		if (d->error == ALLOCFAILED){
			error = ALLOCFAILED;
		} else {
			//room for one more, doubling rather than growing by one pointer
			if (!reserveOneMore()){
				error = ALLOCFAILED;
				delete d;
			} else {
				data[dataCount] = d;
				dataCount++;
			}
		}
		return *this;
	}

	//several at once, each typed as add(x) types it:
	//msg.add(1, 2.5f, "three") is msg.add(1).add(2.5f).add("three")
	template <typename T, typename U, typename... Rest>
	OSCMessage& add(T first, U second, Rest... rest){
		add(first);
		return add(second, rest...);
	}

    //a blob is a uint8_t * followed by its length, alone or anywhere in a
    //list. The length is a template so these win over the list above whatever
    //integer type it is: a plain int parameter loses to the list for a size_t
    //or uint16_t length, and the pointer would go out as T. It also lets
    //intBlobLength() see the value before it becomes an int.
    template <typename N>
    OSCMessage& add(uint8_t * blob, N length){
		return addBlob(blob, intBlobLength(length));
	}
    template <typename N, typename V, typename... Rest>
    OSCMessage& add(uint8_t * blob, N length, V next, Rest... rest){
		addBlob(blob, intBlobLength(length));
		return add(next, rest...);
	}

	//sets the data at a position
	template <typename T>
	OSCMessage& set(int position, T datum){
		if (position >= 0 && position < dataCount){
			//Build the replacement BEFORE destroying the old one.  The old
			//order deleted first and then only stored the new pointer on
			//success, so a failed allocation left data[position] pointing at
			//freed memory -- read by the next getOSCData() and freed a second
			//time by empty()/~OSCMessage().
			OSCData * newDatum = new OSCData(datum);
			if (newDatum == NULL){
				error = ALLOCFAILED;
			} else if (newDatum->error == ALLOCFAILED){
				error = ALLOCFAILED;
				delete newDatum;
			} else {
				delete data[position];
				data[position] = newDatum;
			}
		} else if (position == (dataCount)){
			//add the data to the end
			add(datum);
		} else {
			//else out of bounds error
			error = INDEX_OUT_OF_BOUNDS;
		}
		return *this;
	}

    //blob specific setter, the same shape as set() above.  It was missed when
    //that one was hardened: with no lower bound set(-1, ...) stored the new
    //datum at data[-1], and it destroyed the old datum before building the new.
    //The length is a template, as in add(), so intBlobLength() sees its value.
    template <typename N>
    OSCMessage& set(int position, uint8_t * blob, N length){
		if (position >= 0 && position < dataCount){
			OSCData * newDatum = new OSCData(blob, intBlobLength(length));
			if (newDatum == NULL){
				error = ALLOCFAILED;
			} else if (newDatum->error != OSC_OK){
				//ALLOCFAILED, or INVALID_OSC for a bad length: see addBlob()
				error = newDatum->error;
				delete newDatum;
			} else {
				delete data[position];
				data[position] = newDatum;
			}
		} else if (position == (dataCount)){
			//add the data to the end
			add(blob, length);
		} else {
			//else out of bounds error
			error = INDEX_OUT_OF_BOUNDS;
		}
		return *this;
    }

    OSCMessage& setAddress(const char *);

/*=============================================================================
	GETTING DATA

	getters take a position as an argument
=============================================================================*/

	intOSC_t getInt(int);
    osctime_t getTime(int);

	int64_t getInt64(int);
	float getFloat(int);
	double getDouble(int);
    bool getBoolean(int);
    oscrgba_t getRgba(int);
    oscmidi_t getMidi(int);
    oscevent_t getEvent(int);

	//return the copied string's length
	int getString(int, char *);
	//check that it won't overflow the passed buffer's size with a third argument
	int getString(int, char *, int);
	//offset and size can be defined in order to only query a part of the string 
	int getString(int, char *, int, int, int);

	//returns the number of unsigned int8's copied into the buffer
	int getBlob(int, uint8_t *);
	//check that it won't overflow the passed buffer's size with a third argument
	int getBlob(int, uint8_t *, int);
	//offset and size can be defined in order to only query a part of the blob's content 
	int getBlob(int, uint8_t *, int, int, int);
	//get pointer to blob
	const uint8_t* getBlob(int);


	// returns the length of blob 
  	uint32_t getBlobLength(int position);

	//returns the number of bytes of the data at that position
	int getDataLength(int);

	//returns the type at the position
	char getType(int);

	//put the address in the buffer
	int getAddress(char * buffer, int offset = 0);
	int getAddress(char * buffer, int offset, int len);

	const char* getAddress();
	
	// Find out address length so we can create a buffer
	int getAddressLength(int offset = 0);


/*=============================================================================
	TESTING DATA

	testers take a position as an argument
=============================================================================*/

	bool isInt(int);
	bool isInt64(int);
	bool isFloat(int);
	bool isBlob(int);
	bool isChar(int);
	bool isString(int);
	bool isDouble(int);
    bool isBoolean(int);
    bool isTime(int);
    bool isRgba(int);
    bool isMidi(int);
    bool isEvent(int);

/*=============================================================================
	PATTERN MATCHING
=============================================================================*/

	//match the pattern against the address
	//returns true only for a complete match
	bool fullMatch( const char * pattern, int = 0);

	//returns the number of characters matched in the address
	int match( const char * pattern, int = 0);

	//calls the function with the message as the arg if it was a full match
	bool dispatch(const char * pattern, void (*callback)(OSCMessage &), int = 0);

	//like dispatch, but allows for partial matches
	//the address match offset is sent as an argument to the callback
	//also room for an option address offset to allow for multiple nested routes
	bool route(const char * pattern, void (*callback)(OSCMessage &, int), int = 0);



/*=============================================================================
	SIZE
=============================================================================*/

	//the number of data that the message contains
	int size();

	//computes the number of bytes the OSCMessage occupies if everything is 32-bit aligned
	int bytes();

/*=============================================================================
    TRANSMISSION
 =============================================================================*/

    //send the message
    OSCMessage& send(Print &p);

    //fill the message from a byte stream
    OSCMessage& fill(uint8_t);
    OSCMessage& fill(uint8_t *, int);

/*=============================================================================
	ERROR
=============================================================================*/

	bool hasError();

	OSCErrorCode getError();

};

#endif

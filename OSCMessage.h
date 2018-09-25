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
    //clears the incoming buffer
    void clearIncomingBuffer();

    //decoding function
    void decode(uint8_t);
    void decodeAddress();
    void decodeType(uint8_t);
    void decodeData(uint8_t);

/*=============================================================================
	HELPER FUNCTIONS
=============================================================================*/

	void setupMessage();

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

	//can optionally accept all of the data after the address
	//OSCMessage(const char * _address, char * types, ... );
    //created from another OSCMessage
    OSCMessage (OSCMessage *);

	//DESTRUCTOR
	~OSCMessage();

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
			//resize the data array
			OSCData ** dataMem = (OSCData **) realloc(data, sizeof(OSCData *) * (dataCount + 1));
			if (dataMem == NULL){
				error = ALLOCFAILED;
			} else {
				data = dataMem;
				//add data to the end of the array
				data[dataCount] = d;
				//increment the data size
				dataCount++;
			}
		}
		return *this;
	}

    //blob specific add
    OSCMessage& add(uint8_t * blob, int length){
		//make a piece of data
		OSCData * d = new OSCData(blob, length);
		//check if it has any errors
		if (d->error == ALLOCFAILED){
			error = ALLOCFAILED;
		} else {
			//resize the data array
			OSCData ** dataMem = (OSCData **) realloc(data, sizeof(OSCData *) * (dataCount + 1));
			if (dataMem == NULL){
				error = ALLOCFAILED;
			} else {
				data = dataMem;
				//add data to the end of the array
				data[dataCount] = d;
				//increment the data size
				dataCount++;
			}
		}
		return *this;
	}

	//sets the data at a position
	template <typename T>
	OSCMessage& set(int position, T datum){
		if (position < dataCount){
			//replace the OSCData with a new one
			OSCData * oldDatum = getOSCData(position);
			//destroy the old one
			delete oldDatum;
			//make a new one
			OSCData * newDatum = new OSCData(datum);
			//test if there was an error
			if (newDatum->error == ALLOCFAILED){
				error = ALLOCFAILED;
			} else {
				//otherwise, put it in the data array
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

    //blob specific setter
    OSCMessage& set(int position, uint8_t * blob, int length){
        if (position < dataCount){
			//replace the OSCData with a new one
			OSCData * oldDatum = getOSCData(position);
			//destroy the old one
			delete oldDatum;
			//make a new one
			OSCData * newDatum = new OSCData(blob, length);
			//test if there was an error
			if (newDatum->error == ALLOCFAILED){
				error = ALLOCFAILED;
			} else {
				//otherwise, put it in the data array
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

	int32_t getInt(int);
    osctime_t getTime(int);

	float getFloat(int);
	double getDouble(int);
    bool getBoolean(int);

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


	// returns the length of blob 
  	uint32_t getBlobLength(int position);

	//returns the number of bytes of the data at that position
	int getDataLength(int);

	//returns the type at the position
	char getType(int);

	//put the address in the buffer
	int getAddress(char * buffer, int offset = 0);
	int getAddress(char * buffer, int offset, int len);

	// TODO: int getAddressLength(int offset = 0);


/*=============================================================================
	TESTING DATA

	testers take a position as an argument
=============================================================================*/

	bool isInt(int);
	bool isFloat(int);
	bool isBlob(int);
	bool isChar(int);
	bool isString(int);
	bool isDouble(int);
    bool isBoolean(int);
    bool isTime(int);

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

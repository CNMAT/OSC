/*
 Written by Yotam Mann, The Center for New Music and Audio Technologies,
 University of California, Berkeley.  Copyright (c) 2012, 2013, The Regents of
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
 

 */

#ifndef OSCBUNDLE_h
#define OSCBUNDLE_h

#include "OSCMessage.h"

extern osctime_t zerotime;
class OSCBundle
{

private:

/*=============================================================================
	PRIVATE VARIABLES
=============================================================================*/

	//the array of messages contained in the bundle
	OSCMessage ** messages;

	//the number of messages in the array
	int numMessages;
    
    osctime_t timetag;
    
    //error codes
    OSCErrorCode error;
    
/*=============================================================================
 DECODING INCOMING BYTES
 =============================================================================*/
    
    //the decoding states for incoming bytes
    enum DecodeState {
        STANDBY,
        HEADER,
        TIMETAG,
        MESSAGE_SIZE,
        MESSAGE,
    } decodeState;
    
    //stores incoming bytes until they can be decoded
    uint8_t * incomingBuffer;
    int incomingBufferSize;
    
    //the size of the incoming message
    int incomingMessageSize;
    
    //adds a byte to the buffer
    void addToIncomingBuffer(uint8_t);
    //clears the incoming buffer
    void clearIncomingBuffer();
    
    //decoding functions
    void decode(uint8_t);
    void decodeTimetag();
    void decodeHeader();
    void decodeMessage(uint8_t);
    
    //just a placeholder while filling
    OSCMessage & add();


public:

/*=============================================================================
	CONSTRUCTORS / DESTRUCTOR
=============================================================================*/
		
    //default timetag of
      	OSCBundle(osctime_t = zerotime);

	//DESTRUCTOR
	~OSCBundle();

    //clears all of the OSCMessages inside
    OSCBundle& empty();
	
/*=============================================================================
    SETTERS
=============================================================================*/
    
	//start a new OSC Message in the bundle
    OSCMessage & add(const char * address);
    //add with nothing in it produces an invalid osc message
	//copies an existing message into the bundle
	OSCMessage & add(OSCMessage & msg);
    
    template <typename T>
    OSCBundle& setTimetag(T t){
        timetag = (osctime_t) t;
        return *this;
    }
    //sets the timetag from a buffer
    OSCBundle& setTimetag(uint8_t * buff){
        memcpy(&timetag, buff, 8);
        return *this;
    }
    
/*=============================================================================
    GETTERS
 =============================================================================*/

    //gets the message the matches the address string
	//will do regex matching
	OSCMessage * getOSCMessage(char * addr);
	
	//get message by position
	OSCMessage * getOSCMessage(int position);
	
/*=============================================================================
    MATCHING
=============================================================================*/

	//if the bundle contains a message that matches the pattern, 
	//call the function callback on that message
	bool dispatch(const char * pattern, void (*callback)(OSCMessage&), int = 0);
	
	//like dispatch, but allows for partial matches
	//the address match offset is sent as an argument to the callback
	bool route(const char * pattern, void (*callback)(OSCMessage&, int), int = 0);
	
/*=============================================================================
     SIZE
=============================================================================*/
	//returns the number of messages in the bundle;
	int size();
    
/*=============================================================================
    ERROR
 =============================================================================*/
    
	bool hasError();
    
	OSCErrorCode getError();
    
/*=============================================================================
    SENDING
 =============================================================================*/
    
    OSCBundle& send(Print &p);
    
/*=============================================================================
    FILLING
 =============================================================================*/
    
    OSCBundle& fill(uint8_t incomingByte);
    
    OSCBundle& fill(const uint8_t * incomingBytes, int length);
};

#endif

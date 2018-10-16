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

#include "OSCBundle.h"
#include <stdlib.h>

 /*=============================================================================
	CONSTRUCTORS / DESTRUCTOR
=============================================================================*/

OSCBundle::OSCBundle(osctime_t _timetag){
    setTimetag(_timetag);
    numMessages = 0;
    error = OSC_OK;
    messages = NULL;
    incomingBuffer = NULL;
    incomingBufferSize = 0;
    decodeState = STANDBY;
}

OSCBundle::~OSCBundle(){
    for (int i = 0; i < numMessages; i++){
        OSCMessage * msg = getOSCMessage(i);
        delete msg;
    }
    free(messages);
    free(incomingBuffer);
}

//clears all of the OSCMessages inside
OSCBundle& OSCBundle::empty(){
    error = OSC_OK;
    for (int i = 0; i < numMessages; i++){
        OSCMessage * msg = getOSCMessage(i);
        delete msg;
    }
    free(messages);
    messages = NULL;
    clearIncomingBuffer();
    numMessages = 0;
    return *this;
}

/*=============================================================================
 SETTERS
 =============================================================================*/

OSCMessage & OSCBundle::add(const char * _address){
	OSCMessage * msg = new OSCMessage(_address);
    if (!msg->hasError()){
        //realloc the array to fit the message
        OSCMessage ** messageMem = (OSCMessage **) realloc(messages, sizeof(OSCMessage *) * (numMessages + 1));
        if (messageMem != NULL){
            messages = messageMem;
            messages[numMessages] = msg;
            numMessages++;
        } else {
            error = ALLOCFAILED;
        }
    }
    return *msg;
}

OSCMessage & OSCBundle::add(){
	OSCMessage * msg = new OSCMessage();
    //realloc the array to fit the message
    OSCMessage ** messageMem = (OSCMessage **) realloc(messages, sizeof(OSCMessage *) * (numMessages + 1));
    if (messageMem != NULL){
        messages = messageMem;
        messages[numMessages] = msg;
        numMessages++;
    } else {
        error = ALLOCFAILED;
    }
    return *msg;
}

OSCMessage & OSCBundle::add(OSCMessage & _msg){
    OSCMessage * msg = new OSCMessage(&_msg);
    if (!msg->hasError()){
        //realloc the array to fit the message
        OSCMessage ** messageMem = (OSCMessage **) realloc(messages, sizeof(OSCMessage *) * (numMessages + 1));
        if (messageMem != NULL){
            messages = messageMem;
            messages[numMessages] = msg;
            numMessages++;
        } else {
            error = ALLOCFAILED;
        }
    }
    return *msg;
}

/*=============================================================================
    GETTERS
 =============================================================================*/

//returns the first fullMatch.
OSCMessage * OSCBundle::getOSCMessage( char * addr){
	for (int i = 0; i < numMessages; i++){
        OSCMessage * msg = getOSCMessage(i);
        if (msg->fullMatch(addr)){
            return msg;
        }
	}
	return NULL;
}

//the position is the same as the order they were declared in
OSCMessage * OSCBundle::getOSCMessage(int pos){
	if (pos < numMessages){
		return messages[pos];
	} 
	return NULL;
}

/*=============================================================================
    PATTERN MATCHING
 =============================================================================*/


bool OSCBundle::dispatch(const char * pattern, void (*callback)(OSCMessage&), int initial_offset){
	bool called = false;
	for (int i = 0; i < numMessages; i++){
        OSCMessage msg = getOSCMessage(i);
		called = msg.dispatch(pattern, callback, initial_offset) || called ;
	}
	return called;
}


bool OSCBundle::route(const char * pattern, void (*callback)(OSCMessage&, int), int initial_offset){
	bool called = false;
	for (int i = 0; i < numMessages; i++){
        OSCMessage msg = getOSCMessage(i);
		called =  msg.route(pattern, callback, initial_offset) || called;
	}
	return called;
}

/*=============================================================================
    SIZE
 =============================================================================*/


int OSCBundle::size(){
	return numMessages;
}

/*=============================================================================
 ERROR HANDLING
 =============================================================================*/

bool OSCBundle::hasError(){
    bool retError = error != OSC_OK;
    //test each of the data
    for (int i = 0; i < numMessages; i++){
        OSCMessage * msg = getOSCMessage(i);
        retError |= msg->hasError();
    }
	return retError;
}

OSCErrorCode OSCBundle::getError(){
    return error;
}


/*=============================================================================
 SENDING
 =============================================================================*/

OSCBundle& OSCBundle::send(Print &p){
    //don't send a bundle with errors
    if (hasError()){
        return *this;
    }
    //write the bundle header
    static uint8_t header[] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};
    p.write(header, 8);
    //write the timetag
{
    osctime_t time =  timetag;
    uint32_t d = BigEndian(time.seconds);
    uint8_t * ptr = (uint8_t *)    &d;
    p.write(ptr, 4);
    d = BigEndian(time.fractionofseconds);
    ptr = (uint8_t *)    &d;
    p.write(ptr, 4);
}

    //send the messages
    for (int i = 0; i < numMessages; i++){
        OSCMessage * msg = getOSCMessage(i);
        int msgSize = msg->bytes();
        //turn the message size into a pointer
        uint32_t s32 = BigEndian((uint32_t) msgSize);
        uint8_t * sptr = (uint8_t *) &s32;
        //write the message size
        p.write(sptr, 4);
        msg->send(p);
    }
    return *this;
}

/*=============================================================================
    FILLING
 =============================================================================*/

OSCBundle& OSCBundle::fill(uint8_t incomingByte){
    decode(incomingByte);
    return *this;
}

OSCBundle& OSCBundle::fill(const uint8_t * incomingBytes, int length){
    while (length--){
        decode(*incomingBytes++);
    }
    return *this;
}

/*=============================================================================
    DECODING
 =============================================================================*/

void OSCBundle::decodeTimetag(){
    //parse the incoming buffer as a uint64
    setTimetag(incomingBuffer);
    //make sure the endianness is right
    //xxx time tag    timetag = BigEndian(timetag);
    decodeState = MESSAGE_SIZE;
    clearIncomingBuffer();
}

void OSCBundle::decodeHeader(){
    const char * header = "#bundle";
    if (strcmp(header, (char *) incomingBuffer)!=0){
        //otherwise go back to the top and wait for a new bundle header
        decodeState = STANDBY;
        error = INVALID_OSC;
    } else {
        decodeState = TIMETAG;
    }
    clearIncomingBuffer();
}

void OSCBundle::decodeMessage(uint8_t incomingByte){
    //get the current message
    if (numMessages > 0){
       OSCMessage * lastMessage = messages[numMessages - 1];
        //put the bytes in there
        lastMessage->fill(incomingByte);
        //if it's all done
        if (incomingBufferSize == incomingMessageSize){
            //move onto the next message
            decodeState = MESSAGE_SIZE;
            clearIncomingBuffer();
        } else if (incomingBufferSize > incomingMessageSize){
            error = INVALID_OSC;
        }
    }
}

//does not validate the incoming OSC for correctness
void OSCBundle::decode(uint8_t incomingByte){
    addToIncomingBuffer(incomingByte);
    switch (decodeState){
        case STANDBY:
            if (incomingByte == '#'){
                decodeState = HEADER;
            } else if (incomingByte == '/'){
                add();//add a simple message to the bundle
                decodeMessage(incomingByte);
                decodeState = MESSAGE;
            }
            break;
        case HEADER:
			if (incomingBufferSize == 8){
                decodeHeader();
                decodeState = TIMETAG;
            }
			break;
		case TIMETAG:
            if (incomingBufferSize == 8){
                decodeTimetag();
                decodeState = MESSAGE_SIZE;
            }
			break;
		case MESSAGE_SIZE:
            if (incomingBufferSize == 4){
                //make sure the message size is valid
                int32_t msgSize;
                memcpy(&msgSize, incomingBuffer, 4);
                msgSize = BigEndian(msgSize);
                if (msgSize % 4 != 0 || msgSize == 0){
                    error = INVALID_OSC;
                } else {
                    //add a message to the buffer
                    decodeState = MESSAGE;
                    incomingMessageSize = msgSize;
                    clearIncomingBuffer();
                    //add a new empty message
                    add();
                }
            }
            break;
		case MESSAGE:
            decodeMessage(incomingByte);
            break;
    }
}


/*=============================================================================
 INCOMING BUFFER MANAGEMENT
 =============================================================================*/

void OSCBundle::addToIncomingBuffer(uint8_t incomingByte){
    //realloc some space for the new byte and stick it on the end
	incomingBuffer = (uint8_t *) realloc ( incomingBuffer, incomingBufferSize + 1);
	if (incomingBuffer != NULL){
		incomingBuffer[incomingBufferSize++] = incomingByte;
	} else {
		error = ALLOCFAILED;
	}
}

void OSCBundle::clearIncomingBuffer(){
    incomingBufferSize = 0;
    free(incomingBuffer);
    incomingBuffer = NULL;
}




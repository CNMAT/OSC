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

#include "OSCMessage.h"
#include "OSCMatch.h"

/*=============================================================================
	CONSTRUCTORS / DESTRUCTOR
=============================================================================*/

//constructor with address
OSCMessage::OSCMessage(const char * _address){
	setupMessage();
    setAddress(_address);
}

//constructor with nothing
//just a placeholder since the message is invalid
OSCMessage::OSCMessage(){
    setupMessage();
    error = INVALID_OSC;
}

//variable length constructor
//for example OSCMessage msg("/address", "isf", 1, "two", 3.0);
/*
OSCMessage::OSCMessage(const char * _address, char * types, ... ){
	setupMessage(_address);
}
 */

//sets up a new message
void OSCMessage::setupMessage(){
	address = NULL;
	//setup the attributes
	dataCount = 0;
	error = OSC_OK;
	//setup the space for data
	data = NULL;
    //setup for filling the message
    incomingBuffer = NULL;
    incomingBufferSize = 0;
    //set the decode state
    decodeState = STANDBY;
}

//DESTRUCTOR
OSCMessage::~OSCMessage(){
	//free everything that needs to be freed
    //free the address
	free(address);
    //free the data
    empty();
    //free the filling buffer
    free(incomingBuffer);
}

void OSCMessage::empty(){
    error = OSC_OK;
    //free each of hte data in the array
    for (int i = 0; i < dataCount; i++){
        OSCData * datum = getOSCData(i);
        //explicitly destruct the data
        //datum->~OSCData();
        delete datum;
    }
    //and free the array
    free(data);
    data = NULL;
    dataCount = 0;
}

//COPY
OSCMessage::OSCMessage(OSCMessage * msg){
	//start with a message with the same address
    setupMessage();
    setAddress(msg->address);
	//add each of the data to the other message
	for (int i = 0; i < msg->dataCount; i++){
        add(msg->data[i]);
	}
}

/*=============================================================================
	GETTING DATA
=============================================================================*/

OSCData * OSCMessage::getOSCData(int position){
	if (position < dataCount){
		OSCData * datum = data[position];
		return datum;
	} else {
		error = INDEX_OUT_OF_BOUNDS;
        return NULL;
	}
}

int32_t OSCMessage::getInt(int position){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
		return datum->getInt();
    } else {
        return NULL;
    }
}

float OSCMessage::getFloat(int position){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
		return datum->getFloat();
    } else {
        return NULL;
    }
}

double OSCMessage::getDouble(int position){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
		return datum->getDouble();
    } else {
        return NULL;
    }
}

int OSCMessage::getString(int position, char * buffer, int bufferSize){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
        //the number of bytes to copy is the smaller between the buffer size and the datum's byte length
        int copyBytes = bufferSize < datum->bytes? bufferSize : datum->bytes;
		return datum->getString(buffer, copyBytes);
    } else {
        return NULL;
    }
}

int OSCMessage::getBlob(int position, uint8_t * buffer, int bufferSize){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
        //the number of bytes to copy is the smaller between the buffer size and the datum's byte length
        int copyBytes = bufferSize < datum->bytes? bufferSize : datum->bytes;
		return datum->getBlob(buffer, copyBytes);
    } else {
        return NULL;
    }
}

char OSCMessage::getType(int position){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
		return datum->type;
	} else {
        return NULL;
    }
}

int OSCMessage::getDataLength(int position){
    OSCData * datum = getOSCData(position);
    if (!hasError()){
        return datum->bytes;
    } else {
        return 0;
    }
}

/*=============================================================================
	TESTING DATA
=============================================================================*/

bool OSCMessage::testType(int position, char type){
	OSCData * datum = getOSCData(position);
	if (!hasError()){
		return datum->type == type;
	} else {
		return false;
	}
}

bool OSCMessage::isInt(int position){
	return testType(position, 'i');
}

bool OSCMessage::isFloat(int position){
	return testType(position, 'f');
}

bool OSCMessage::isBlob(int position){
	return testType(position, 'b');
}

bool OSCMessage::isChar(int position){
	return testType(position, 'c');
}

bool OSCMessage::isString(int position){
	return testType(position, 's');
}

bool OSCMessage::isDouble(int position){
	return testType(position, 'd');
}


/*=============================================================================
	PATTERN MATCHING
=============================================================================*/

int OSCMessage::match(const  char * pattern, int addr_offset){
	int pattern_offset;
	int address_offset;
	int ret = osc_match(address + addr_offset, pattern, &pattern_offset, &address_offset);
	char * next = (char *) (address + addr_offset + pattern_offset);
	if (ret==3){
		return pattern_offset;
	} else if (pattern_offset > 0 && *next == '/'){
		return pattern_offset;
	} else {
		return 0;
	}
}

bool OSCMessage::fullMatch( const char * pattern, int addr_offset){
	int pattern_offset;
	int address_offset;
	int ret = osc_match(address + addr_offset, pattern, &address_offset, &pattern_offset);
	return (ret==3);
}

bool OSCMessage::dispatch(const char * pattern, void (*callback)(OSCMessage &), int addr_offset){
	if (fullMatch(pattern, addr_offset)){
		callback(*this);
		return true;
	} else {
		return false;
	}
}

bool OSCMessage::route(const char * pattern, void (*callback)(OSCMessage &, int), int initial_offset){
	int match_offset = match(pattern, initial_offset);
	if (match_offset>0){
		callback(*this, match_offset + initial_offset);
		return true;
	} else {
		return false;
	}
}

/*=============================================================================
    ADDRESS
 =============================================================================*/

int OSCMessage::getAddress(char * buffer, int offset){
    strcpy(buffer, address+offset);
	return strlen(buffer);
}

int OSCMessage::getAddress(char * buffer, int offset, int len){
    strncpy(buffer, address+offset, len);
	return strlen(buffer);
}

void OSCMessage::setAddress(const char * _address){
    //free the previous address
    free(address);
    //copy the address
	char * addressMemory = (char *) malloc( (strlen(_address) + 1) * sizeof(char) );
	if (addressMemory == NULL){
		error = ALLOCFAILED;
		address = NULL;
	} else {
		strcpy(addressMemory, _address);
		address = addressMemory;
	}
}

/*=============================================================================
	SIZE
=============================================================================*/

int OSCMessage::padSize(int _bytes){
    int space = (_bytes + 3) / 4;
    space *= 4;
	return space - _bytes;
}

//returns the number of OSCData in the OSCMessage
int OSCMessage::size(){
	return dataCount;
}

int OSCMessage::bytes(){
    int messageSize = 0;
    //send the address
    int addrLen = strlen(address) + 1;
    messageSize += addrLen;
    //padding amount
    int addrPad = padSize(addrLen);
    messageSize += addrPad;
    //add the comma seperator
    messageSize += 1;
    //add the types
    messageSize += dataCount;
    //pad the types
    int typePad = padSize(dataCount + 1);
    if (typePad == 0){
        typePad = 4;
    }
    messageSize+=typePad;
    //then the data
    for (int i = 0; i < dataCount; i++){
        OSCData * datum = getOSCData(i);
        messageSize+=datum->bytes;
        messageSize += padSize(datum->bytes);
    }
    return messageSize;
}

/*=============================================================================
	ERROR HANDLING
=============================================================================*/

bool OSCMessage::hasError(){
    bool retError = error != OSC_OK;
    //test each of the data
    for (int i = 0; i < dataCount; i++){
        OSCData * datum = getOSCData(i);
        retError |= datum->error != OSC_OK;
    }
	return retError;
}

OSCErrorCode OSCMessage::getError(){
    return error;
}

/*=============================================================================
    SENDING
 =============================================================================*/

void OSCMessage::send(Print &p){
    //don't send a message with errors
    if (hasError()){
        return;
    }
    uint8_t nullChar = '\0';
    //send the address
    int addrLen = strlen(address) + 1;
    //padding amount
    int addrPad = padSize(addrLen);
    //write it to the stream
    p.write((uint8_t *) address, addrLen);
    //add the padding
    while(addrPad--){
        p.write(nullChar);
    }
    //add the comma seperator
    p.write((uint8_t) ',');
    //add the types
    for (int i = 0; i < dataCount; i++){
        p.write((uint8_t) getType(i));
    }
    //pad the types
    int typePad = padSize(dataCount + 1);
    if (typePad == 0){
        typePad = 4;
    }
    while(typePad--){
        p.write(nullChar);
    }
    //write the data
    for (int i = 0; i < dataCount; i++){
        OSCData * datum = getOSCData(i);
        if (datum->type == 's'){
            p.write(datum->data.b, datum->bytes);
            int dataPad = padSize(datum->bytes);
            while(dataPad--){
                p.write(nullChar);
            }
        } else if(datum->type == 'b'){
            p.write(datum->data.b, datum->bytes);
            int dataPad = padSize(datum->bytes);
            while(dataPad--){
                p.write(nullChar);
            }
        } else if (datum->type == 'd'){
            double d = BigEndian(datum->data.d);
            uint8_t * ptr = (uint8_t *) &d;
            p.write(ptr, 8);
        } else {
            uint32_t i = BigEndian(datum->data.i);
            uint8_t * ptr = (uint8_t *) &i;
            p.write(ptr, datum->bytes);
        }
    }
}

/*=============================================================================
    FILLING
 =============================================================================*/

void OSCMessage::fill(uint8_t incomingByte){
    decode(incomingByte);
}

void OSCMessage::fill(uint8_t * incomingBytes, int length){
    while (length--){
        decode(*incomingBytes++);
    }
}

/*=============================================================================
    DECODING
 =============================================================================*/

void OSCMessage::decodeAddress(){
    setAddress((char *) incomingBuffer);
    //change the error from invalide message
    error = OSC_OK;
    clearIncomingBuffer();
}

void OSCMessage::decodeType(uint8_t incomingByte){
    char type = incomingByte;
    add(type);
}

void OSCMessage::decodeData(uint8_t incomingByte){
    //get the first OSCData to re-set
    for (int i = 0; i < dataCount; i++){
        OSCData * datum = getOSCData(i);
        if (datum->error == INVALID_OSC){
            //set the contents of datum with the data received
            switch (datum->type){
                case 'i':
                    if (incomingBufferSize == 4){
                        //parse the buffer as an int
                        union {
                            uint32_t i;
                            uint8_t b[4];
                        } u;
                        memcpy(u.b, incomingBuffer, 4);
                        int dataVal = BigEndian(u.i);
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;
                case 'f':
                    if (incomingBufferSize == 4){
                        //parse the buffer as an int
                        union {
                            float f;
                            uint8_t b[4];
                        } u;
                        memcpy(u.b, incomingBuffer, 4);
                        float dataVal = BigEndian(u.f);
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;
                case 'd':
                    if (incomingBufferSize == 8){
                        //parse the buffer as an int
                        union {
                            double d;
                            uint8_t b[8];
                        } u;
                        memcpy(u.b, incomingBuffer, 8);
                        double dataVal = BigEndian(u.d);
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;
                case 's':
                    if (incomingByte == 0){
                        char * str = (char *) incomingBuffer;
                        set(i, str);
                        clearIncomingBuffer();
                        decodeState = DATA_PADDING;
                    }
                    break;
                case 'b':
                    if (incomingBufferSize > 4){
                        //compute the expected blob size
                        union {
                            uint32_t i;
                            uint8_t b[4];
                        } u;
                        memcpy(u.b, incomingBuffer, 4);
                        int blobLength = BigEndian(u.i);
                        if (incomingBufferSize == blobLength + 4){
                            set(i, incomingBuffer + 4, blobLength);
                            clearIncomingBuffer();
                            decodeState = DATA_PADDING;
                        }
                        
                    }
                    break;
            }
            //break out of the for loop once we've selected the first invalid message
            break;
        } 
    }
}

//does not validate the incoming OSC for correctness
void OSCMessage::decode(uint8_t incomingByte){
    addToIncomingBuffer(incomingByte);
    switch (decodeState){
        case STANDBY:
            if (incomingByte == '/'){
                decodeState = ADDRESS;
            }
            break;
        case ADDRESS:
			if (incomingByte == 0){
				//end of the address
				//decode the address
                decodeAddress();
				//next state
				decodeState = ADDRESS_PADDING;
			} 
			break;
		case ADDRESS_PADDING:
            //it does not count the padding
			if (incomingByte==','){
				//next state
				decodeState = TYPES;
                clearIncomingBuffer();
			}
			break;
		case TYPES:
			if (incomingByte != 0){
				//next state
                decodeType(incomingByte);
			} else {
                decodeState = TYPES_PADDING;
            }
            //FALL THROUGH to test if it should go to the data state
		case TYPES_PADDING: {
                //compute the padding size for the types
                //to determine the start of the data section
                int typePad = padSize(dataCount + 1);
                if (typePad == 0){
                    typePad = 4;
                }
                if (incomingBufferSize == (typePad + dataCount)){
                    clearIncomingBuffer();
                    decodeState = DATA;
                }
            }
			break;
		case DATA:
            decodeData(incomingByte);
            break;
		case DATA_PADDING:{
                //get hte last valid data
                for (int i = dataCount - 1; i >= 0; i--){
                    OSCData * datum = getOSCData(i);
                    if (datum->error == OSC_OK){
                        //compute the padding size for the data
                        int dataPad = padSize(datum->bytes);
                        if (incomingBufferSize == dataPad){
                            clearIncomingBuffer();
                            decodeState = DATA;
                        }
                        break;
                    }
                }
            }
			break;

    }
}


/*=============================================================================
    INCOMING BUFFER MANAGEMENT
 =============================================================================*/

void OSCMessage::addToIncomingBuffer(uint8_t incomingByte){
    //realloc some space for the new byte and stick it on the end
	incomingBuffer = (uint8_t *) realloc ( incomingBuffer, incomingBufferSize + 1);
	if (incomingBuffer != NULL){
		incomingBuffer[incomingBufferSize++] = incomingByte;
	} else {
		error = ALLOCFAILED;
	}
}

void OSCMessage::clearIncomingBuffer(){
    incomingBufferSize = 0;
    free(incomingBuffer);
    incomingBuffer = NULL;
}
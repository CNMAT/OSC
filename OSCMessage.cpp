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
#include "OSCTiming.h"

extern osctime_t zerotime;
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
	dataCapacity = 0;
    //setup for filling the message
    incomingBuffer = NULL;
    incomingBufferSize = 0;
    incomingBufferFree = 0;
    clearIncomingBuffer();
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

OSCMessage& OSCMessage::empty(){
    error = OSC_OK;
    //free each of the data in the array
    for (int i = 0; i < dataCount; i++){
        const auto datum = getOSCData(i);
        //explicitly destruct the data
        //datum->~OSCData();
        delete datum;
    }
    //and free the array
    free(data);
    data = NULL;
    dataCount = 0;
    dataCapacity = 0;
    decodeState = STANDBY;
    clearIncomingBuffer();
    shrinkIncomingBuffer();
    return *this;
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

//Deep copy. The implicit copy constructor copied address, data and
//incomingBuffer as raw pointers, so the copy and the original both freed the
//same memory. `OSCMessage m = bundle.add("/a")` was a double free, and any
//data added afterwards went to the copy rather than to the bundle.
OSCMessage::OSCMessage(const OSCMessage & other){
	setupMessage();
	if (other.address != NULL){
		setAddress(other.address);
	}
	for (int i = 0; i < other.dataCount; i++){
		add(other.data[i]);
	}
	if (error == OSC_OK){
		error = other.error;
	}
}

OSCMessage& OSCMessage::operator=(const OSCMessage & other){
	if (this == &other){
		return *this;
	}
	empty();
	free(address);
	address = NULL;
	if (other.address != NULL){
		setAddress(other.address);
	}
	for (int i = 0; i < other.dataCount; i++){
		add(other.data[i]);
	}
	if (error == OSC_OK){
		error = other.error;
	}
	return *this;
}

//Doubles the data array when it is full. Starting at four keeps the common
//short message to a single allocation, and an N-argument message now costs
//log2(N) reallocs instead of N.
bool OSCMessage::reserveOneMore(){
	if (dataCount < dataCapacity){
		return true;
	}
	int want = dataCapacity ? dataCapacity * 2 : 4;
	OSCData ** mem = (OSCData **) realloc(data, sizeof(OSCData *) * want);
	if (mem == NULL){
		return false;
	}
	data = mem;
	dataCapacity = want;
	return true;
}

/*=============================================================================
	GETTING DATA
=============================================================================*/

OSCData * OSCMessage::getOSCData(int position){
	//position >= 0: without the lower bound a negative index read data[-1]
	//and then dereferenced whatever happened to be in front of the array
	if (position >= 0 && position < dataCount){
		const auto datum = data[position];
		return datum;
	} else {
		error = INDEX_OUT_OF_BOUNDS;
        return nullptr;
	}
}

intOSC_t OSCMessage::getInt(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getInt();
    } else {
        return 0;
    }
}
int64_t OSCMessage::getInt64(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getInt64();
    } else {
        return 0;
    }
}
osctime_t OSCMessage::getTime(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getTime();
    } else {
        return zerotime;
    }
}
float OSCMessage::getFloat(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getFloat();
    } else {
        return 0.0f;
    }
}
oscrgba_t OSCMessage::getRgba(int position) {
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getRgba();
	} else {
	return zeroRgba;
	}
}
oscmidi_t OSCMessage::getMidi(int position) {
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getMidi();
	} else {
	return zeroMidi;
	}
}
double OSCMessage::getDouble(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getDouble();
    } else {

        return 0.0;
    }
}

bool  OSCMessage::getBoolean(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getBoolean();
    } else {
        return false;
    }
}

oscevent_t  OSCMessage::getEvent(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->getEvent();
    } else {
        return OSC_NULL;
    }
}

int OSCMessage::getString(int position, char * buffer){
    const auto datum = getOSCData(position);
    if (!hasError()){
        return datum->getString(buffer, datum->bytes);
    } else {
    return -1;
    }
}

int OSCMessage::getString(int position, char * buffer, int bufferSize){
	const auto datum = getOSCData(position);
	if (!hasError()){
        //the number of bytes to copy is the smaller between the buffer size and the datum's byte length
        int copyBytes = bufferSize < datum->bytes? bufferSize : datum->bytes;
		return datum->getString(buffer, copyBytes);
    } else {
    return -1;
    }
}

int OSCMessage::getString(int position, char * buffer, int bufferSize, int offset, int size){
    const auto datum = getOSCData(position);
    if (!hasError()){
        //the number of bytes to copy is the smaller between the buffer size and the datum's byte length
        int copyBytes = bufferSize < datum->bytes? bufferSize : datum->bytes;
        return datum->getString(buffer, copyBytes, offset, size);
    } else {
    return -1;
    }
}


int OSCMessage::getBlob(int position, uint8_t * buffer){
    const auto datum = getOSCData(position);
    if (!hasError()){
        return datum->getBlob(buffer);
  } else {
    return -1;
  }
}

int OSCMessage::getBlob(int position, uint8_t * buffer, int bufferSize){
    const auto datum = getOSCData(position);
    if (!hasError()){
        return datum->getBlob(buffer, bufferSize);
  } else {
    return -1;
  }
}

int OSCMessage::getBlob(int position, uint8_t * buffer, int bufferSize, int offset, int size){
    const auto datum = getOSCData(position);
    if (!hasError()){
        return datum->getBlob(buffer, bufferSize, offset, size);
  } else {
    return -1;
  }
}

const uint8_t*  OSCMessage::getBlob(int position) {
	const auto datum = getOSCData(position);
	if(!hasError()) {
		return datum->getBlob();
	} else {
		return nullptr;
	}
}

uint32_t OSCMessage::getBlobLength(int position)
{
  const auto datum = getOSCData(position);
  if (!hasError()){
    return datum->getBlobLength();
  } else {
    return 0;
  }

}

char OSCMessage::getType(int position){
	const auto datum = getOSCData(position);
	if (!hasError()){
		return datum->type;
	} else {
            return '\0';
    }
}

int OSCMessage::getDataLength(int position){
    const auto datum = getOSCData(position);
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
	//Bounds-check directly instead of going through getOSCData(), which raises
	//INDEX_OUT_OF_BOUNDS on the message. That error is sticky and message-wide,
	//so probing a position that turned out not to exist -- the natural way to
	//use these predicates -- permanently poisoned the message, after which
	//send() refused to emit anything at all.
	if (position < 0 || position >= dataCount){
		return false;
	}
	//Nor should an unrelated pre-existing error make a type test lie; report
	//what the datum actually is.
	return data[position]->type == type;
}

bool OSCMessage::isInt(int position){
	return testType(position, 'i');
}
bool OSCMessage::isInt64(int position){
	return testType(position, 'h');
}
bool OSCMessage::isTime(int position){
	return testType(position, 't');
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
bool OSCMessage::isMidi(int position){
	return testType(position, 'm');
}
bool OSCMessage::isRgba(int position){
	return testType(position, 'r');
}
bool OSCMessage::isEvent(int position){
	return testType(position, 'N') || testType(position, 'I');
}
bool OSCMessage::isBoolean(int position){
	return testType(position, 'T') || testType(position, 'F');
}


/*=============================================================================
	PATTERN MATCHING
=============================================================================*/

int OSCMessage::match(const  char * pattern, int addr_offset){
	int pattern_offset;
	int address_offset;
	if (address == NULL){ return 0; }
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
	if (address == NULL){ return false; }
	int ret = osc_match(address + addr_offset, pattern, &pattern_offset, &address_offset );
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
	if (address == NULL){ *buffer = 0; return 0; }
	int result = strlen(address);
	if (result > offset)
		strcpy(buffer, address+offset);
	else
		*buffer = 0;
	return result - offset; // could be negative!
}

int OSCMessage::getAddress(char * buffer, int offset, int len){
	if (address == NULL || len <= 0){ if (len > 0) *buffer = 0; return 0; }
	int result = strlen(address);
	
	if (result > offset)
	{
		strncpy(buffer, address+offset, len); // N.B. NOT guaranteed to null-terminate! So...
		buffer[len-1] = 0; // ...prevent strlen() blowing up
	}
	else
		*buffer = 0;
	return strlen(buffer);
}

const char* OSCMessage::getAddress(){
	return address;
}

int OSCMessage::getAddressLength(int offset)
{
	if (address == NULL){ return 0; }
	int result = (int) strlen(address) - offset;
	if (result < 0) // offset past end!
		result = 0; // do the best we can
	return result;
}

OSCMessage& OSCMessage::setAddress(const char * _address){
    //free the previous address
    free(address); // are we sure address was allocated?
    //copy the address
	char * addressMemory = (char *) malloc( (strlen(_address) + 1) * sizeof(char) );
	if (addressMemory == NULL){
		error = ALLOCFAILED;
		address = NULL;
	} else {
		strcpy(addressMemory, _address);
		address = addressMemory;
	}
    return *this;
}

/*=============================================================================
	SIZE
=============================================================================*/

#ifdef SLOWpadcalculation
int OSCMessage::padSize(int _bytes){
    int space = (_bytes + 3) / 4;
    space *= 4;
	return space - _bytes;
}
#else
static inline  int padSize(int bytes) { return (4- (bytes&03))&3; }
#endif
//returns the number of OSCData in the OSCMessage
int OSCMessage::size(){
	return dataCount;
}

int OSCMessage::bytes(){
    //A message whose address never decoded - random bytes in, or a
    //default-constructed placeholder - has a NULL address. Every accessor
    //below has to survive that; this one used to strlen(NULL).
    if (address == NULL){
        return 0;
    }
    int messageSize = 0;
    //send the address
    int addrLen = strlen(address) + 1;
    messageSize += addrLen;
    //padding amount
    int addrPad = padSize(addrLen);
    messageSize += addrPad;
    //add the comma separator
    messageSize += 1;
    //add the types
    messageSize += dataCount;
    //pad the types
    int typePad = padSize(dataCount + 1);   //for the comma
    if (typePad == 0){
         typePad = 4; // to make sure the type string is null terminated
    }
    messageSize+=typePad;
    //then the data
    for (int i = 0; i < dataCount; i++){
        const auto datum = getOSCData(i);
        messageSize+=datum->bytes;
        messageSize += padSize(datum->bytes);
    }
    return messageSize;
}

/*=============================================================================
	ERROR HANDLING
=============================================================================*/

bool OSCMessage::hasError(){
	if(error != OSC_OK) return true;
    //test each of the data
    for (int i = 0; i < dataCount; i++){
        if(getOSCData(i)->error) return true;
    }
	return false;
}

OSCErrorCode OSCMessage::getError(){
    return error;
}

/*=============================================================================
    SENDING
 =============================================================================*/

OSCMessage& OSCMessage::send(Print &p){
    //don't send a message with errors
    if (hasError()){
        return *this;
    }
    if (address == NULL){
        return *this;
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
    //add the comma separator
    p.write((uint8_t) ',');
    //add the types
#ifdef PAULSSUGGESTION
    // Paul suggested buffering on the stack
    // to improve performance. The problem is this could exhaust the stack
    // for long complex messages
    {
        uint8_t typstr[dataCount];

        for (int i = 0; i < dataCount; i++){
            typstr[i] =  getType(i);
        }
        p.write(typstr,dataCount);
    }
#else
    for (int i = 0; i < dataCount; i++){
        p.write((uint8_t) getType(i));
    }
#endif
    //pad the types
    int typePad = padSize(dataCount + 1); // 1 is for the comma
    if (typePad == 0){
            typePad = 4;  // This is because the type string has to be null terminated
    }
    while(typePad--){
        p.write(nullChar);
    }
    //write the data
    for (int i = 0; i < dataCount; i++){
        const auto datum = getOSCData(i);
        if ((datum->type == 's') || (datum->type == 'b')){
            p.write(datum->data.b, datum->bytes);
            int dataPad = padSize(datum->bytes);
            while(dataPad--){
                p.write(nullChar);
            }
        } else if (datum->type == 'd'){
            double d = BigEndian(datum->data.d);
            uint8_t * ptr = (uint8_t *) &d;
            p.write(ptr, 8);
        } else if (datum->type == 'h'){
            //the 32-bit branch below byte-swapped only the low word and then
            //read 8 bytes out of a 4-byte local, so 'h' went on the wire as
            //garbage plus whatever followed it on the stack
            int64_t l = BigEndian(datum->data.l);
            uint8_t * ptr = (uint8_t *) &l;
            p.write(ptr, 8);
        } else if (datum->type == 't'){
            osctime_t time =  datum->data.time;
            uint32_t d = BigEndian(time.seconds);
            uint8_t * ptr = (uint8_t *)    &d;
            p.write(ptr, 4);
            d = BigEndian(time.fractionofseconds);
            ptr = (uint8_t *)    &d;
            p.write(ptr, 4);

        } else if (datum->type == 'r'){
            //'r' is a byte array, not a scalar: r,g,b,a already in wire order,
            //so it must not go through BigEndian()
            const oscrgba_t & c = datum->data.rgba;
            uint8_t rgba[4] = { c.r, c.g, c.b, c.a };
            p.write(rgba, 4);
        } else if (datum->type == 'm'){
            //'m' is a byte array too: port id, status, data1, data2.
            //oscmidi_t's separate `channel` field folds into the status byte.
            const oscmidi_t & m = datum->data.midi;
            uint8_t midi[4] = { m.port, oscMidiStatusByte(m), m.data1, m.data2 };
            p.write(midi, 4);
        } else if (datum->type == 'T' || datum->type == 'F')
                    { }
        else { // float or int
            uint32_t i = BigEndian(datum->data.i);
            uint8_t * ptr = (uint8_t *) &i;
            p.write(ptr, datum->bytes);
        }
    }
    return *this;
}

/*=============================================================================
    FILLING
 =============================================================================*/

OSCMessage& OSCMessage::fill(uint8_t incomingByte){
    decode(incomingByte);
    return *this;
}

OSCMessage& OSCMessage::fill(uint8_t * incomingBytes, int length){
    while (length--){
        decode(*incomingBytes++);
    }
    return *this;
}

/*=============================================================================
    DECODING
 =============================================================================*/

void OSCMessage::decodeAddress(){
    setAddress((char *) incomingBuffer);
    //Only clear the "not decoded yet" error if the address was actually
    //stored.  setAddress() reports a failed malloc by setting ALLOCFAILED and
    //leaving address NULL; clearing error unconditionally erased that, so the
    //message reported hasError()==false with a NULL address and every
    //match()/send()/getAddress() path dereferenced it.
    if (address != NULL){
        error = OSC_OK;
    }
    clearIncomingBuffer();
}

void OSCMessage::decodeType(uint8_t incomingByte){
    char type = incomingByte;
    add(type);
}

void OSCMessage::decodeData(uint8_t incomingByte){
    //get the first OSCData to re-set
    for (int i = 0; i < dataCount; i++){
        const auto datum = getOSCData(i);
        if (datum->error == INVALID_OSC){
            //set the contents of datum with the data received
            switch (datum->type){
                case 'i':
                    if (incomingBufferSize == 4){
                        //parse the buffer as an int
                        union {
                            intOSC_t i;
                            uint8_t b[4];
                        } u;
                        memcpy(u.b, incomingBuffer, 4);
                        intOSC_t dataVal = BigEndian(u.i);
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;
                case 'f':
                    if (incomingBufferSize == 4){
                        //parse the buffer as a float
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
                case 'r':
                    if (incomingBufferSize == 4){
                        //the four bytes are r,g,b,a in wire order - no swap
                        oscrgba_t dataVal;
                        dataVal.r = incomingBuffer[0];
                        dataVal.g = incomingBuffer[1];
                        dataVal.b = incomingBuffer[2];
                        dataVal.a = incomingBuffer[3];
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;
                case 'h':
                    if (incomingBufferSize == 8){
                        //parse the buffer as a 64 bit integer
                        union {
                            int64_t longint;
                            uint8_t b[8];
                        } u;
                        memcpy(u.b, incomingBuffer, 8);
                        int64_t dataVal = BigEndian(u.longint);
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;

                case 'm':
                    if (incomingBufferSize == 4){
                        //port id, status, data1, data2 in wire order - no swap.
                        //`status` is kept exactly as received and `channel`
                        //mirrors its low nibble, so this re-encodes unchanged.
                        oscmidi_t dataVal;
                        dataVal.port = incomingBuffer[0];
                        dataVal.status = incomingBuffer[1];
                        dataVal.channel = oscMidiChannelOf(incomingBuffer[1]);
                        dataVal.data1 = incomingBuffer[2];
                        dataVal.data2 = incomingBuffer[3];
                        set(i, dataVal);
                        clearIncomingBuffer();
                    }
                    break;
                case 'd':
                    if (incomingBufferSize == 8){
                        //parse the buffer as a double
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
                case 't':
                    if (incomingBufferSize == 8){
                        //parse the buffer as a timetag
                        union {
                            osctime_t t;
                            uint8_t b[8];
                        } u;
                        memcpy(u.b, incomingBuffer, 8);

                        u.t.seconds = BigEndian(u.t.seconds);
                        u.t.fractionofseconds = BigEndian(u.t.fractionofseconds);
                        set(i, u.t);
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
                    //>= 4, not > 4: a zero-length blob is four bytes of length
                    //prefix and no payload, so the buffer never grows past 4
                    //and the test never fired.  Such a blob is legal OSC and
                    //this library emits it, but could not read its own output.
                    if (incomingBufferSize >= 4){
                        //compute the expected blob size
                        union {
                            uint32_t i;
                            uint8_t b[4];
                        } u;
                        memcpy(u.b, incomingBuffer, 4);
                        uint32_t blobLength = BigEndian(u.i);
                        if (incomingBufferSize == (int)(blobLength + 4)){
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
            int typePad = padSize(dataCount + 1); // 1 is the comma
                if (typePad == 0){
                    typePad = 4;     // to make sure it will be null terminated
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
                //Find the datum whose padding we are consuming.  Only 's' and
                //'b' enter this state, so the datum we just completed always
                //has bytes > 0.  Data not yet decoded are still INVALID_OSC,
                //which is what makes this backwards scan work -- but the
                //zero-byte types 'T' 'F' 'I' 'N' are OSC_OK from the moment
                //their type tag is read.  Without the bytes>0 test, a zero-byte
                //type sitting after the string would be picked up here instead,
                //padSize(0) would report no padding, and the string's pad byte
                //would be left in the buffer and consumed as the first byte of
                //the next argument -- silently shifting every remaining
                //argument by one byte, with no error raised.
                for (int i = dataCount - 1; i >= 0; i--){
                    const auto datum = getOSCData(i);
                    if (datum->error == OSC_OK && datum->bytes > 0){
                        //compute the padding size for the data
                        int dataPad = padSize(datum->bytes);
                        //  if there is no padding required, switch back to DATA, and don't clear the incomingBuffer because it holds next data
                        if (dataPad == 0){
                             decodeState = DATA;
                             //This byte belongs to the next argument.  It is
                             //already in the buffer, but it has never been
                             //offered to decodeData(), and the 's' handler
                             //triggers on the byte's *value* rather than on the
                             //buffer contents.  A zero-length string is nothing
                             //but its terminating null, so without this
                             //dispatch that terminator is never seen: the empty
                             //string instead swallows the first byte of
                             //whatever follows and every later argument shifts.
                             decodeData(incomingByte);
                        }
                        else if (incomingBufferSize == dataPad){
                            clearIncomingBuffer();
                            decodeState = DATA;
                        }
                        break;
                    }
                }
            }
			break;
		case DONE:
			break; // TODO: is this correct? - was missing from original code, it did this by default
    }
}


/*=============================================================================
    INCOMING BUFFER MANAGEMENT
 =============================================================================*/
#define OSCPREALLOCATEIZE 16
//capacity above which empty() gives the extra back rather than holding it
#define OSCINCOMINGKEEP 64
void OSCMessage::addToIncomingBuffer(uint8_t incomingByte){
    //refuse to grow without bound on a length taken from the wire
    if (incomingBufferSize >= OSC_MAX_INCOMING){
        error = BUFFER_FULL;
        return;
    }
    //realloc some space for the new byte and stick it on the end
    if(incomingBufferFree>0)
    {
            incomingBuffer[incomingBufferSize++] = incomingByte;
            incomingBufferFree--;
    }
    else
	{

        //Double rather than adding a fixed 16. A 200-byte blob used to take
        //about thirteen reallocs to accumulate; it now takes four. Clamped to
        //OSC_MAX_INCOMING so doubling cannot overshoot the cap.
        int capacity = incomingBufferSize + incomingBufferFree;
        int want = capacity ? capacity * 2 : OSCPREALLOCATEIZE;
        if (want > OSC_MAX_INCOMING){
            want = OSC_MAX_INCOMING;
        }
        if (want <= incomingBufferSize){
            want = incomingBufferSize + 1;
        }
        uint8_t * mem = (uint8_t *) realloc(incomingBuffer, want);
        if (mem != NULL){
            incomingBuffer = mem;
            incomingBuffer[incomingBufferSize++] = incomingByte;
            incomingBufferFree = want - incomingBufferSize;
        } else {
            error = ALLOCFAILED;
        }
    }
}

//Called after every completed argument, so this is the hot path. It used to
//realloc the buffer back down to OSCPREALLOCATEIZE every single time, even when
//it was already that size, which cost roughly one realloc per argument: a
//sixteen-argument message spent 24 reallocs where it now spends 5. Keep
//whatever capacity is already there and just forget the contents.
//shrinkIncomingBuffer() below is what actually gives memory back.
void OSCMessage::clearIncomingBuffer(){
    if (incomingBuffer == NULL){
        incomingBuffer = (uint8_t *) malloc(OSCPREALLOCATEIZE);
        if (incomingBuffer == NULL){
            error = ALLOCFAILED;
            incomingBufferSize = 0;
            incomingBufferFree = 0;
            return;
        }
        incomingBufferFree = OSCPREALLOCATEIZE;
        incomingBufferSize = 0;
        return;
    }
    incomingBufferFree += incomingBufferSize;   //capacity is unchanged
    incomingBufferSize = 0;
}

//Hand back memory that an unusually large packet caused us to acquire, but
//leave a normal working buffer in place so the steady state stays allocation
//free. Called from empty(), i.e. once the caller has said it is done with the
//message, never from the per-argument path.
void OSCMessage::shrinkIncomingBuffer(){
    int capacity = incomingBufferSize + incomingBufferFree;
    if (capacity <= OSCINCOMINGKEEP){
        return;
    }
    uint8_t * mem = (uint8_t *) realloc(incomingBuffer, OSCPREALLOCATEIZE);
    if (mem != NULL){
        incomingBuffer = mem;
        incomingBufferSize = 0;
        incomingBufferFree = OSCPREALLOCATEIZE;
    }
}


#include "OSCData.h"


osctime_t zerotime = {0,0};
/*=============================================================================
	CONSTRUCTORS

	overloaded methods for each of the types which will
	set the type flag, the size (in bytes), and the data
=============================================================================*/


OSCData::OSCData(const char * s){
	error = OSC_OK;
	type = 's';
	bytes = (strlen(s) + 1);
	//own the data
	char * mem = (char *) malloc(bytes);
	if (mem == NULL){
		error = ALLOCFAILED;
	} else {
		strcpy(mem, s);
		data.s = mem;
	}
}




OSCData::OSCData(int32_t i){
	error = OSC_OK;
	type = 'i';
	bytes = 4;
	data.i = i;
}
#ifndef ESPxx
OSCData::OSCData(int i){
	error = OSC_OK;
	type = 'i';
	bytes = 4;
	data.i = i;
}
#endif
OSCData::OSCData(unsigned int i){
	error = OSC_OK;
	type = 'i';
	bytes = 4;
	data.i = i;
}
#if defined(__SAM3X8E__)
OSCData::OSCData(int16_t i){
	error = OSC_OK;
	type = 'i';
	bytes = 4;
	data.i = i;
}
#endif

OSCData::OSCData(float f){
	error = OSC_OK;
	type = 'f';
	bytes = 4;
	data.f = f;
}

OSCData::OSCData(osctime_t t){
	error = OSC_OK;
	type = 't';
	bytes = 8;
	data.time = t;
}
OSCData::OSCData(boolean b){
	error = OSC_OK;
	type = b?'T':'F';
	bytes = 0;
}


OSCData::OSCData(double d){
	error = OSC_OK;
	bytes = sizeof(double);
	//if it's not 8 bytes it's not a true double
	if (bytes == 8){
		type = 'd';
		data.d = d;
	} else {
		type = 'f';
		data.f = d;
	}
}

OSCData::OSCData(uint8_t * b, int len){
	error = OSC_OK;
	type = 'b';
	bytes = len + 4;
	//add the size to the front of the blob
	uint32_t len32 = (uint32_t) len;
    //make sure the length is endian-safe
    len32 = BigEndian(len32);
	uint8_t * lenPtr = (uint8_t *) (& len32);
	//own the data
	if(bytes>0)
    {

        uint8_t * mem = (uint8_t * ) malloc(bytes);
        if (mem == NULL){
            error = ALLOCFAILED;
        } else {
            //copy over the blob length
            memcpy(mem, lenPtr, 4);
            //copy over the blob data
            memcpy(mem + 4, b, len);
            data.b = mem;
        }
    }
    else
        data.b = 0;
}

OSCData::OSCData (OSCData * datum){
	error = OSC_OK;
	type = datum->type;
	bytes = datum->bytes;
	if ( (type == 'i') || (type == 'f') || (type == 'd') || (type == 't')
        || (type == 'h') || (type == 'c') || (type == 'r') || (type == 'm')
        )
    {
		data = datum->data;
	} else if ((type == 's') || (type == 'b')){
		//allocate a new piece of memory
        uint8_t * mem = (uint8_t * ) malloc(bytes);
        if (mem == NULL){
            error = ALLOCFAILED;
        } else {
            //copy over the blob length
            memcpy(mem, datum->data.b, bytes);
            data.b = mem;
        }
	}
}

//DESTRUCTOR
OSCData::~OSCData(){
    //if there are no bytes, there is nothing to free
    if (bytes>0){
        //if the data is of type 's' or 'b', need to free that memory
        if (type == 's'){
            free(data.s);
        }else if( type == 'b'){
            free(data.b);
        }
    }
}

//sets just the type as a message placeholder
//no data
OSCData::OSCData(char t){
	error = (t == 'T' || t == 'F') ? OSC_OK : INVALID_OSC;
	type = t;
    bytes = 0;
}

/*=============================================================================
    GETTERS

    perform a safety check to make sure the data type matches the request
    otherwise returns NULL
=============================================================================*/

int32_t OSCData::getInt(){
    if (type == 'i'){
        return data.i;
    } else {
    #ifndef ESPxx
        return (int32_t)NULL;
    #else
        return -1;
    #endif
    }
}
osctime_t OSCData::getTime(){
    if (type == 't'){
        return data.time;
    } else {

        return zerotime;
    }
}
float OSCData::getFloat(){
    if (type == 'f'){
        return data.f;
    } else {
    #ifndef ESPxx
        return (float)NULL;
    #else
        return -1;
    #endif
    }
}

double OSCData::getDouble(){
    if (type == 'd'){
        return data.d;
    } else {
    #ifndef ESPxx
        return (double)NULL;
    #else
        return -1;
    #endif
    }
}
bool OSCData::getBoolean(){
    if (type == 'T'){
        return true;
    } else if (type=='F'){
        return false;
    }
    else
    #ifndef ESPxx
        return NULL;
    #else
        return -1;
    #endif
}


// no-safety-check straightforward way to fill the passed buffer
// with the received string
int OSCData::getString(char * strBuffer){
    if (type == 's'){
        strncpy(strBuffer, data.s, bytes);
        return bytes;
    } else {
    #ifndef ESPxx
        return (int)NULL;
    #else
        return -1;
    #endif
    }
}

// it's possible to pass strBuffer's size as argument (length) 
// in order to check that it won't be overflown
int OSCData::getString(char * strBuffer, int length){
    if (type == 's' && bytes <= length){
        strncpy(strBuffer, data.s, bytes);
        return bytes;
    } else {
    #ifndef ESPxx
        return (int)NULL;
    #else
        return -1;
    #endif
    }
}

// Here we can get only a part of the string 
int OSCData::getString(char * strBuffer, int length, int offset, int size)
{
	int maxLen = bytes - offset; 
    if (type == 's' && maxLen >= 0 && size <= maxLen && size <= length){
        strncpy(strBuffer, data.s + offset, size);
        return size;
    } else {
    #ifndef ESPxx
        return (int)NULL;
    #else
        return -1;
    #endif
    }
}

// no-safety-check straightforward way to fill the passed buffer
// with the contents of the received blob
int OSCData::getBlob(uint8_t * blobBuffer){
    // read the blob length
    int blobLength =  getBlobLength();

    if (type == 'b'){
        memcpy(blobBuffer, data.b + 4, blobLength);
        return blobLength;
    } else {
    #ifndef ESPxx
        return (int)NULL;
    #else
        return -1;
    #endif
    }
}

// it's possible to pass blobBuffer's size as argument (length) 
// in order to check that it won't be overflown
int OSCData::getBlob(uint8_t * blobBuffer, int length){
    //jump over the first 4 bytes which encode the length
    int blobLength =  bytes-4;
    if (type == 'b' && blobLength <= length){
        memcpy(blobBuffer, data.b + 4, blobLength);
        return blobLength;
    } else {
    #ifndef ESPxx
        return (int)NULL;
    #else
        return -1;
    #endif
    }
}

// Here we can get only a part of the blob 
int OSCData::getBlob(uint8_t * blobBuffer, int length, int offset, int size){
    //jump over the first 4 bytes which encode the length
    int blobLength =  bytes-4-offset;
    if (type == 'b' && blobLength >= 0 && size <= blobLength && size <= length){
        memcpy(blobBuffer, data.b + 4 + offset, size);
        return size;
    } else {
    #ifndef ESPxx
        return (int)NULL;
    #else
        return -1;
    #endif
    }
}



int OSCData::getBlobLength(){
  if (type == 'b'){
    //jump over the first 4 bytes which encode the length
    return bytes-4;
  }
  return -1;
}

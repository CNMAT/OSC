
#include "OSCData.h"

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

OSCData::OSCData(int i){
	error = OSC_OK;
	type = 'i';
	//cast it to an int32
	bytes = sizeof(int32_t);
	int32_t i32 = (int32_t) i;
	data.i = i32;
}

OSCData::OSCData(int32_t i){
	error = OSC_OK;
	type = 'i';
	bytes = sizeof(i);
	data.i = i;
}


OSCData::OSCData(float f){
	error = OSC_OK;
	type = 'f';
	bytes = sizeof(float);
	data.f = f;	
}


OSCData::OSCData(double d){
	error = OSC_OK;
	bytes = sizeof(double);
	//if it's not 8 bits it's not a true double
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

OSCData::OSCData (OSCData * datum){
	error = OSC_OK;
	type = datum->type;
	bytes = datum->bytes;
	if (type == 'i' || type == 'f' || type == 'd'){
		data = datum->data;
	} else if (type == 's' || type == 'b'){
		//allocate a new peice of memory
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
	error = INVALID_OSC;
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
        return NULL;
    }
}

float OSCData::getFloat(){
    if (type == 'f'){
        return data.f;
    } else {
        return NULL;
    }
}

double OSCData::getDouble(){
    if (type == 'd'){
        return data.d;
    } else {
        return NULL;
    }
}

int OSCData::getString(char * strBuffer, int length){
    if (type == 's' && bytes <= length){
        strncpy(strBuffer, data.s, bytes);
        return bytes;
    } else {
        return NULL;
    }
}

int OSCData::getBlob(uint8_t * blobBuffer, int length){
    if (type == 'b' && bytes <= length){
        memcpy(blobBuffer, data.b, bytes);
        return bytes;
    } else {
        return NULL;
    }
}
#include "SLIPEncodedSerial.h"

/*
 CONSTRUCTOR
 */
//instantiate with the tranmission layer
//use HardwareSerial
SLIPEncodedSerial::SLIPEncodedSerial(HardwareSerial &s){
	serial = &s;
	rstate = CHAR;
}

static const uint8_t eot = 0300;
static const uint8_t slipesc = 0333;
static const uint8_t slipescend = 0334;
static const uint8_t slipescesc = 0335;
/*
 SERIAL METHODS
 */
bool SLIPEncodedSerial::endofPacket()
{
	if(rstate == SECONDEOT)
	{
		rstate = CHAR; 
		return true;
	}
	if (rstate==FIRSTEOT)
	{
        if(serial->available())
        {
            uint8_t c =serial->peek();
            if(c==eot)
            {
                serial->read(); // throw it on the floor
            }
        }
		rstate = CHAR;
		return true;
	}
	return false;
}
int SLIPEncodedSerial::available(){
back:
	int cnt = serial->available();
	
	if(cnt==0)
		return 0;
	if(rstate==CHAR)
	{
		uint8_t c =serial->peek();
		if(c==slipesc)
		{
			rstate = SLIPESC;
			serial->read(); // throw it on the floor
			goto back;
		}
		else if( c==eot)
		{
			rstate = FIRSTEOT;
			serial->read(); // throw it on the floor
			goto back;
		}
		return 1; // we may have more but this is the only sure bet
	}
	else if(rstate==SLIPESC)	
		return 1;
	else if(rstate==FIRSTEOT)
	{
		if(serial->peek()==eot)
		{
			rstate = SECONDEOT;
			serial->read(); // throw it on the floor
			return 0;
		}		
		rstate = CHAR;
	}else if (rstate==SECONDEOT) {
		rstate = CHAR;
	}
	
	return 0;
		
}

//reads a byte from the buffer
int SLIPEncodedSerial::read(){
back:
	uint8_t c = serial->read();
	if(rstate==CHAR)
	{
		if(c==slipesc)
		{
			rstate=SLIPESC;
			goto back;
		}	
		else if(c==eot){
		
			return -1; // xxx this is an error
		}
		return c;
	}
	else
	if(rstate==SLIPESC)
	{
		rstate=CHAR;
		if(c==slipescend)
			return eot;
		else if(c==slipescesc)
			return slipesc;
			else {
				// insert some error code here
				return -1;
			}

	}
	else
		return -1;
}

// as close as we can get to correct behavior
int SLIPEncodedSerial::peek(){
	uint8_t c = serial->peek();
	if(rstate==SLIPESC)
	{
		if(c==slipescend)
			return eot;
		else if(c==slipescesc)
			return slipesc;
	}
	return c; 
}

//the arduino and wiring libraries have different return types for the write function
#if defined(WIRING) || defined(BOARD_DEFS_H)

//encode SLIP
 void SLIPEncodedSerial::write(uint8_t b){
	if(b == eot){ 
		serial->write(slipesc);
		return serial->write(slipescend); 
	} else if(b==slipesc) {  
		serial->write(slipesc);
		return serial->write(slipescesc); 
	} else {
		return serial->write(b);
	}	
}
void SLIPEncodedSerial::write(const uint8_t *buffer, size_t size) {  while(size--) write(*buffer++); }
#else
//encode SLIP
size_t SLIPEncodedSerial::write(uint8_t b){
	if(b == eot){ 
		serial->write(slipesc);
		return serial->write(slipescend); 
	} else if(b==slipesc) {  
		serial->write(slipesc);
		return serial->write(slipescesc); 
	} else {
		return serial->write(b);
	}	
}
size_t SLIPEncodedSerial::write(const uint8_t *buffer, size_t size) { size_t result; while(size--) result = write(*buffer++); return result; }

#endif

void SLIPEncodedSerial::begin(unsigned long baudrate){
	serial->begin(baudrate);
}
//SLIP specific method which begins a transmitted packet
void SLIPEncodedSerial::beginPacket() { 	serial->write(eot); }

//signify the end of the packet with an EOT
void SLIPEncodedSerial::endPacket(){
	serial->write(eot);

}

void SLIPEncodedSerial::flush(){
	serial->flush();
}


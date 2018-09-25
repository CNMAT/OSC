
#include "SLIPEncodedUSBSerial.h"

/*
 CONSTRUCTOR
 */
//instantiate with the transmission layer

#if (defined(CORE_TEENSY) && defined(USB_SERIAL)) || (!defined(CORE_TEENSY) && defined(__AVR_ATmega32U4__)) || defined(__SAM3X8E__) || (defined(_USB) && defined(_USE_USB_FOR_SERIAL_)) || defined(BOARD_maple_mini) || defined(_SAMD21_)  || defined(__ARM__) || (defined(__PIC32MX__) || defined(__PIC32MZ__))


//USB Serials
SLIPEncodedUSBSerial::SLIPEncodedUSBSerial(

#if  defined(CORE_TEENSY)
#if defined(USB_HOST_TEENSY36)
                                           USBSerial
#else
                                           usb_serial_class
#endif
#elif defined(__SAM3X8E__) || defined(__AVR_ATmega32U4__) || defined(_SAMD21_)  || defined(__ARM__)
                                           Serial_
                                        
#elif (defined(__PIC32MX__) || defined(__PIC32MZ__))
                                           CDCACM
#else
#error unknown platform
#endif
                                           &s){
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

bool SLIPEncodedUSBSerial::endofPacket()
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
int SLIPEncodedUSBSerial::available(){
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
int SLIPEncodedUSBSerial::read(){
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
#ifdef FUTUREDEVELOPMENT
int SLIPEncodedUSBSerial::readBytes( uint8_t *buffer, size_t size)
{
    int count = 0;
    while(!endofPacket() && available() && (size>0))
    {
        int c = read();
        if(c>=0)
        {
            *buffer++ = c;
            ++count;
            --size;
            
        }
        else
            break;
    }
    return count;
}
#endif

// as close as we can get to correct behavior
int SLIPEncodedUSBSerial::peek(){
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

//encode SLIP
size_t SLIPEncodedUSBSerial::write(uint8_t b){
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
size_t SLIPEncodedUSBSerial::write(const uint8_t *buffer, size_t size)
{
    size_t result=0;
    while(size--)
        result = write(*buffer++); return result;
}

void SLIPEncodedUSBSerial::begin(unsigned long baudrate){
	serial->begin(baudrate);
        //
        // needed on Leonardo?
        // while(!serial)
        //        ;
}
//SLIP specific method which begins a transmitted packet
void SLIPEncodedUSBSerial::beginPacket() { 	serial->write(eot); }

//signify the end of the packet with an EOT
void SLIPEncodedUSBSerial::endPacket(){
	serial->write(eot);
#if defined(CORE_TEENSY)
    serial->send_now();
#endif
}

void SLIPEncodedUSBSerial::flush(){
	serial->flush();
}
#endif


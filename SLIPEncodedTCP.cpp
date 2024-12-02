#include "SLIPEncodedTCP.h"
#include <Client.h>
/*
 CONSTRUCTOR
 */
//instantiate with the tranmission layer

SLIPEncodedTCP::SLIPEncodedTCP(Client &s){
  tcpClient = &s;
  rstate = CHAR;
}

static const uint8_t eot = 0300;
static const uint8_t slipesc = 0333;
static const uint8_t slipescend = 0334;
static const uint8_t slipescesc = 0335;
/*
 tcpClient METHODS
 */
bool SLIPEncodedTCP::endofPacket()
{
  if(rstate == SECONDEOT)
  {
    rstate = CHAR; 
    return true;
  }
  if (rstate==FIRSTEOT)
  {
        if(tcpClient->available())
        {
            uint8_t c =tcpClient->peek();
            if(c==eot)
            {
                tcpClient->read(); // throw it on the floor
            }
        }
    rstate = CHAR;
    return true;
  }
  return false;
}
int SLIPEncodedTCP::available(){
back:
  int cnt = tcpClient->available();
  
  if(cnt==0)
    return 0;
  if(rstate==CHAR)
  {
    uint8_t c =tcpClient->peek();
    if(c==slipesc)
    {
      rstate = SLIPESC;
      tcpClient->read(); // throw it on the floor
      goto back;
    }
    else if( c==eot)
    {
      rstate = FIRSTEOT;
      tcpClient->read(); // throw it on the floor
      goto back;
    }
    return 1; // we may have more but this is the only sure bet
  }
  else if(rstate==SLIPESC)  
    return 1;
  else if(rstate==FIRSTEOT)
  {
    if(tcpClient->peek()==eot)
    {
      rstate = SECONDEOT;
      tcpClient->read(); // throw it on the floor
      return 0;
    }   
    rstate = CHAR;
  }else if (rstate==SECONDEOT) {
    rstate = CHAR;
  }
  
  return 0;
    
}

//reads a byte from the buffer
int SLIPEncodedTCP::read(){
back:
  uint8_t c = tcpClient->read();
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
int SLIPEncodedTCP::peek(){
  uint8_t c = tcpClient->peek();
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
 void SLIPEncodedTCP::write(uint8_t b){
  if(b == eot){ 
    tcpClient->write(slipesc);
    return tcpClient->write(slipescend); 
  } else if(b==slipesc) {  
    tcpClient->write(slipesc);
    return tcpClient->write(slipescesc); 
  } else {
    return tcpClient->write(b);
  } 
}
void SLIPEncodedTCP::write(const uint8_t *buffer, size_t size) {  while(size--) write(*buffer++); }
#else
//encode SLIP
size_t SLIPEncodedTCP::write(uint8_t b){
  if(b == eot){ 
    tcpClient->write(slipesc);
    return tcpClient->write(slipescend); 
  } else if(b==slipesc) {  
    tcpClient->write(slipesc);
    return tcpClient->write(slipescesc); 
  } else {
    return tcpClient->write(b);
  } 
}
size_t SLIPEncodedTCP::write(const uint8_t *buffer, size_t size) { size_t result=0; while(size--) result = write(*buffer++); return result; }

#endif

//SLIP specific method which begins a transmitted packet
void SLIPEncodedTCP::beginPacket() {  tcpClient->write(eot); }

//signify the end of the packet with an EOT
void SLIPEncodedTCP::endPacket(){
  tcpClient->write(eot);
}

void SLIPEncodedTCP::flush(){
  tcpClient->flush();
}

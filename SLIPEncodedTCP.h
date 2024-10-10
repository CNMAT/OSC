/*
Extends the TCP client class to encode SLIP over TCP
*/

#ifndef SLIPEncodedTCP_H
#define SLIPEncodedTCP_H

#include "Arduino.h"
#include <Stream.h>
#include <Client.h>


class SLIPEncodedTCP: public Stream{
  
private:
  enum erstate {CHAR, FIRSTEOT, SECONDEOT, SLIPESC } rstate;
  
  //the TCP client used
  Client * tcpClient;

public:
  SLIPEncodedTCP(Client & );

  int available();
  int read();
  int peek();
  void flush();
  void beginPacket(); // SLIP specific method which begins a transmitted packet
  void endPacket(); // SLIP specific method which ends a transmittedpacket
  bool endofPacket(); // SLIP specific method which indicates that an EOT was received 
  
//the arduino and wiring libraries have different return types for the write function
#if defined(WIRING) || defined(BOARD_DEFS_H)
  void write(uint8_t b);
  void write(const uint8_t *buffer, size_t size);

#else
  //overrides the Stream's write function to encode SLIP
  size_t write(uint8_t b);
  size_t write(const uint8_t *buffer, size_t size);

  //using Print::write;
#endif

};


#endif

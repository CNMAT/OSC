/*
SLIP over a TCP Stream.

This used to be a hand-maintained copy of the _SLIPSerial state machine, and
it drifted: it missed the read() underrun fix (-1 narrowed into a 0xFF data
byte -- likelier on TCP than serial, since a stream can go empty mid-packet
at any point), the write(buffer, size) return count, and block transmit
(every Client::write on a W5100/W5500 shield is its own SPI transaction).
It is now the same template bound to Client, so the two transports cannot
diverge again. Client only ever calls the Stream subset of the template;
begin() is never instantiated for it.
*/

#ifndef SLIPEncodedTCP_H
#define SLIPEncodedTCP_H

#include "SLIPEncodedSerial.h"
#include <Client.h>

using SLIPEncodedTCP = _SLIPSerial<Client>;

#endif

/*
SLIP over a TCP Stream.

This used to be a hand-maintained copy of the _SLIPSerial state machine, and
it drifted: it missed the read() underrun fix (-1 narrowed into a 0xFF data
byte -- likelier on TCP than serial, since a stream can go empty mid-packet
at any point), the write(buffer, size) return count, and block transmit
(every Client::write on a W5100/W5500 shield is its own SPI transaction).
It is now the same template bound to Client, so the two transports cannot
diverge again. Client only ever calls the Stream subset of the template;
begin() is never instantiated for it. The block transmit costs each
instance OSC_SLIP_TX_BUFFER+2 bytes of RAM (65 at the 64-byte default,
sized before olen widened) that the old copy did not carry -- deliberate,
and tunable down with -DOSC_SLIP_TX_BUFFER on a tight AVR.
*/

#ifndef SLIPEncodedTCP_H
#define SLIPEncodedTCP_H

#include "SLIPEncodedSerial.h"
#include <Client.h>

using SLIPEncodedTCP = _SLIPSerial<Client>;

#endif

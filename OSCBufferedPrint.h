/*
Collect an OSC packet in RAM and hand it to the transport in one write.

OSCMessage::send() and OSCBundle::send() write in small pieces: the address,
then each pad byte on its own, the comma, each type character, more padding,
then the data. On a transport where a write costs a round trip that is
expensive. On the UNO R4 WiFi and its clones (the Waveshare R7FA4 PLUS B, for
one) the radio is a separate ESP32-S3 and every WiFiUDP::write() becomes an
AT+UDPWRITE command over a 115200 baud UART that blocks until the answer comes
back, so "/analog/0" with one integer costs eight round trips to move twenty
bytes. Wiznet Ethernet shields pay the same tax in SPI transactions.

Wrap the transport in one of these and the whole packet leaves in a single
write:

    uint8_t packetbuf[128];

    Udp.beginPacket(outIp, outPort);
      OSCBufferedPrint out(Udp, packetbuf, sizeof(packetbuf));
      msg.send(out);   // accumulates
      out.flush();     // one Udp.write() for the whole packet
    Udp.endPacket();

The buffer is yours: nothing is allocated here, and the size is a performance
knob rather than a limit. A packet larger than the buffer is not truncated —
the buffer is handed to the transport as soon as it fills and filling carries
on — so an undersized buffer costs writes, not data.

flush() is not optional. Bytes still sitting in the buffer when the packet is
closed never reach the transport.
*/
#include "Arduino.h"

#ifndef OSCBufferedPrint_h
#define OSCBufferedPrint_h

#include <string.h>

class OSCBufferedPrint : public Print {

public:

    OSCBufferedPrint(Print &sink, uint8_t *buffer, size_t capacity)
        : sink(sink), buffer(buffer), capacity(capacity), length(0) {}

    //hand whatever has accumulated to the transport
    void flush(){
        while (length){
            size_t sent = sink.write(buffer, length);
            //a transport is allowed to take less than it was offered. Keep
            //going rather than leaving the tail of a packet in the buffer,
            //which would truncate it silently. A transport taking nothing at
            //all is not going to take more for being asked twice: leave the
            //bytes pending and let the caller find out through pending().
            if (sent == 0)
                return;
            if (sent < length)
                memmove(buffer, buffer + sent, length - sent);
            length -= sent;
        }
    }

    //bytes waiting for a flush
    size_t pending() const { return length; }

    size_t write(uint8_t b){ return write(&b, 1); }

    size_t write(const uint8_t *data, size_t size){
        size_t taken = 0;
        while (size){
            if (length == capacity){
                flush();
                if (length == capacity){
                    //nothing drained. With no buffer at all there is nothing
                    //to reorder, so pass the data straight through; otherwise
                    //report the short write and let the caller decide.
                    if (length == 0)
                        return taken + sink.write(data, size);
                    break;
                }
            }
            size_t n = capacity - length;
            if (n > size)
                n = size;
            memcpy(buffer + length, data, n);
            length += n;
            data += n;
            size -= n;
            taken += n;
        }
        return taken;
    }

    using Print::write;

private:

    Print &sink;
    uint8_t * const buffer;
    const size_t capacity;
    size_t length;
};

#endif

// Host driver: encodes a set of OSC messages/bundles with the CNMAT library and
// dumps the resulting wire bytes as hex, one case per line:
//     <case-name> <hex bytes>
// Also exercises the decode path and prints the recovered struct fields.

#include <stdio.h>
#include "OSCMessage.h"
#include "OSCBundle.h"

static void dump(const char * name, Print & p) {
    printf("%s ", name);
    for (size_t i = 0; i < p.len; i++) printf("%02x", p.buf[i]);
    printf("\n");
}

int main() {
    printf("# sizeof(oscmidi_t)=%zu sizeof(oscrgba_t)=%zu\n",
           sizeof(oscmidi_t), sizeof(oscrgba_t));

    // ---- ENCODE -----------------------------------------------------------
    {
        Print p;
        OSCMessage msg("/rgba");
        oscrgba_t c = {0x11, 0x22, 0x33, 0x44};
        msg.add(c);
        msg.send(p);
        dump("encode.rgba", p);
    }
    {
        Print p;
        OSCMessage msg("/midi");
        oscmidi_t m = {1, 0x90, 2, 0x3C, 0x40};
        msg.add(m);
        msg.send(p);
        dump("encode.midi", p);
    }
    {
        // channel already folded into the status byte, channel field left at 0
        Print p;
        OSCMessage msg("/midi");
        oscmidi_t m = {1, 0x92, 0, 0x3C, 0x40};
        msg.add(m);
        msg.send(p);
        dump("encode.midi_status_only", p);
    }
    {
        // system-common status: low nibble is not a channel
        Print p;
        OSCMessage msg("/midi");
        oscmidi_t m = {0, 0xF2, 0, 0x2A, 0x01};
        msg.add(m);
        msg.send(p);
        dump("encode.midi_syscommon", p);
    }
    {
        // regression guard: the scalar types must be unchanged
        Print p;
        OSCMessage msg("/mixed");
        msg.add((int32_t) 0x01020304);
        msg.add(1.0f);
        msg.add("hi");
        msg.add(true);
        msg.send(p);
        dump("encode.mixed", p);
    }
    {
        Print p;
        OSCBundle b;   // default timetag
        b.add("/a").add((int32_t) 1);
        b.send(p);
        dump("encode.bundle_default_timetag", p);
    }

    // ---- DECODE (spec-correct wire bytes in, struct fields out) ------------
    {
        // "/rgba\0\0\0" ",r\0\0" 11 22 33 44
        const uint8_t wire[] = {
            '/','r','g','b','a', 0, 0, 0,
            ',','r', 0, 0,
            0x11, 0x22, 0x33, 0x44
        };
        OSCMessage msg;
        msg.fill((uint8_t *) wire, sizeof(wire));
        oscrgba_t c = msg.getRgba(0);
        printf("decode.rgba r=%02x g=%02x b=%02x a=%02x err=%d\n",
               c.r, c.g, c.b, c.a, (int) msg.hasError());
    }
    {
        // "/midi\0\0\0" ",m\0\0" 01 92 3c 40
        const uint8_t wire[] = {
            '/','m','i','d','i', 0, 0, 0,
            ',','m', 0, 0,
            0x01, 0x92, 0x3C, 0x40
        };
        OSCMessage msg;
        msg.fill((uint8_t *) wire, sizeof(wire));
        oscmidi_t m = msg.getMidi(0);
        printf("decode.midi port=%02x status=%02x channel=%02x data1=%02x data2=%02x err=%d\n",
               m.port, m.status, m.channel, m.data1, m.data2, (int) msg.hasError());

        // re-encode what we just decoded: must reproduce the input bytes exactly
        Print p;
        OSCMessage again("/midi");
        again.add(m);
        again.send(p);
        dump("roundtrip.midi", p);
    }
    {
        // system-common round trip: 00 f2 2a 01
        const uint8_t wire[] = {
            '/','m','i','d','i', 0, 0, 0,
            ',','m', 0, 0,
            0x00, 0xF2, 0x2A, 0x01
        };
        OSCMessage msg;
        msg.fill((uint8_t *) wire, sizeof(wire));
        oscmidi_t m = msg.getMidi(0);
        printf("decode.midi_syscommon port=%02x status=%02x channel=%02x data1=%02x data2=%02x err=%d\n",
               m.port, m.status, m.channel, m.data1, m.data2, (int) msg.hasError());
        Print p;
        OSCMessage again("/midi");
        again.add(m);
        again.send(p);
        dump("roundtrip.midi_syscommon", p);
    }
    {
        // a received bundle must re-send byte-identically, timetag included
        const uint8_t wire[] = {
            '#','b','u','n','d','l','e', 0,
            0x83, 0xAA, 0x7E, 0x80, 0x40, 0x00, 0x00, 0x00,   // timetag
            0, 0, 0, 12,
            '/','a', 0, 0,
            ',','i', 0, 0,
            0, 0, 0, 1
        };
        printf("input.bundle_timetag ");
        for (size_t i = 0; i < sizeof(wire); i++) printf("%02x", wire[i]);
        printf("\n");

        OSCBundle b;
        b.fill((uint8_t *) wire, sizeof(wire));

        //and the sketch-visible timetag must match what arrived
        osctime_t t = b.getTimetag();
        printf("decode.bundle_timetag seconds=%08x fraction=%08x err=%d\n",
               t.seconds, t.fractionofseconds, (int) b.hasError());

        Print p;
        b.send(p);
        dump("roundtrip.bundle_timetag", p);
    }
    {
        //a bundle built without a timetag reports "immediately"
        OSCBundle b;
        osctime_t t = b.getTimetag();
        printf("default.bundle_timetag seconds=%08x fraction=%08x\n",
               t.seconds, t.fractionofseconds);
    }
    {
        // rgba round trip through encode -> decode
        Print p;
        OSCMessage msg("/rgba");
        oscrgba_t c = {0xDE, 0xAD, 0xBE, 0xEF};
        msg.add(c);
        msg.send(p);
        OSCMessage back;
        back.fill(p.buf, (int) p.len);
        oscrgba_t d = back.getRgba(0);
        printf("roundtrip.rgba r=%02x g=%02x b=%02x a=%02x err=%d\n",
               d.r, d.g, d.b, d.a, (int) back.hasError());
    }
    return 0;
}

"""Independent OSC 1.0 decoder, written from the spec. Shares no code with the library."""
import struct, sys

class Bad(Exception): pass

def _pad(n): return (4 - (n % 4)) % 4

class R:
    def __init__(self, b): self.b, self.i = b, 0
    def take(self, n):
        if self.i + n > len(self.b): raise Bad("truncated: want %d at %d of %d" % (n, self.i, len(self.b)))
        v = self.b[self.i:self.i+n]; self.i += n; return v
    def ostring(self):
        j = self.b.find(b'\0', self.i)
        if j < 0: raise Bad("unterminated string")
        s = self.b[self.i:j]
        total = (j - self.i) + 1
        total += _pad(total)
        if self.i + total > len(self.b): raise Bad("string padding runs off end")
        pad = self.b[self.i + (j - self.i) + 1: self.i + total]
        if pad.strip(b'\0'): raise Bad("string padding not zero: %r" % pad)
        self.i += total
        return s.decode('ascii', 'replace')
    def i32(self): return struct.unpack('>i', self.take(4))[0]
    def u32(self): return struct.unpack('>I', self.take(4))[0]
    def i64(self): return struct.unpack('>q', self.take(8))[0]
    def f32(self): return struct.unpack('>f', self.take(4))[0]
    def f64(self): return struct.unpack('>d', self.take(8))[0]
    def blob(self):
        n = self.i32()
        if n < 0: raise Bad("negative blob length %d" % n)
        data = self.take(n); self.take(_pad(n))
        return data

ZERO = set('TFNI')
def decode_msg(r, end):
    addr = r.ostring()
    if not addr.startswith('/'): raise Bad("address does not start with '/': %r" % addr)
    tt = r.ostring()
    if not tt.startswith(','): raise Bad("typetag does not start with ',': %r" % tt)
    args = []
    for t in tt[1:]:
        if   t == 'i': args.append(('i', r.i32()))
        elif t == 'h': args.append(('h', r.i64()))
        elif t == 'f': args.append(('f', r.f32()))
        elif t == 'd': args.append(('d', r.f64()))
        elif t == 's': args.append(('s', r.ostring()))
        elif t == 'S': args.append(('S', r.ostring()))
        elif t == 'b': args.append(('b', r.blob()))
        elif t == 'c': args.append(('c', r.u32()))
        elif t == 'r': args.append(('r', r.take(4)))
        elif t == 'm': args.append(('m', r.take(4)))
        elif t == 't': args.append(('t', (r.u32(), r.u32())))
        elif t in ZERO: args.append((t, None))
        else: raise Bad("unknown typetag %r" % t)
    if r.i != end: raise Bad("trailing %d bytes after args" % (end - r.i))
    return ('msg', addr, args)

def decode(b):
    if len(b) % 4: raise Bad("packet length %d not a multiple of 4" % len(b))
    r = R(b)
    if b.startswith(b'#bundle\0'):
        r.take(8); tt = (r.u32(), r.u32()); elems = []
        while r.i < len(b):
            n = r.i32()
            if n % 4: raise Bad("bundle element size %d not multiple of 4" % n)
            sub = r.take(n); elems.append(decode(sub))
        return ('bundle', tt, elems)
    return decode_msg(r, len(b))

EXPECT = {
 'int32_minmax':  ('msg','/i',[('i',-2147483648),('i',2147483647)]),
 'int_spellings': ('msg','/i',[('i',123456),('i',-7),('i',-2),('i',-1)]),
 'uint_spellings':('msg','/u',[('i',-1),('i',0),('i',65535),('i',255)]),
 'long_host':     ('msg','/l',[('h',1)]),
 'int64_pattern': ('msg','/h',[('h',0x0123456789ABCDEF)]),
 'int64_minmax':  ('msg','/h',[('h',-1),('h',9223372036854775807)]),
 'uint64_max':    ('msg','/h',[('h',-1)]),
 'floats':        ('msg','/f',[('f',1.5),('f',-0.0)]),
 'double':        ('msg','/d',[('d',1.5)]),
 'bools':         ('msg','/b',[('T',None),('F',None)]),
 'strings_pad':   ('msg','/s',[('s',''),('s','a'),('s','ab'),('s','abc'),('s','abcd')]),
 'blob_len0':('msg','/blob',[('b',b'')]),
 'blob_len1':('msg','/blob',[('b',bytes([0xA0]))]),
 'blob_len2':('msg','/blob',[('b',bytes([0xA0,0xA1]))]),
 'blob_len3':('msg','/blob',[('b',bytes([0xA0,0xA1,0xA2]))]),
 'blob_len4':('msg','/blob',[('b',bytes([0xA0,0xA1,0xA2,0xA3]))]),
 'blob_len5':('msg','/blob',[('b',bytes([0xA0,0xA1,0xA2,0xA3,0xA4]))]),
 'addr3_noargs':('msg','/a',[]), 'addr4_noargs':('msg','/ab',[]),
 'addr5_noargs':('msg','/abc',[]), 'addr6_noargs':('msg','/abcd',[]),
 'mixed':('msg','/mix',[('i',7),('s','hi'),('f',2.5),('h',-2)]),
 # (0,1) is OSC's reserved "immediately" timetag -- a bundle built without an
 # explicit timetag must send 1, not 0 (0 is a real absolute time, 1900-01-01).
 'bundle':('bundle',(0,1),[('msg','/x',[('i',1)]),('msg','/y',[('h',2)])]),
}

# Guarded so the decoder is importable (extras/python/test_host.py leans on
# it); run as a script this still filters name/hex lines from stdin unchanged.
if __name__ == '__main__':
    fails = 0; n = 0
    for line in sys.stdin:
        line = line.strip()
        if not line: continue
        name, hexs = line.split()
        raw = bytes.fromhex(hexs); n += 1
        try:
            got = decode(raw)
        except Bad as e:
            print("DECODE-FAIL %-16s %s" % (name, e)); fails += 1; continue
        exp = EXPECT.get(name)
        if exp is None:
            print("NO-EXPECT   %-16s %r" % (name, got)); fails += 1
        elif got != exp:
            print("MISMATCH    %-16s\n   got %r\n   exp %r" % (name, got, exp)); fails += 1
        else:
            print("ok          %-16s %r" % (name, got))
    print("\n%d packets, %d failures" % (n, fails))
    sys.exit(1 if fails else 0)

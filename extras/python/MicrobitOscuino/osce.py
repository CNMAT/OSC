# osce.py -- OSC 1.0 encoder, imported by main.py.
# GENERATED; squeezed so the V1's on-board compiler fits in RAM.
# Readable source: extras/webserial/template-microbit-osce.py  docs: extras/python/
import struct
def _pad(b):
 return b + b'\0' * ((4 - len(b) % 4) % 4)
def _s(s):
 return _pad(bytes(s, 'utf8') + b'\0')
def _i32(v):
 return struct.pack('>i', ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000)
def message(addr, *args):
 tags, data = ',', b''
 for a in args:
  if a is True:
   tags += 'T'
  elif a is False:
   tags += 'F'
  elif a is None:
   tags += 'N'
  elif isinstance(a, int):
   tags += 'i'
   data += _i32(a)
  elif isinstance(a, float):
   tags += 'f'
   data += struct.pack('>f', a)
  elif isinstance(a, str):
   tags += 's'
   data += _s(a)
  else:
   tags += 'b'
   data += _i32(len(a)) + _pad(bytes(a))
 return _s(addr) + _s(tags) + data
def bundle(elems):
 out = b'#bundle\0' + b'\0\0\0\0\0\0\0\x01'
 for e in elems:
  out += _i32(len(e)) + e
 return out

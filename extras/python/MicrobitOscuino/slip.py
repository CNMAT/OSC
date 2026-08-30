# slip.py -- SLIP layer (RFC 1055), imported by main.py.
# GENERATED; squeezed so the V1's on-board compiler fits in RAM.
# Readable source: extras/webserial/template-microbit-slip.py  docs: extras/python/
END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD
def slip_encode(payload):
 out = bytearray([END])
 for b in payload:
  if b == END:
   out += b'\xdb\xdc'
  elif b == ESC:
   out += b'\xdb\xdd'
  else:
   out.append(b)
 out.append(END)
 return bytes(out)
class SlipDecoder:
 def __init__(self):
  self.buf = bytearray()
  self.esc = False
 def feed(self, data):
  frames = []
  for b in data:
   if self.esc:
    self.buf.append(END if b == ESC_END else ESC if b == ESC_ESC else b)
    self.esc = False
   elif b == ESC:
    self.esc = True
   elif b == END:
    if self.buf:
     frames.append(bytes(self.buf))
     self.buf = bytearray()
   else:
    self.buf.append(b)
  return frames

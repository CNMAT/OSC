# oscd.py -- OSC 1.0 decoder, imported by main.py.
# GENERATED; squeezed so the V1's on-board compiler fits in RAM.
# Readable source: extras/webserial/template-microbit-oscd.py  docs: extras/python/
import struct
def _dstr(p, i):
 j = p.index(b'\0', i)
 n = j - i + 1
 return str(p[i:j], 'utf8'), i + n + ((4 - n % 4) % 4)
def messages(p, out=None):
 if out is None:
  out = []
 if p[:8] == b'#bundle\0':
  i = 16
  while i + 4 <= len(p):
   n = struct.unpack('>i', p[i:i + 4])[0]
   if n <= 0 or i + 4 + n > len(p):
    break
   messages(p[i + 4:i + 4 + n], out)
   i += 4 + n
  return out
 addr, i = _dstr(p, 0)
 tags, i = _dstr(p, i)
 args = []
 for t in tags[1:]:
  if t == 'i':
   args.append(struct.unpack('>i', p[i:i + 4])[0])
   i += 4
  elif t == 'f':
   args.append(struct.unpack('>f', p[i:i + 4])[0])
   i += 4
  elif t == 's':
   a, i = _dstr(p, i)
   args.append(a)
  elif t == 'b':
   n = struct.unpack('>i', p[i:i + 4])[0]
   args.append(p[i + 4:i + 4 + n])
   i += 4 + ((n + 3) & ~3)
  elif t in 'TFNI':
   args.append({'T': True, 'F': False}.get(t))
  elif t in 'hdt':
   i += 8
  else:
   break
 out.append((addr, args))
 return out

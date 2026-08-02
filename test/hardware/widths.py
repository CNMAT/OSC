import sys
sys.path.insert(0,'.')
from oscprobe import Port, slip_encode, slip_frames, msg, decode
import struct
def dec_full(p):
    from oscprobe import dec_str
    addr,i = dec_str(p,0); tags,i = dec_str(p,i)
    out=[]
    for t in tags[1:]:
        if t=='i': out.append(('i',struct.unpack('>i',p[i:i+4])[0])); i+=4
        elif t=='h': out.append(('h',struct.unpack('>q',p[i:i+8])[0])); i+=8
    return addr,out
p=Port(sys.argv[1]); p.drain(0.4)
p.write(slip_encode(msg('/go'))); raw=p.drain(1.0); p.close()
frames=slip_frames(raw)
res={}
for f in frames:
    a,v=dec_full(f); res[a]=v
if '/sizes' in res:
    s=[v for _,v in res['/sizes']]
    print(f"  target widths: int={s[0]} long={s[1]} long long={s[2]} double={s[3]}")
EXPECT=[('i',-1),('i',255),('i',-2),('i',65535),('i',-3),('i',65534),
        ('i',-100000),('i',-294967296),('h',-5000000000),('i',123456),('h',-9000000000)]
NAMES=['signed char','unsigned char','short','unsigned short','int','unsigned int',
       'long','unsigned long','long long','int32_t','int64_t']
got=res.get('/w',[])
fails=0
if len(got)!=len(EXPECT):
    print(f"  FAIL arg count {len(got)} != {len(EXPECT)}"); fails+=1
for n,(name,e) in enumerate(zip(NAMES,EXPECT)):
    g = got[n] if n<len(got) else ('?',None)
    ok = g==e
    if not ok: fails+=1
    print(f"  {name:<16} -> '{g[0]}' {str(g[1]):<14} want '{e[0]}' {e[1]}   {'ok' if ok else 'FAIL'}")
print(f"\n{'FAILURES' if fails else 'all integer widths correct'} ({fails})")
sys.exit(1 if fails else 0)

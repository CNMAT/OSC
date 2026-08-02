import sys, time
sys.path.insert(0, '.')
from oscprobe import Port, slip_encode, slip_frames, msg, decode

p = Port(sys.argv[1]); p.drain(0.4)

def run(label, n, per_write, gap=0.0):
    p.drain(0.15)
    pkts = [slip_encode(msg('/n', [i])) for i in range(n)]
    if per_write:
        p.write(b''.join(pkts))                       # all frames in ONE write
    else:
        for q in pkts:
            p.write(q)
            if gap: time.sleep(gap)
    raw = p.drain(1.2 + n * 0.004)
    frames = slip_frames(raw)
    got = []
    for f in frames:
        try:
            a = decode(f)
            if a[1]: got.append(a[1][0])
        except Exception: pass
    missing = [i for i in range(n) if i not in got]
    ok = len(missing) == 0
    print(f"  {label:<46} {'ok  ' if ok else 'LOST'} {len(got)}/{n}"
          + (f"  missing={missing[:8]}" if missing else ""))
    return ok

fails = 0
print("SLIP framing stress (each frame carries its own index)")
for n in (2, 5, 10, 25, 50):
    if not run(f"{n} frames back-to-back in ONE write", n, True): fails += 1
for gap in (0.0, 0.001):
    if not run(f"20 frames, separate writes, {int(gap*1000)}ms gap", 20, False, gap): fails += 1
p.close()
print(f"\n{'FRAMES LOST' if fails else 'no frames lost'} ({fails} failing cases)")
sys.exit(1 if fails else 0)

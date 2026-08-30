# MicrobitOscuino -- Oscuino over SLIP-encoded USB serial, in MicroPython,
# GENERATED; squeezed so the V1's on-board compiler fits in RAM.
# Readable source: extras/webserial/template-microbit.py  docs: extras/python/
from slip import slip_encode, SlipDecoder
from oscd import messages
from osce import message, bundle
def run():
 import microbit
 from microbit import uart, display, accelerometer, button_a, button_b, Image
 import music
 try:
  import micropython
  micropython.kbd_intr(-1)
 except (ImportError, AttributeError):
  pass
 pins = {}
 for n in range(21):
  p = getattr(microbit, 'pin%d' % n, None)
  if p:
   pins[n] = p
 analog = (0, 1, 2, 3, 4, 10)
 full = Image('99999:99999:99999:99999:99999')
 try:
  from utime import ticks_us as micros
 except ImportError:
  micros = lambda: microbit.running_time() * 1000
 def _num(s):
  try:
   return int(s)
  except ValueError:
   return -1
 def handle(addr, args, out):
  a0 = args[0] if args else None
  if addr == '/s/m':
   out.append(message('/s/m', micros() & 0x7FFFFFFF))
  elif addr == '/s/d':
   out.append(message('/s/d', len(pins)))
  elif addr == '/s/a':
   out.append(message('/s/a', len(analog)))
  elif addr == '/s/l' and isinstance(a0, int):
   display.show(full) if a0 > 0 else display.clear()
   out.append(message('/s/l', a0))
  elif addr == '/s/q':
   raise SystemExit
  elif addr == '/mb/a':
   out.append(message('/mb/a', accelerometer.get_x(),
       accelerometer.get_y(), accelerometer.get_z()))
  elif addr == '/mb/b':
   out.append(message('/mb/b', 1 if button_a.is_pressed() else 0,
       1 if button_b.is_pressed() else 0))
  elif addr == '/mb/t' and isinstance(a0, str):
   display.scroll(a0, wait=False)
  elif addr.startswith('/tone/'):
   pin = pins.get(_num(addr[6:]))
   f = int(a0) if isinstance(a0, (int, float)) else 0
   if pin is None:
    return
   if f <= 0:
    music.stop(pin)
   else:
    d = args[1] if len(args) > 1 and isinstance(args[1], int) else -1
    music.pitch(f, d, pin, wait=False)
  elif addr.startswith('/d/') or addr.startswith('/a/'):
   part = addr.split('/')          # ['', 'd', '13'] or ['', 'd', '13', 'u']
   n = _num(part[2])
   pin = pins.get(n)
   if pin is None:
    return
   if len(part) == 4 and part[3] == 'u':
    pin.set_pull(pin.PULL_UP)
    out.append(message(addr, pin.read_digital()))
   elif isinstance(a0, int):
    pin.write_digital(1 if a0 else 0)
   elif isinstance(a0, float):
    pin.write_analog(int(min(max(a0, 0.0), 1.0) * 1023))
   elif part[1] == 'a':
    if n in analog:
     out.append(message(addr, pin.read_analog()))
   else:
    out.append(message(addr, pin.read_digital()))
 uart.init(baudrate=115200)
 dec = SlipDecoder()
 rx = bytearray(64)
 import gc
 gc.collect()
 uart.write(slip_encode(bundle([message('/hello', 'MicrobitOscuino')])))
 while True:
  if uart.any():
   n = uart.readinto(rx)
   if n:
    for frame in dec.feed(rx[:n]):
     out = []
     try:
      for addr, args in messages(frame):
       handle(addr, args, out)
     except Exception:
      pass
     if out:
      uart.write(slip_encode(bundle(out)))
if __name__ == '__main__':
 run()

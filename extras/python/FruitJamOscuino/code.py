# FruitJamOscuino -- Oscuino over SLIP-encoded USB serial, in CircuitPython,
# for the Adafruit Fruit Jam (RP2350B).
#
# GENERATED FILE -- do not edit directly.
# Source: extras/webserial/template-fruitjam.py + extras/webserial/boards.json
# Regenerate:  cd extras/webserial && make generate
#
# Two firmwares answer this page with the same address space: code.py under CircuitPython (extras/python/FruitJamOscuino/, copy boot.py + code.py to CIRCUITPY) and the hand-written examples/FruitJamOscuino sketch under arduino-pico (needs Adafruit NeoPXL8, Adafruit TLV320 I2S and Adafruit DVI HSTX; see the FruitJamArduino entry). /btn reads the three user buttons (1 = pressed), /rgb drives the five NeoPixels (/rgb/<n> for one), /buzz plays a sine through the headphone/speaker codec, /display/text puts text on the DVI output -- the same addresses every Oscuino board uses, see ADDRESSES.md. The USB filter matches Adafruit's and Raspberry Pi's vendor ids to cover both firmwares' CDC identities.
#
# Copy this file to the CIRCUITPY drive as code.py, and boot.py alongside it,
# then press reset; open FruitJamOscuino.html next to this file -- served over
# http://localhost or https://, never file:// -- click Connect, and pick the
# board's SECOND serial port (the data channel boot.py adds). Same wire
# contract as everything else in this repo: OSC 1.0 bundles inside RFC 1055
# SLIP frames. The hand-written Arduino twin in examples/FruitJamOscuino
# answers the same addresses through the same page.
#
# Beyond the standard Oscuino set this board answers:
#   /btn                    -> /btn <b1> <b2> <b3>   (1 = pressed)
#   /rgb <r> <g> <b>      all five NeoPixels, 0..255 each
#   /rgb <n> <r> <g> <b>  one NeoPixel
#   /btneep <freq> [<ms>]   sine via the TLV320 codec (headphones + speaker);
#                            0 stops; answers -1 if audio is unavailable
#   /display/text <string>           a line of text on the DVI display
#   /s/q                     exit to the REPL
#
# Everything below matches what was measured on the board (CircuitPython
# 10.2.1, 2026-08-27). The NeoPixels use the builtin neopixel_write, no
# library. DVI is initialised explicitly -- this build does NOT auto-create
# board.DISPLAY -- and the console terminal lands on the screen, so /display/text is
# print() plus a status reply. Audio uses the official adafruit_tlv320
# driver (vendored in this folder's lib/, copy it across too) with the
# builtin audiobusio and synthio; without it /btneep answers -1 instead of
# breaking the rest. Two hardware facts worth keeping: the codec shares its
# I2C bus with the DVI connector's DDC lines, and a wedged bus object NACKs
# every multi-byte write while still ACKing address probes -- creating the
# bus fresh with busio.I2C and retrying is what makes codec init reliable.
# PERIPH_RESET is left alone: the codec comes up fine without it, and
# toggling it also resets the ESP32 co-processor.

import struct

# ---- SLIP (RFC 1055), same constants as SLIPEncodedSerial.h ----------------
END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD


def slip_encode(payload):
    # Leading AND trailing END, like CNMAT beginPacket()/endPacket(). Every
    # decoder on the other side skips the empty frame the leading END closes.
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
    # Stateful across feed() calls: a frame usually spans several reads.
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


# ---- OSC 1.0, the subset the browser page speaks ---------------------------
def _pad(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4)


def _s(s):
    # bytes(s, 'utf8') rather than s.encode(): the micro:bit V1's MicroPython
    # has no str.encode, and the codecs stay textually identical across ports.
    return _pad(bytes(s, 'utf8') + b'\0')


def _i32(v):
    # Two's-complement fold so a large tick count packs instead of raising.
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


def _dstr(p, i):
    j = p.index(b'\0', i)
    n = j - i + 1
    # str(b, 'utf8') rather than b.decode(): absent on the micro:bit V1.
    return str(p[i:j], 'utf8'), i + n + ((4 - n % 4) % 4)


# Flatten a packet -- message or bundle, nested included -- to a list of
# (address, args). Raises on malformed input; the caller drops the frame.
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
            i += 8          # known width, value unused here
        else:
            break           # unknown width: stop rather than misparse
    out.append((addr, args))
    return out


# ---- Fruit Jam I/O ---------------------------------------------------------
def run():
    import time, board, busio, digitalio, analogio, usb_cdc, neopixel_write

    ser = usb_cdc.data
    if ser is None:
        # boot.py absent or data channel disabled. The console still moves
        # SLIP bytes -- until one of them is 0x03 -- so limp along and say so.
        ser = usb_cdc.console
    ser.timeout = 0

    # DVI out, 320x240x8 on the HSTX pins. The default root group is the
    # supervisor terminal, so once this exists print() draws on the monitor.
    # Lane order is the board's, not the TMDS textbook's: red on D0 — wiring
    # it "by the book" (sync channel on D0 as blue) leaves the monitor
    # reporting no signal, since DVI carries sync in the channel the D0 pair
    # actually feeds. Matches Adafruit_CircuitPython_FruitJam. The display is
    # parked on supervisor.runtime so it survives reloads.
    display = None
    try:
        import picodvi, framebufferio, displayio, supervisor
        display = supervisor.runtime.display
        if display is None:
            displayio.release_displays()
            fb = picodvi.Framebuffer(320, 240,
                                     clk_dp=board.CKP, clk_dn=board.CKN,
                                     red_dp=board.D0P, red_dn=board.D0N,
                                     green_dp=board.D1P, green_dn=board.D1N,
                                     blue_dp=board.D2P, blue_dn=board.D2N,
                                     color_depth=8)
            display = framebufferio.FramebufferDisplay(fb)
            supervisor.runtime.display = display
        print('FruitJamOscuino: OSC over SLIP over USB serial')
    except Exception:
        display = None                      # headless: everything else serves

    # Buttons pull to ground; 1 = pressed in replies.
    btns = []
    for nm in ('BUTTON1', 'BUTTON2', 'BUTTON3'):
        p = getattr(board, nm, None)
        if p:
            io = digitalio.DigitalInOut(p)
            io.switch_to_input(digitalio.Pull.UP)
            btns.append(io)

    # Five NeoPixels through the builtin neopixel_write: byte order is GRB.
    npx = digitalio.DigitalInOut(board.NEOPIXEL)
    npx.switch_to_output()
    npbuf = bytearray(15)

    def led(args):
        if len(args) >= 4:
            n, r, g, b = args[0], args[1], args[2], args[3]
            if 0 <= n < 5:
                npbuf[n * 3:n * 3 + 3] = bytes((g & 255, r & 255, b & 255))
        elif len(args) == 3:
            trip = bytes((args[1] & 255, args[0] & 255, args[2] & 255))
            for n in range(5):
                npbuf[n * 3:n * 3 + 3] = trip
        neopixel_write.neopixel_write(npx, npbuf)

    # Audio: the official adafruit_tlv320 driver + builtin audiobusio and
    # synthio, each absence degrading /btneep to a -1 reply rather than
    # taking the firmware down. Measured: the first bus object sometimes
    # comes up wedged (multi-byte writes NACK while probes ACK), so the bus
    # is created fresh and recreated between attempts.
    # The codec is clocked from a 15 MHz PWM on I2S_MCLK, exactly as
    # Adafruit_CircuitPython_FruitJam does — configured BCLK-only it takes
    # every register write and stays silent. Speaker and headphone routes are
    # exclusive on this DAC; the built-in speaker is the accessory-free
    # default (flip the two lines below for the jack). -10 dB is an audible
    # demo level on the -63..+23 scale the driver exposes.
    synth = None
    try:
        from adafruit_tlv320 import TLV320DAC3100
        import audiobusio, synthio, pwmio
        mclk = pwmio.PWMOut(board.I2S_MCLK, frequency=15_000_000,
                            duty_cycle=2 ** 15)
        try:
            board.I2C().deinit()
        except Exception:
            pass
        for attempt in range(3):
            i2c = busio.I2C(board.SCL, board.SDA)
            try:
                dac = TLV320DAC3100(i2c)
                dac.configure_clocks(sample_rate=22050, bit_depth=16,
                                     mclk_freq=15_000_000)
                break
            except OSError:
                i2c.deinit()
                time.sleep(0.2)
        else:
            raise OSError('codec unreachable')
        dac.speaker_output = True
        dac.headphone_output = False
        dac.dac_volume = -10
        audio = audiobusio.I2SOut(board.I2S_BCLK, board.I2S_WS, board.I2S_DIN)
        synth = synthio.Synthesizer(sample_rate=22050)
        audio.play(synth)
    except Exception:
        synth = None
    beep_off = [0.0]

    # Index -> board pin, with gaps kept so /d/13 is always board.D13.
    dpins, apins = [], []
    for i in range(64):
        dpins.append(getattr(board, 'D%d' % i, None))
    while dpins and dpins[-1] is None:
        dpins.pop()
    for i in range(32):
        p = getattr(board, 'A%d' % i, None)
        if p is None:
            break
        apins.append(p)
    led_pin = getattr(board, 'LED', None)

    # One live object per pin, whatever its current mode. Switching modes on a
    # pin means deinit first or CircuitPython raises "in use".
    claimed = {}

    def release(p):
        o = claimed.pop(p, None)
        if o:
            o.deinit()

    def digital(p):
        o = claimed.get(p)
        if not isinstance(o, digitalio.DigitalInOut):
            release(p)
            o = digitalio.DigitalInOut(p)
            claimed[p] = o
        return o

    def micros():
        return time.monotonic_ns() // 1000 & 0x7FFFFFFF

    def _num(s):
        try:
            return int(s)
        except ValueError:
            return -1

    def handle(addr, args, out):
        a0 = args[0] if args else None
        if addr == '/s/m':
            out.append(message('/s/m', micros()))
        elif addr == '/s/d':
            out.append(message('/s/d', len(dpins)))
        elif addr == '/s/a':
            out.append(message('/s/a', len(apins)))
        elif addr == '/s/l' and isinstance(a0, int) and led_pin:
            o = digital(led_pin)
            o.switch_to_output(a0 > 0)
            out.append(message('/s/l', a0))
        elif addr == '/s/q':
            raise SystemExit
        elif addr == '/enq':
            out.extend(enq())
        elif addr == '/btn':
            out.append(message('/btn', *[0 if b.value else 1 for b in btns]))
        elif addr == '/rgb' and len(args) >= 3 and isinstance(a0, int):
            led(args)
            out.append(message(addr, *args))    # echo: probes can't see photons
        elif addr.startswith('/rgb/') and len(args) >= 3 and isinstance(a0, int):
            n = _num(addr[5:])
            if 0 <= n < 5:
                led([n] + list(args[:3]))
                out.append(message(addr, *args[:3]))
        elif addr == '/buzz':
            f = int(a0) if isinstance(a0, (int, float)) else 0
            if synth is None:
                out.append(message('/buzz', -1))
            elif f <= 0:
                import synthio
                synth.release_all()
                beep_off[0] = 0.0
                out.append(message('/buzz', 0))
            else:
                import synthio
                synth.release_all()
                synth.press(synthio.Note(frequency=f))
                ms = args[1] if len(args) > 1 and isinstance(args[1], int) else 0
                beep_off[0] = time.monotonic() + ms / 1000.0 if ms > 0 else 0.0
                out.append(message('/buzz', f))
        elif addr == '/display/text' and isinstance(a0, str):
            print(a0)
            out.append(message('/display/text', 1 if display else 0))
        elif addr.startswith('/d/') or addr.startswith('/a/'):
            part = addr.split('/')          # ['', 'd', '13'] or ['', 'd', '13', 'u']
            n = _num(part[2])
            pins = dpins if part[1] == 'd' else apins
            p = pins[n] if 0 <= n < len(pins) else None
            if p is None:
                return
            if len(part) == 4 and part[3] == 'u':
                o = digital(p)
                o.switch_to_input(digitalio.Pull.UP)
                out.append(message(addr, 1 if o.value else 0))
            elif isinstance(a0, int):
                o = digital(p)
                o.switch_to_output(a0 > 0)
            elif part[1] == 'a':
                o = claimed.get(p)
                if not isinstance(o, analogio.AnalogIn):
                    release(p)
                    o = analogio.AnalogIn(p)
                    claimed[p] = o
                out.append(message(addr, o.value))
            else:
                o = digital(p)
                o.switch_to_input()
                out.append(message(addr, 1 if o.value else 0))

    dec = SlipDecoder()
    def enq():
        # The capability bundle of ADDRESSES.md: only what actually came up.
        out = [message('/enq', 'FruitJamOscuino'), message('/enq/btn', len(btns)),
               message('/enq/rgb', 5)]
        if synth is not None:
            out.append(message('/enq/buzz'))
        if display:
            out.append(message('/enq/display', display.width, display.height))
        return out
    ser.write(slip_encode(bundle(enq())))
    while True:
        if synth is not None and beep_off[0] and time.monotonic() >= beep_off[0]:
            synth.release_all()
            beep_off[0] = 0.0
        n = ser.in_waiting
        if n:
            for frame in dec.feed(ser.read(n)):
                out = []
                try:
                    for addr, args in messages(frame):
                        handle(addr, args, out)
                except Exception:
                    pass                    # malformed frame: drop, keep serving
                if out:
                    ser.write(slip_encode(bundle(out)))
        else:
            time.sleep(0.001)


if __name__ == '__main__':
    run()

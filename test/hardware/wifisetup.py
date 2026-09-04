#!/usr/bin/env python3
"""
wifisetup.py -- bench harness for examples/WiFiSetup: the setup-network,
join, stream, settings-page and fallback paths, driven over the USB serial
console and checked over UDP and HTTP from this machine.

    python3 test/hardware/wifisetup.py PORT --secrets examples/XiaoC3WiFi/arduino_secrets.h
                                            [--captive] [--long] [--keep]

The secrets file's SECRET_SSID / SECRET_PASS are typed into the board over the
tty and never printed. Standard library only.

What it checks, in order (each later step assumes the earlier ones passed):

  1  the sketch answers `show` on the serial console
  2  `forget` reboots onto the setup network, and this Mac's WiFi scan sees it
  3  ssid/pass/dest/rate/join over serial: the board joins and prints its address
  4  UDP /enq answers /enq "WiFiSetup" + /enq/net with that address
  5  UDP /net x20: name, ip, mac; the round-trip median
  6  the /state stream arrives at the destination at the set period, no gaps
  7  /rate 0 stops it, /rate 100 restarts it, both echoed
  8  /net broadcast to 255.255.255.255 finds the board (x-OSC's /ping)
  9  GET / is the settings form, ssid prefilled, password never in it
 10  GET /enq, GET /state, POST /osc, OPTIONS /osc (the browser page's bridge)
 11  POST /save changes the destination and period, reboots, and the stream follows
 12  [--captive, ESP32 built with -DWIFI_SETUP_KEEP_AP] the DNS catch-all and the
     captive redirect, reached over the joined network
 13  a wrong ssid falls back to the setup network within the join window
 14  [--long] alone on the fallback network, the board retries and joins
 15  the real ssid again: joined, left running (or `forget` with --keep unset)

The Mac never joins the board's access point (that would drop this machine
off its own network); the setup network is seen by a WiFi scan, and its
servers are exercised over the joined network with the bench flag.
"""
import argparse
import http.client
import os
import re
import socket
import statistics
import struct
import subprocess
import sys
import termios
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from oscprobe import msg, bundle, decode  # noqa: E402

AIRPORT = "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport"

passed, failed = [], []


def ok(name, cond, detail=""):
    (passed if cond else failed).append(name)
    print(f"  {'PASS' if cond else 'FAIL'}  {name}" + (f"  -- {detail}" if detail else ""))
    return cond


# ---------------------------------------------------------------- serial tty

class Port:
    def __init__(self, path):
        self.path = path
        self.fd = None
        self.buf = b""
        self.open()

    def open(self, wait=20.0):
        end = time.time() + wait
        while True:
            try:
                self.fd = os.open(self.path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                break
            except OSError:
                if time.time() > end:
                    raise
                time.sleep(0.2)
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0            # raw
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def close(self):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
            self.fd = None

    def reopen(self):
        """The port vanished (a reboot re-enumerates USB). Wait for it to come
        back, then ask for the banner again: an ESP32's USB-Serial-JTAG drops
        whatever it printed while no host had the port open, so the boot
        lines themselves may never have reached us."""
        self.close()
        time.sleep(0.3)
        self.open()
        time.sleep(0.5)
        self.send("show")

    def send(self, line):
        data = (line + "\n").encode()
        while data:
            try:
                n = os.write(self.fd, data)
                data = data[n:]
            except BlockingIOError:
                time.sleep(0.01)
        time.sleep(0.05)

    def read_until(self, pattern, timeout, quiet=False):
        """Accumulate output until `pattern` (regex) matches or timeout; the
        port may vanish and come back (a reboot), which is survived."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        text = ""
        while time.time() < end:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                chunk = b""
            except OSError:
                self.reopen()
                continue
            if chunk:
                text += chunk.decode("utf-8", "replace")
                m = rx.search(text)
                if m:
                    if not quiet:
                        for ln in text.strip().splitlines():
                            print("    | " + ln)
                    return m, text
            else:
                time.sleep(0.02)
        if not quiet:
            for ln in text.strip().splitlines():
                print("    | " + ln)
        return None, text

    def drain(self, t=0.3):
        end = time.time() + t
        while time.time() < end:
            try:
                if not os.read(self.fd, 4096):
                    time.sleep(0.02)
            except (BlockingIOError, OSError):
                time.sleep(0.02)


# ------------------------------------------------------------------ helpers

def secrets(path):
    txt = open(path).read()
    ssid = re.search(r'#define\s+SECRET_SSID\s+"(.*)"', txt)
    pw = re.search(r'#define\s+SECRET_PASS\s+"(.*)"', txt)
    if not ssid or not pw:
        sys.exit(f"{path}: SECRET_SSID / SECRET_PASS not found")
    return ssid.group(1), pw.group(1)


def my_ip(toward):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((toward, 9))
    ip = s.getsockname()[0]
    s.close()
    return ip


def scan_for(ssid):
    try:
        out = subprocess.run([AIRPORT, "-s"], capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    for ln in out.splitlines():
        if ssid in ln:
            m = re.search(r"(-\d+)\s+(\d+)", ln[ln.index(ssid) + len(ssid):])
            return (int(m.group(1)), int(m.group(2))) if m else (None, None)
    return None


def flat(p):
    """decode() output as a list of (addr, args) whatever the shape."""
    d = decode(p)
    return d[1] if d[0] == "bundle" else [d]


def ask(ip, port, packet, timeout=2.0, sock=None):
    own = sock is None
    if own:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    t0 = time.time()
    sock.sendto(packet, (ip, port))
    try:
        data, addr = sock.recvfrom(2048)
    except socket.timeout:
        data, addr = None, None
    dt = (time.time() - t0) * 1000
    if own:
        sock.close()
    return data, addr, dt


def http_req(ip, method, path, body=None, headers=None):
    c = http.client.HTTPConnection(ip, 80, timeout=6)
    c.request(method, path, body=body, headers=headers or {})
    r = c.getresponse()
    data = r.read()
    hdrs = {k.lower(): v for k, v in r.getheaders()}
    c.close()
    return r.status, hdrs, data


def dns_query(ip, name, qtype=1):
    q = struct.pack(">HHHHHH", 0x1234, 0x0100, 1, 0, 0, 0)
    for label in name.split("."):
        q += bytes([len(label)]) + label.encode()
    q += b"\0" + struct.pack(">HH", qtype, 1)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(2.0)
    s.sendto(q, (ip, 53))
    try:
        r, _ = s.recvfrom(512)
    except socket.timeout:
        return None
    finally:
        s.close()
    flags, qd, an = struct.unpack(">HHH", r[2:8])
    if an == 0:
        return (flags & 0xF, None)
    return (flags & 0xF, ".".join(str(b) for b in r[-4:]))


def wait_joined(port, timeout=50):
    m, _ = port.read_until(r'joined "(.*)" as (\d+\.\d+\.\d+\.\d+)', timeout)
    return m.group(2) if m else None


def stream_sample(listen_port, seconds, expect_period_ms):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", listen_port))
    s.settimeout(0.5)
    seqs, times = [], []
    end = time.time() + seconds
    while time.time() < end:
        try:
            data, _ = s.recvfrom(1024)
        except socket.timeout:
            continue
        for addr, args in flat(data):
            if addr == "/state":
                seqs.append(args[0])
                times.append(time.time())
    s.close()
    gaps = sum(b - a - 1 for a, b in zip(seqs, seqs[1:]) if b > a)
    period = statistics.median([(b - a) * 1000 for a, b in zip(times, times[1:])]) if len(times) > 2 else None
    return len(seqs), gaps, period


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--secrets", required=True, help="arduino_secrets.h with SECRET_SSID / SECRET_PASS")
    ap.add_argument("--captive", action="store_true", help="board built with -DWIFI_SETUP_KEEP_AP: test DNS + redirect over the LAN")
    ap.add_argument("--long", action="store_true", help="also wait out the 3-minute fallback retry")
    ap.add_argument("--keep", action="store_true", help="leave the board joined at the end (default: yes); --no-keep forgets")
    ap.add_argument("--forget-at-end", action="store_true")
    a = ap.parse_args()

    ssid, pw = secrets(a.secrets)
    print(f"secrets: ssid {ssid!r}, password (set, {len(pw)} chars, not shown)")
    port = Port(a.port)
    port.drain(0.5)

    # 1 ---------------------------------------------------------------------
    print("\n1. serial console")
    port.send("show")
    m, text = port.read_until(r'WiFiSetup "(\S+)"', 4)
    if not ok("show answers", bool(m)):
        sys.exit("the board is not running WiFiSetup, or not on this port")
    name = m.group(1)
    print(f"  board name {name}")

    # 2 ---------------------------------------------------------------------
    print("\n2. forget -> setup network")
    port.send("forget")
    m, _ = port.read_until(r'setup network "(\S+)" \(open\) at http://(\d+\.\d+\.\d+\.\d+)/', 30)
    ok("boots onto the setup network after forget", bool(m))
    ap_ssid, ap_ip = (m.group(1), m.group(2)) if m else (name, None)
    time.sleep(3)
    seen = scan_for(ap_ssid)
    ok("this Mac's WiFi scan sees the setup network", bool(seen),
       f"{ap_ssid} rssi {seen[0]} ch {seen[1]}" if seen else f"{ap_ssid} not in scan")

    # 3 ---------------------------------------------------------------------
    print("\n3. provision over serial and join")
    port.send(f"ssid {ssid}")
    port.send(f"pass {pw}")
    port.read_until(r"\(set\)", 3, quiet=True)
    port.send("rate 100")
    port.send("join")
    ip = wait_joined(port, 50)
    if not ok("joins and prints its address", bool(ip), ip or "no 'joined' line"):
        sys.exit("cannot continue without the board on the network")
    mac_ip = my_ip(ip)
    print(f"  board {ip}, this Mac {mac_ip}")
    time.sleep(1)

    # 4 ---------------------------------------------------------------------
    print("\n4. UDP /enq")
    data, addr, dt = ask(ip, 8000, msg("/enq"))
    msgs = flat(data) if data else []
    enq = [x for x in msgs if x[0] == "/enq"]
    net = [x for x in msgs if x[0] == "/enq/net"]
    ok("/enq answers /enq WiFiSetup", bool(enq) and enq[0][1][:1] == ["WiFiSetup"], str(enq))
    ok("/enq/net carries the board's ip and port 8000", bool(net) and net[0][1][0] == ip and net[0][1][2] == 8000, str(net))

    # 5 ---------------------------------------------------------------------
    print("\n5. UDP /net x20")
    rtts, last = [], None
    for _ in range(20):
        data, addr, dt = ask(ip, 8000, msg("/net"))
        if data:
            rtts.append(dt)
            last = flat(data)
    good = len(rtts)
    ok("/net answers 20/20", good == 20, f"{good}/20, median {statistics.median(rtts):.1f} ms, worst {max(rtts):.1f} ms" if rtts else "none")
    if last:
        n = [x for x in last if x[0] == "/net"]
        ok("/net reply is name ip mac rssi port", bool(n) and n[0][1][0] == name and n[0][1][1] == ip and len(n[0][1]) == 5, str(n))

    # 6 ---------------------------------------------------------------------
    print("\n6. the stream: dest = this Mac:9000, every 100 ms")
    data, _, _ = ask(ip, 8000, msg("/net/dest", [f"{mac_ip}:9000"]))
    echo = flat(data) if data else []
    ok("/net/dest echoed", bool(echo) and echo[0][0] == "/net/dest" and echo[0][1] == [mac_ip, 9000], str(echo))
    count, gaps, period = stream_sample(9000, 3.0, 100)
    ok("/state arrives at ~100 ms with no gaps", count >= 25 and gaps == 0 and period and 80 <= period <= 125,
       f"{count} in 3 s, {gaps} gaps, median period {period and round(period, 1)} ms")

    # 7 ---------------------------------------------------------------------
    print("\n7. /rate 0 stops, /rate 100 restarts")
    data, _, _ = ask(ip, 8000, msg("/rate", [0]))
    echo = flat(data) if data else []
    ok("/rate 0 echoed as 0", bool(echo) and echo[0][1] == [0], str(echo))
    count, _, _ = stream_sample(9000, 1.0, 100)
    ok("stream silent after /rate 0", count == 0, f"{count} packets in 1 s")
    data, _, _ = ask(ip, 8000, msg("/rate", [100]))
    count, gaps, period = stream_sample(9000, 1.5, 100)
    ok("stream back after /rate 100", count >= 10 and gaps == 0, f"{count} in 1.5 s, {gaps} gaps")

    # 8 ---------------------------------------------------------------------
    print("\n8. broadcast discovery")
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    data, addr, dt = ask("255.255.255.255", 8000, msg("/net"), sock=s)
    s.close()
    ok("/net to 255.255.255.255:8000 answered by the board", bool(data) and addr and addr[0] == ip,
       f"from {addr[0] if addr else None} in {dt:.0f} ms")

    # 9 ---------------------------------------------------------------------
    print("\n9. the settings page")
    st, h, body = http_req(ip, "GET", "/")
    page = body.decode("utf-8", "replace")
    ok("GET / is 200 text/html", st == 200 and "text/html" in h.get("content-type", ""), f"{st} {h.get('content-type')} {len(body)} B")
    ok("form has name, ssid, pass, dest, port, rate, mode fields",
       all(f'name="{f}"' in page for f in ("name", "ssid", "pass", "dest", "port", "rate", "mode")))
    ok("ssid prefilled, password never in the page", f'value="{ssid}"' in page and pw not in page and 'name="pass"' in page)
    ok("page says joined, with the board's address", "Joined" in page and ip in page)

    # 10 --------------------------------------------------------------------
    print("\n10. the HTTP bridge for the browser page")
    st, h, body = http_req(ip, "GET", "/enq")
    got = flat(body) if st == 200 and body else []
    ok("GET /enq is the enq bundle with CORS", st == 200 and h.get("access-control-allow-origin") == "*" and any(x[0] == "/enq" for x in got), str(got))
    st, h, body = http_req(ip, "GET", "/state")
    got = flat(body) if st == 200 and body else []
    ok("GET /state is a /state bundle", st == 200 and got and got[0][0] == "/state", str(got))
    st, h, body = http_req(ip, "POST", "/osc", body=msg("/net"), headers={"Content-Type": "application/octet-stream"})
    got = flat(body) if st == 200 and body else []
    ok("POST /osc /net answers the /net reply in the body", st == 200 and got and got[0][0] == "/net" and got[0][1][1] == ip, str(got)[:80])
    st, h, body = http_req(ip, "OPTIONS", "/osc")
    ok("OPTIONS /osc is 204 with CORS headers", st == 204 and "POST" in h.get("access-control-allow-methods", ""), f"{st} {h.get('access-control-allow-methods')}")

    # 11 --------------------------------------------------------------------
    print("\n11. save from the form: dest port 9001, rate 50, reboot")
    form = f"name={name}&ssid={ssid}&pass=&dest={mac_ip}%3A9001&port=8000&rate=50&mode=join"
    st, h, body = http_req(ip, "POST", "/save", body=form, headers={"Content-Type": "application/x-www-form-urlencoded"})
    ok("POST /save answers the Saved page", st == 200 and b"Restarting to join" in body, f"{st}")
    ip2 = wait_joined(port, 50)
    ok("board rejoined after save", ip2 == ip, str(ip2))
    time.sleep(0.5)
    count, gaps, period = stream_sample(9001, 2.0, 50)
    ok("stream now on port 9001 at ~50 ms, no gaps", count >= 30 and gaps == 0 and period and 40 <= period <= 65,
       f"{count} in 2 s, {gaps} gaps, median {period and round(period, 1)} ms")
    st, h, body = http_req(ip, "GET", "/")
    page = body.decode("utf-8", "replace")
    ok("the saved password survived a blank pass field", f'value="{ssid}"' in page and "Joined" in page)

    # 12 --------------------------------------------------------------------
    if a.captive:
        print("\n12. captive portal, over the joined network (bench flag)")
        seen = scan_for(name)
        ok("setup network on the air beside the joined one", bool(seen), f"{name} rssi {seen[0]} ch {seen[1]}" if seen else "not in scan")
        r = dns_query(ip, "captive.apple.com")
        ok("DNS catch-all answers captive.apple.com -> 192.168.4.1", r == (0, "192.168.4.1"), str(r))
        r = dns_query(ip, "connectivitycheck.gstatic.com", qtype=28)
        ok("AAAA query gets NOERROR with no answer", r == (0, None), str(r))
        st, h, body = http_req(ip, "GET", "/hotspot-detect.html", headers={"Host": "captive.apple.com"})
        ok("Host: captive.apple.com -> 302 to http://192.168.4.1/", st == 302 and h.get("location") == "http://192.168.4.1/", f"{st} {h.get('location')}")
        st, h, body = http_req(ip, "GET", "/generate_204", headers={"Host": "connectivitycheck.gstatic.com"})
        ok("Android probe -> 302 as well", st == 302, f"{st}")
        st, h, body = http_req(ip, "GET", "/", headers={"Host": ip})
        ok("our own host still serves the page", st == 200 and b"<form" in body, f"{st}")

    # 13 --------------------------------------------------------------------
    print("\n13. a wrong network falls back to the setup network")
    port.send("ssid no-such-network-4dF8")
    port.send("join")
    m, _ = port.read_until(r"could not join", 45)
    ok("gives up within the join window", bool(m))
    m, _ = port.read_until(r'setup network "(\S+)" \(open\) at http://(\d+\.\d+\.\d+\.\d+)/', 30)
    ok("comes back on the setup network", bool(m))
    m2, _ = port.read_until(r"because joining .* failed", 3)
    ok("and says why", bool(m2))
    time.sleep(3)
    seen = scan_for(name)
    ok("scan sees the fallback setup network", bool(seen), f"rssi {seen[0]} ch {seen[1]}" if seen else "not in scan")

    # 14 --------------------------------------------------------------------
    if a.long:
        print("\n14. alone on the fallback network: retry after 3 min, with the real ssid saved meanwhile")
        port.send(f"ssid {ssid}")
        port.send("save")
        port.read_until(r"saved", 3, quiet=True)
        t0 = time.time()
        m, _ = port.read_until(r"nobody on the setup network", 240)
        ok("retries after the fallback period", bool(m), f"after {time.time() - t0:.0f} s")
        ip3 = wait_joined(port, 50)
        ok("and joins the real network", ip3 == ip, str(ip3))
    else:
        print("\n14. (skipped: --long)")

    # 15 --------------------------------------------------------------------
    print("\n15. restore")
    if a.forget_at_end:
        port.send("forget")
        m, _ = port.read_until(r"setup network", 30)
        ok("forgotten, on the setup network", bool(m))
    else:
        port.send(f"ssid {ssid}")
        port.send("rate 0")
        port.send("join")
        ip4 = wait_joined(port, 50)
        ok("joined again with the real ssid, stream off", ip4 == ip, str(ip4))

    print(f"\n{len(passed)}/{len(passed) + len(failed)} passed" + (f"; FAILED: {', '.join(failed)}" if failed else ""))
    port.close()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""The numbers KB2040Oscuino.html's drawing reads from the two boards' own
Eagle files, printed from those files.

    python3 -I brdgeom.py KB2040.brd ADAPTER.brd          readable
    python3 -I brdgeom.py --json KB2040.brd ADAPTER.brd   the same, as JSON

extras/webserial/test/test-kb2040-contract.mjs runs the second and holds the
page's KB and AD to it when KB2040_BRD and ADAPTER_BRD name the two files.

The files are Adafruit's, Creative Commons Attribution-ShareAlike 3.0
Unported by each repository's README and license.txt:
  "Adafruit KB2040.brd" in github.com/adafruit/Adafruit-KB2040-PCB at
      b4160dcf4385ef2cc30e9652489328321720ee4b
  "Adafruit Wii Nunchuck Breakout Adapter.brd" in
      github.com/adafruit/Adafruit-Wii-Nunchuck-Breakout-Adapter-PCB at
      d3f2e25c4f9211553e32b36ffce03212f414ead3
The SHA-256 of each file read is printed with its numbers.

Millimetres, on Eagle's axes (y up). An element's pads and drawing are turned
(and mirrored, for one on the bottom) as the element is placed. A part's
boxes are the extent of its package's wires' end points and rectangles'
corners in tDocu (layer 51) and in tPlace (21), or for a part on the bottom
in bDocu (52) and bPlace (22), each where it draws anything; its circles are
those in the same layers: centre, radius, line width, layer. An smd pad's
layer is the board's, Top (1) or Bottom (16), the two swapped for a part on
the bottom, and its roundness is Eagle's, in percent. The outline is the
board's dimension layer (20), wire by wire, each with its curve in degrees.
Texts are the board's own top silkscreen texts (layer 21).
"""
import hashlib
import json
import math
import re
import sys
import xml.etree.ElementTree as ET


def r3(v):
    return round(v + 0.0, 3)


def turn(rot, x, y):
    """A package point (x, y) turned as Eagle's rot attribute turns it. For a
    mirrored part (M) x is negated, then the point turned. At 90 and 270
    degrees that order matters, and neither board file checks it: their only
    parts placed MR90 or MR270 are three logos on the adapter's bottom, with
    no pads, which the page does not draw."""
    rot = rot or "R0"
    a = math.radians(float(re.sub(r"[^0-9.]", "", rot) or 0))
    if "M" in rot:
        x = -x
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


def board(path):
    data = open(path, "rb").read()
    root = ET.fromstring(data)
    packages = {(lib.get("name"), p.get("name")): p
                for lib in root.iter("library") for p in lib.iter("package")}
    net = {}
    for s in root.iter("signal"):
        for c in s.iter("contactref"):
            net[(c.get("element"), c.get("pad"))] = s.get("name")
    plain = root.find(".//plain")
    out = {"sha256": hashlib.sha256(data).hexdigest(), "outline": [], "texts": [], "elements": {}}
    for w in plain.findall("wire"):
        if w.get("layer") == "20":
            out["outline"].append([r3(float(w.get(k))) for k in ("x1", "y1", "x2", "y2")]
                                  + [r3(float(w.get("curve") or 0))])
    for t in plain.findall("text"):
        if t.get("layer") == "21":
            out["texts"].append({"x": r3(float(t.get("x"))), "y": r3(float(t.get("y"))), "text": t.text,
                                 "size": float(t.get("size")), "rot": t.get("rot") or "R0",
                                 "align": t.get("align") or "bottom-left"})
    for e in root.iter("element"):
        p = packages[(e.get("library"), e.get("package"))]
        ex, ey, rot = float(e.get("x")), float(e.get("y")), e.get("rot")

        def place(x, y):
            dx, dy = turn(rot, x, y)
            return ex + dx, ey + dy

        el = {"package": e.get("package"), "value": e.get("value") or "", "at": [r3(ex), r3(ey)],
              "rot": rot or "R0", "boxes": {}, "circles": [], "pads": []}
        # the board layer each package layer lands on: a part on the bottom is mirrored
        side = {"51": "52", "21": "22"} if "M" in (rot or "") else {"51": "51", "21": "21"}
        copper = {"1": "16", "16": "1"} if "M" in (rot or "") else {"1": "1", "16": "16"}
        for layer in ("51", "21"):
            pts = []
            for w in p.findall("wire"):
                if w.get("layer") == layer:
                    pts += [place(float(w.get("x1")), float(w.get("y1"))), place(float(w.get("x2")), float(w.get("y2")))]
            for r in p.findall("rectangle"):
                if r.get("layer") == layer:
                    x1, y1, x2, y2 = (float(r.get(k)) for k in ("x1", "y1", "x2", "y2"))
                    pts += [place(x1, y1), place(x2, y2), place(x1, y2), place(x2, y1)]
            if pts:
                xs, ys = [q[0] for q in pts], [q[1] for q in pts]
                el["boxes"][side[layer]] = [r3(min(xs)), r3(min(ys)), r3(max(xs)), r3(max(ys))]
        for c in p.findall("circle"):
            if c.get("layer") in side:
                x, y = place(float(c.get("x")), float(c.get("y")))
                el["circles"].append([r3(x), r3(y), float(c.get("radius")), float(c.get("width")), side[c.get("layer")]])
        for s in p.findall("pad"):
            x, y = place(float(s.get("x")), float(s.get("y")))
            el["pads"].append({"name": s.get("name"), "x": r3(x), "y": r3(y),
                               "net": net.get((e.get("name"), s.get("name"))),
                               "drill": float(s.get("drill")), "diameter": float(s.get("diameter") or 0),
                               "shape": s.get("shape") or "round", "rot": s.get("rot") or "R0"})
        for s in p.findall("smd"):
            x, y, dx, dy = (float(s.get(k)) for k in ("x", "y", "dx", "dy"))
            corners = []
            for cx, cy in ((-dx / 2, -dy / 2), (dx / 2, dy / 2), (-dx / 2, dy / 2), (dx / 2, -dy / 2)):
                ox, oy = turn(s.get("rot"), cx, cy)
                corners.append(place(x + ox, y + oy))
            cx, cy = place(x, y)
            xs, ys = [q[0] for q in corners], [q[1] for q in corners]
            el["pads"].append({"name": s.get("name"), "x": r3(cx), "y": r3(cy),
                               "net": net.get((e.get("name"), s.get("name"))),
                               "layer": copper[s.get("layer")], "roundness": int(s.get("roundness") or 0),
                               "box": [r3(min(xs)), r3(min(ys)), r3(max(xs)), r3(max(ys))]})
        out["elements"][e.get("name")] = el
    return out


def readable(name, b):
    print(f"{name}  sha256 {b['sha256']}")
    xs = [v for w in b["outline"] for v in (w[0], w[2])]
    ys = [v for w in b["outline"] for v in (w[1], w[3])]
    print(f"  outline x {min(xs)}..{max(xs)}  y {min(ys)}..{max(ys)}")
    for w in b["outline"]:
        print(f"    ({w[0]}, {w[1]}) -> ({w[2]}, {w[3]})" + (f"  curve {w[4]}" if w[4] else ""))
    for t in b["texts"]:
        print(f"  text {t['text']!r} at ({t['x']}, {t['y']}) size {t['size']} {t['rot']} {t['align']}")
    for n, e in sorted(b["elements"].items()):
        box = "  ".join(f"layer {k} box {v}" for k, v in e["boxes"].items()) or "no box"
        print(f"  {n} {e['package']} {e['value']!r} at {e['at']} {e['rot']}: {box}")
        for x, y, radius, width, layer in e["circles"]:
            print(f"    circle ({x}, {y}) radius {radius} width {width} layer {layer}")
        for p in e["pads"]:
            extra = (f" drill {p['drill']} dia {p['diameter']} {p['shape']} {p['rot']}" if "drill" in p
                     else f" layer {p['layer']} box {p['box']}" + (f" round {p['roundness']}" if p["roundness"] else ""))
            print(f"    pad {p['name']} ({p['x']}, {p['y']}) {p['net']}{extra}")


def main():
    args = [a for a in sys.argv[1:] if a != "--json"]
    if len(args) != 2:
        sys.exit(__doc__)
    boards = {"kb2040": board(args[0]), "adapter": board(args[1])}
    if "--json" in sys.argv:
        json.dump(boards, sys.stdout)
        print()
    else:
        for name, b in boards.items():
            readable(name, b)


main()

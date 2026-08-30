# boot.py for CircuitPythonOscuino -- copy to the CIRCUITPY drive next to code.py.
#
# GENERATED FILE -- do not edit directly.
# Source: extras/webserial/template-boot.py + extras/webserial/boards.json
#
# Runs once at power-up, before USB enumerates, and adds a second CDC serial
# port: the "data" channel code.py speaks SLIP on. The console channel stays
# for the REPL. Without this file code.py falls back to the console, where a
# 0x03 byte in a SLIP frame reads as ctrl-C and kills the program.
import usb_cdc

usb_cdc.enable(console=True, data=True)

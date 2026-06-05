#!/usr/bin/env python3
import os
import socket
import time
import serial

SERIAL_PORT = os.environ.get("ARDUINO_PORT", "/dev/ttyACM0")
BAUD = int(os.environ.get("ARDUINO_BAUD", "115200"))

TOPST_IP = os.environ.get("TOPST_IP", "192.168.50.20")
TOPST_PORT = int(os.environ.get("TOPST_PORT", "5005"))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"[ARDUINO_BRIDGE] serial={SERIAL_PORT} baud={BAUD}")
print(f"[ARDUINO_BRIDGE] TOPST={TOPST_IP}:{TOPST_PORT}")

while True:
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
        print("[ARDUINO_BRIDGE] serial opened")
        break
    except Exception as e:
        print(f"[ARDUINO_BRIDGE] serial open retry: {e}")
        time.sleep(1)

while True:
    raw = ser.readline().decode(errors="ignore").strip()
    if not raw:
        continue

    # Arduino may send CTRL or CONTROLLER.
    # TOPST expects CONTROLLER most safely.
    line = raw
    if line.startswith("CTRL "):
        line = "CONTROLLER " + line[len("CTRL "):]

    # Arduino code may use joyx/joyy. TOPST patched version can accept both,
    # but normalize here too.
    line = line.replace(" joyx=", " joy_x=")
    line = line.replace(" joyy=", " joy_y=")

    if not line.startswith("CONTROLLER "):
        print(f"[ARDUINO_BRIDGE IGNORE] {raw}")
        continue

    sock.sendto(line.encode(), (TOPST_IP, TOPST_PORT))
    print(f"[ARDUINO_TX] {line}")

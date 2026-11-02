#!/usr/bin/env python3
"""Fake OpenRGB SDK server (protocol v4) that records what a client sends.

  fake-openrgb.py PORT LOG     serve on 127.0.0.1:PORT; append every packet received to LOG

Used to check that the Rust and C++ clients send the same bytes: run a client command against
it once per binary and diff the logs. The devices copy your hardware, encoded the way
OpenLinkHub's buildDeviceDataPayload does (same layout as crates/wpc-openrgb/src/tests.rs):
two RAM modules (type 1, 10 LEDs each) and a Commander Core (AIO 29 + 4 fans × 8 LEDs).

One line per packet: "device id payload-hex". Runs until killed.
"""
import socket, struct, sys

SET_CLIENT_NAME, REQ_PROTO, REQ_COUNT, REQ_DATA, DEVICE_LIST_UPDATED = 50, 40, 0, 1, 100
SERVER_PROTO = 4

DEVICES = [
    ("VENGEANCE RGB DDR5", 1, [("DIMM", 10)]),
    ("VENGEANCE RGB DDR5", 1, [("DIMM", 10)]),
    ("iCUE COMMANDER Core", 7, [("H150i ELITE CAPELLIX XT", 29)]
     + [(f"8-LED Series Fan", 8)] * 4),
]


def u16(v): return struct.pack("<H", v)
def u32(v): return struct.pack("<I", v)
def string(s): return u16(len(s.encode()) + 1) + s.encode() + b"\0"


def encode(name, kind, zones):
    b = u32(kind) + string(name)
    for s in ["Corsair", "desc", "1.0", "SERIAL", "HID: /dev/hidraw8"]:
        b += string(s)
    b += u16(1) + u32(0) + string("Direct")  # one mode, active 0
    b += b"".join(u32(v) for v in [0, 1 << 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]) + u16(0)
    b += u16(len(zones))
    for zname, leds in zones:
        b += string(zname) + u32(1) + u32(leds) * 3
        b += u16(12) + u32(1) + u32(1) + u32(0)  # matrix 1x1
        b += u16(1) + string("seg") + u32(0) + u32(0) + u32(leds)
    total = sum(n for _, n in zones)
    b += u16(total) + b"".join(string(f"LED {i}") + u32(i) for i in range(total))
    b += u16(total) + bytes([0, 255, 255, 0]) * total
    return u32(len(b) + 4) + b


def reply(conn, device, packet, payload=b""):
    conn.sendall(b"ORGB" + struct.pack("<III", device, packet, len(payload)) + payload)


def recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def serve(conn, log):
    while (h := recv_exact(conn, 16)) is not None:
        _, device, packet, size = struct.unpack("<4sIII", h)
        payload = recv_exact(conn, size) if size else b""
        if payload is None:
            return
        log.write(f"{device} {packet} {payload.hex()}\n")
        log.flush()
        if packet == REQ_PROTO:
            reply(conn, 0, DEVICE_LIST_UPDATED)  # unsolicited: the client must skip it
            reply(conn, 0, packet, u32(SERVER_PROTO))
        elif packet == REQ_COUNT:
            reply(conn, 0, packet, u32(len(DEVICES)))
        elif packet == REQ_DATA:
            data = encode(*DEVICES[device]) if device < len(DEVICES) else b""
            reply(conn, device, packet, data)


def main():
    port, log_path = int(sys.argv[1]), sys.argv[2]
    srv = socket.create_server(("127.0.0.1", port), reuse_port=True)
    with open(log_path, "a") as log:
        while True:
            conn, _ = srv.accept()
            with conn:
                log.write("# connection\n")
                serve(conn, log)


if __name__ == "__main__":
    main()

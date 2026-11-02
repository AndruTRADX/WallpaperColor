#!/usr/bin/env python3
"""Phase 0 probe: talk to OpenLinkHub's built-in OpenRGB SDK server (protocol v4).

  orgb-probe.py list              controllers exposed (devices with "OpenRGB Integration" on)
  orgb-probe.py set RRGGBB        paint every LED of every exposed controller

Throwaway tool to validate the hardware path before the Rust implementation.
"""
import socket, struct, sys

HOST, PORT, PROTO = "127.0.0.1", 6743, 4
SET_CLIENT_NAME, REQ_PROTO, REQ_COUNT, REQ_DATA, UPDATE_LEDS = 50, 40, 0, 1, 1050


def send(sock, packet, payload=b"", device=0):
    sock.sendall(b"ORGB" + struct.pack("<III", device, packet, len(payload)) + payload)


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("server closed the connection")
        buf += chunk
    return buf


def recv(sock):
    magic, device, packet, size = struct.unpack("<4sIII", recv_exact(sock, 16))
    assert magic == b"ORGB", magic
    return packet, recv_exact(sock, size)


def controller(sock, i):
    """(name, number of LEDs). The colour list ends the payload: u16 count + count*4 bytes."""
    send(sock, REQ_DATA, struct.pack("<I", PROTO), device=i)
    _, data = recv(sock)
    name_len = struct.unpack_from("<H", data, 8)[0]
    name = data[10:10 + name_len].rstrip(b"\0").decode(errors="replace")
    for n in range(1, 4096):
        pos = len(data) - 4 * n - 2
        if pos < 0:
            break
        if struct.unpack_from("<H", data, pos)[0] == n:
            return name, n
    return name, 0


def main():
    sock = socket.create_connection((HOST, PORT), timeout=5)
    send(sock, SET_CLIENT_NAME, b"WallpaperColor probe\0")
    send(sock, REQ_PROTO, struct.pack("<I", PROTO))
    print("server protocol:", struct.unpack("<I", recv(sock)[1])[0])
    send(sock, REQ_COUNT)
    count = struct.unpack("<I", recv(sock)[1])[0]
    ctrls = [controller(sock, i) for i in range(count)]
    for i, (name, leds) in enumerate(ctrls):
        print(f"  [{i}] {name}: {leds} LEDs")
    if not ctrls:
        print("  (none: turn on 'OpenRGB Integration' for a device in the WebUI)")

    if len(sys.argv) > 2 and sys.argv[1] == "set":
        r, g, b = bytes.fromhex(sys.argv[2].lstrip("#"))
        for i, (name, leds) in enumerate(ctrls):
            colours = bytes([r, g, b, 0]) * leds
            body = struct.pack("<H", leds) + colours
            send(sock, UPDATE_LEDS, struct.pack("<I", 4 + len(body)) + body, device=i)
            print(f"  sent #{r:02x}{g:02x}{b:02x} to [{i}] {name}")
    sock.close()


if __name__ == "__main__":
    main()

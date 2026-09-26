"""Plays the console's side of discovery, to test the phone without a PS2.

Sends RAP1 the way the PS2 agent does (a broadcast to UDP 18194) and
lists every client that answers RAO1, with the round trip. Run it from a
PC on the same Wi-Fi as the phone:

    python android/tools/fake_ps2.py                  one discovery
    python android/tools/fake_ps2.py --every 15       keep going: turn the
                                                      phone's screen off and
                                                      watch the answers
    python android/tools/fake_ps2.py --hash H --serial SLUS_210.65
                                                      also identify a game
                                                      (RAQ1): proves the
                                                      phone reaches
                                                      RetroAchievements

A desktop xerabora running on the PC answers too; each answer shows its
address.
"""
import argparse
import socket
import time

CLIENT_PORT = 18194


def local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 53))  # no packet is sent; picks the LAN interface
        return s.getsockname()[0]
    finally:
        s.close()


def text(data: bytes) -> str:
    return data.decode("ascii", "replace").rstrip(" \0")


def discover(sock: socket.socket, me: str, port: int, targets: list, wait: float) -> dict:
    sent = time.monotonic()
    for t in targets:
        sock.sendto(f"RAP1 {me} {port}".encode(), (t, CLIENT_PORT))
    found = {}
    while time.monotonic() - sent < wait:
        sock.settimeout(max(0.01, wait - (time.monotonic() - sent)))
        try:
            data, src = sock.recvfrom(2048)
        except socket.timeout:
            break
        if data.startswith(b"RAO1") and src[0] not in found:
            found[src[0]] = (text(data), (time.monotonic() - sent) * 1000)
    return found


def identify(sock: socket.socket, me: str, port: int, target: str, game_hash: str, serial: str) -> None:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        sock.sendto(f"RAQ1 {game_hash} {serial} {me} {port}".encode(), (target, CLIENT_PORT))
        sock.settimeout(2)
        try:
            data, src = sock.recvfrom(2048)
        except socket.timeout:
            print("  RAQ1: no answer yet")
            continue
        reply = text(data)
        print(f"  {src[0]}: {reply}")
        if reply.startswith("RAA1 OK") or reply.startswith("RAA1 NO"):
            return
        time.sleep(1)  # RAA1 WAIT: the client is still asking the server
    print("  RAQ1: gave up after 60 s")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--to", help="one address instead of the broadcast (the phone's IP)")
    ap.add_argument("--port", type=int, default=18195, help="this script's UDP port (default 18195)")
    ap.add_argument("--every", type=float, default=0, help="repeat the discovery every N seconds")
    ap.add_argument("--wait", type=float, default=2.0, help="seconds to collect answers (default 2)")
    ap.add_argument("--hash", help="RA image hash to identify with RAQ1")
    ap.add_argument("--serial", default="SLUS_000.00", help="game serial sent with --hash")
    args = ap.parse_args()

    me = local_ip()
    # The console assumes a /24, like the client does for its pushes.
    subnet = me.rsplit(".", 1)[0] + ".255"
    targets = [args.to] if args.to else [subnet, "255.255.255.255"]

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", args.port))
    print(f"this PC: {me}:{args.port}; RAP1 to {', '.join(targets)}")

    while True:
        found = discover(sock, me, args.port, targets, args.wait)
        stamp = time.strftime("%H:%M:%S")
        if not found:
            print(f"{stamp} no client answered")
        for ip, (reply, ms) in found.items():
            print(f"{stamp} {ip}: {reply} ({ms:.0f} ms)")
        target = args.to or next((ip for ip in found if ip != me), None)
        if args.hash and target:
            identify(sock, me, args.port, target, args.hash, args.serial)
            args.hash = None  # once
        if not args.every:
            break
        time.sleep(args.every)


if __name__ == "__main__":
    main()

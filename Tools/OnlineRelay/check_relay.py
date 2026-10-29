#!/usr/bin/env python3
"""
Checks a Chaos Impact relay from any PC: asks its room list and prints how long the answer took.
  python check_relay.py <address>[:port] [password]
"""
import json
import socket
import sys
import time

target = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1:7700"
password = sys.argv[2] if len(sys.argv) > 2 else "0000"
host, _, port = target.partition(":")
address = (host, int(port or 7700))
request = {"op": "search", "ver": "any", "password": password, "rid": 1}
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(2.0)
for attempt in range(3):
    started = time.perf_counter()
    sock.sendto(json.dumps(request).encode("utf-8"), address)
    try:
        data, _ = sock.recvfrom(16384)
    except socket.timeout:
        print(f"try {attempt + 1}: no answer")
        continue
    print(f"answer in {(time.perf_counter() - started) * 1000:.0f} ms: {data.decode('utf-8')}")
    sys.exit(0)
print("the relay did not answer (check the address, the security list and the VM's firewall)")
sys.exit(1)

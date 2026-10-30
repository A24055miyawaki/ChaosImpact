"""Measure real guest -> relay -> host -> relay -> guest RTT, reusing verified TLS connections.
Creates and closes a temporary private room; never changes ranks.
Run with requirements-wss.txt installed: python benchmark_wss.py [wss://server] [samples]
"""
import asyncio
import json
import secrets
import statistics
import sys
import time

from websockets.asyncio.client import connect


async def benchmark(url, samples):
    async with connect(url.rstrip("/") + "/directory", proxy=None, compression=None) as directory:
        async def request(**fields):
            await directory.send(json.dumps(dict(fields, rid=1)))
            return json.loads(await directory.recv())

        answer = await request(op="ping")
        assert answer["ok"], answer
        made = await request(op="create", ver="ci-wss-probe", password=secrets.token_hex(8), name="WSS latency probe", host="probe")
        assert made["ok"], made
        try:
            endpoint = url.rstrip("/") + "/room/" + str(made["port"])
            async with connect(endpoint, proxy=None, compression=None) as host, connect(endpoint, proxy=None, compression=None) as guest:
                await host.send(b"CIH1" + made["key"].encode())
                assert await host.recv() == b"CIHK"

                async def echo():
                    async for packet in host:
                        if packet[:4] == b"CIRD":
                            await host.send(b"CIHD" + packet[4:])

                task = asyncio.create_task(echo())
                values = []
                try:
                    for index in range(samples + 3):
                        packet = index.to_bytes(4, "big") + b"x" * 124
                        started = time.perf_counter()
                        await guest.send(packet)
                        returned = await asyncio.wait_for(guest.recv(), 4)
                        assert returned == packet
                        elapsed = (time.perf_counter() - started) * 1000
                        if index >= 3:
                            values.append(elapsed)
                        await asyncio.sleep(.02)
                finally:
                    task.cancel()
                    await asyncio.gather(task, return_exceptions=True)
                ordered = sorted(values)
                print(json.dumps({"samples": len(values), "rtt_median_ms": round(statistics.median(values), 1),
                                  "rtt_p95_ms": round(ordered[min(len(values) - 1, int(len(values) * .95))], 1),
                                  "rtt_max_ms": round(max(values), 1)}, indent=2))
        finally:
            await request(op="close", room=made["room"], key=made["key"])


if __name__ == "__main__":
    asyncio.run(benchmark(sys.argv[1] if len(sys.argv) > 1 else "wss://132-145-120-172.sslip.io",
                          int(sys.argv[2]) if len(sys.argv) > 2 else 30))

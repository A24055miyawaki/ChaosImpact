"""The relay's one-port UDP entry (--mux-port): the directory and every room on one port, for a line that lets only
well-known ports out (UDP 443 redirected to it).   python -m unittest test_relay_mux"""
import asyncio
import json
import socket
import struct
import unittest

from ci_relay import MuxProtocol, Relay


class Inbox(asyncio.DatagramProtocol):
    def __init__(self):
        self.queue = asyncio.Queue()

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        self.queue.put_nowait((data, addr))

    async def receive(self):
        return await asyncio.wait_for(self.queue.get(), 2)


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class MuxTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.relay = Relay("127.0.0.1", [free_port()], None)
        loop = asyncio.get_running_loop()
        self.mux_transport, _ = await loop.create_datagram_endpoint(lambda: MuxProtocol(self.relay),
                                                                    local_addr=("127.0.0.1", 0))
        self.mux = ("127.0.0.1", self.mux_transport.get_extra_info("sockname")[1])
        self.udp = []

    async def asyncTearDown(self):
        for inbox in self.udp:
            inbox.transport.close()
        for room in list(self.relay.rooms.values()):
            self.relay.remove(room, "test cleanup")
        self.mux_transport.close()

    async def udp_socket(self):
        _, inbox = await asyncio.get_running_loop().create_datagram_endpoint(Inbox, local_addr=("127.0.0.1", 0))
        self.udp.append(inbox)
        return inbox

    async def ask(self, inbox, request):
        inbox.transport.sendto(json.dumps(request).encode(), self.mux)
        data, addr = await inbox.receive()
        self.assertEqual(addr, self.mux)
        return json.loads(data)

    def to_room(self, inbox, port, data):
        inbox.transport.sendto(b"CIMX" + struct.pack(">H", port) + data, self.mux)

    async def test_directory_and_room_on_one_port(self):
        host = await self.udp_socket()
        made = await self.ask(host, {"op": "create", "ver": "test", "password": "4321", "name": "mux", "host": "h", "rid": 7})
        self.assertTrue(made["ok"])
        self.assertEqual(made["rid"], 7)
        port, key = made["port"], made["key"].encode("ascii")
        self.assertIn("mux", (await self.ask(host, {"op": "ping"}))["transports"])

        # The host says hello through the entry and is answered from it.
        self.to_room(host, port, b"CIH1" + key)
        data, addr = await host.receive()
        self.assertEqual((data, addr), (b"CIHK", self.mux))

        # A guest's packets reach the host (from the entry), and the host's answer reaches the guest (from the entry).
        guest = await self.udp_socket()
        self.to_room(guest, port, b"hello host")
        data, addr = await host.receive()
        self.assertEqual(addr, self.mux)
        self.assertEqual(data, b"CIRD" + struct.pack(">I", 1) + b"hello host")
        self.to_room(host, port, b"CIHD" + struct.pack(">I", 1) + b"hello guest")
        data, addr = await guest.receive()
        self.assertEqual((data, addr), (b"hello guest", self.mux))

    async def test_mixes_with_the_rooms_own_port(self):
        host = await self.udp_socket()
        made = await self.ask(host, {"op": "create", "ver": "test", "password": "4321", "name": "mixed", "host": "h"})
        port, key = made["port"], made["key"].encode("ascii")
        # The host on the room's own port, a guest through the one-port entry.
        host.transport.sendto(b"CIH1" + key, ("127.0.0.1", port))
        self.assertEqual((await host.receive())[0], b"CIHK")
        guest = await self.udp_socket()
        self.to_room(guest, port, b"from the entry")
        data, addr = await host.receive()
        self.assertEqual(addr, ("127.0.0.1", port))
        self.assertEqual(data[8:], b"from the entry")
        host.transport.sendto(b"CIHD" + data[4:8] + b"back", ("127.0.0.1", port))
        self.assertEqual(await guest.receive(), (b"back", self.mux))

    async def test_ignores_unknown_rooms_and_junk(self):
        stranger = await self.udp_socket()
        self.to_room(stranger, 1, b"nobody here")
        stranger.transport.sendto(b"CIMX", self.mux)
        stranger.transport.sendto(b"not json at all", self.mux)
        with self.assertRaises(asyncio.TimeoutError):
            await stranger.receive()
        self.assertEqual(self.relay.mux_addrs, {})


if __name__ == "__main__":
    unittest.main()

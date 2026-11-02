"""The relay's part of a direct connection: it tells a client and the room's host each other's addresses (and a token
for both), and never passes those messages to the game.   python -m unittest test_relay_direct"""
import asyncio
import socket
import struct
import unittest

from ci_relay import Relay


class Inbox(asyncio.DatagramProtocol):
    def __init__(self):
        self.queue = asyncio.Queue()

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        self.queue.put_nowait(data)

    async def receive(self):
        return await asyncio.wait_for(self.queue.get(), 2)

    def port(self):
        return self.transport.get_extra_info("sockname")[1]


class DirectConnectionTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        self.relay = Relay("127.0.0.1", [port], None)
        made = await self.relay.handle({"op": "create", "ver": "test", "password": "1234", "name": "direct", "host": "h"},
                                       ("127.0.0.1", 1))
        self.room = self.relay.rooms[made["room"]]
        self.target = ("127.0.0.1", made["port"])
        self.key = made["key"].encode("ascii")
        self.udp = []
        self.host = await self.udp_socket()
        self.send(self.host, b"CIH1" + self.key)
        self.assertEqual(await self.host.receive(), b"CIHK")

    async def asyncTearDown(self):
        for udp in self.udp:
            udp.transport.close()
        for room in list(self.relay.rooms.values()):
            self.relay.remove(room, "test cleanup")

    async def udp_socket(self):
        _, inbox = await asyncio.get_running_loop().create_datagram_endpoint(Inbox, local_addr=("127.0.0.1", 0))
        self.udp.append(inbox)
        return inbox

    def send(self, inbox, data):
        inbox.transport.sendto(data, self.target)

    async def test_introduces_client_and_host(self):
        self.send(self.host, b"CI2PPRIV" + b"192.168.1.10:%d,bad,10.0.0.5:99999" % self.host.port())
        guest = await self.udp_socket()
        self.send(guest, b"CI2PPRIV192.168.1.20:%d" % guest.port())
        to_guest = await guest.receive()
        to_host = await self.host.receive()
        self.assertEqual(to_guest[:8], b"CI2PHOST")
        self.assertEqual(to_host[:8], b"CI2PPEER")
        # The same client id and token to both.
        self.assertEqual(to_guest[8:20], to_host[8:20])
        (client_id,) = struct.unpack(">I", to_guest[8:12])
        self.assertEqual(client_id, 1)
        self.assertEqual(to_guest[20:].decode(), "pub=127.0.0.1:%d;priv=192.168.1.10:%d" % (self.host.port(), self.host.port()))
        self.assertEqual(to_host[20:].decode(), "pub=127.0.0.1:%d;priv=192.168.1.20:%d" % (guest.port(), guest.port()))

        # The game's own packets still go through as before, and the token stays the client's own.
        self.send(guest, b"game packet")
        self.assertEqual(await self.host.receive(), b"CIRD" + struct.pack(">I", 1) + b"game packet")
        self.send(guest, b"CI2PPRIV")
        self.assertEqual((await guest.receive())[8:20], to_guest[8:20])
        await self.host.receive()
        other = await self.udp_socket()
        self.send(other, b"CI2PPRIV")
        self.assertNotEqual((await other.receive())[12:20], to_guest[12:20])


if __name__ == "__main__":
    unittest.main()

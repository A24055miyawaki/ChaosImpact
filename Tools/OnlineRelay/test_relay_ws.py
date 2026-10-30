import asyncio
import json
import socket
import struct
import unittest

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed, InvalidStatus

from ci_relay import Relay
from ci_relay_ws import start_server


class Inbox(asyncio.DatagramProtocol):
    def __init__(self):
        self.queue = asyncio.Queue()

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        self.queue.put_nowait(data)

    async def receive(self):
        return await asyncio.wait_for(self.queue.get(), 2)


class WebSocketRelayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        self.relay = Relay("127.0.0.1", [port], None)
        self.server = await start_server(self.relay, "127.0.0.1", 0)
        self.url = "ws://127.0.0.1:" + str(self.server.sockets[0].getsockname()[1])
        self.directory = await connect(self.url + "/directory", proxy=None)
        self.sockets = [self.directory]
        self.udp = []
        self.rid = 0
        made = await self.request(op="create", ver="test", password="1234", name="WSS test", host="host")
        self.port, self.key = made["port"], made["key"]
        self.room = self.relay.rooms[made["room"]]

    async def asyncTearDown(self):
        for ws in reversed(self.sockets):
            await ws.close()
        for udp in self.udp:
            udp.transport.close()
        for room in list(self.relay.rooms.values()):
            self.relay.remove(room, "test cleanup")
        self.server.close()
        await self.server.wait_closed()

    async def request(self, **fields):
        self.rid += 1
        await self.directory.send(json.dumps(dict(fields, rid=self.rid)))
        answer = json.loads(await asyncio.wait_for(self.directory.recv(), 2))
        self.assertEqual(answer["rid"], self.rid)
        return answer

    async def ws(self):
        ws = await connect(self.url + "/room/" + str(self.port), proxy=None)
        self.sockets.append(ws)
        return ws

    async def udp_socket(self):
        _, inbox = await asyncio.get_running_loop().create_datagram_endpoint(Inbox, local_addr=("127.0.0.1", 0))
        self.udp.append(inbox)
        return inbox

    async def test_wss_pair_and_fragmented_datagrams(self):
        host, guest = await self.ws(), await self.ws()
        await host.send(b"CIH1" + self.key.encode())
        self.assertEqual(await host.recv(), b"CIHK")
        listing = await self.request(op="search", ver="test", password="1234")
        self.assertEqual(len(listing["rooms"]), 1)
        self.assertEqual((await self.request(op="search", ver="different", password="1234"))["rooms"], [])
        await guest.send([b"a" * 1100, b"b" * 948])
        packet = await host.recv()
        self.assertEqual(packet[:4], b"CIRD")
        self.assertEqual(packet[8:], b"a" * 1100 + b"b" * 948)
        await host.send(b"CIHD" + packet[4:8] + b"c" * 2048)
        self.assertEqual(await guest.recv(), b"c" * 2048)
        await guest.close()
        self.assertEqual(await host.recv(), b"CIRX" + packet[4:8])

    async def test_udp_host_wss_guest(self):
        from ci_relay import RoomProtocol
        # open_room already attached the UDP room protocol to the shared state.
        host, guest = await self.udp_socket(), await self.ws()
        host.transport.sendto(b"CIH1" + self.key.encode(), ("127.0.0.1", self.port))
        self.assertEqual(await host.receive(), b"CIHK")
        await guest.send(b"guest to UDP host")
        packet = await host.receive()
        self.assertEqual(packet[8:], b"guest to UDP host")
        host.transport.sendto(b"CIHD" + packet[4:8] + b"h" * 2048, ("127.0.0.1", self.port))
        self.assertEqual(await guest.recv(), b"h" * 2048)

    async def test_wss_host_udp_guest(self):
        host, guest = await self.ws(), await self.udp_socket()
        await host.send(b"CIH1" + self.key.encode())
        self.assertEqual(await host.recv(), b"CIHK")
        guest.transport.sendto(b"UDP guest", ("127.0.0.1", self.port))
        packet = await host.recv()
        self.assertEqual(packet[8:], b"UDP guest")
        await host.send(b"CIHD" + packet[4:8] + b"WSS host")
        self.assertEqual(await guest.receive(), b"WSS host")

    async def test_reject_unknown_room_and_oversize_guest(self):
        with self.assertRaises(InvalidStatus):
            async with connect(self.url + "/room/99999", proxy=None):
                pass
        host, guest = await self.ws(), await self.ws()
        await host.send(b"CIH1" + self.key.encode())
        await host.recv()
        await guest.send(b"x" * 2050)
        with self.assertRaises(ConnectionClosed):
            await guest.recv()

    async def test_close_room_closes_game_channels(self):
        host = await self.ws()
        await host.send(b"CIH1" + self.key.encode())
        await host.recv()
        await self.request(op="close", room=self.room.id, key=self.key)
        with self.assertRaises(ConnectionClosed):
            await host.recv()


if __name__ == "__main__":
    unittest.main()

"""CONNECT command: the everyday proxy use."""
import socket
import time

from hevtest import client
from hevtest.case import ServerTestCase


class ConnectTest(ServerTestCase):

    def setUp(self):
        super().setUp()
        self.server()

    def test_echo_ipv4(self):
        for size in (1, 1000, 100000, 4 << 20):
            with self.subTest(size=size):
                s = self.connect(self.t4, self.bed.P_ECHO)
                self.assertTrue(client.tcp_echo(s, size))
                s.close()

    def test_echo_ipv6(self):
        for size in (1, 100000, 4 << 20):
            with self.subTest(size=size):
                s = self.connect(self.t6, self.bed.P_ECHO)
                self.assertTrue(client.tcp_echo(s, size))
                s.close()

    def test_client_over_ipv6(self):
        # the proxy is reached over IPv6, the target over both families
        for host in (self.t4, self.t6):
            s = self.connect(host, self.bed.P_ECHO, proxy=self.proxy6)
            self.assertTrue(client.tcp_echo(s, 50000))
            s.close()

    def test_upload_digest(self):
        for host in (self.t4, self.t6):
            s = self.connect(host, self.bed.P_DIGEST)
            self.assertTrue(client.tcp_upload_digest(s, 8 << 20))
            s.close()

    def test_download(self):
        s = self.connect(self.t4, self.bed.P_SOURCE)
        got = client.recv_exact(s, 8 << 20)
        self.assertEqual(got, b"\x5a" * (8 << 20))
        s.close()

    def test_server_speaks_first(self):
        s = self.connect(self.t4, self.bed.P_HELLO)
        self.assertEqual(client.recv_exact(s, 5), b"hello")
        self.assertEqual(s.recv(10), b"")
        s.close()

    def test_half_close(self):
        # the client closes its write side; the answer still arrives
        s = self.connect(self.t4, self.bed.P_COUNT)
        s.sendall(b"x" * 12345)
        s.shutdown(socket.SHUT_WR)
        self.assertEqual(client.read_line(s), "12345")
        s.close()

    def test_target_name(self):
        for name in (self.bed.name_both, self.bed.name_v4, self.bed.name_v6):
            with self.subTest(name=name):
                s = self.connect(name, self.bed.P_ECHO)
                self.assertTrue(client.tcp_echo(s, 5000))
                s.close()

    def test_concurrent_connections(self):
        def one(i):
            host = self.t4 if i % 2 else self.t6
            s = self.connect(host, self.bed.P_ECHO)
            ok = client.tcp_echo(s, 20000)
            s.close()
            return ok
        from hevtest.benchlib import run_threads
        self.assertTrue(all(run_threads(64, one)))

    def test_many_sequential_connections(self):
        for _ in range(200):
            s = self.connect(self.t4, self.bed.P_ECHO)
            self.assertTrue(client.tcp_echo(s, 10))
            s.close()

    def test_idle_connection_stays_open(self):
        s = self.connect(self.t4, self.bed.P_ECHO)
        time.sleep(1.5)
        self.assertTrue(client.tcp_echo(s, 100))
        s.close()

    def test_target_refuses(self):
        with self.assertRaises(client.ReplyError) as cm:
            self.connect(self.t4, self.bed.P_CLOSED)
        self.assertNotEqual(cm.exception.rep, 0)
        # the server keeps serving
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_target_unresolvable(self):
        with self.assertRaises(client.ReplyError):
            self.connect("no-such-host.test", self.bed.P_ECHO)

    def test_target_resets(self):
        s = self.connect(self.t4, self.bed.P_RST)
        s.settimeout(5)
        try:
            self.assertEqual(s.recv(10), b"")
        except ConnectionError:
            pass
        s.close()
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_client_aborts_mid_transfer(self):
        for _ in range(20):
            s = self.connect(self.t4, self.bed.P_SOURCE)
            s.recv(1000)
            s.close()
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_clients_abort_during_handshake(self):
        for payload in (b"", b"\x05", b"\x05\x01", b"\x05\x01\x00\x05\x01",
                        b"\x05\x01\x00\x05\x01\x00\x01\x0a"):
            s = client.tcp_connect(*self.proxy)
            if payload:
                s.sendall(payload)
            s.close()
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_pipelined_handshake(self):
        s = self.connect(self.t4, self.bed.P_ECHO, pipeline=True)
        self.assertTrue(client.tcp_echo(s, 10000))
        s.close()

    def test_slow_handshake(self):
        # one byte at a time
        s = client.tcp_connect(*self.proxy)
        msg = (client.greeting() + bytes([5, 1, 0]) +
               client.pack_addr(self.t4, self.bed.P_ECHO))
        for b in msg[:6]:
            s.sendall(bytes([b]))
            time.sleep(0.02)
        s.sendall(msg[6:])
        r = client.recv_exact(s, 2)
        self.assertEqual(r, b"\x05\x00")
        rep, _, _ = client.read_reply(s)
        self.assertEqual(rep, 0)
        self.assertTrue(client.tcp_echo(s, 100))
        s.close()


class ProtocolTest(ServerTestCase):

    def setUp(self):
        super().setUp()
        self.server()

    def test_bind_not_supported(self):
        with self.assertRaises(client.ReplyError) as cm:
            client.open_session(self.proxy, client.CMD_BIND, "0.0.0.0", 0)
        self.assertEqual(cm.exception.rep, 7)    # command not supported

    def test_unknown_command(self):
        with self.assertRaises(client.ReplyError) as cm:
            client.open_session(self.proxy, 0x42, self.t4, 80)
        self.assertNotEqual(cm.exception.rep, 0)

    def test_wrong_version(self):
        s = client.tcp_connect(*self.proxy)
        s.sendall(b"\x04\x01\x00\x50\x01\x02\x03\x04\x00")   # SOCKS4
        s.settimeout(5)
        try:
            # closed, or told that no method is acceptable
            self.assertIn(s.recv(100), (b"", b"\x05\xff"))
        except ConnectionError:
            pass
        s.close()

    def test_garbage(self):
        s = client.tcp_connect(*self.proxy)
        s.sendall(b"GET / HTTP/1.0\r\n\r\n")
        s.settimeout(5)
        try:
            d = s.recv(100)
        except ConnectionError:
            d = b""
        self.assertNotIn(b"HTTP", d)
        s.close()
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_no_auth_server_accepts_offer_of_both(self):
        s = self.connect(self.t4, self.bed.P_ECHO, offer=[2, 0])
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_no_acceptable_method(self):
        with self.assertRaises(client.AuthError):
            self.connect(self.t4, self.bed.P_ECHO, offer=[1, 3])

    def test_ipv4_literal_as_domain(self):
        # an address type 3 (name) carrying a dotted quad
        s = client.tcp_connect(*self.proxy)
        client.negotiate(s)
        name = self.t4.encode()
        s.sendall(bytes([5, 1, 0, 3, len(name)]) + name +
                  self.bed.P_ECHO.to_bytes(2, "big"))
        rep, _, _ = client.read_reply(s)
        self.assertEqual(rep, 0)
        self.assertTrue(client.tcp_echo(s, 100))
        s.close()

"""UDP ASSOCIATE (UDP in UDP) and FWD UDP (UDP in TCP)."""
import os
import socket
import time

from hevtest import client
from hevtest.case import ServerTestCase

MODES = ("udp", "tcp")


class UdpTest(ServerTestCase):

    def setUp(self):
        super().setUp()
        self.server()

    def session(self, mode, proxy=None, **kw):
        s = client.UdpSession(proxy or self.proxy, mode=mode, **kw)
        self.addCleanup(s.close)
        return s

    def test_echo(self):
        for mode in MODES:
            for host in (self.t4, self.t6):
                with self.subTest(mode=mode, host=host):
                    u = self.session(mode)
                    d = os.urandom(300)
                    got, src = client.udp_roundtrip(u, host, self.bed.P_ECHO,
                                                    d)
                    self.assertEqual(got, d)
                    self.assertEqual(src, (host, self.bed.P_ECHO))

    def test_client_over_ipv6(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                u = self.session(mode, self.proxy6)
                for host in (self.t4, self.t6):
                    d = os.urandom(100)
                    got, src = client.udp_roundtrip(u, host, self.bed.P_ECHO,
                                                    d)
                    self.assertEqual(got, d)
                    self.assertEqual(src[0], host)

    def test_payload_sizes(self):
        # the server relays datagrams in 1500 byte buffers
        sizes = (0, 1, 512, 1400, 1472)
        for mode in MODES:
            for size in sizes:
                with self.subTest(mode=mode, size=size):
                    u = self.session(mode)
                    d = os.urandom(size)
                    got, _ = client.udp_roundtrip(u, self.t4, self.bed.P_ECHO,
                                                  d, 4.0)
                    self.assertEqual(got, d)

    def test_reply_from_another_port(self):
        # full cone: the target answers from a port the client never wrote to
        for mode in MODES:
            with self.subTest(mode=mode):
                u = self.session(mode)
                d = b"reflect-me"
                got, src = client.udp_roundtrip(u, self.t4, self.bed.P_REFLECT,
                                                d)
                self.assertEqual(got, d)
                self.assertEqual(src, (self.t4, self.bed.P_REFLECT_FROM))

    def test_many_targets_one_association(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                u = self.session(mode)
                for host, port in ((self.t4, 7001), (self.t6, 7001),
                                   (self.t4, 7006), (self.t6, 7006)):
                    d = os.urandom(64)
                    got, _ = client.udp_roundtrip(u, host, port, d)
                    self.assertEqual(got, d, (host, port))

    def test_target_name(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                u = self.session(mode)
                got, src = client.udp_roundtrip(u, self.bed.name_v6,
                                                self.bed.P_ECHO, b"by-name")
                self.assertEqual(got, b"by-name")
                self.assertEqual(src[0], self.t6)

    def test_oversize_datagram_is_survivable(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                u = self.session(mode)
                u.sendto(os.urandom(4000), self.t4, self.bed.P_ECHO)
                got, _ = client.udp_roundtrip(u, self.t4, self.bed.P_ECHO,
                                              b"after")
                if mode == "udp":
                    self.assertEqual(got, b"after")
                else:
                    # the frame is cut: the stream may carry on or be closed
                    self.assertIn(got, (b"after", None))

    def test_datagram_boundaries(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                u = self.session(mode)
                msgs = [bytes([i]) * (i + 1) for i in range(50)]
                for m in msgs:
                    u.sendto(m, self.t4, self.bed.P_ECHO)
                got = set()
                try:
                    while len(got) < len(msgs):
                        got.add(u.recvfrom(2.0)[0])
                except socket.timeout:
                    pass
                self.assertGreaterEqual(len(got), 45)
                self.assertTrue(got <= set(msgs))

    def test_concurrent_associations(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                sess = [self.session(mode) for _ in range(16)]
                for i, u in enumerate(sess):
                    u.sendto(b"s%d" % i, self.t4, self.bed.P_ECHO)
                for i, u in enumerate(sess):
                    self.assertEqual(u.recvfrom(3.0)[0], b"s%d" % i)

    def test_udp_reaches_target_exactly(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                client.tcp_ctrl(self.proxy, self.t4, self.bed.P_CTRL,
                                "udp-sink")                  # reset
                u = self.session(mode)
                for _ in range(20):
                    u.sendto(b"z" * 100, self.t4, self.bed.P_SINK)
                    time.sleep(0.005)
                time.sleep(0.3)
                n, pk = map(int, client.tcp_ctrl(
                    self.proxy, self.t4, self.bed.P_CTRL,
                    "udp-sink").split())
                self.assertEqual((n, pk), (2000, 20))

    def test_association_ends_with_control_connection(self):
        u = self.session("udp")
        d = b"alive"
        self.assertEqual(client.udp_roundtrip(u, self.t4, self.bed.P_ECHO,
                                              d)[0], d)
        relay = u.relay
        u.ctrl.close()
        time.sleep(0.5)
        # the relay socket is gone: nothing answers any more
        u.sock.settimeout(1.0)
        u.sock.sendto(b"\0\0\0" + client.pack_addr(self.t4, self.bed.P_ECHO) +
                      d, relay)
        with self.assertRaises((socket.timeout, ConnectionError)):
            u.sock.recv(100)

    def test_relay_ignores_other_senders(self):
        # only the client that created the association may use the relay
        u = self.session("udp")
        self.assertEqual(client.udp_roundtrip(u, self.t4, self.bed.P_ECHO,
                                              b"x")[0], b"x")
        other = socket.socket(client.family(self.proxy[0]), socket.SOCK_DGRAM)
        self.addCleanup(other.close)
        other.settimeout(1.0)
        other.sendto(b"\0\0\0" + client.pack_addr(self.t4, self.bed.P_ECHO) +
                     b"intruder", u.relay)
        with self.assertRaises(socket.timeout):
            other.recv(100)

    def check_foreign_first(self, u, source):
        # RFC 1928: datagrams from any other source IP are dropped, so one
        # sent before the client's own cannot take the association over
        other = socket.socket(client.family(source), socket.SOCK_DGRAM)
        self.addCleanup(other.close)
        other.bind((source, 0))
        other.settimeout(1.0)
        other.sendto(b"\0\0\0" + client.pack_addr(self.t4, self.bed.P_ECHO) +
                     b"intruder", u.relay)
        with self.assertRaises(socket.timeout):
            other.recv(100)
        got, _ = client.udp_roundtrip(u, self.t4, self.bed.P_ECHO, b"owner")
        self.assertEqual(got, b"owner")

    def test_relay_drops_other_ip_before_client(self):
        # DST 0.0.0.0:0: the client IP is the one of the control connection
        u = self.session("udp", want_port=False, source=self.bed.cli_addr)
        self.check_foreign_first(u, self.bed.cli_addr2)

    def test_relay_drops_other_ip_before_client_port(self):
        u = self.session("udp", source=self.bed.cli_addr)
        self.check_foreign_first(u, self.bed.cli_addr2)

    def test_relay_drops_other_ip_before_client_dst(self):
        # DST names the client IP, the control connection IP is foreign
        u = self.session("udp", want_port=False, source=self.bed.cli_addr,
                         host=self.bed.cli_addr2)
        self.check_foreign_first(u, self.bed.cli_addr)

    def test_garbage_datagram(self):
        u = self.session("udp")
        for junk in (b"", b"\0", b"\0\0\0", b"\0\0\0\x01", b"\0\0\x05" + b"x" * 8,
                     os.urandom(100)):
            u.sock.sendto(junk, u.relay)
        got, _ = client.udp_roundtrip(u, self.t4, self.bed.P_ECHO, b"still ok")
        self.assertEqual(got, b"still ok")

    def test_tcp_still_works(self):
        u = self.session("udp")
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 1000))
        s.close()
        self.assertEqual(client.udp_roundtrip(u, self.t4, self.bed.P_ECHO,
                                              b"x")[0], b"x")


class UdpConfigTest(ServerTestCase):
    """UDP related keys of the main section."""

    def roundtrip(self, mode="udp", proxy=None, traffic=True):
        with client.UdpSession(proxy or self.proxy, mode=mode) as u:
            if traffic:
                d = os.urandom(64)
                got, _ = client.udp_roundtrip(u, self.t4, self.bed.P_ECHO, d)
                self.assertEqual(got, d)
            return u.bound

    def test_udp_port_fixed(self):
        self.server(main={"udp-port": 31000})
        # shared across client IPs, and by client ports sent in DST
        self.assertEqual(self.roundtrip()[1], 31000)

    def associate(self, proxy=None, **kw):
        """UDP ASSOCIATE with DST 0.0.0.0:0, as Chrome and tunnels send it."""
        kw.setdefault("want_port", False)
        u = client.UdpSession(proxy or self.proxy, **kw)
        self.addCleanup(u.close)
        return u

    def associate_when_free(self):
        # the server drops the old association asynchronously
        got = []

        def attempt():
            try:
                got.append(self.associate())
            except client.ReplyError:
                pass
            return got

        self.assertTrue(client.wait_for(attempt), "udp port not released")
        return got[0]

    def check_own_replies(self, sess):
        msgs = [b"assoc%d" % i for i in range(len(sess))]
        for u, m in zip(sess, msgs):
            u.sendto(m, self.t4, self.bed.P_ECHO)
        for u, m in zip(sess, msgs):
            self.assertEqual(u.recvfrom(3.0)[0], m)

    def test_udp_port_fixed_same_client(self):
        self.server(main={"udp-port": 31000, "workers": 4})
        first = self.associate()
        self.assertEqual(first.bound[1], 31000)
        for _ in range(5):
            with self.assertRaises(client.ReplyError):
                self.associate()
        self.check_own_replies([first])
        first.close()
        self.check_own_replies([self.associate_when_free()])

    def test_udp_port_fixed_other_client(self):
        self.server(main={"udp-port": 31000})
        a = self.associate()
        b = self.associate(self.proxy6)
        self.assertEqual((a.bound[1], b.bound[1]), (31000, 31000))
        self.check_own_replies([a, b])

    def test_udp_port_fixed_client_ports(self):
        # a DST with the real client port is told apart by that port
        self.server(main={"udp-port": 31000})
        sess = [self.associate()]
        sess += [self.associate(want_port=True) for _ in range(3)]
        self.assertEqual([u.bound[1] for u in sess], [31000] * 4)
        self.check_own_replies(sess)

    def test_udp_port_fixed_dst_address(self):
        # the limit follows the DST address, not the control connection
        self.server(main={"udp-port": 31000})
        cli, cli2 = self.bed.cli_addr, self.bed.cli_addr2
        a = self.associate(source=cli2, host=cli)
        with self.assertRaises(client.ReplyError):
            self.associate(source=cli)
        b = self.associate(source=cli2)
        self.assertEqual((a.bound[1], b.bound[1]), (31000, 31000))
        self.check_own_replies([a, b])

    def test_udp_port_range_no_reuse(self):
        # the round-robin pick must skip a port the client still holds
        self.server(main={"udp-port": "31300-31301"})
        a = self.associate()
        b = self.associate()
        b.close()
        c = self.associate_when_free()
        # on a shared port the first datagrams land in the wrong relay: the
        # replies get lost, or the relays swap clients and ending one
        # association cuts the other client off
        self.check_own_replies([c, a])
        c.close()
        self.check_own_replies([a])
        self.assertNotEqual(a.bound[1], c.bound[1])

    def test_udp_port_range_same_client(self):
        self.server(main={"udp-port": "31200-31205", "workers": 4})
        sess = [self.associate() for _ in range(6)]
        self.assertEqual(sorted(u.bound[1] for u in sess),
                         list(range(31200, 31206)))
        self.check_own_replies(sess)
        with self.assertRaises(client.ReplyError):
            self.associate()
        sess[2].close()
        sess[2] = self.associate_when_free()
        self.check_own_replies(sess)

    def test_udp_port_range(self):
        self.server(main={"udp-port": "31100-31199"})
        for _ in range(5):
            port = self.roundtrip()[1]
            self.assertTrue(31100 <= port <= 31199, port)

    def test_udp_listen_address(self):
        self.server(main={"udp-listen-address": self.bed.srv_addr6})
        host = self.roundtrip(proxy=self.proxy6)[0]
        self.assertEqual(host, self.bed.srv_addr6)

    def test_udp_public_address(self):
        self.server(main={"udp-public-address-v4": "192.0.2.77",
                          "udp-public-address-v6": "2001:db8:ffff::77"})
        # the advertised address is not reachable here: check the reply only
        self.assertEqual(self.roundtrip(traffic=False)[0], "192.0.2.77")
        self.assertEqual(self.roundtrip(proxy=self.proxy6, traffic=False)[0],
                         "2001:db8:ffff::77")

    def test_udp_buffer_options(self):
        self.server(misc={"udp-recv-buffer-size": 65536,
                          "udp-copy-buffer-nums": 4})
        for mode in MODES:
            self.roundtrip(mode)

    def test_udp_idle_timeout(self):
        self.server(misc={"udp-read-write-timeout": 1000})
        for mode in MODES:
            with self.subTest(mode=mode):
                with client.UdpSession(self.proxy, mode=mode) as u:
                    self.assertEqual(client.udp_roundtrip(
                        u, self.t4, self.bed.P_ECHO, b"x")[0], b"x")
                    t0 = time.time()
                    self.assertTrue(u.closed_by_server(6.0))
                    self.assertLess(time.time() - t0, 5.0)

    def test_udp_traffic_keeps_association_alive(self):
        self.server(misc={"udp-read-write-timeout": 1500})
        with client.UdpSession(self.proxy, mode="udp") as u:
            for _ in range(6):
                self.assertEqual(client.udp_roundtrip(
                    u, self.t4, self.bed.P_ECHO, b"k")[0], b"k")
                time.sleep(0.5)

    def test_listen_ipv6_only(self):
        # the relay follows the family of the control connection
        self.server(main={"listen-ipv6-only": True})
        self.roundtrip(proxy=self.proxy6)

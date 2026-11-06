"""Configuration keys and their effect as seen by a client."""
import os
import socket
import time

from hevtest import client
from hevtest.case import ServerTestCase


class ListenTest(ServerTestCase):

    def echo_ok(self, proxy, host=None):
        s = client.connect(proxy, host or self.t4, self.bed.P_ECHO)
        ok = client.tcp_echo(s, 1000)
        s.close()
        return ok

    def test_dual_stack(self):
        self.server(main={"listen-address": "::"})
        self.assertTrue(self.echo_ok(self.proxy))
        self.assertTrue(self.echo_ok(self.proxy6))

    def test_ipv4_only(self):
        self.server(main={"listen-address": "0.0.0.0"})
        self.assertTrue(self.echo_ok(self.proxy))
        with self.assertRaises(OSError):
            client.tcp_connect(*self.proxy6, timeout=2)

    def test_ipv6_only_option(self):
        self.server(main={"listen-address": "::", "listen-ipv6-only": True})
        self.assertTrue(self.echo_ok(self.proxy6))
        with self.assertRaises(OSError):
            client.tcp_connect(*self.proxy, timeout=2)

    def test_specific_address(self):
        self.server(main={"listen-address": self.bed.srv_addr})
        self.assertTrue(self.echo_ok(self.proxy))
        with self.assertRaises(OSError):
            client.tcp_connect(*self.proxy6, timeout=2)

    def test_port(self):
        self.server(main={"port": 31080})
        self.assertTrue(self.echo_ok((self.bed.srv_addr, 31080)))
        with self.assertRaises(OSError):
            client.tcp_connect(self.bed.srv_addr, self.bed.port, timeout=2)

    def test_workers(self):
        for workers in (1, 2, 4, 8):
            with self.subTest(workers=workers):
                srv = self.server(main={"workers": workers})
                from hevtest.benchlib import run_threads
                def one(_):
                    return self.echo_ok(self.proxy)
                self.assertTrue(all(run_threads(32, one)))
                threads = len(os.listdir("/proc/%d/task" % srv.pid))
                self.assertGreaterEqual(threads, workers)
                srv.stop()

    def test_tcp_fastopen(self):
        self.server(main={"tcp-fastopen": True})
        self.assertTrue(self.echo_ok(self.proxy))
        self.assertTrue(self.echo_ok(self.proxy6, self.t6))


class OutgoingTest(ServerTestCase):
    """How the server reaches the targets."""

    def test_default_source(self):
        self.server()
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.out_addr)
        self.assertEqual(client.whoami(self.proxy, self.t6),
                         self.bed.out_addr6)

    def test_bind_address_v4(self):
        self.server(main={"bind-address-v4": self.bed.alt_addr})
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.alt_addr)
        # the other family is not bound
        self.assertEqual(client.whoami(self.proxy, self.t6),
                         self.bed.out_addr6)

    def test_bind_address_v6(self):
        self.server(main={"bind-address-v6": self.bed.alt_addr6})
        self.assertEqual(client.whoami(self.proxy, self.t6),
                         self.bed.alt_addr6)
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.out_addr)

    def test_bind_address_applies_to_both(self):
        # a v4 and a v6 literal cannot be in one key: bind-address is used
        # for the family it belongs to
        self.server(main={"bind-address": self.bed.alt_addr})
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.alt_addr)

    def test_bind_address_family_keys_override(self):
        self.server(main={"bind-address": self.bed.out_addr,
                          "bind-address-v4": self.bed.alt_addr})
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.alt_addr)

    def test_bind_address_udp(self):
        self.server(main={"bind-address-v4": self.bed.alt_addr})
        for mode in ("udp", "tcp"):
            with client.UdpSession(self.proxy, mode=mode) as u:
                got, _ = client.udp_roundtrip(u, self.t4, self.bed.P_WHOAMI,
                                              b"?")
                self.assertEqual(got.decode(), self.bed.alt_addr, mode)

    def test_bind_interface(self):
        self.server(main={"bind-interface": self.bed.iface})
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.out_addr)

    def test_bind_interface_wrong(self):
        self.server(main={"bind-interface": "nonexistent0"})
        with self.assertRaises(client.ReplyError):
            client.connect(self.proxy, self.t4, self.bed.P_ECHO)

    def test_bind_address_unusable(self):
        self.server(main={"bind-address-v4": "192.0.2.123"})
        with self.assertRaises(client.ReplyError):
            client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        # IPv6 targets are not affected
        self.assertEqual(client.whoami(self.proxy, self.t6),
                         self.bed.out_addr6)

    def test_domain_address_type(self):
        fam = lambda ip: 6 if ":" in ip else 4
        for typ, want in (("ipv4", 4), ("ipv6", 6)):
            with self.subTest(type=typ):
                srv = self.server(main={"domain-address-type": typ})
                ip = client.whoami(self.proxy, self.bed.name_both)
                self.assertEqual(fam(ip), want, ip)
                srv.stop()

    def test_domain_address_type_unspec(self):
        self.server(main={"domain-address-type": "unspec"})
        self.assertTrue(client.whoami(self.proxy, self.bed.name_both))
        self.assertTrue(client.whoami(self.proxy, self.bed.name_v4))
        self.assertTrue(client.whoami(self.proxy, self.bed.name_v6))

    def test_domain_address_type_has_no_effect_on_literals(self):
        self.server(main={"domain-address-type": "ipv6"})
        self.assertEqual(client.whoami(self.proxy, self.t4), self.bed.out_addr)

    def test_domain_address_type_without_such_record(self):
        self.server(main={"domain-address-type": "ipv6"})
        with self.assertRaises(client.ReplyError):
            client.connect(self.proxy, self.bed.name_v4, self.bed.P_ECHO)

    def test_mark(self):
        self.require(self.bed.have_nft, "nft is required to see the marks")
        self.bed.watch_mark(0x77)
        self.server(main={"mark": "0x77"})
        self.assertEqual(self.bed.mark_packets(0x77), 0)
        client.whoami(self.proxy, self.t4)
        self.assertGreater(self.bed.mark_packets(0x77), 0)

    def test_mark_notations(self):
        self.require(self.bed.have_nft, "nft is required to see the marks")
        for text, val in (("0x40", 0x40), ("65", 65), ("0100", 0o100)):
            with self.subTest(mark=text):
                self.bed.watch_mark(val)
                srv = self.server(main={"mark": text})
                client.whoami(self.proxy, self.t4)
                self.assertGreater(self.bed.mark_packets(val), 0)
                srv.stop()


class TimeoutTest(ServerTestCase):

    def test_connect_timeout(self):
        self.server(misc={"connect-timeout": 1000})
        t0 = time.time()
        with self.assertRaises(client.ReplyError):
            client.connect(self.proxy, self.bed.blackhole, self.bed.P_DROP,
                           timeout=15)
        took = time.time() - t0
        self.assertGreaterEqual(took, 0.8)
        self.assertLess(took, 4.0)

    def test_tcp_idle_timeout(self):
        self.server(misc={"tcp-read-write-timeout": 1000})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.settimeout(6)
        t0 = time.time()
        self.assertEqual(s.recv(10), b"")    # closed by the server
        self.assertLess(time.time() - t0, 5)
        s.close()

    def test_tcp_activity_defers_timeout(self):
        self.server(misc={"tcp-read-write-timeout": 1500})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        for _ in range(6):
            self.assertTrue(client.tcp_echo(s, 10))
            time.sleep(0.6)
        s.close()

    def test_read_write_timeout_sets_both(self):
        self.server(misc={"read-write-timeout": 1000})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        s.settimeout(6)
        self.assertEqual(s.recv(10), b"")
        s.close()
        with client.UdpSession(self.proxy, mode="udp") as u:
            self.assertTrue(u.closed_by_server(6.0))

    def test_specific_timeout_beats_read_write_timeout(self):
        self.server(misc={"read-write-timeout": 1000,
                          "tcp-read-write-timeout": 60000})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        s.settimeout(2.5)
        with self.assertRaises(socket.timeout):
            s.recv(10)
        s.close()

    def test_handshake_timeout_does_not_block_others(self):
        # a client that connects and says nothing must not stall the server
        self.server(misc={"tcp-read-write-timeout": 2000})
        idle = client.tcp_connect(*self.proxy)
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 100))
        s.close()
        idle.settimeout(6)
        try:
            self.assertIn(idle.recv(10), (b"", b"\x05\xff"))
        except ConnectionError:
            pass
        idle.close()


class MiscTest(ServerTestCase):

    def test_task_stack_size(self):
        self.server(misc={"task-stack-size": 32768})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 100000))
        s.close()

    def test_limit_nofile(self):
        srv = self.server(misc={"limit-nofile": 4321})
        self.assertEqual(self.bed.nofile_limit(srv.pid), 4321)

    def test_limit_nofile_serves_connections(self):
        srv = self.server(misc={"limit-nofile": 20000})
        socks = [client.connect(self.proxy, self.t4, self.bed.P_ECHO)
                 for _ in range(300)]
        for s in socks:
            self.assertTrue(client.tcp_echo(s, 10))
            s.close()


class LogTest(ServerTestCase):

    def test_log_to_file(self):
        path = self.bed.tmpfile("log.txt")
        self.server(misc={"log-file": path, "log-level": "debug"})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        client.tcp_echo(s, 10)
        s.close()
        time.sleep(0.2)
        with open(path) as f:
            text = f.read()
        self.assertGreater(len(text), 100)

    def test_log_level(self):
        sizes = {}
        for level in ("error", "debug"):
            path = self.bed.tmpfile("log-%s.txt" % level)
            srv = self.server(misc={"log-file": path, "log-level": level})
            for _ in range(5):
                s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
                client.tcp_echo(s, 10)
                s.close()
            time.sleep(0.2)
            srv.stop()
            sizes[level] = os.path.getsize(path)
        self.assertGreater(sizes["debug"], sizes["error"])

    def test_log_to_stderr(self):
        srv = self.server(misc={"log-file": "stderr", "log-level": "debug"})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        client.tcp_echo(s, 10)
        s.close()
        time.sleep(0.2)
        self.assertGreater(len(srv.log()), 50)

    def test_log_null(self):
        srv = self.server(misc={"log-file": "null", "log-level": "debug"})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        client.tcp_echo(s, 10)
        s.close()
        time.sleep(0.2)
        self.assertEqual(srv.log().strip(), "")

    def test_refusals_are_logged_at_info(self):
        srv = self.server(misc={"log-file": "stderr", "log-level": "info"},
                          auth={"username": "a", "password": "b"})
        self.assertRejected(client.connect, self.proxy, self.t4,
                            self.bed.P_ECHO, user="a", pw="wrong")
        time.sleep(0.2)
        self.assertIn("wrong", srv.log())

"""Username/password authentication, auth file, live reload, user marks."""
import signal
import time

from hevtest import client
from hevtest.case import ServerTestCase


class AuthTest(ServerTestCase):

    def echo_ok(self, user=None, pw=None, **kw):
        s = self.connect(self.t4, self.bed.P_ECHO, user=user, pw=pw, **kw)
        ok = client.tcp_echo(s, 1000)
        s.close()
        return ok

    def test_correct_credentials(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        self.assertTrue(self.echo_ok("alice", "s3cret"))
        self.assertTrue(self.echo_ok("alice", "s3cret", pipeline=True))
        s = self.connect(self.t6, self.bed.P_ECHO, user="alice", pw="s3cret",
                         proxy=self.proxy6)
        self.assertTrue(client.tcp_echo(s, 1000))
        s.close()

    def test_wrong_password(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="alice", pw="wrong")

    def test_wrong_user(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="mallory", pw="s3cret")

    def test_password_is_not_a_prefix_match(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        for pw in ("s3cre", "s3cret!", "S3CRET", ""):
            with self.subTest(pw=pw):
                self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                                    user="alice", pw=pw)

    def test_no_credentials(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO)

    def test_anonymous_method_not_accepted_when_auth_is_on(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        s = client.tcp_connect(*self.proxy)
        s.sendall(b"\x05\x02\x00\x02")      # offers none + user/pass
        r = client.recv_exact(s, 2)
        s.close()
        self.assertEqual(r, b"\x05\x02")    # the server insists on user/pass

    def test_long_credentials(self):
        user, pw = "u" * 255, "p" * 255
        self.server(auth={"username": user, "password": pw})
        self.assertTrue(self.echo_ok(user, pw))
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user=user, pw=pw[:-1])

    def test_udp_requires_login(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        for mode in ("udp", "tcp"):
            with self.subTest(mode=mode):
                with client.UdpSession(self.proxy, "alice", "s3cret",
                                       mode=mode) as u:
                    self.assertEqual(client.udp_roundtrip(
                        u, self.t4, self.bed.P_ECHO, b"hi")[0], b"hi")
                with self.assertRaises(OSError):
                    client.UdpSession(self.proxy, "alice", "bad", mode=mode)
                with self.assertRaises(OSError):
                    client.UdpSession(self.proxy, mode=mode)

    def test_auth_failure_does_not_stop_the_server(self):
        self.server(auth={"username": "alice", "password": "s3cret"})
        for _ in range(20):
            self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                                user="alice", pw="x")
        self.assertTrue(self.echo_ok("alice", "s3cret"))


class AuthFileTest(ServerTestCase):

    USERS = "tom pass\njerry pass 1a\n"

    def echo_ok(self, user, pw):
        s = self.connect(self.t4, self.bed.P_ECHO, user=user, pw=pw)
        ok = client.tcp_echo(s, 1000)
        s.close()
        return ok

    def server_with(self, text):
        path = self.write_file("auth.txt", text)
        srv = self.server(auth={"file": path})
        return srv, path

    def test_multiple_users(self):
        self.server_with(self.USERS + "spock live-long 0\n")
        self.assertTrue(self.echo_ok("tom", "pass"))
        self.assertTrue(self.echo_ok("jerry", "pass"))
        self.assertTrue(self.echo_ok("spock", "live-long"))
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="tom", pw="live-long")
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="nobody", pw="pass")

    def test_file_wins_over_inline_credentials(self):
        path = self.write_file("auth.txt", self.USERS)
        self.server(auth={"file": path, "username": "inline",
                          "password": "pw"})
        self.assertTrue(self.echo_ok("tom", "pass"))
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="inline", pw="pw")

    def test_reload_with_sigusr1(self):
        srv, path = self.server_with(self.USERS)
        self.assertTrue(self.echo_ok("tom", "pass"))
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="newbie", pw="np")
        keep = self.connect(self.t4, self.bed.P_ECHO, user="tom", pw="pass")

        with open(path, "w") as f:
            f.write("newbie np\njerry changed\n")
        srv.proc.send_signal(signal.SIGUSR1)

        def reloaded():
            try:
                return self.echo_ok("newbie", "np")
            except OSError:
                return False
        self.assertTrue(client.wait_for(reloaded, 5.0), srv.log())
        self.assertTrue(self.echo_ok("jerry", "changed"))
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="tom", pw="pass")
        self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                            user="jerry", pw="pass")
        # a session that was established before the reload is not cut
        self.assertTrue(client.tcp_echo(keep, 1000))
        keep.close()

    def test_reload_repeatedly(self):
        srv, path = self.server_with(self.USERS)
        for i in range(5):
            with open(path, "w") as f:
                f.write("tom pass%d\n" % i)
            srv.proc.send_signal(signal.SIGUSR1)
            self.assertTrue(client.wait_for(
                lambda: self._try("tom", "pass%d" % i), 5.0))
        self.assertTrue(srv.proc.alive())

    def _try(self, user, pw):
        try:
            return self.echo_ok(user, pw)
        except OSError:
            return False

    def test_reload_with_workers(self):
        path = self.write_file("auth.txt", self.USERS)
        srv = self.server(main={"workers": 4}, auth={"file": path})
        with open(path, "w") as f:
            f.write("fresh pw\n")
        srv.proc.send_signal(signal.SIGUSR1)
        self.assertTrue(client.wait_for(lambda: self._try("fresh", "pw"), 5.0))
        # every worker serves the new list
        time.sleep(0.3)
        for _ in range(40):
            self.assertTrue(self.echo_ok("fresh", "pw"))
            self.assertRejected(self.connect, self.t4, self.bed.P_ECHO,
                                user="tom", pw="pass")

    def test_user_mark(self):
        self.require(self.bed.have_nft, "nft is required to see the marks")
        self.bed.watch_mark(0x1a)
        self.server_with(self.USERS)
        before = self.bed.mark_packets(0x1a)
        self.assertTrue(self.echo_ok("tom", "pass"))
        self.assertEqual(self.bed.mark_packets(0x1a), before)
        self.assertTrue(self.echo_ok("jerry", "pass"))
        self.assertGreater(self.bed.mark_packets(0x1a), before)

    def test_user_mark_beats_global_mark(self):
        self.require(self.bed.have_nft, "nft is required to see the marks")
        self.bed.watch_mark(0x1a)
        self.bed.watch_mark(0x33)
        path = self.write_file("auth.txt", self.USERS)
        self.server(main={"mark": "0x33"}, auth={"file": path})
        self.assertTrue(self.echo_ok("jerry", "pass"))
        self.assertGreater(self.bed.mark_packets(0x1a), 0)
        self.assertEqual(self.bed.mark_packets(0x33), 0)
        self.assertTrue(self.echo_ok("tom", "pass"))
        self.assertGreater(self.bed.mark_packets(0x33), 0)

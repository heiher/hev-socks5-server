"""Command line, config file handling, daemon mode and signals."""
import os
import signal
import subprocess
import time

from hevtest import client
from hevtest.case import ServerTestCase

MINIMAL = "main:\n  workers: 1\n  port: 1080\n  listen-address: '::'\n"


class CommandLineTest(ServerTestCase):

    def run_server(self, *args, timeout=10):
        """Run the server in its environment until it exits on its own."""
        proc = self.bed.spawn_server_env(
            [self.bed.server_bin] + list(args), "cli")
        try:
            rc = proc.popen.wait(timeout)
        except subprocess.TimeoutExpired:
            rc = None                    # still running: it started up
        proc.stop(signal.SIGKILL)
        return subprocess.CompletedProcess(args, rc, proc.log())

    def test_no_arguments_prints_usage(self):
        r = self.run_server()
        self.assertNotIn(r.returncode, (0, None), r.stdout)
        self.assertIn("CONFIG_PATH", r.stdout)

    def test_version(self):
        r = self.run_server("--version")
        self.assertRegex(r.stdout, r"Version: \d+\.\d+\.\d+")

    def test_missing_config_file(self):
        r = self.run_server(self.bed.tmpfile("does-not-exist.yml"))
        self.assertNotIn(r.returncode, (0, None), r.stdout)

    def test_bad_configs(self):
        cases = {
            "empty": "",
            "not yaml": "{{{{ nonsense",
            "no main": "misc:\n  log-level: warn\n",
            "no workers": "main:\n  port: 1080\n  listen-address: '::'\n",
            "no port": "main:\n  workers: 1\n  listen-address: '::'\n",
            "no address": "main:\n  workers: 1\n  port: 1080\n",
        }
        for name, text in cases.items():
            with self.subTest(name):
                path = self.write_file("bad.yml", text)
                r = self.run_server(path, timeout=5)
                self.assertNotIn(r.returncode, (0, None), r.stdout)

    def test_unknown_keys_are_ignored(self):
        self.server(main={"future-option": "x"}, extra={"anything": "y"})
        s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_config_from_the_shipped_example(self):
        # conf/main.yml is what users start from
        here = os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))))
        with open(os.path.join(here, "conf", "main.yml")) as f:
            text = f.read()
        path = self.write_file("main.yml", text)
        proc = self.bed.start_server(path, port=1080)
        try:
            s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
            self.assertTrue(client.tcp_echo(s, 1000))
            s.close()
        finally:
            self.bed.stop_server(proc)

    def test_config_with_comments_and_quotes(self):
        text = ("# comment\nmain:\n  workers: 2   # trailing\n"
                '  port: "1080"\n  listen-address: "::"\n')
        path = self.write_file("c.yml", text)
        proc = self.bed.start_server(path, port=1080)
        try:
            s = client.connect(self.proxy, self.t4, self.bed.P_ECHO)
            self.assertTrue(client.tcp_echo(s, 10))
            s.close()
        finally:
            self.bed.stop_server(proc)


class SignalTest(ServerTestCase):

    def test_sigint_stops_cleanly(self):
        srv = self.server()
        srv.proc.send_signal(signal.SIGINT)
        self.assertEqual(srv.proc.popen.wait(5), 0)

    def test_sigint_stops_with_open_sessions(self):
        srv = self.server(main={"workers": 2})
        socks = [client.connect(self.proxy, self.t4, self.bed.P_ECHO)
                 for _ in range(10)]
        with client.UdpSession(self.proxy, mode="udp"):
            srv.proc.send_signal(signal.SIGINT)
            self.assertEqual(srv.proc.popen.wait(5), 0)
        for s in socks:
            s.settimeout(3)
            try:
                self.assertEqual(s.recv(10), b"")
            except ConnectionError:
                pass
            s.close()

    def test_sigterm_stops(self):
        srv = self.server()
        srv.proc.send_signal(signal.SIGTERM)
        srv.proc.popen.wait(5)
        with self.assertRaises(OSError):
            client.tcp_connect(*self.proxy, timeout=2)

    def test_sigpipe_is_harmless(self):
        srv = self.server()
        for _ in range(30):
            s = self.connect(self.t4, self.bed.P_SOURCE)
            s.recv(10)
            s.close()
        self.assertTrue(srv.proc.alive())
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()

    def test_sigusr1_without_auth_file(self):
        srv = self.server()
        srv.proc.send_signal(signal.SIGUSR1)
        time.sleep(0.3)
        self.assertTrue(srv.proc.alive())
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 10))
        s.close()


class DaemonTest(ServerTestCase):

    def test_pid_file_daemon(self):
        pid_file = self.bed.tmpfile("server.pid")
        srv = self.server(misc={"pid-file": pid_file})
        with open(pid_file) as f:
            pid = int(f.read().strip())
        self.assertEqual(srv.pid, pid)
        # the launcher returned: the server runs detached
        self.assertEqual(srv.proc.popen.wait(5), 0)
        self.assertTrue(os.path.exists("/proc/%d" % pid))
        s = self.connect(self.t4, self.bed.P_ECHO)
        self.assertTrue(client.tcp_echo(s, 1000))
        s.close()
        os.kill(pid, signal.SIGINT)
        self.assertTrue(client.wait_for(
            lambda: not os.path.exists("/proc/%d" % pid), 5.0))
        with self.assertRaises(OSError):
            client.tcp_connect(*self.proxy, timeout=2)

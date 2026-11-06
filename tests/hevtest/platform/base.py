"""Platform independent testbed definition.

Topology (every back-end must provide the same logical layout)::

  +-- client env --+     +----------- server env -----------+     +------- remote env -------+
  | test process   |link | hev-socks5-server                |link | targets (echo, sink ...) |
  | (the "user")   +---->+ listens on srv_addr/srv_addr6    +---->+ target4/6: reachable     |
  +----------------+     | upstream sockets leave via link  |     | ONLY through the server  |
                         +----------------------------------+     +--------------------------+

The client is an ordinary SOCKS5 user.  The target addresses live only in the
remote environment and are routed only from the server environment, so a
connection can succeed only if it was really relayed by the server.
"""
import os
import signal
import subprocess
import tempfile
import time


class Proc:
    """A background process with captured output."""

    def __init__(self, argv, log_path, popen):
        self.argv = argv
        self.log_path = log_path
        self.popen = popen
        self.pid_override = None      # daemons: pid read from the pid file

    @property
    def pid(self):
        return self.pid_override or self.popen.pid

    def alive(self):
        if self.pid_override:
            return os.path.exists("/proc/%d" % self.pid_override)
        return self.popen.poll() is None

    def log(self):
        try:
            with open(self.log_path, "r", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def send_signal(self, sig):
        os.kill(self.pid, sig)

    def stop(self, sig=signal.SIGINT, timeout=5.0):
        if self.pid_override:
            return self._stop_daemon(sig, timeout)
        if self.popen.poll() is None:
            try:
                self.popen.send_signal(sig)
            except ProcessLookupError:
                pass
            try:
                self.popen.wait(timeout)
            except subprocess.TimeoutExpired:
                self.popen.kill()
                self.popen.wait()
        return self.popen.returncode

    def _stop_daemon(self, sig, timeout):
        try:
            os.kill(self.pid_override, sig)
        except ProcessLookupError:
            return 0
        end = time.time() + timeout
        while time.time() < end and self.alive():
            time.sleep(0.02)
        if self.alive():
            os.kill(self.pid_override, signal.SIGKILL)
        return 0

    def wait(self, timeout=None):
        return self.popen.wait(timeout)


class Bed:
    """Abstract testbed."""

    name = "base"

    # --- addressing (back-ends may override) ---------------------------
    cli_addr = "10.202.1.2"           # the "user" host
    cli_addr6 = "fd00:202:1::2"
    srv_addr = "10.202.1.1"           # where the server listens (client side)
    srv_addr6 = "fd00:202:1::1"
    out_addr = "10.202.0.1"           # server's default source towards remote
    out_addr6 = "fd00:202::1"
    alt_addr = "10.202.0.3"           # second server address (bind-address)
    alt_addr6 = "fd00:202::3"
    target4 = "203.0.113.10"          # only reachable through the server
    target6 = "2001:db8::10"
    blackhole = "203.0.113.99"        # SYN is dropped (connect timeout)
    port = 1080                       # default server port
    iface = "hvs0"                    # server's interface towards remote

    # names known to the server's resolver
    name_both = "target.test"         # A + AAAA
    name_v4 = "target4.test"
    name_v6 = "target6.test"

    # target service ports (see targets.py)
    P_ECHO = 7001
    P_ECHO_POOL = tuple(range(7030, 7062))   # same service, more 4-tuples
    P_SINK = 7002
    P_SOURCE = 7003
    P_DIGEST = 7004
    P_COUNT = 7005
    P_REFLECT = 7006                  # UDP: answers from another port
    P_REFLECT_FROM = 7007
    P_WHOAMI = 7010                   # answers with the peer address it sees
    P_HELLO = 7021                    # TCP: sends "hello", closes
    P_RST = 7022                      # TCP: accepts, resets
    P_CLOSED = 7998                   # nothing listens: connection refused
    P_DROP = 7999                     # SYN dropped
    P_CTRL = 7100

    def __init__(self, server_bin, workdir=None, verbose=False):
        self.server_bin = os.path.abspath(server_bin)
        self.verbose = verbose
        self._tmp = tempfile.TemporaryDirectory(prefix="hevtest-", dir=workdir)
        self.workdir = self._tmp.name
        self._procs = []
        self._seq = 0

    @property
    def proxy(self):
        return (self.srv_addr, self.port)

    @property
    def proxy6(self):
        return (self.srv_addr6, self.port)

    # --- lifecycle -----------------------------------------------------
    def setup(self):
        raise NotImplementedError

    def teardown(self):
        for p in reversed(self._procs):
            p.stop()
        self._procs = []
        self._tmp.cleanup()

    def enter_client(self):
        """Move the calling thread into the client environment."""
        raise NotImplementedError

    # --- processes -----------------------------------------------------
    def tmpfile(self, name):
        self._seq += 1
        return os.path.join(self.workdir, "%d-%s" % (self._seq, name))

    def spawn_server_env(self, argv, name=None, **kw):
        """Run a process in the environment of the server."""
        raise NotImplementedError

    def spawn_remote_env(self, argv, name=None, **kw):
        """Run a process in the remote environment (targets)."""
        raise NotImplementedError

    def _track(self, proc):
        self._procs.append(proc)
        return proc

    # --- services ------------------------------------------------------
    def start_targets(self):
        import sys
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        argv = [sys.executable, "-I", os.path.join(here, "targets.py"),
                self.target4, self.target6]
        proc = self.spawn_remote_env(argv, "targets")
        self.wait_ready(proc)
        return proc

    def wait_ready(self, proc, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if "READY" in proc.log():
                return
            if not proc.alive():
                raise RuntimeError("%s exited early:\n%s" %
                                   (proc.argv[0], proc.log()))
            time.sleep(0.01)
        raise RuntimeError("%s did not become ready" % proc.argv[0])

    # --- server --------------------------------------------------------
    def start_server(self, config_path, extra_args=(), wait=True, port=None,
                     pid_file=None, env=None):
        """Start hev-socks5-server in the server environment.  With ``wait``
        block until it listens on ``port`` (None: do not check)."""
        proc = self.spawn_server_env(
            [self.server_bin, config_path] + list(extra_args), "server",
            env=env)
        if wait:
            self.wait_listen(proc, port, pid_file)
        return proc

    def wait_listen(self, proc, port, pid_file=None, timeout=10.0):
        raise NotImplementedError

    def stop_server(self, proc, sig=signal.SIGINT):
        return proc.stop(sig)

    def watch_mark(self, mark):
        """Start counting packets the server sends with socket ``mark``."""
        raise NotImplementedError

    def mark_packets(self, mark):
        raise NotImplementedError

    # --- introspection -------------------------------------------------
    def proc_stats(self, pid):
        """Return dict(rss_kb=, peak_rss_kb=, cpu_s=) for a process."""
        raise NotImplementedError

    def nofile_limit(self, pid):
        raise NotImplementedError

    def listening(self, pid, proto, port):
        """True if the process listens on ``port`` (proto tcp|udp)."""
        raise NotImplementedError

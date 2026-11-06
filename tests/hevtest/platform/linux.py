"""Linux back-end: three network namespaces (client - server - remote)."""
import os
import re
import shutil
import subprocess
import time

from . import base


def sh(*argv, check=True, stdin=None):
    r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, input=stdin)
    if check and r.returncode != 0:
        raise RuntimeError("%s failed (%d): %s" %
                           (" ".join(argv), r.returncode, r.stdout.strip()))
    return r


class Bed(base.Bed):
    name = "linux"

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        tag = "hevs%d" % os.getpid()
        self.ns_c = tag + "c"          # client (the user)
        self.ns_s = tag + "s"          # server
        self.ns_r = tag + "r"          # remote (targets)
        self._created = []
        self._etc = []
        self.have_nft = False

    # --- lifecycle -----------------------------------------------------
    def setup(self):
        if os.geteuid() != 0:
            raise SystemExit("hevtest: root privileges are required "
                             "(network namespaces)")
        for ns in (self.ns_c, self.ns_s, self.ns_r):
            sh("ip", "netns", "add", ns)
            self._created.append(ns)
            sh("ip", "-n", ns, "link", "set", "lo", "up")

        c = ["ip", "-n", self.ns_c]
        s = ["ip", "-n", self.ns_s]
        r = ["ip", "-n", self.ns_r]

        # client <-> server
        sh("ip", "link", "add", "hvc0", "netns", self.ns_c, "type", "veth",
           "peer", "name", "hvs1", "netns", self.ns_s)
        sh(*c, "addr", "add", self.cli_addr + "/24", "dev", "hvc0")
        sh(*c, "addr", "add", self.cli_addr6 + "/64", "dev", "hvc0", "nodad")
        sh(*s, "addr", "add", self.srv_addr + "/24", "dev", "hvs1")
        sh(*s, "addr", "add", self.srv_addr6 + "/64", "dev", "hvs1", "nodad")
        sh(*c, "link", "set", "hvc0", "up")
        sh(*s, "link", "set", "hvs1", "up")

        # server <-> remote
        sh("ip", "link", "add", self.iface, "netns", self.ns_s, "type",
           "veth", "peer", "name", "hvr0", "netns", self.ns_r)
        sh(*s, "addr", "add", self.out_addr + "/24", "dev", self.iface)
        sh(*s, "addr", "add", self.alt_addr + "/24", "dev", self.iface)
        sh(*s, "addr", "add", self.out_addr6 + "/64", "dev", self.iface,
           "nodad")
        # deprecated: never picked by source address selection, but it can
        # still be bound explicitly (bind-address tests)
        sh(*s, "addr", "add", self.alt_addr6 + "/64", "dev", self.iface,
           "nodad", "preferred_lft", "0")
        sh(*r, "addr", "add", "10.202.0.2/24", "dev", "hvr0")
        sh(*r, "addr", "add", "fd00:202::2/64", "dev", "hvr0", "nodad")
        sh(*s, "link", "set", self.iface, "up")
        sh(*r, "link", "set", "hvr0", "up")

        # remote "internet": exists only inside the remote environment
        for a in (self.target4, self.blackhole):
            sh(*r, "addr", "add", a + "/32", "dev", "lo")
        sh(*r, "addr", "add", self.target6 + "/128", "dev", "lo", "nodad")
        sh(*s, "-4", "route", "add", "203.0.113.0/24", "via", "10.202.0.2")
        sh(*s, "-6", "route", "add", "2001:db8::/64", "via", "fd00:202::2")

        # connection rate benchmarks burn through ephemeral ports
        for ns in (self.ns_c, self.ns_s):
            sh("ip", "netns", "exec", ns, "sysctl", "-q", "-w",
               "net.ipv4.tcp_tw_reuse=1",
               "net.ipv4.ip_local_port_range=10240 65535", check=False)

        # names resolved by the server
        etc = "/etc/netns/" + self.ns_s
        os.makedirs(etc, exist_ok=True)
        self._etc.append(etc)
        with open(etc + "/hosts", "w") as f:
            f.write("127.0.0.1 localhost\n::1 localhost\n"
                    "%s %s %s\n%s %s %s\n%s %s\n%s %s\n" %
                    (self.target4, self.name_both, self.name_v4,
                     self.target6, self.name_both, self.name_v6,
                     self.target4, self.name_v4, self.target6, self.name_v6))

        # packet accounting (marks) and a black hole port
        self.have_nft = shutil.which("nft") is not None
        if self.have_nft:
            sh("ip", "netns", "exec", self.ns_s, "nft", "-f", "-", stdin="""
table inet hevt {
  chain out { type filter hook output priority mangle; policy accept; }
}
""")
            sh("ip", "netns", "exec", self.ns_r, "nft", "-f", "-", stdin="""
table inet hevt {
  chain in { type filter hook input priority filter; policy accept;
    tcp dport %d drop
  }
}
""" % self.P_DROP)

    def teardown(self):
        super().teardown()
        for ns in self._created:
            sh("ip", "netns", "del", ns, check=False)
        self._created = []
        for d in self._etc:
            shutil.rmtree(d, ignore_errors=True)
        self._etc = []

    def enter_client(self):
        fd = os.open("/run/netns/" + self.ns_c, os.O_RDONLY)
        try:
            os.setns(fd, os.CLONE_NEWNET)
        finally:
            os.close(fd)

    # --- processes -----------------------------------------------------
    def _spawn(self, argv, name, env=None, **kw):
        log = self.tmpfile((name or os.path.basename(argv[0])) + ".log")
        f = open(log, "wb")
        popen = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=f,
                                 stderr=subprocess.STDOUT, env=env, **kw)
        f.close()
        return self._track(base.Proc(argv, log, popen))

    def spawn_server_env(self, argv, name=None, **kw):
        return self._spawn(["ip", "netns", "exec", self.ns_s] + list(argv),
                           name, **kw)

    def spawn_remote_env(self, argv, name=None, **kw):
        return self._spawn(["ip", "netns", "exec", self.ns_r] + list(argv),
                           name, **kw)

    # --- server --------------------------------------------------------
    def wait_listen(self, proc, port, pid_file=None, timeout=10.0):
        end = time.time() + timeout
        pid = proc.pid
        while time.time() < end:
            if pid_file:
                try:
                    with open(pid_file) as f:
                        pid = int(f.read().strip())
                    proc.pid_override = pid
                except (OSError, ValueError):
                    time.sleep(0.02)
                    continue
            elif not proc.alive():
                raise RuntimeError("server exited early (%s):\n%s" %
                                   (proc.popen.returncode, proc.log()))
            if port is None or self.listening(pid, "tcp", port):
                return
            time.sleep(0.02)
        raise RuntimeError("server is not listening:\n" + proc.log())

    def watch_mark(self, mark):
        if not self.have_nft:
            raise RuntimeError("nft is required")
        sh("ip", "netns", "exec", self.ns_s, "nft", "-f", "-", stdin="""
add counter inet hevt m%d
add rule inet hevt out meta mark %d counter name m%d
""" % (mark, mark, mark))

    def mark_packets(self, mark):
        r = sh("ip", "netns", "exec", self.ns_s, "nft", "list", "counter",
               "inet", "hevt", "m%d" % mark)
        m = re.search(r"packets (\d+)", r.stdout)
        return int(m.group(1)) if m else 0

    # --- introspection -------------------------------------------------
    def proc_stats(self, pid):
        out = {}
        try:
            with open("/proc/%d/status" % pid) as f:
                for line in f:
                    m = re.match(r"(VmRSS|VmHWM):\s+(\d+) kB", line)
                    if m:
                        out["rss_kb" if m.group(1) == "VmRSS" else
                            "peak_rss_kb"] = int(m.group(2))
            with open("/proc/%d/stat" % pid) as f:
                rest = f.read().rsplit(")", 1)[1].split()
            tck = os.sysconf("SC_CLK_TCK")
            out["cpu_s"] = (int(rest[11]) + int(rest[12])) / tck
        except (OSError, IndexError):
            pass
        return out

    def nofile_limit(self, pid):
        with open("/proc/%d/limits" % pid) as f:
            for line in f:
                if line.startswith("Max open files"):
                    return int(line.split()[3])
        return None

    def listening(self, pid, proto, port):
        # /proc/<pid>/net shows the network namespace of that process
        state = "0A" if proto == "tcp" else "07"
        hexport = ":%04X" % port
        for t in (proto, proto + "6"):
            try:
                with open("/proc/%d/net/%s" % (pid, t)) as f:
                    for line in f.readlines()[1:]:
                        cols = line.split()
                        if cols[3] == state and cols[1].endswith(hexport):
                            return True
            except OSError:
                pass
        return False

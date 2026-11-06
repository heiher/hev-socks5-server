"""Latency through the server: TCP connect, TCP echo, UDP echo."""
import time

from hevtest import benchlib as bl
from hevtest import client

N = 2000


def stats(rep, group, name, vals):
    vals = sorted(v * 1e6 for v in vals)
    rep.add(group, name + " p50", vals[len(vals) // 2], "us")
    rep.add(group, name + " p99", bl.percentile(vals, 0.99), "us")


def run(bed, rep, args):
    with bl.running(bed) as (proc, smp):
        rep.group("latency")

        # request/response over an established CONNECT session
        for fam, host in (("ipv4", bed.target4), ("ipv6", bed.target6)):
            s = client.connect(bed.proxy, host, bed.P_ECHO)
            for _ in range(100):
                s.sendall(b"x")
                s.recv(1)
            vals = []
            for _ in range(N):
                t0 = time.perf_counter()
                s.sendall(b"x")
                s.recv(1)
                vals.append(time.perf_counter() - t0)
            s.close()
            stats(rep, "latency", "tcp echo rtt " + fam, vals)

        # connection setup: handshake + CONNECT + first byte
        for mode in ("default", "pipeline"):
            vals = []
            for _ in range(300):
                t0 = time.perf_counter()
                s = client.connect(bed.proxy, bed.target4, bed.P_ECHO,
                                   pipeline=(mode == "pipeline"))
                s.sendall(b"x")
                s.recv(1)
                vals.append(time.perf_counter() - t0)
                s.close()
            stats(rep, "latency", "tcp connect to first byte (%s)" % mode,
                  vals)

        for mode, name in (("udp", "udp-in-udp"), ("tcp", "udp-in-tcp")):
            with client.UdpSession(bed.proxy, mode=mode) as u:
                vals = []
                for i in range(N + 100):
                    t0 = time.perf_counter()
                    u.sendto(b"ping", bed.target4, bed.P_ECHO)
                    u.recvfrom(2.0)
                    if i >= 100:
                        vals.append(time.perf_counter() - t0)
            stats(rep, "latency", "%s echo rtt" % name, vals)

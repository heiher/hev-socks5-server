"""UDP relay throughput, in both transports (UDP ASSOCIATE and FWD UDP)."""
import socket
import threading
import time

from hevtest import benchlib as bl
from hevtest import client

SIZE = 1200
NAMES = {"udp": "udp-in-udp", "tcp": "udp-in-tcp"}


def sink_reset(bed):
    client.tcp_ctrl(bed.proxy, bed.target4, bed.P_CTRL, "udp-sink")


def sink_read(bed):
    n, pk = client.tcp_ctrl(bed.proxy, bed.target4, bed.P_CTRL,
                            "udp-sink").split()
    return int(n), int(pk)


def upload(bed, rep, smp, mode, D):
    """Client floods the target through the relay; the target counts."""
    group = "udp upload (%s)" % NAMES[mode]
    rep.group(group)
    with client.UdpSession(bed.proxy, mode=mode) as u:
        pkt = b"\xa5" * SIZE
        sink_reset(bed)
        smp.start()
        t0 = time.time()
        end = t0 + D
        sent = 0
        while time.time() < end:
            for _ in range(64):
                try:
                    u.sendto(pkt, bed.target4, bed.P_SINK)
                except BlockingIOError:
                    break
                sent += 1
        el = time.time() - t0
        time.sleep(0.5)
        st = smp.stop()
        _, got = sink_read(bed)
    rep.add(group, "offered", sent / el / 1000, "kpps")
    rep.add(group, "delivered", got * SIZE * 8 / el / 1e6, "Mbit/s")
    rep.add(group, "delivered packets", got / el / 1000, "kpps")
    rep.add(group, "loss", 100.0 * (sent - got) / sent if sent else 0.0, "%")
    rep.add(group, "server cpu", st["cpu_pct"], "%")


def download(bed, rep, smp, mode, D):
    """The target floods the client through the relay."""
    group = "udp download (%s)" % NAMES[mode]
    rep.group(group)
    with client.UdpSession(bed.proxy, mode=mode) as u:
        u.sendto(b"warmup", bed.target4, bed.P_ECHO)
        u.recvfrom(3.0)
        smp.start()
        u.sendto(b"SRC %d %f" % (SIZE, D), bed.target4, bed.P_SOURCE)
        got = 0
        t0 = time.time()
        last = t0
        while time.time() < t0 + D + 1.0:
            try:
                d, _ = u.recvfrom(0.5)
            except socket.timeout:
                if got and time.time() - last > 0.5:
                    break
                continue
            got += 1
            last = time.time()
        el = max(last - t0, 1e-3)
        st = smp.stop()
    rep.add(group, "received", got * SIZE * 8 / el / 1e6, "Mbit/s")
    rep.add(group, "received packets", got / el / 1000, "kpps")
    rep.add(group, "server cpu", st["cpu_pct"], "%")


def run(bed, rep, args):
    D = args.duration
    with bl.running(bed) as (proc, smp):
        for mode in ("udp", "tcp"):
            upload(bed, rep, smp, mode, D)
            download(bed, rep, smp, mode, D)

"""Connection setup rate, authentication cost and per-session memory."""
import socket
import struct
import time

from hevtest import benchlib as bl
from hevtest import client


def conn_rate(bed, host, secs, workers, **kw):
    end = time.time() + secs

    pool = bed.P_ECHO_POOL

    def one(i):
        n = 0
        while time.time() < end:
            # every closed session leaves a TIME_WAIT socket behind on the
            # server side: spread them over several destination ports
            s = client.connect(bed.proxy, host, pool[(i + n) % len(pool)],
                               timeout=10, **kw)
            s.sendall(b"x")
            s.recv(1)
            # RST instead of FIN: no TIME_WAIT piles up on the client
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                         struct.pack("ii", 1, 0))
            s.close()
            n += 1
        return n

    t0 = time.time()
    total = sum(bl.run_threads(workers, one))
    return total / (time.time() - t0)


def run(bed, rep, args):
    rep.group("sessions")
    # request/response handshake vs everything in one segment, and with
    # username/password authentication
    variants = [("default", {}, {}),
                ("pipeline", {"pipeline": True}, {}),
                ("auth", {"user": "u", "pw": "p"},
                 {"auth": {"username": "u", "password": "p"}})]
    for name, kw, over in variants:
        with bl.running(bed, **over) as (proc, smp):
            for workers in (1, 16):
                smp.start()
                rate = conn_rate(bed, bed.target4, args.duration, workers,
                                 **kw)
                st = smp.stop()
                rep.add("sessions", "connections/s x%d (%s)" % (workers, name),
                        rate, "conn/s")
                rep.add("sessions", "server cpu x%d (%s)" % (workers, name),
                        st["cpu_pct"], "%")

    # multi-worker scaling of the connection rate
    with bl.running(bed, main={"workers": 4}) as (proc, smp):
        smp.start()
        rate = conn_rate(bed, bed.target4, args.duration, 16)
        st = smp.stop()
        rep.add("sessions", "connections/s x16 (4 workers)", rate, "conn/s")
        rep.add("sessions", "server cpu x16 (4 workers)", st["cpu_pct"], "%")

    # idle sessions are opened in batches and each batch is confirmed before
    # the next one, so a slow machine cannot overrun the setup path.
    with bl.running(bed, misc={"limit-nofile": 65535}) as (proc, smp):
        base = bed.proc_stats(proc.pid).get("rss_kb", 0)
        n, batch = 1000, 100
        socks = []
        try:
            while len(socks) < n:
                new = []
                for _ in range(batch):
                    s = client.connect(bed.proxy, bed.target4, bed.P_ECHO,
                                       timeout=15)
                    s.sendall(b"x")
                    new.append(s)
                socks += new
                for s in new:
                    s.recv(1)
            held = bed.proc_stats(proc.pid).get("rss_kb", 0)
        except OSError as e:
            print("  %d of %d sessions established before: %s" %
                  (len(socks), n, e))
            print("  --- server log ---\n" + proc.log()[-2000:])
            held = bed.proc_stats(proc.pid).get("rss_kb", 0)
        finally:
            count = len(socks)
            for s in socks:
                s.close()
        rep.add("sessions", "rss idle", base / 1024.0, "MiB")
        rep.add("sessions", "rss with %d sessions" % count, held / 1024.0,
                "MiB")
        if count:
            rep.add("sessions", "rss per session",
                    (held - base) / float(count), "KiB")

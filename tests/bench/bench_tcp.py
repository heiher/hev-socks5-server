"""TCP (CONNECT) throughput and the server's CPU cost."""
from hevtest import benchlib as bl


def measure(bed, rep, smp, group, direction, tag, host, n, D):
    smp.start()
    if direction == "upload":
        nbytes, secs = bl.tcp_upload(bed, host, D, n)
    else:
        nbytes, secs = bl.tcp_download(bed, host, D, n)
    st = smp.stop()
    rep.add(group, "throughput " + tag, bl.mbps(nbytes, secs), "Mbit/s")
    rep.add(group, "server cpu " + tag, st["cpu_pct"], "%")
    if nbytes:
        rep.add(group, "cpu per GB " + tag, st["cpu_s"] / (nbytes / 1e9),
                "s/GB")


def run(bed, rep, args):
    D = args.duration
    cases = [
        ("ipv4", bed.target4, 1), ("ipv4", bed.target4, 8),
        ("ipv6", bed.target6, 1), ("ipv6", bed.target6, 8),
    ]
    with bl.running(bed) as (proc, smp):
        for direction in ("upload", "download"):
            group = "tcp %s" % direction
            rep.group(group)
            for fam, host, n in cases:
                measure(bed, rep, smp, group, direction,
                        "%s x%d" % (fam, n), host, n, D)
        st = smp.stop()
        rep.group("tcp memory")
        rep.add("tcp memory", "server peak RSS", st["peak_rss_kb"] / 1024.0,
                "MiB")

    # more worker threads, same load
    with bl.running(bed, main={"workers": 4}) as (proc, smp):
        for direction in ("upload", "download"):
            group = "tcp %s (4 workers)" % direction
            rep.group(group)
            measure(bed, rep, smp, group, direction, "ipv4 x8",
                    bed.target4, 8, D)

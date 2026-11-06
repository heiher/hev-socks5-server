"""Base class of the functional tests."""
import unittest

from . import client, config


class Server:
    def __init__(self, bed, proc, cfg_path, cfg):
        self.bed = bed
        self.proc = proc
        self.cfg_path = cfg_path
        self.cfg = cfg

    @property
    def pid(self):
        return self.proc.pid

    def stop(self, *a, **kw):
        return self.bed.stop_server(self.proc, *a, **kw)

    def log(self):
        return self.proc.log()


class ServerTestCase(unittest.TestCase):
    """Each test gets an isolated server instance via ``self.server()``.

    ``bed`` is injected by the runner.  The remote side (target services) is
    always up.  The client side is a plain SOCKS5 user, see ``client``.
    """
    bed = None

    def setUp(self):
        self.t4 = self.bed.target4
        self.t6 = self.bed.target6
        self.proxy = self.bed.proxy
        self.proxy6 = self.bed.proxy6

    def write_file(self, name, text):
        path = self.bed.tmpfile(name)
        with open(path, "w") as f:
            f.write(text)
        return path

    def server(self, wait=True, extra_args=(), raw=None, **over):
        """Start the server.  ``over`` overrides config sections, e.g.
        ``self.server(main={'workers': 4}, auth=None)``."""
        cfg = config.build(self.bed, **over)
        if raw is not None:
            path = self.write_file("server.yml", raw)
            port = None
        else:
            path = self.write_file("server.yml", config.dump(cfg))
            port = int(cfg["main"]["port"]) if "main" in cfg else None
        pid_file = cfg.get("misc", {}).get("pid-file")
        proc = self.bed.start_server(path, extra_args, wait=wait, port=port,
                                     pid_file=pid_file)
        srv = Server(self.bed, proc, path, cfg)
        self.addCleanup(srv.stop)
        return srv

    # --- client helpers ------------------------------------------------
    def connect(self, host, port, proxy=None, **kw):
        return client.connect(proxy or self.proxy, host, port, **kw)

    def assertRejected(self, fn, *a, **kw):
        """The server must refuse the request: error reply, close or
        timeout while handshaking."""
        try:
            s = fn(*a, **kw)
        except OSError:
            return
        s.close()
        self.fail("request was accepted")

    def require(self, cond, why):
        if not cond:
            self.skipTest(why)

    def fail_with_log(self, srv, msg):
        self.fail("%s\n--- server log ---\n%s" % (msg, srv.log()))

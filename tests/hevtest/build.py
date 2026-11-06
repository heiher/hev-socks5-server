"""Build hev-socks5-server (if needed)."""
import os
import subprocess


def ensure_server(root, path=None, jobs=None, build=True):
    if path:
        if not os.access(path, os.X_OK):
            raise SystemExit("hevtest: %s is not executable" % path)
        return os.path.abspath(path)
    exe = os.path.join(root, "bin", "hev-socks5-server")
    if build:
        r = subprocess.run(["make", "-j%d" % (jobs or os.cpu_count() or 1)],
                           cwd=root)
        if r.returncode != 0:
            raise SystemExit("hevtest: make failed")
    if not os.access(exe, os.X_OK):
        raise SystemExit("hevtest: %s missing, build the project first" % exe)
    return exe

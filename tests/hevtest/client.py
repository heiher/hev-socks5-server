"""A small SOCKS5 client written from RFC 1928/1929 plus the two hev
extensions (``FWD UDP`` = UDP in TCP).  It behaves like an ordinary
application; nothing here knows the server's internals."""
import hashlib
import os
import socket
import struct
import threading
import time

CMD_CONNECT = 1
CMD_BIND = 2
CMD_UDP_ASSOCIATE = 3
CMD_FWD_UDP = 5

REP_OK = 0


class ProtoError(OSError):
    pass


class AuthError(ProtoError):
    pass


class ReplyError(ProtoError):
    def __init__(self, rep):
        super().__init__("socks5 reply %d" % rep)
        self.rep = rep


def family(host):
    return socket.AF_INET6 if ":" in host else socket.AF_INET


def pack_addr(host, port):
    for fam, atype in ((socket.AF_INET, 1), (socket.AF_INET6, 4)):
        try:
            return (bytes([atype]) + socket.inet_pton(fam, host) +
                    struct.pack(">H", port))
        except OSError:
            pass
    name = host.encode()
    return bytes([3, len(name)]) + name + struct.pack(">H", port)


def unpack_addr(buf, off=0):
    """-> (host, port, next offset); IndexError/struct.error if short."""
    atype = buf[off]
    off += 1
    if atype == 1:
        host = socket.inet_ntop(socket.AF_INET, bytes(buf[off:off + 4]))
        off += 4
    elif atype == 4:
        host = socket.inet_ntop(socket.AF_INET6, bytes(buf[off:off + 16]))
        off += 16
    elif atype == 3:
        n = buf[off]
        host = bytes(buf[off + 1:off + 1 + n]).decode()
        off += 1 + n
    else:
        raise ProtoError("bad address type %d" % atype)
    port = struct.unpack(">H", bytes(buf[off:off + 2]))[0]
    return host, port, off + 2


def tcp_connect(host, port, timeout=5.0):
    s = socket.socket(family(host), socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
    except BaseException:
        s.close()
        raise
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def recv_exact(s, n):
    buf = bytearray()
    while len(buf) < n:
        d = s.recv(min(1 << 20, n - len(buf)))
        if not d:
            raise ConnectionError("EOF after %d of %d bytes" % (len(buf), n))
        buf += d
    return bytes(buf)


def read_reply(s):
    """-> (rep, bound host, bound port)"""
    h = recv_exact(s, 4)
    if h[0] != 5:
        raise ProtoError("bad version %d in reply" % h[0])
    atype = h[3]
    if atype == 1:
        rest = recv_exact(s, 6)
    elif atype == 4:
        rest = recv_exact(s, 18)
    elif atype == 3:
        n = recv_exact(s, 1)
        rest = n + recv_exact(s, n[0] + 2)
    else:
        raise ProtoError("bad address type %d in reply" % atype)
    host, port, _ = unpack_addr(bytes([atype]) + rest)
    return h[1], host, port


def greeting(user=None, offer=None):
    methods = offer if offer is not None else ([2] if user is not None
                                                else [0])
    return bytes([5, len(methods)]) + bytes(methods)


def auth_request(user, pw):
    u, p = user.encode(), pw.encode()
    return bytes([1, len(u)]) + u + bytes([len(p)]) + p


def negotiate(s, user=None, pw=None, offer=None):
    """Method selection and, if the server asks for it, RFC 1929 login.
    Returns the selected method."""
    s.sendall(greeting(user, offer))
    r = recv_exact(s, 2)
    if r[0] != 5:
        raise ProtoError("bad version %d" % r[0])
    if r[1] == 0xFF:
        raise AuthError("no acceptable method")
    if r[1] == 2:
        if user is None:
            raise AuthError("server wants credentials")
        s.sendall(auth_request(user, pw))
        r2 = recv_exact(s, 2)
        if r2[1] != 0:
            raise AuthError("credentials rejected")
    return r[1]


def request(s, cmd, host, port):
    s.sendall(bytes([5, cmd, 0]) + pack_addr(host, port))
    return read_reply(s)


def open_session(proxy, cmd, host, port, user=None, pw=None, timeout=10.0,
                 pipeline=False, offer=None):
    """Handshake + request.  Returns (socket, bound host, bound port); the
    socket is closed again on error.  A non-zero reply raises ReplyError."""
    s = tcp_connect(proxy[0], proxy[1], timeout)
    try:
        if pipeline:
            # everything in one segment: only possible when the method is
            # known in advance
            data = greeting(user, offer)
            if user is not None:
                data += auth_request(user, pw)
            s.sendall(data + bytes([5, cmd, 0]) + pack_addr(host, port))
            r = recv_exact(s, 2)
            if r[1] == 0xFF:
                raise AuthError("no acceptable method")
            if user is not None:
                if r[1] != 2:
                    raise AuthError("unexpected method")
                if recv_exact(s, 2)[1] != 0:
                    raise AuthError("credentials rejected")
            rep, bh, bp = read_reply(s)
        else:
            negotiate(s, user, pw, offer)
            rep, bh, bp = request(s, cmd, host, port)
        if rep != REP_OK:
            raise ReplyError(rep)
        return s, bh, bp
    except BaseException:
        s.close()
        raise


def connect(proxy, host, port, **kw):
    """Like socket.create_connection() through the proxy."""
    return open_session(proxy, CMD_CONNECT, host, port, **kw)[0]


# ----------------------------------------------------------------- UDP

def _is_any(host):
    return host in ("0.0.0.0", "::")


class UdpSession:
    """A UDP relay through the proxy.

    mode ``udp``: UDP ASSOCIATE, datagrams carry the RFC 1928 header.
    mode ``tcp``: FWD UDP (UDP in TCP), datagrams are framed on the control
    connection.
    """

    def __init__(self, proxy, user=None, pw=None, mode="udp", timeout=5.0,
                 want_port=True, **kw):
        self.mode = mode
        self.proxy = proxy
        self.buf = b""
        any_addr = "::" if ":" in proxy[0] else "0.0.0.0"
        if mode == "udp":
            self.sock = socket.socket(family(proxy[0]), socket.SOCK_DGRAM)
            self.sock.bind((any_addr, 0))
            port = self.sock.getsockname()[1] if want_port else 0
            try:
                self.ctrl, bh, bp = open_session(
                    proxy, CMD_UDP_ASSOCIATE, any_addr, port, user, pw,
                    timeout, **kw)
            except BaseException:
                self.sock.close()
                raise
            self.relay = (proxy[0] if _is_any(bh) else bh, bp)
            self.bound = (bh, bp)
        else:
            self.sock = None
            self.ctrl, bh, bp = open_session(
                proxy, CMD_FWD_UDP, any_addr, 0, user, pw, timeout, **kw)
            self.relay = None
            self.bound = (bh, bp)

    def close(self):
        for s in (self.sock, self.ctrl):
            if s is not None:
                try:
                    s.close()
                except OSError:
                    pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def sendto(self, data, host, port):
        addr = pack_addr(host, port)
        if self.mode == "udp":
            self.sock.sendto(b"\0\0\0" + addr + data, self.relay)
        else:
            self.ctrl.sendall(struct.pack(">HB", len(data), 3 + len(addr)) +
                              addr + data)

    def recvfrom(self, timeout=3.0):
        """-> (data, (host, port)); raises socket.timeout"""
        if self.mode == "udp":
            self.sock.settimeout(timeout)
            while True:
                d, _ = self.sock.recvfrom(65535)
                if len(d) < 10 or d[2] != 0:
                    continue
                host, port, off = unpack_addr(d, 3)
                return d[off:], (host, port)
        end = time.time() + timeout
        while True:
            if len(self.buf) >= 3:
                dlen, hlen = struct.unpack(">HB", self.buf[:3])
                if len(self.buf) >= hlen + dlen:
                    host, port, off = unpack_addr(self.buf, 3)
                    data = self.buf[off:off + dlen]
                    self.buf = self.buf[hlen + dlen:]
                    return data, (host, port)
            left = end - time.time()
            if left <= 0:
                raise socket.timeout("timed out")
            self.ctrl.settimeout(left)
            d = self.ctrl.recv(65536)
            if not d:
                raise ConnectionError("control connection closed")
            self.buf += d

    def closed_by_server(self, timeout=0.5):
        """True if the control connection was closed by the server."""
        self.ctrl.settimeout(timeout)
        try:
            d = self.ctrl.recv(65536)
            if not d:
                return True
            if self.mode == "tcp":
                self.buf += d
            return False
        except socket.timeout:
            return False
        except OSError:
            return True


def udp_roundtrip(sess, host, port, payload, timeout=3.0, retries=3):
    """One datagram round trip; (reply, (src host, src port)) or (None, None).
    UDP may lose packets, so the request is retried."""
    for _ in range(retries):
        sess.sendto(payload, host, port)
        try:
            return sess.recvfrom(timeout / retries)
        except socket.timeout:
            continue
    return None, None


# ------------------------------------------------------- TCP exchanges

def tcp_echo(sock, size):
    """Send ``size`` random bytes on ``sock`` and expect them back."""
    data = os.urandom(size)
    err = []

    def writer():
        try:
            sock.sendall(data)
        except OSError as e:
            err.append(e)

    t = threading.Thread(target=writer)
    t.start()
    try:
        got = recv_exact(sock, size)
    finally:
        t.join()
    if err:
        raise err[0]
    return got == data


def tcp_upload_digest(sock, size):
    """Upload ``size`` bytes to the digest service, True if the far end saw
    the same hash."""
    data = os.urandom(min(size, 1 << 20))
    h = hashlib.sha256()
    sent = 0
    while sent < size:
        chunk = data[:size - sent]
        sock.sendall(chunk)
        h.update(chunk)
        sent += len(chunk)
    sock.shutdown(socket.SHUT_WR)
    line = read_line(sock)
    n, digest = line.split()
    return int(n) == size and digest == h.hexdigest()


def read_line(sock):
    line = b""
    while not line.endswith(b"\n"):
        d = sock.recv(256)
        if not d:
            break
        line += d
    return line.decode().strip()


def tcp_ctrl(proxy, host, port, cmd, **kw):
    s = connect(proxy, host, port, **kw)
    try:
        s.sendall(cmd.encode() + b"\n")
        return read_line(s)
    finally:
        s.close()


def whoami(proxy, host, **kw):
    """The source address the target saw for a connection made via proxy."""
    s = connect(proxy, host, 7010, **kw)
    try:
        return read_line(s)
    finally:
        s.close()


def wait_for(pred, timeout=5.0, step=0.02):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(step)
    return pred()

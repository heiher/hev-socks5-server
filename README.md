# HevSocks5Server

[![status](https://github.com/heiher/hev-socks5-server/actions/workflows/build.yaml/badge.svg?branch=main&event=push)](https://github.com/heiher/hev-socks5-server)

HevSocks5Server is a simple, lightweight socks5 server.

## Features

* IPv4/IPv6. (dual stack)
* Standard `CONNECT` command.
* Standard `UDP ASSOCIATE` command.
* Extended `FWD UDP` command. (UDP in TCP) [^1]
* Multiple username/password authentication.

## Benchmarks

See [here](https://github.com/heiher/hev-socks5-server/wiki/Benchmarks) for more details.

### Speed

![](https://github.com/heiher/hev-socks5-server/wiki/res/upload-speed.png)
![](https://github.com/heiher/hev-socks5-server/wiki/res/download-speed.png)

### CPU usage

![](https://github.com/heiher/hev-socks5-server/wiki/res/upload-cpu.png)
![](https://github.com/heiher/hev-socks5-server/wiki/res/download-cpu.png)

### Memory usage

![](https://github.com/heiher/hev-socks5-server/wiki/res/upload-mem.png)
![](https://github.com/heiher/hev-socks5-server/wiki/res/download-mem.png)

## How to Build

### Unix

```bash
git clone --recursive https://github.com/heiher/hev-socks5-server
cd hev-socks5-server
make

# statically link
make ENABLE_STATIC=1
```

### Android

```bash
mkdir hev-socks5-server
cd hev-socks5-server
git clone --recursive https://github.com/heiher/hev-socks5-server jni
cd jni
ndk-build
```

### iOS and MacOS

```bash
git clone --recursive https://github.com/heiher/hev-socks5-server
cd hev-socks5-server
# will generate HevSocks5Server.xcframework
./build-apple.sh
```

### Windows (MSVC + Clang)

The same `Makefile` builds natively with clang targeting MSVC. Open a Visual
Studio developer prompt (clang from the VS LLVM component or `PATH`), make
sure GNU make and a POSIX `sh.exe` (for example the one of Git for Windows,
`C:\Program Files\Git\usr\bin`) are on `PATH`, then:

```bat
git clone --recursive https://github.com/heiher/hev-socks5-server
cd hev-socks5-server
make
make ARCH=x86
make ARCH=arm64
make shared
```

`ARCH` is `x64` (default), `x86` or `arm64` and must match the developer
prompt architecture. The executable is `bin/hev-socks5-server.exe`; the shared
library is `bin/hev-socks5-server.dll` with the import library
`bin/hev-socks5-server.dll.lib`, it embeds the third-parts. Run `make clean`
when switching between the executable and the shared library.

On native Windows there is no daemon mode (`pid-file` only writes the process
id), no `limit-nofile`, no `bind-interface`/`mark`, and no reload on `SIGUSR1`.
Stop the server with Ctrl+C.

## How to Use

### Config

```yaml
main:
  # Worker threads
  workers: 4
  # Listen port
  port: 1080
  # Listen address (ipv4|ipv6)
  listen-address: '::'
  # UDP listen port (0: random, a-b: range)
# udp-port: 0
  # UDP listen address (ipv4|ipv6)
# udp-listen-address: '::1'
  # UDP public address (ipv4)
# udp-public-address-v4: ''
  # UDP public address (ipv6)
# udp-public-address-v6: ''
  # Listen ipv6 only
  listen-ipv6-only: false
  # Bind source address (ipv4|ipv6)
  # It is overridden by bind-address-v{4,6} if specified
  bind-address: ''
  # Bind source address (ipv4)
  bind-address-v4: ''
  # Bind source address (ipv6)
  bind-address-v6: ''
  # Bind source network interface
  bind-interface: ''
  # Domain address type (ipv4|ipv6|unspec)
  domain-address-type: unspec
  # Socket mark (hex: 0x1, dec: 1, oct: 01)
  mark: 0
  # TCP fastopen
# tcp-fastopen: false

#auth:
# file: conf/auth.txt
# username:
# password:

#misc:
  # task stack size (bytes)
# task-stack-size: 8192
  # udp socket recv buffer (SO_RCVBUF) size (bytes)
# udp-recv-buffer-size: 524288
  # number of udp buffers in splice, 1500 bytes per buffer.
# udp-copy-buffer-nums: 10
  # TCP connect timeout (ms)
# connect-timeout: 10000
  # TCP read-write timeout (ms)
# tcp-read-write-timeout: 300000
  # UDP read-write timeout (ms)
# udp-read-write-timeout: 60000
  # null, stdout, stderr or file-path
# log-file: null
  # debug, info, warn or error
# log-level: warn
  # If present, run as a daemon with this pid file
# pid-file: /run/hev-socks5-server.pid
  # If present, set rlimit nofile; else use default value
# limit-nofile: 65535
```

### Authentication file

```
<USERNAME> <SPACE> <PASSWORD> <SPACE> <MARK> <LF>
```

- USERNAME: A string of up to 255 characters
- PASSWORD: A string of up to 255 characters
- MARK: Hexadecimal

### Run

```bash
bin/hev-socks5-server conf/main.yml
```

### Live updating authentication file

Send signal `SIGUSR1` to socks5 server process after the authentication file is updated.

```bash
killall -SIGUSR1 hev-socks5-server
```

### Limit number of connections

For example, limit the number of connections for `jerry` up to `2`:

#### Config

```yaml
auth:
  file: conf/auth.txt
```

#### Auth file

```
jerry pass 1a
```

#### IPtables

```bash
iptables -A OUTPUT -p tcp --syn -m mark --mark 0x1a -m connlimit --connlimit-above 2 -j REJECT
```

#### OpenWrt 23.05+

Repo: https://github.com/openwrt/packages/tree/master/net/hev-socks5-server

```sh
# Install package
opkg install hev-socks5-server

# Edit /etc/config/hev-socks5-server

# Restart service
/etc/init.d/hev-socks5-server restart
```

## API

### C

```c
/**
 * hev_socks5_server_main_from_file:
 * @config_path: config file path
 *
 * Start and run the socks5 server, this function will blocks until the
 * hev_socks5_server_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.6.7
 */
int hev_socks5_server_main_from_file (const char *config_path);

/**
 * hev_socks5_server_main_from_str:
 * @config_str: string config
 * @config_len: the byte length of string config
 *
 * Start and run the socks5 server, this function will blocks until the
 * hev_socks5_server_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.6.7
 */
int hev_socks5_server_main_from_str (const unsigned char *config_str,
                                     unsigned int config_len);

/**
 * hev_socks5_server_quit:
 *
 * Stop the socks5 server.
 *
 * Since: 2.6.7
 */
void hev_socks5_server_quit (void);
```

### Java

```java
public class Socks5Service {
	private static native boolean Socks5StartService(String config_path);
	private static native boolean Socks5StopService();
	private static native boolean Socks5IsRunning();

	static {
		System.loadLibrary("hev-socks5-server");
	}
```

### Kotlin

```kt
object Socks5Service {
    private external fun Socks5StartService(config_path: String): Boolean
    private external fun Socks5StopService(): Boolean
    private external fun Socks5IsRunning(): Boolean

    init {
        System.loadLibrary("hev-socks5-server")
    }
}
```

Allow overriding the package and class names in `Application.mk`[^2].

```makefile
APP_CFLAGS := -DPKGNAME=hev/socks5 -DCLSNAME=Socks5Service
```

## Use Cases

### Android App

* [Socks5](https://github.com/heiher/socks5)

### iOS App

* [Socks5](https://github.com/heiher/socks5-ios)

## Contributors

* **ammar faizi** - https://github.com/ammarfaizi2
* **hev** - https://hev.cc
* **pexcn** - <i@pexcn.me>

## License

MIT

[^1]: See [protocol specification](https://github.com/heiher/hev-socks5-core/tree/main?tab=readme-ov-file#udp-in-tcp). The [hev-socks5-tunnel](https://github.com/heiher/hev-socks5-tunnel) and [hev-socks5-tproxy](https://github.com/heiher/hev-socks5-tproxy) clients support UDP relay over TCP.
[^2]: See [Application.mk](https://github.com/heiher/socks5/blob/364084079be989233286af2e33048a442fd06f45/app/src/main/jni/Application.mk#L19)

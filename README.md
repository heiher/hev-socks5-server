# HevSocks5Server

[![status](https://gitlab.com/hev/hev-socks5-server/badges/master/pipeline.svg)](https://gitlab.com/hev/hev-socks5-server/commits/master)

HevSocks5Server is a simple, lightweight socks5 server for Unix.

**Features**
* standard `CONNECT` command.
* private `DNSFWD` command. (see [tproxy](https://gitlab.com/hev/hev-socks5-tproxy))
* username/password authentication.
* IPv4/IPv6. (dual stack)

## How to Build

**Unix**:
```bash
git clone --recursive git://github.com/heiher/hev-socks5-server
cd hev-socks5-server
make

# statically link
make ENABLE_STATIC=1
```

**Android**:
```bash
mkdir hev-socks5-server
cd hev-socks5-server
git clone --recursive git://github.com/heiher/hev-socks5-server jni
cd jni
ndk-build
```

## How to Use

### Config

```yaml
main:
  # Worker threads
  workers: 4
  port: 1080 # Listen port
  listen-address: 0.0.0.0 # Listen address (ipv4|ipv6)
  dns-address: 8.8.8.8 # DNS server address (ipv4|ipv6)
  ipv6-first: false # Resolve domain to IPv6 address first

#auth:
#  username:
#  password:

#misc:
#  log-file: null # stdout, stderr or file-path
#  log-level: warn # debug, info or error
#  pid-file: /run/hev-socks5-server.pid # If present, run as a daemon
#  limit-nofile: -1 # If present, set rlimit nofile; else use default value
```

### Run

```bash
bin/hev-socks5-server conf/main.ini
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL

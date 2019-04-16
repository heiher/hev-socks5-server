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

```ini
[Main]
; Worker threads
Workers=4
; Listen port
Port=1080
; Listen address
;  ipv4
;  ipv6
ListenAddress=0.0.0.0
; DNS server address
;  ipv4
;  ipv6
DNSAddress=8.8.8.8
; Resolve domain to IPv6 address first
IPv6First=true

;[Auth]
;Username=
;Password=

;[Misc]
; If present, run as a daemon with this pid file
;PidFile=/run/hev-socks5-server.pid
; If present, set rlimit nofile; else use default value of environment
; -1: unlimit
;LimitNOFile=-1
```

### Run

```bash
bin/hev-socks5-server conf/main.ini
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL

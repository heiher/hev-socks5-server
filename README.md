# HevSocks5Server

[![status](https://gitlab.com/hev/hev-socks5-server/badges/master/pipeline.svg)](https://gitlab.com/hev/hev-socks5-server/commits/master)

HevSocks5Server is a simple, lightweight socks5 server for Unix.

**Features**
* standard CONNECT command supported.
* private DNSFWD command supported.
* username/password authentication.

## How to Build

**Unix**:
```bash
git clone --recursive git://github.com/heiher/hev-socks5-server
cd hev-socks5-server
make
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

```bash
# Run with config file.
bin/hev-socks5-server conf/main.ini

# Run as a daemon with specific pid file.
bin/hev-socks5-server conf/main.ini /run/hev-socks5-server.pid
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL

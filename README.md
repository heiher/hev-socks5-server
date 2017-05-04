# HevSocks5Server

HevSocks5Server is a simple, lightweight socks5 server for Linux.

**Features**
* standard CONNECT command supported.
* private DNSFWD command supported.

## How to Build

**Linux**:
```bash
git clone git://github.com/heiher/hev-socks5-server
cd hev-socks5-server
git submodule init
git submodule update
make
```

**Android**:
```bash
mkdir hev-socks5-server
cd hev-socks5-server
git clone git://github.com/heiher/hev-socks5-server jni
cd jni
git submodule init
git submodule update
nkd-build
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL

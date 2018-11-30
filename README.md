# HevSocks5Server

HevSocks5Server is a simple, lightweight socks5 server for Linux.

**Features**
* standard CONNECT command supported.
* private DNSFWD command supported.
* username/password authentication.

## How to Build

**Linux**:
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

## Authors
* **Heiher** - https://hev.cc

## License
LGPL

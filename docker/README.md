# hev-socks5-server for docker

## Usage

Available environment variables:
```bash
PORT
LISTEN_ADDRESS
AUTH
```

Prebuilt images are published to GitHub Packages:

* `ghcr.io/heiher/hev-socks5-server:latest`: the latest published version
* `ghcr.io/heiher/hev-socks5-server:nightly`: the latest source build of `main`
* `ghcr.io/heiher/hev-socks5-server:X.Y.Z`: the specific version

```bash
docker run -d \
  --name hev-socks5-server \
  --restart always \
  --net host \
  -e PORT=1081 \
  -e AUTH="user:pass" \
  ghcr.io/heiher/hev-socks5-server:latest
```

Build image from source:

```bash
cd hev-socks5-server
docker build -t hev-socks5-server -f docker/Dockerfile .
docker run -d \
  --name hev-socks5-server \
  --restart always \
  --net host \
  -e PORT=1081 \
  -e AUTH="user:pass" \
  hev-socks5-server
```

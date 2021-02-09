# hev-socks5-server for docker

## Usage

Available environment variables:
```bash
PORT
LISTEN_ADDRESS
DNS_ADDRESS
IPV6_FIRST
AUTH
```

```bash
cd hev-socks5-server/docker
docker build -t hev-socks5-server .
docker run -d \
  --name hev-socks5-server \
  --restart always \
  --net host \
  -e PORT=1081 \
  -e IPV6_FIRST=true \
  -e AUTH="user:pass" \
  hev-socks5-server
```

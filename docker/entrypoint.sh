#!/bin/sh

if [[ -n "$PORT" && "$PORT" != 1080 ]]; then
  sed -i "s/  port: 1080/  port: $PORT/" /app/etc/hev-socks5-server.yml
fi

if [[ -n "$LISTEN_ADDRESS" && "$LISTEN_ADDRESS" != '::' ]]; then
  sed -i "s/  listen-address: '::'/  listen-address: $LISTEN_ADDRESS/" /app/etc/hev-socks5-server.yml
fi

if [[ -n "$AUTH" ]]; then
  sed -i "s/#auth:/auth:/" /app/etc/hev-socks5-server.yml
  sed -i "s/#  username:/  username: $(echo $AUTH | cut -d ':' -f 1)/" /app/etc/hev-socks5-server.yml
  sed -i "s/#  password:/  password: $(echo $AUTH | cut -d ':' -f 2)/" /app/etc/hev-socks5-server.yml
fi

exec /app/bin/hev-socks5-server /app/etc/hev-socks5-server.yml

#
# Dockerfile for hev-socks5-server
#

#
# Build stage
#
FROM alpine AS builder

COPY . /tmp/hev-socks5-server

# build
RUN apk update \
  && apk add --no-cache --virtual .build-deps build-base \
  && cd /tmp/hev-socks5-server \
  && make -j$(nproc) \
  && make install INSTDIR="/app" \
  && make clean \
  && cd / \
  && rm -r /tmp/hev-socks5-server \
  && apk del .build-deps \
  && rm -rf /var/cache/apk/*

#
# Runtime stage
#
FROM alpine

ENV TZ=Asia/Taipei

# add depends
RUN apk update \
  && apk add --no-cache tzdata \
  && rm -rf /var/cache/apk/*

# copy files
COPY --from=builder /app /app/
RUN chown nobody:nobody /app/etc
COPY docker/entrypoint.sh /app/entrypoint.sh

USER nobody
ENTRYPOINT ["/app/entrypoint.sh"]

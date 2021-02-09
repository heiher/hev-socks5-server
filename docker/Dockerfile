#
# Dockerfile for hev-socks5-server
#

#
# Build stage
#
FROM alpine AS builder

ENV VERSION=1.9.9
ENV GIT_URL=https://github.com/heiher/hev-socks5-server.git

# add depends
RUN apk update \
  && apk add --no-cache --virtual .build-deps build-base git

# build executable
RUN git clone $GIT_URL --branch $VERSION --recurse-submodules -j2 && cd hev-socks5-server \
  && make -j$(nproc) && make install INSTDIR="/app" \
  && cd .. && rm -r hev-socks5-server

# clean up
RUN apk del .build-deps \
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
COPY entrypoint.sh /app/entrypoint.sh

USER nobody
ENTRYPOINT ["/app/entrypoint.sh"]

FROM dhi.io/alpine-base:3.23-dev AS stage

RUN apk update && apk add wget make gcc libevent-dev msgpack-c-dev musl-dev openssl-dev bsd-compat-headers jq redis
RUN wget -q https://api.github.com/repos/nicolasff/webdis/tags -O /dev/stdout | jq '.[] | .name' | head -1  | sed 's/"//g' > latest
RUN wget https://github.com/nicolasff/webdis/archive/$(cat latest).tar.gz -O webdis-latest.tar.gz
RUN tar -xvzf webdis-latest.tar.gz
RUN cd webdis-$(cat latest) && make && make install && make clean && make SSL=1 && cp webdis /usr/local/bin/webdis-ssl && cd ..

# update Webdis config
RUN mv /etc/webdis.prod.json /tmp/ && \
    cat /tmp/webdis.prod.json | jq '.daemonize = false | .logfile = "/dev/stderr"' > /etc/webdis.prod.json && \
    rm /tmp/webdis.prod.json

# update Redis config
RUN sed -i -E \
   -e '/^(unixsocket|dir|logfile|pidfile|daemonize|appendonly|save|maxmemory|maxmemory-policy|bind)/d' \
   /etc/redis.conf
RUN printf "\n# Edits for local Webdis use:\ndir /tmp\nlogfile /tmp/redis.log\npidfile /tmp/redis.pid\ndaemonize yes\nappendonly no\nsave \"\"\nmaxmemory 100mb\nmaxmemory-policy allkeys-lru\nbind 127.0.0.1\n" >> /etc/redis.conf

# main image
FROM dhi.io/alpine-base:3.23
LABEL maintainer="Nicolas Favre-Felix <n.favrefelix@gmail.com>"
LABEL org.opencontainers.image.source=https://github.com/nicolasff/webdis

COPY --from=stage /usr/local/bin/webdis /usr/local/bin/webdis-ssl /usr/local/bin/
COPY --from=stage /usr/bin/redis-server /usr/bin/

COPY --from=stage /etc/webdis.prod.json \
    /etc/redis.conf /etc/
COPY --from=stage /usr/lib/libevent-2.1.so.* \
    /usr/lib/libmsgpackc.so.* \
    /usr/lib/libssl.so.* \
    /usr/lib/libcrypto.so.* \
    /usr/lib/libstdc++.so.* \
    /usr/lib/libgcc_s.so.* /usr/lib/

CMD ["/bin/sh", "-c", "/usr/bin/redis-server /etc/redis.conf && exec /usr/local/bin/webdis /etc/webdis.prod.json"]

EXPOSE 7379

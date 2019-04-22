FROM alpine AS builder

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.ustc.edu.cn/g' /etc/apk/repositories \
    && apk add --no-cache --virtual .build-deps make gcc g++ bsd-compat-headers libevent-dev

COPY . /tmp

WORKDIR /tmp

RUN make

FROM alpine
COPY --from=builder /tmp/webdis /usr/bin/
COPY --from=builder /tmp/webdis.prod.json /etc/

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.ustc.edu.cn/g' /etc/apk/repositories \
    && apk add --no-cache libevent-dev

ENTRYPOINT [ "/usr/bin/webdis", "/etc/webdis.prod.json" ]

EXPOSE 7379

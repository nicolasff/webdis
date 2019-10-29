FROM debian:buster-slim AS BUILDER

ARG APP_FOLDER=/app
ARG BUILD_DEPENDENCIES="gcc \
                        libc6-dev \
                        libevent-dev \
                        libmsgpack-dev \
                        make"

ENV DEBIAN_FRONTEND noninteractive

COPY . $APP_FOLDER/

WORKDIR $APP_FOLDER

RUN set -x && \
    apt-get update -qq && \
    apt-get -y --no-install-recommends install \
      ${BUILD_DEPENDENCIES} && \
    make && \
    apt-get autoremove -y \
      ${BUILD_DEPENDENCIES}


FROM debian:buster-slim

ARG APP_USER=appuser
ARG APP_FOLDER=/app

ENV APP_USER $APP_USER
ENV APP_FOLDER $APP_FOLDER
ENV TERM xterm
ENV PORT 7379
ENV REDIS_HOST redis

EXPOSE $PORT

WORKDIR $APP_FOLDER

COPY --from=BUILDER $APP_FOLDER/webdis /usr/bin/
COPY --from=BUILDER $APP_FOLDER/docker-entrypoint $APP_FOLDER/
COPY --from=BUILDER $APP_FOLDER/webdis.prod.json $APP_FOLDER/

RUN apt-get update -qq && \
    useradd --create-home --shell /bin/bash ${APP_USER} && \
    apt-get -y --no-install-recommends install \
      curl \
      libevent-dev && \
    ln -sf /dev/stdout /var/log/webdis.log && \
    chown -R ${APP_USER}:${APP_USER} ${APP_FOLDER}

USER $APP_USER

HEALTHCHECK CMD curl --fail http://127.0.0.1:${PORT}/PING || exit 1

ENTRYPOINT [ "./docker-entrypoint" ]

CMD [ "webdis", "webdis.prod.json" ]

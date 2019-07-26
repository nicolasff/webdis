FROM debian:jessie
MAINTAINER Nicolas Favre-Felix <n.favrefelix@gmail.com>

RUN apt-get -y update && apt-get -y upgrade && apt-get -y --force-yes install wget make gcc libevent-dev libmsgpack-dev redis-server
RUN wget https://github.com/nicolasff/webdis/archive/0.1.7.tar.gz -O webdis-0.1.7.tar.gz
RUN tar -xvzf webdis-0.1.7.tar.gz
RUN cd webdis-0.1.7 && make && make install && cd ..
RUN rm -rf webdis-0.1.7 webdis-0.1.7.tag.gz
RUN apt-get remove -y wget make gcc libevent-dev libmsgpack-dev
RUN sed -i -e 's/"daemonize":.*true,/"daemonize": false,/g' /etc/webdis.prod.json

CMD /etc/init.d/redis-server start && /usr/local/bin/webdis /etc/webdis.prod.json

EXPOSE 7379

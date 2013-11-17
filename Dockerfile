FROM tianon/debian:wheezy
MAINTAINER Nicolas Favre-Felix <n.favrefelix@gmail.com>

RUN apt-get -y --force-yes install wget make gcc libevent-dev
RUN apt-get -y --force-yes install redis-server
RUN wget --no-check-certificate https://github.com/nicolasff/webdis/archive/0.1.1.tar.gz -O webdis-0.1.1.tar.gz
RUN tar -xvzf webdis-0.1.1.tar.gz
RUN cd webdis-0.1.1 && make && make install && cd ..
RUN rm -rf webdis-0.1.1 webdis-0.1.1.tag.gz
RUN apt-get remove -y wget make gcc

CMD /etc/init.d/redis-server start && /usr/local/bin/webdis /etc/webdis.prod.json && bash

EXPOSE 7379

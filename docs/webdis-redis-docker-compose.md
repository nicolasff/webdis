# Running Webdis & Redis in Docker Compose

This page describes how to start Redis and Webdis in [Docker Compose](https://docs.docker.com/compose/). A different page describes a variant of this model, where connections from Webdis to Redis are encrypted: see "[Running Webdis & Redis in Docker Compose with SSL connections](webdis-redis-docker-compose-ssl.md#running-webdis--redis-in-docker-compose-with-ssl-connections)".

## Setup

We'll keep all our files together in a `playground` directory:

```shell
mkdir playground
cd playground
```

The files we'll need are:
1. A config file for webdis, named `webdis.json`
2. A Compose file for Docker Compose, named `docker-compose.yml` 

### Webdis configuration

First, download `webdis.json` from GitHub by following [this link](https://github.com/nicolasff/webdis/raw/0.1.19/webdis.json) or running this `curl` command:

```sh
curl -sL -o ./webdis.json https://github.com/nicolasff/webdis/raw/0.1.19/webdis.json
```

Edit `./webdis.json` in the `playground` directory and set:

- `"redis_host"` to `"redis"`
- `"logfile"` to `"/dev/stderr"`

### Docker Compose configuration

Create a new file named `docker-compose.yml` in your `playground` directory, with the following contents:

```yaml
services:
  webdis:
    image: nicolas/webdis:latest
    command: /usr/local/bin/webdis /config/webdis.json
    volumes:  # mount volume containing the config file
      - ./:/config
    networks:
      - shared
    depends_on:  # make sure Redis starts first, so that Webdis can connect to it without retries
      - redis
    ports:  # allow connections from the Docker host on localhost, port 7379
      - "127.0.0.1:7379:7379"

  redis:
    image: redis:6.2.6
    networks:
      - shared
    ports:   # make the Redis port visible to Webdis
      - "6379:6379"

networks:
  shared: 
```

This configures two services named `webdis` and `redis`, sharing a common network named `shared`. With the `expose` property, Redis allows connections from Webdis on port 6379. The `webdis` container mounts the local `playground` directory under `/config` and starts its binary using the configuration file we've just downloaded and edited. Finally, Webdis also allows binds its (container) port 7379 to the hosts's loopback interface also on port 7379. This will let us run `curl` locally to connect to Webdis from the host.

**Note:** While the Webdis Docker image does bundle a Redis binary, it makes more sense to use multiple containers to demonstrate the use of SSL connections. This bundled Redis service does not run in this example, since we replace the Webdis command with one that only starts Webdis instead of starting Redis and Webdis together in the same container.

## Start the Docker Compose stack

From the `playground` directory, run:

```shell
docker-compose up
```

You should see both services logging to the console in different colors, with an output like:
```none
Creating playground_redis_1 ... done
Creating playground_webdis_1 ... done
Attaching to playground_redis_1, playground_webdis_1
redis_1   | 1:C 30 Oct 2021 06:14:55.150 # oO0OoO0OoO0Oo Redis is starting oO0OoO0OoO0Oo
redis_1   | 1:C 30 Oct 2021 06:14:55.150 # Redis version=6.2.6, bits=64, commit=00000000, modified=0, pid=1, just started
redis_1   | 1:C 30 Oct 2021 06:14:55.150 # Warning: no config file specified, using the default config. In order to specify a config file use redis-server /path/to/redis.conf
redis_1   | 1:M 30 Oct 2021 06:14:55.152 * monotonic clock: POSIX clock_gettime
redis_1   | 1:M 30 Oct 2021 06:14:55.153 * Running mode=standalone, port=6379.
redis_1   | 1:M 30 Oct 2021 06:14:55.154 # Server initialized
redis_1   | 1:M 30 Oct 2021 06:14:55.155 * Ready to accept connections
webdis_1  | [1] 30 Oct 06:14:56 I Webdis listening on port 7379
webdis_1  | [1] 30 Oct 06:14:56 I Webdis 0.1.19 up and running
```

You can now run commands against Webdis by connecting to port 7379 on `localhost`, e.g.

```sh
$ curl -s 'http://localhost:7379/ping'
{"ping":[true,"PONG"]}

$ curl -s 'http://localhost:7379/info' | jq -r .info.uptime_in_seconds
27
```

## Clean-up

Stop the services with ctrl-c and remove the entire Docker Compose stack by running `docker-compose rm` from the `playground` directory.

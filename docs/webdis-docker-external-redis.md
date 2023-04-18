# Running Webdis in Docker with an external Redis instance

The [Docker images for Webdis](https://hub.docker.com/r/nicolas/webdis/tags) are built to help users discover Webdis and get started quickly. For this reason, they _embed_ a Redis server and the Webdis process running alongside Redis provides access to its data over HTTP.

To run Webdis in Docker as the front-end for an existing Redis instance, we need to configure Webdis to connect to the external Redis instance instead of its own embedded instance. In Docker, Webdis starts with the following command:

```sh
/usr/bin/redis-server /etc/redis.conf && /usr/local/bin/webdis /etc/webdis.prod.json
```

We first need to edit the Webdis config file, and then run the container with the new config file mounted into the container, with the command line updated to load the new file. We'll also be able to remove the part that starts Redis.

## Configuring Webdis

First, let's extract the current config file from the Docker image:

```sh
docker run --rm -ti nicolas/webdis:latest cat /etc/webdis.prod.json > webdis.orig.json
```

We'll define two variables for our external Redis instance, and update the config file to use these values (adjust the `REDIS_HOST` and `REDIS_PORT` variables to match your environment):

```sh
REDIS_HOST=redis.example.com
REDIS_PORT=6379

cat webdis.orig.json | sed -E -e 's/"redis_host":.+$/"redis_host": "$REDIS_HOST",/' -e 's/"redis_port":.+$/"redis_port": "$REDIS_PORT",/' > webdis.new.json
rm -f webdis.orig.json
```

Verify that the Redis host and port are correct in the new config file:

```sh
grep -E 'redis_host|redis_port' webdis.new.json
```
which should give:
```json
    "redis_host": "$REDIS_HOST",
    "redis_port": "$REDIS_PORT",
```

You may have noticed that `redis_port` is a string here when it's supposed to be an integer, but environment variable expansion will only convert variables embedded in double-quoted strings. This is not an issue, since Webdis will convert the string to an integer when it loads the config file.

## Running Webdis

Now that we have a new config file, we can run Webdis with the file mounted into the container.

We'll use `-v` to create a "mount point" into the container, mounting our config directory (for more information on mounting volumes, see the [Docker documentation](https://docs.docker.com/storage/volumes/)). Be sure to provide the absolute path to `webdis.new.json` on your system, or Docker will not mount it as a file but as an empty directory – this is why we use `$(pwd)` in the parameter after `-v`.

We'll also need to pass in the two environment variables we defined earlier, since the config file refers to them. We'll use `-e` to inject these values into the container. Refer to the [Docker documentation](https://docs.docker.com/engine/reference/commandline/run/#env) for more information on this parameter.

```sh
docker run --name webdis-test --rm -d \
    -e REDIS_HOST="$REDIS_HOST" \
    -e REDIS_PORT="$REDIS_PORT" \
    -v "$(pwd)/webdis.new.json:/etc/webdis.new.json" \
    -p 127.0.0.1:7379:7379 nicolas/webdis:latest \
    /usr/local/bin/webdis /etc/webdis.new.json
```

We can verify that Webdis is running and connected to the external Redis instance by running a `PING` first, then `INFO`:

```sh
curl -s http://localhost:7379/PING
```
which should give:
```json
{"PING":[true,"PONG"]}
```

and
```sh
curl -s http://localhost:7379/INFO.txt
```

which should look like:
```none
# Server
redis_version:7.0.8
redis_git_sha1:00000000
redis_git_dirty:0
redis_build_id:73fe4a3beb619f6
...
```

To stop and clean up the container, run:
```sh
docker stop webdis-test
```

## Troubleshooting

If even a basic `PING` command fails, the first step is to verify that Webdis is running and listening on port 7379.

Run the `curl` command again with verbose output (`-v`) to see the full HTTP response:

```sh
curl -v http://localhost:7379/PING
```

If the error from `curl` says that it could not connect, like this:
```none
*   Trying 127.0.0.1:7379...
* connect to 127.0.0.1 port 7379 failed: Connection refused
```

then review your `docker run` command and especially the `-p` option to make sure that the port is being mapped correctly.

### 503 Service Unavailable

An error 503 indicates that Webdis is unable to connect to the Redis instance. The most common cause of this is that the Redis instance is not running, or it is not accessible from within the Webdis container.

If you still have the Webdis container running under the name `webdis-test`, you can start a shell inside it and try to connect to Redis manually:

```sh
docker exec -ti webdis-test /bin/sh
apk update
apk add busybox-extras

telnet $REDIS_HOST $REDIS_PORT
```
(close telnet with `Ctrl+]` followed by `e` and `Enter`)

If you can't connect with `telnet`, then Webdis won't be able to connect to Redis either. Follow the [Docker documentation about container networking](https://docs.docker.com/network/) to resolve this issue.

### 403 Forbidden

This error indicates that Webdis is running and is connected to Redis, but that the Redis instance refused the command sent via Webdis. Review your ACL configuration to make sure that the user that Webdis is using has the correct permissions. See also the [Redis documentation](https://redis.io/docs/management/security/acl/) about ACLs, as well as the [Webdis documentation](https://github.com/nicolasff/webdis#acl) about restricting access to certain commands via Webdis.

### Other errors

If you're still having trouble, change the log file to `/dev/stdout` and the verbosity (log level) to `5` in the Webdis config file, and restart Webdis. You should see more detailed error messages in the container logs.

To do this, change the two logging configuration keys in `webdis.new.json`, going from:
```json
"verbosity": 3,
"logfile": "/var/log/webdis.log",
```
to:
```json
"verbosity": 5,
"logfile": "/dev/stdout",
```

After saving the file, stop the container:
```sh
docker stop webdis-test
```
and start it again with the same `docker run` command [as above](#running-webdis).

Once the container has started, you can tail the logs with:
```sh
docker logs -f webdis-test
```

It should look something like this after a `/PING` and a `/INFO.txt`:

```none
[1] 18 Apr 19:29:49 I Webdis listening on port 7379
[1] 18 Apr 19:29:49 I Webdis 0.1.21 up and running
[1] 18 Apr 19:30:00 D /PING
[1] 18 Apr 19:30:02 D /INFO.txt
```

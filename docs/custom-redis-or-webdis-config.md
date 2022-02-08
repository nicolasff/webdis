# Running in Docker with custom configuration

A [Docker image for Webdis](https://hub.docker.com/r/nicolas/webdis) is available for easy testing of the service. At under 7 MB in size, it bundles both Redis and Webdis so that new users are able to try Webdis quickly and without having to build or configure anything.

While this is helpful as a demo, you might want to change the Redis or Webdis config (or both) to run these services as part of a larger platform. To achieve this, you have a few different options.

## Long-term option: building your own image

If you are running and maintaining Webdis and Redis in the long term, the option that will give you the most control is to create your own image(s) for the two services. Depending on your needs, you might want to run these services separately in order to control resource usage and scaling independently or to run them in the same container in order to minimize latency (make sure the gains are worth it before choosing this approach).

There are already plenty of tutorials for how to build your own Docker image for any service. For Webdis, please follow these recommendations:

1. Choose a specific version of Webdis to run, instead of using `latest`. Using the latest *version* is fine, but don't use the literal `latest` tag as a new release of Webdis could suddenly break your system if (for any reason) the way you are using it no longer works in a later release.
2. If you want to run Redis and Webdis as separate containers, make sure to change the `CMD` entry in the Webdis Dockerfile to not start its embedded Redis service.
3. If they run in the same container, make sure to keep `daemonize: yes` in the Redis config file. Without it, the default command defined as `CMD` will run `redis-server` but not Webdis.
4. Do not hardcode the Redis hostname in `webdis.json`. Instead, use either a hostname resolved at runtime (e.g. `"redis_host": "redis.myapp.org"`) or an environment variable whose value is injected into the container (e.g. `"redis_host": "$REDIS"`)

If Redis and Webdis run in separate containers, consider using encrypted connections between the two. An example of this setup is described in detail in the documentation page titled "[Running Webdis & Redis in Docker Compose with SSL connections](https://github.com/nicolasff/webdis/blob/master/docs/webdis-redis-docker-compose-ssl.md#running-webdis--redis-in-docker-compose-with-ssl-connections)".

## For Redis only: command-line parameters

Redis can be started on the command line with either a config file (e.g. `redis-server /path/to/redis.conf`) or by passing individual configuration values using the format `--key value` as parameters to `redis-server`.

To run Redis with custom parameters using the Webdis image, you'll need to replace the existing container command defined as `CMD` in the [Webdis Dockerfile](https://github.com/nicolasff/webdis/blob/master/Dockerfile#L18) and provide your own. Here again, make sure to include `--daemonize yes` otherwise `redis-server` won't return and Webdis won't start.

### Example: persisting Redis data

To persist Redis data, we need to mount a storage directory into the container and configure Redis to use it. The latter is done with the `dir` configuration parameter, and we can add `appendonly` to provide better [durability](https://redis.io/topics/persistence).

First, create a directory for Redis data:
```sh
mkdir ./redis-data
```

Then run the Webdis image with this new directory mounted into the container and a custom command line:
```sh
docker run --rm -ti -p127.0.0.1:7379:7379 \
    -v $(pwd)/redis-data:/mnt/redis-data \
    nicolas/webdis:latest /bin/sh -c \
    '/usr/bin/redis-server --daemonize yes --dir /mnt/redis-data --appendonly yes && /usr/local/bin/webdis /etc/webdis.prod.json'
```

where:
- `--rm` removes the container when it stops.
- `--ti` makes it an interactive execution with a terminal.
- The `-p127.0.0.1:7379:7379` exposes port 7379 from inside the container to the same port on localhost ([docs](https://docs.docker.com/engine/reference/commandline/run/#publish-or-expose-port--p---expose)).
- `-v $(pwd)/redis-data:/mnt/redis-data` mounts the local directory `redis-data` to `/mnt/redis-data` in the container ([docs](https://docs.docker.com/engine/reference/commandline/run/#mount-volume--v---read-only)).
- The `/bin/sh -c …` is a replacement for the original command to run defined in the Dockerfile with `CMD`.

Once the container runs, it's easy to verify that it works. From another terminal, let's write a key and read it back:
```sh
$ curl -s http://127.0.0.1:7379/SET/hello/world
{"SET":[true,"OK"]}

$ curl -s http://127.0.0.1:7379/GET/hello
{"GET":"world"}
```

Then ask Redis to persist its data:
```sh
$ curl -s http://127.0.0.1:7379/SAVE
{"SAVE":[true,"OK"]}
```

And finally, look into the `.rdb` file to see if we can find the key-value pair we just wrote:
```
$ xxd ./redis-data/dump.rdb
00000000: 5245 4449 5330 3030 39fa 0972 6564 6973  REDIS0009..redis
00000010: 2d76 6572 0536 2e32 2e36 fa0a 7265 6469  -ver.6.2.6..redi
00000020: 732d 6269 7473 c040 fa05 6374 696d 65c2  s-bits.@..ctime.
00000030: dcaa 0162 fa08 7573 6564 2d6d 656d c225  ...b..used-mem.%
00000040: a00e 00fa 0c61 6f66 2d70 7265 616d 626c  .....aof-preambl
00000050: 65c0 00fe 00fb 0100 0005 6865 6c6c 6f05  e.........hello.
00000060: 776f 726c 64ff e142 4193 ae96 846c       world..BA....l
```
We can clearly see `hello` followed by `world`. The AOF file at `./redis-data/appendonly.aof` should also contain a log of all mutations processed by Redis.

### Command line parameters with Webdis

The previous section showed how to pass custom config files for `redis-server` on the command line, but Webdis **does not** support this feature. To configure Webdis to your liking without building your own image, your only option is to mount the Webdis config file into the container, [as documented below](#for-redis-or-webdis-mounting-new-config-files).

## For Redis or Webdis: mounting new config files

In the example above, we used `docker run` with `-v` to mount a local directory to write Redis data into. We can do the same for the Redis or Webdis config files.

First, create a directory to store the configuration files:

```sh
mkdir ./configs
```

Then, copy both `redis.conf` and `webdis.prod.json` to this directory. We can use two short-lived Docker containers for this:

```sh
docker run --rm -ti nicolas/webdis:latest cat /etc/redis.conf > ./configs/redis.custom.conf
docker run --rm -ti nicolas/webdis:latest cat /etc/webdis.prod.json > ./configs/webdis.custom.json
```

Once the two files are copied, edit them to your liking.

Finally, mount the `./configs` directory into the container and run the services with their new config files:

```sh
docker run --rm -ti -p127.0.0.1:7379:7379 \
    -v $(pwd)/configs:/mnt/configs \
    nicolas/webdis:latest /bin/sh -c \
    '/usr/bin/redis-server /mnt/configs/redis.custom.conf && /usr/local/bin/webdis /mnt/configs/webdis.custom.json'
```

## Other options

Any combination of the various techniques documented here should work. You can certainly build your own image, mount your own config files into it, and add custom Redis config flags on the command line all at once, if the use case calls for this.

In particular, passing secrets for TLS or ACL config to Redis or Webdis will often require this sort of approach. Do not store secrets in the Docker image, but mount them at runtime and configure the services to access these mounted directories.

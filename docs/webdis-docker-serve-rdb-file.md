# Serving data from a Redis `RDB` snapshot over HTTP with Webdis and Docker

Redis can produce snapshots of its dataset using the [`SAVE`](https://redis.io/commands/save/) command, which creates a file usually named `dump.rdb`. This page explains how to take such a file and make its contents available over HTTP with Webdis and Redis running in a single Docker container.

## TL;DR: if you just need the command

First, create a directory on the host machine and copy `dump.rdb` into it:
```shell
$ mkdir redis-data
$ cp dump.rdb redis-data/
```

Then, run the Webdis container with this directory mounted as `/var/lib/redis`:

```shell
$ docker run --name webdis-local-data --rm -d -p 127.0.0.1:7379:7379 \
      -v$(pwd)/redis-data:/var/lib/redis nicolas/webdis:latest
```

The contents of the `dump.rdb` file are now accessible over HTTP on port `7379`.

Stop the container with:
```shell
$ docker stop webdis-local-data
```

## How this works

For a full explanation, please read on.

### Structure of the Webdis container

The [Docker images for Webdis](https://hub.docker.com/r/nicolas/webdis/tags) are built to help users discover Webdis and get started quickly. For this reason, they _embed_ a Redis server and the Webdis process running alongside Redis provides access to its keys over HTTP.

To start Redis with an existing `dump.rdb` file, we first need to find the directory used by Redis to save its data. This directory is configured by the `dir` entry in `redis.conf` ([see documentation](https://github.com/redis/redis/blob/39d216a326539e8f3d51fca961d193e4d7be43e0/redis.conf#L496-L504)), so all we have to do is `grep` for it in `redis.conf`:

```shell
$ docker run --rm -ti nicolas/webdis:latest cat /etc/redis.conf | grep ^dir
dir /var/lib/redis
```
ℹ️ Side note: a tool like [Dive](https://github.com/wagoodman/dive) can also be helpful to explore Docker images.

### Creating a `dump.rdb` file

To demonstrate the reloading of a `dump.rdb` file, we'll first need to create one.

In order to have access to `/var/lib/redis` from the host, we can run the Docker container [with a mounted volume](https://docs.docker.com/storage/volumes/) such that a local directory on the host (we'll call it `redis-data`) will be accessible as `/var/lib/redis` from inside the container.
So let's create this directory, run Webdis with the custom mount, write a couple of keys, save the `dump.rdb` file, and stop the container:

```shell
$ mkdir ./redis-data

$ docker run -d --rm --name webdis-local -v$(pwd)/redis-data:/var/lib/redis \
    -p127.0.0.1:7379:7379 nicolas/webdis:latest
f95a587c644a7b8b838eee2ac60b9bb92613e3aa4a5cc6347ca61fdce324c477

$ curl -s http://127.0.0.1:7379/SET/hello/world
{"SET":[true,"OK"]}
$ curl -s http://127.0.0.1:7379/SET/foo/bar
{"SET":[true,"OK"]}
$ curl -s http://127.0.0.1:7379/SAVE
{"SAVE":[true,"OK"]}

$ docker stop webdis-local
webdis-local
```

After writing the keys `hello` and `foo` and asking Redis to persist its data, we now have `dump.rdb` in our local directory and we can verify that it contains the keys we've just written:

```none
$ ls redis-data/
dump.rdb

$ xxd redis-data/dump.rdb
00000000: 5245 4449 5330 3030 39fa 0972 6564 6973  REDIS0009..redis
00000010: 2d76 6572 0536 2e32 2e36 fa0a 7265 6469  -ver.6.2.6..redi
00000020: 732d 6269 7473 c040 fa05 6374 696d 65c2  s-bits.@..ctime.
00000030: 5765 dc62 fa08 7573 6564 2d6d 656d c281  We.b..used-mem..
00000040: a00e 00fa 0c61 6f66 2d70 7265 616d 626c  .....aof-preambl
00000050: 65c0 00fe 00fb 0200 0005 6865 6c6c 6f05  e.........hello.       <--- hello…
00000060: 776f 726c 6400 0366 6f6f 0362 6172 ffaf  world..foo.bar..       <--- :world and foo:bar
00000070: ebb4 9ade 2632 58                        ....&2X
```

The `docker stop` command not only stopped the container, but also removed it entirely since we had started it with `--rm`. While this means that the container itself is gone, we still have `dump.rdb` in our local directory and we can run a new container with this file already pre-loaded into Redis.

### Running a new container with our `dump.rdb` file

To inject this file into a new Webdis container, we just have to start it the same way – except this time our `/var/lib/redis` directory will contain a file for Redis to load at startup. We'll use a different container name here, just to make it obvious that we're not reusing the previous one:

```shell
$ docker run -d --rm --name webdis-new -v$(pwd)/redis-data:/var/lib/redis \
    -p127.0.0.1:7379:7379 nicolas/webdis:latest
4002cf5a699150875efa445a1e862865886b2515bbab6ad1d556ebd5f0234ae1

$ curl -s http://127.0.0.1:7379/GET/hello
{"GET":"world"}
$ curl -s http://127.0.0.1:7379/GET/foo
{"GET":"bar"}

$ docker stop webdis-new
webdis-new
```

This shows that we successfully re-imported `dump.rdb` as the source data that Redis will start with in a new container.

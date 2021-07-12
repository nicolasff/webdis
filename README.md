[![Build](https://github.com/nicolasff/webdis/actions/workflows/build.yml/badge.svg)](https://github.com/nicolasff/webdis/actions/workflows/build.yml)


# About

A very simple web server providing an HTTP interface to Redis. It uses [hiredis](https://github.com/antirez/hiredis), [jansson](https://github.com/akheron/jansson), [libevent](https://monkey.org/~provos/libevent/), and [http-parser](https://github.com/ry/http-parser/).

Webdis depends on libevent-dev. You can install it on Ubuntu by typing `sudo apt-get install libevent-dev` or on macOS by typing `brew install libevent`.

# Build and run from sources

```sh
$ make clean all

$ ./webdis &

$ curl http://127.0.0.1:7379/SET/hello/world
→ {"SET":[true,"OK"]}

$ curl http://127.0.0.1:7379/GET/hello
→ {"GET":"world"}

$ curl -d "GET/hello" http://127.0.0.1:7379/
→ {"GET":"world"}
```

# Try in Docker

```sh
$ docker run --name webdis-test --rm -d -p 127.0.0.1:7379:7379 nicolas/webdis
0d2ce311a4834d403cc3e7cfd571b168ba40cede6a0e155a21507bb0bf7bee81

$ curl http://127.0.0.1:7379/PING
{"PING":[true,"PONG"]}

# To stop it:
$ docker stop webdis-test
0d2ce311a483
```

## Docker repositories and Docker Content Trust

Webdis images are published on [Docker Hub](https://hub.docker.com/r/nicolas/webdis) and [Amazon ECR](https://gallery.ecr.aws/nicolas/webdis).

**Docker Hub**

```sh
$ docker pull nicolas/webdis:0.1.16
$ docker pull nicolas/webdis:latest
```
Starting from release `0.1.12` and including `latest`, Docker Hub images are signed ([download public key](nicolasff.pub)). You should see the following key ID if you verify the trust:

```
$ docker trust inspect nicolas/webdis:0.1.16 --pretty

Signatures for nicolas/webdis:0.1.16

SIGNED TAG   DIGEST                                                             SIGNERS
0.1.16       4d8663cf247fecb685b2788d78fb7b69f6dca7d649f8ec746bc0d39c42ed9db0   nicolasff

List of signers and their keys for nicolas/webdis:0.1.16

SIGNER      KEYS
nicolasff   dd0768b9d35d

Administrative keys for nicolas/webdis:0.1.16

  Repository Key:	fed0b56b8a8fd4d156fb2f47c2e8bd3eb61948b72a787c18e2fa3ea3233bba1a
  Root Key:	40be21f47831d593892370a8e3fc5bfffb16887c707bd81a6aed2088dc8f4bef
```


**Amazon Elastic Container Registry (ECR)**

```sh
$ docker pull public.ecr.aws/nicolas/webdis:0.1.16
$ docker pull public.ecr.aws/nicolas/webdis:latest
```
ECR images are not signed at this time, but they use the exact same hash as the Docker Hub images which _are_ signed.

# Build and run a Docker image locally

Clone the repository and open a terminal in the webdis directory, then run:
```sh
$ docker build -t webdis .
[...]

$ docker run --name webdis-test --rm -d -p 7379:7379 webdis
f0a2763fd456ac1f7ebff80eeafd6a5cd0fc7f06c69d0f7717fb2bdcec65926e

$ curl http://127.0.0.1:7379/PING
{"PING":[true,"PONG"]}

# To stop it:
$ docker stop webdis-test
f0a2763fd456
```


# Features
* `GET` and `POST` are supported, as well as `PUT` for file uploads.
* JSON output by default, optional JSONP parameter (`?jsonp=myFunction` or `?callback=myFunction`).
* Raw Redis 2.0 protocol output with `.raw` suffix
* MessagePack output with `.msg` suffix
* HTTP 1.1 pipelining (70,000 http requests per second on a desktop Linux machine.)
* Multi-threaded server, configurable number of worker threads.
* WebSocket support (Currently using the “hixie-76” specification).
* Connects to Redis using a TCP or UNIX socket.
* Restricted commands by IP range (CIDR subnet + mask) or HTTP Basic Auth, returning 403 errors.
* Support for Redis authentication in the config file: set `redis_auth` to a single string to use a password value, or to an array of two strings to use username+password auth ([new in Redis 6.0](https://redis.io/commands/auth)).
* Environment variables can be used as values in the config file, starting with `$` and in all caps (e.g. `$REDIS_HOST`).
* Pub/Sub using `Transfer-Encoding: chunked`, works with JSONP as well. Webdis can be used as a Comet server.
* Drop privileges on startup.
* Custom Content-Type using a pre-defined file extension, or with `?type=some/thing`.
* URL-encoded parameters for binary data or slashes and question marks. For instance, `%2f` is decoded as `/` but not used as a command separator.
* Logs, with a configurable verbosity.
* Configurable `fsync` frequency for the log file:
    * Set `"log_fsync": "auto"` (default) to let the file system handle file persistence on its own.
    * Set `"log_fsync": N` where `N` is a number to call `fsync` every `N` milliseconds.
    * Set `"log_fsync": "all"` (very slow) to persist the log file to its storage device on each log message.
* Cross-origin requests, usable with XMLHttpRequest2 (Cross-Origin Resource Sharing - CORS).
* File upload with PUT.
* With the JSON output, the return value of INFO is parsed and transformed into an object.
* Optional daemonize: set `"daemonize": true` and `"pidfile": "/var/run/webdis.pid"` in webdis.json.
* Default root object: Add `"default_root": "/GET/index.html"` in webdis.json to substitute the request to `/` with a Redis request.
* HTTP request limit with `http_max_request_size` (in bytes, set to 128MB by default).
* Database selection in the URL, using e.g. `/7/GET/key` to run the command on DB 7.

# Ideas, TODO…
* Add better support for PUT, DELETE, HEAD, OPTIONS? How? For which commands?
	* This could be done using a “strict mode” with a table of commands and the verbs that can/must be used with each command. Strict mode would be optional, configurable. How would webdis know of new commands remains to be determined.
* MULTI/EXEC/DISCARD/WATCH are disabled at the moment; find a way to use them.
* Support POST of raw Redis protocol data, and execute the whole thing. This could be useful for MULTI/EXEC transactions.
* Enrich config file:
	* Provide timeout (maybe for some commands only?). What should the response be? 504 Gateway Timeout? 503 Service Unavailable?
* Multi-server support, using consistent hashing.
* SSL?
	* Not sure if this is such a good idea.
* SPDY?
	* SPDY is mostly useful for parallel fetches. Not sure if it would make sense for Webdis.
* Send your ideas using the github tracker, on twitter [@yowgi](https://twitter.com/yowgi) or by e-mail to n.favrefelix@gmail.com.

# HTTP error codes
* Unknown HTTP verb: 405 Method Not Allowed.
* Redis is unreachable: 503 Service Unavailable.
* Matching ETag sent using `If-None-Match`: 304 Not Modified.
* Could also be used:
	* Timeout on the redis side: 503 Service Unavailable.
	* Missing key: 404 Not Found.
	* Unauthorized command (disabled in config file): 403 Forbidden.

# Command format
The URI `/COMMAND/arg0/arg1/.../argN.ext` executes the command on Redis and returns the response to the client. GET, POST, and PUT are supported:

* `GET /COMMAND/arg0/.../argN.ext`
* `POST /` with `COMMAND/arg0/.../argN` in the HTTP body.
* `PUT /COMMAND/arg0.../argN-1` with `argN` in the HTTP body (see section on [file uploads](#file-upload).)

`.ext` is an optional extension; it is not read as part of the last argument but only represents the output format. Several formats are available (see below).

Special characters: `/` and `.` have special meanings, `/` separates arguments and `.` changes the Content-Type. They can be replaced by `%2f` and `%2e`, respectively.

# Redis authentication

Webdis can connect to a Redis server that requires credentials.
For Redis versions before 6.0, provide the password as a single string in `webdis.json` using the key `"redis_auth"`. For example:
```json
	"redis_auth": "enter-password-here"
```
Redis 6.0 introduces a more granular [access control system](https://redis.io/topics/acl) and switches from a single password to a pair of username and password. To use these two values with Webdis, set `"redis_auth"` to an array containing the two strings, e.g.
```json
	"redis_auth": ["my-username", "my-password"]
```
This new authentication system is only supported in Webdis 0.1.13 and above.

# ACL
Access control is configured in `webdis.json`. Each configuration tries to match a client profile according to two criterias:

* [CIDR](https://en.wikipedia.org/wiki/CIDR) subnet + mask
* [HTTP Basic Auth](https://en.wikipedia.org/wiki/Basic_access_authentication) in the format of "user:password".

Each ACL contains two lists of commands, `enabled` and `disabled`. All commands being enabled by default, it is up to the administrator to disable or re-enable them on a per-profile basis.
Examples:
```json
{
	"disabled":	["DEBUG", "FLUSHDB", "FLUSHALL"],
},

{
	"http_basic_auth": "user:password",
	"disabled":	["DEBUG", "FLUSHDB", "FLUSHALL"],
	"enabled":	["SET"]
},

{
	"ip": 		"192.168.10.0/24",
	"enabled":	["SET"]
},

{
	"http_basic_auth": "user:password",
	"ip": 		"192.168.10.0/24",
	"enabled":	["SET", "DEL"]
}
```
ACLs are interpreted in order, later authorizations superseding earlier ones if a client matches several. The special value "*" matches all commands.

# Environment variables

Environment variables can be used in `webdis.json` to read values from the environment instead of using constant values.
For this, the value must be a string starting with a dollar symbol and written in all caps. For example, to make the redis host and port configurable via environment variables, use the following:

```json
{
	"redis_host": "$REDIS_HOST",
	"redis_port": "$REDIS_PORT",
}
```

# JSON output
JSON is the default output format. Each command returns a JSON object with the command as a key and the result as a value.

**Examples:**
```sh
// string
$ curl http://127.0.0.1:7379/GET/y
{"GET":"41"}

// number
$ curl http://127.0.0.1:7379/INCR/y
{"INCR":42}

// list
$ curl http://127.0.0.1:7379/LRANGE/x/0/1
{"LRANGE":["abc","def"]}

// status
$ curl http://127.0.0.1:7379/TYPE/y
{"TYPE":[true,"string"]}

// error, which is basically a status
$ curl http://127.0.0.1:7379/MAKE-ME-COFFEE
{"MAKE-ME-COFFEE":[false,"ERR unknown command 'MAKE-ME-COFFEE'"]}

// JSONP callback:
$ curl  "http://127.0.0.1:7379/TYPE/y?jsonp=myCustomFunction"
myCustomFunction({"TYPE":[true,"string"]})
```

# RAW output
This is the raw output of Redis; enable it with the `.raw` suffix.
```sh
// string
$ curl http://127.0.0.1:7379/GET/z.raw
$5
hello

// number
$ curl http://127.0.0.1:7379/INCR/a.raw
:2

// list
$ curl http://127.0.0.1:7379/LRANGE/x/0/-1.raw
*2
$3
abc
$3
def

// status
$ curl http://127.0.0.1:7379/TYPE/y.raw
+zset

// error, which is basically a status
$ curl http://127.0.0.1:7379/MAKE-ME-COFFEE.raw
-ERR unknown command 'MAKE-ME-COFFEE'
```

# Custom content-type
Several content-types are available:

* `.json` for `application/json` (this is the default Content-Type).
* `.msg` for `application/x-msgpack`. See [https://msgpack.org/](https://msgpack.org/) for the specs.
* `.txt` for `text/plain`
* `.html` for `text/html`
* `.xhtml` for `application/xhtml+xml`
* `.xml` for `text/xml`
* `.png` for `image/png`
* `.jpg` or `.jpeg` for `image/jpeg`
* Any other with the `?type=anything/youwant` query string.
* Add a custom separator for list responses with `?sep=,` query string.

```
$ curl -v "http://127.0.0.1:7379/GET/hello.html"
[...]
< HTTP/1.1 200 OK
< Content-Type: text/html
< Date: Mon, 03 Jan 2011 20:43:36 GMT
< Content-Length: 137
<
<!DOCTYPE html>
<html>
[...]
</html>

$ curl -v "http://127.0.0.1:7379/GET/hello.txt"
[...]
< HTTP/1.1 200 OK
< Content-Type: text/plain
< Date: Mon, 03 Jan 2011 20:43:36 GMT
< Content-Length: 137
[...]

$ curl -v "http://127.0.0.1:7379/GET/big-file?type=application/pdf"
[...]
< HTTP/1.1 200 OK
< Content-Type: application/pdf
< Date: Mon, 03 Jan 2011 20:45:12 GMT
[...]
```

# File upload
Webdis supports file upload using HTTP PUT. The command URI is slightly different, as the last argument is taken from the HTTP body.
For example: instead of `/SET/key/value`, the URI becomes `/SET/key` and the value is the entirety of the body. This works for other commands such as LPUSH, etc.

**Uploading a binary file to webdis**:
```
$ file redis-logo.png
redis-logo.png: PNG image, 513 x 197, 8-bit/color RGBA, non-interlaced

$ wc -c redis-logo.png
16744 redis-logo.png

$ curl -v --upload-file redis-logo.png http://127.0.0.1:7379/SET/logo
[...]
> PUT /SET/logo HTTP/1.1
> User-Agent: curl/7.19.7 (x86_64-pc-linux-gnu) libcurl/7.19.7 OpenSSL/0.9.8k zlib/1.2.3.3 libidn/1.15
> Host: 127.0.0.1:7379
> Accept: */*
> Content-Length: 16744
> Expect: 100-continue
>
< HTTP/1.1 100 Continue
< HTTP/1.1 200 OK
< Content-Type: application/json
< ETag: "0db1124cf79ffeb80aff6d199d5822f8"
< Date: Sun, 09 Jan 2011 16:48:19 GMT
< Content-Length: 19
<
{"SET":[true,"OK"]}

$ curl -vs http://127.0.0.1:7379/GET/logo.png -o out.png
> GET /GET/logo.png HTTP/1.1
> User-Agent: curl/7.19.7 (x86_64-pc-linux-gnu) libcurl/7.19.7 OpenSSL/0.9.8k zlib/1.2.3.3 libidn/1.15
> Host: 127.0.0.1:7379
> Accept: */*
>
< HTTP/1.1 200 OK
< Content-Type: image/png
< ETag: "1991df597267d70bf9066a7d11969da0"
< Date: Sun, 09 Jan 2011 16:50:51 GMT
< Content-Length: 16744

$ md5sum redis-logo.png out.png
1991df597267d70bf9066a7d11969da0  redis-logo.png
1991df597267d70bf9066a7d11969da0  out.png
```

The file was uploaded and re-downloaded properly: it has the same hash and the content-type was set properly thanks to the `.png` extension.

# WebSockets
Webdis supports WebSocket clients implementing [dixie-76](https://tools.ietf.org/html/draft-hixie-thewebsocketprotocol-76).
Web Sockets are supported with the following formats, selected by the connection URL:

* JSON (on `/` or `/.json`)
* Raw Redis wire protocol (on `/.raw`)

**Example**:
```javascript
function testJSON() {
	var jsonSocket = new WebSocket("ws://127.0.0.1:7379/.json");
	jsonSocket.onopen = function() {

		console.log("JSON socket connected!");
		jsonSocket.send(JSON.stringify(["SET", "hello", "world"]));
		jsonSocket.send(JSON.stringify(["GET", "hello"]));
	};
	jsonSocket.onmessage = function(messageEvent) {
		console.log("JSON received:", messageEvent.data);
	};
}
testJSON();
```

This produces the following output:
```
JSON socket connected!
JSON received: {"SET":[true,"OK"]}
JSON received: {"GET":"world"}
```

# Pub/Sub with chunked transfer encoding
Webdis exposes Redis PUB/SUB channels to HTTP clients, forwarding messages in the channel as they are published by Redis. This is done using chunked transfer encoding.

**Example using XMLHttpRequest**:
```javascript
var previous_response_length = 0
xhr = new XMLHttpRequest()
xhr.open("GET", "http://127.0.0.1:7379/SUBSCRIBE/hello", true);
xhr.onreadystatechange = checkData;
xhr.send(null);

function checkData() {
	if(xhr.readyState == 3)  {
    	response = xhr.responseText;
    	chunk = response.slice(previous_response_length);
    	previous_response_length = response.length;
    	console.log(chunk);
    }
};
```

Publish messages to redis to see output similar to the following:
```json
{"SUBSCRIBE":["subscribe","hello",1]}
{"SUBSCRIBE":["message","hello","some message"]}
{"SUBSCRIBE":["message","hello","some other message"]} 
```

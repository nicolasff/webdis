# About

A very simple web server providing an HTTP interface to Redis. It uses [hiredis](https://github.com/antirez/hiredis), [jansson](https://github.com/akheron/jansson) and libevent.

<pre>
make clean all
./webdis &
curl http://127.0.0.1:7379/SET/hello/world
→ {"SET":[true,"OK"]}
curl http://127.0.0.1:7379/GET/hello
→ {"GET":"world"}

curl -d "GET/hello" http://127.0.0.1:7379/
→ {"GET":"world"}

</pre>

# Features
* GET and POST are supported.
* JSON output by default, optional JSONP parameter.
* Raw Redis 2.0 protocol output with `?format=raw`
* HTTP 1.1 pipelining (50,000 http requests per second on a desktop Linux machine.)
* Connects to Redis using a TCP or UNIX socket.
* Restricted commands by IP range (CIDR subnet + mask) or HTTP Basic Auth, returning 403 errors.
* Possible Redis authentication in the config file.
* Pub/Sub using `Transfer-Encoding: chunked`, works with JSONP as well. Webdis can be used as a Comet server.
* Drop privileges on startup.
* For `GET` commands:
	* MIME type in a second key with `/GET/k?typeKey=type-k`. This will transform the `GET` request into `MGET` and fetch both `k` and `type-k`. If `type-k` is a string, it will be used as Content-Type in the response. If the key doesn't exist or isn't a string, `binary/octet-stream` is used instead.
	* Custom MIME type  with `?type=text/plain` (or any other MIME type).
* URL-encoded parameters for binary data or slashes. For instance, `%2f` is decoded as `/` but not used as a command separator.

# Ideas, TODO...
* Support PUT, DELETE, HEAD, OPTIONS? How? For which commands?
* MULTI/EXEC/DISCARD/WATCH are disabled at the moment; find a way to use them.
* Add logs.
* Support POST of raw Redis protocol data, and execute the whole thing. This could be useful for MULTI/EXEC transactions.
* Enrich config file:
	* Provide timeout (this needs to be added to hiredis first.)
* Multi-server support, using consistent hashing.
* Send your ideas using the github tracker, on twitter [@yowgi](http://twitter.com/yowgi) or by mail to n.favrefelix@gmail.com.
* Add WebSocket support, allow cross-origin XHR.
* Send an `ETag` header, and recognize `If-None-Match`.

# HTTP error codes
* Unknown HTTP verb: 405 Method Not Allowed
* Redis is unreachable: 503 Service Unavailable
* Could also be used:
	* Timeout on the redis side: 503 Service Unavailable (this isn't supported by HiRedis yet).
	* Missing key: 404 Not Found.
	* Unauthorized command (disabled in config file): 403 Forbidden.
	* Matching ETag sent using `If-None-Match`: 304 Not Modified.

# Command format
The URI `/COMMAND/arg0/arg1/.../argN` executes the command on Redis and returns the response to the client. GET and POST are supported:

* `GET /COMMAND/arg0/.../argN`
* `POST /` with `COMMAND/arg0/.../argN` in the HTTP body.

# ACL
Access control is configured in `webdis.json`. Each configuration tries to match a client profile according to two criterias:

* [CIDR](http://en.wikipedia.org/wiki/CIDR) subnet + mask
* [HTTP Basic Auth](http://en.wikipedia.org/wiki/Basic_access_authentication) in the format of "user:password".

Each ACL contains two lists of commands, `enabled` and `disabled`. All commands being enabled by default, it is up to the administrator to disable or re-enable them on a per-profile basis.
Examples:
<pre>
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
</pre>
ACLs are interpreted in order, later authorizations superseding earlier ones if a client matches several.

# JSON output
JSON is the default output format. Each command returns a JSON object with the command as a key and the result as a value.

**Examples:**
<pre>
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

</pre>

# RAW output
This is the raw output of Redis; enable it with `?format=raw`.
<pre>

// string
$ curl http://127.0.0.1:7379/GET/z?format=raw
$5
hello

// number
curl http://127.0.0.1:7379/INCR/a?format=raw
:2

// list
$ curl http://127.0.0.1:7379/LRANGE/x/0/-1?format=raw
*2
$3
abc
$3
def

// status
$ curl http://127.0.0.1:7379/TYPE/y?format=raw
+zset

// error, which is basically a status
$ curl http://127.0.0.1:7379/MAKE-ME-COFFEE?format=raw
-ERR unknown command 'MAKE-ME-COFFEE'

</pre>

# Custom content-type
Webdis can serve `GET` requests with a custom content-type. There are two ways of doing this; the content-type can be in a key that is fetched with the content, or given as a query string parameter.

**Content-Type in parameter:**

<pre>
curl -v "http://127.0.0.1:7379/GET/hello.html?type=text/html"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: text/html
&lt; Date: Mon, 03 Jan 2011 20:43:36 GMT
&lt; Content-Length: 137
&lt;
&lt;!DOCTYPE html&gt;
&lt;html&gt;
...
&lt;/html&gt;
</pre>

**Content-Type in a separate key:**

<pre>
curl "http://127.0.0.1:7379/SET/hello.type/text%2fhtml"
{"SET":[true,"OK"]}

curl "http://127.0.0.1:7379/GET/hello.type"
{"GET":"text/html"}

curl -v "http://127.0.0.1:7379/GET/hello.html?typeKey=hello.type"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: text/html
&lt; Date: Mon, 03 Jan 2011 20:56:43 GMT
&lt; Content-Length: 137
&lt;
&lt;!DOCTYPE html&gt;
&lt;html&gt;
...
&lt;/html&gt;
</pre>

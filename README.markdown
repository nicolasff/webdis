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
* JSON output by default, optional JSONP parameter (`?jsonp=myFunction`).
* Raw Redis 2.0 protocol output with `.raw` suffix
* HTTP 1.1 pipelining (50,000 http requests per second on a desktop Linux machine.)
* Connects to Redis using a TCP or UNIX socket.
* Restricted commands by IP range (CIDR subnet + mask) or HTTP Basic Auth, returning 403 errors.
* Possible Redis authentication in the config file.
* Pub/Sub using `Transfer-Encoding: chunked`, works with JSONP as well. Webdis can be used as a Comet server.
* Drop privileges on startup.
* Custom Content-Type using a pre-defined file extension, or with `?type=some/thing`.
* URL-encoded parameters for binary data or slashes. For instance, `%2f` is decoded as `/` but not used as a command separator.

# Ideas, TODO...
* Support PUT, DELETE, HEAD, OPTIONS? How? For which commands?
* MULTI/EXEC/DISCARD/WATCH are disabled at the moment; find a way to use them.
* Add logs.
* Support POST of raw Redis protocol data, and execute the whole thing. This could be useful for MULTI/EXEC transactions.
* Enrich config file:
	* Provide timeout (this needs to be added to hiredis first.)
* Multi-server support, using consistent hashing.
* Add WebSocket support (with which protocol?)
* Allow cross-origin XHR.
* Allow file upload with PUT? Saving a file in Redis using the `SET` command should be easy to do with cURL.
* Send your ideas using the github tracker, on twitter [@yowgi](http://twitter.com/yowgi) or by mail to n.favrefelix@gmail.com.

# HTTP error codes
* Unknown HTTP verb: 405 Method Not Allowed
* Redis is unreachable: 503 Service Unavailable
* Matching ETag sent using `If-None-Match`: 304 Not Modified.
* Could also be used:
	* Timeout on the redis side: 503 Service Unavailable (this isn't supported by HiRedis yet).
	* Missing key: 404 Not Found.
	* Unauthorized command (disabled in config file): 403 Forbidden.

# Command format
The URI `/COMMAND/arg0/arg1/.../argN.ext` executes the command on Redis and returns the response to the client. GET and POST are supported:

* `GET /COMMAND/arg0/.../argN.ext`
* `POST /` with `COMMAND/arg0/.../argN` in the HTTP body.

`.ext` is an optional extension; it is not read as part of the last argument but only represents the output format. Several formats are available (see below).

Special characters: `/` and `.` have special meanings, `/` separates arguments and `.` changes the Content-Type. They can be replaced by `%2f` and `%2e`, respectively.

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
This is the raw output of Redis; enable it with the `.raw` suffix.
<pre>

// string
$ curl http://127.0.0.1:7379/GET/z.raw
$5
hello

// number
curl http://127.0.0.1:7379/INCR/a.raw
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
</pre>

# Custom content-type
Several content-types are available:

* `.json` for `application/json` (this is the default Content-Type).
* `.txt` for `text/plain`
* `.html` for `text/html`
* `xhtml` for `application/xhtml+xml`
* `xml` for `text/xml`
* `.png` for `image/png`
* `jpg` or `jpeg` for `image/jpeg`
* Any other with the `?type=anything/youwant` query string.

<pre>
curl -v "http://127.0.0.1:7379/GET/hello.html"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: text/html
&lt; Date: Mon, 03 Jan 2011 20:43:36 GMT
&lt; Content-Length: 137
&lt;
&lt;!DOCTYPE html&gt;
&lt;html&gt;
[...]
&lt;/html&gt;

curl -v "http://127.0.0.1:7379/GET/hello.txt"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: text/plain
&lt; Date: Mon, 03 Jan 2011 20:43:36 GMT
&lt; Content-Length: 137
[...]

curl -v "http://127.0.0.1:7379/GET/big-file?type=application/pdf"
[...]
&lt; HTTP/1.1 200 OK
&lt; Content-Type: application/pdf
&lt; Date: Mon, 03 Jan 2011 20:45:12 GMT
[...]
</pre>


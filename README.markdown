# About

A very simple prototype providing an HTTP interface to Redis. It uses [hiredis](https://github.com/antirez/hiredis) and [jansson](https://github.com/akheron/jansson).

<pre>
make clean all
./turnip &
curl http://127.0.0.1:7379/SET/hello/world
curl http://127.0.0.1:7379/GET/hello
→ “world”

curl -d "GET/hello" http://127.0.0.1:7379/
→ “world”

</pre>

# Ideas

* Add meta-data info per key (MIME type in a second key, for instance)
* Find a way to format multi-bulk data
* Support PUT, DELETE, HEAD?
* Add JSONP callbacks
* Add support for Redis UNIX socket
* Enrich config file
	* Provide timeout
	* Restrict commands by IP range
* Send your ideas using the github tracker or on twitter [@yowgi](http://twitter.com/yowgi).

# HTTP error codes
* Missing key: 404 Not Found
* Timeout on the redis side: 503 Service Unavailable
* Unknown verb: 405 Method Not Allowed


# JSON output

The URI `/COMMAND/arg0/arg1/.../argN` returns a JSON object with the command as a key and the result as a value.

**Examples:**
<pre>
// string
$ curl  http://127.0.0.1:7379/GET/y
{"GET":"41"}

// number
$ curl  http://127.0.0.1:7379/INCR/y
{"INCR":42}

// list
$ curl  http://127.0.0.1:7379/LRANGE/x/0/1
{"LRANGE":["abc","def"]}

// status
$ curl  http://127.0.0.1:7379/TYPE/y
{"TYPE":[true,"string"]}

// error, which is basically a status
$ curl  http://127.0.0.1:7379/MAKE-ME-COFFEE
{"MAKE-ME-COFFEE":[false,"ERR unknown command 'MAKE-ME-COFFEE'"]}
</pre>

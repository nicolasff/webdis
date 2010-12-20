# About

A very simple prototype providing an HTTP interface to Redis.

<pre>
make clean all
./turnip &
curl http://127.0.0.1:7379/SET/hello/world
curl http://127.0.0.1:7379/GET/hello

→ “world”
</pre>

# Ideas

* Add meta-data info per key (MIME type in a second key, for instance)
* Find a way to format multi-bulk data
* Support PUT, DELETE, HEAD?
* Add JSON output
* Add JSONP callbacks
* Add a config file
	* Provide host, port, timeout
	* Restrict commands by IP range

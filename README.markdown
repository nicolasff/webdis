# About

A very simple prototype providing an HTTP interface to Redis.

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
* Add JSON output
* Add JSONP callbacks
* Enrich config file
	* Provide timeout
	* Restrict commands by IP range
